#include "controller.h"
#include "logger.h"

Controller::Controller(const QString &configFile) : HOMEd(configFile), m_automations(new AutomationList(getConfig(), this)), m_telegram(new Telegram(getConfig(), this)), m_timer(new QTimer(this)), m_events(QMetaEnum::fromType <Event> ()), m_date(QDate::currentDate())
{
    logInfo << "Starting version" << SERVICE_VERSION;
    logInfo << "Configuration file is" << getConfig()->fileName();

    m_sun = new Sun(getConfig()->value("location/latitude").toDouble(), getConfig()->value("location/longitude").toDouble());
    updateSun();

    connect(m_automations, &AutomationList::statusUpdated, this, &Controller::statusUpdated);
    connect(m_automations, &AutomationList::addSubscription, this, &Controller::addSubscription);

    connect(m_telegram, &Telegram::messageReceived, this, &Controller::telegramReceived);
    connect(m_timer, &QTimer::timeout, this, &Controller::updateTime);

    m_automations->init();
    m_timer->start(1000);
}

QString Controller::composeString(QString string, const Trigger &trigger)
{
    QRegularExpressionMatchIterator match = QRegularExpression("{{(.+?)}}").globalMatch(string);

    while (match.hasNext())
    {
        QString item = match.next().captured(), name, value;
        QList <QString> list = item.mid(2, item.length() - 4).split('|');

        name = list.value(0).trimmed();

        if (name != "triggerName")
        {
            auto it = m_endpoints.find(name);

            if (it != m_endpoints.end())
                value = it.value()->properties().value(list.value(1).trimmed()).toString();
        }
        else
            value = trigger->name();

        string.replace(item, value.isEmpty() ? "[unknown]" : value);
    }

    return string;
}

void Controller::updateSun(void)
{
    m_sun->setDate(QDate::currentDate());
    m_sun->setOffset(QDateTime::currentDateTime().offsetFromUtc());

    m_sun->updateSunrise();
    m_sun->updateSunset();

    logInfo << "Sunrise set to" << m_sun->sunrise().toString("hh:mm").toUtf8().constData() << "and sunset set to" << m_sun->sunset().toString("hh:mm").toUtf8().constData();
}

void Controller::updateEndpoint(const Endpoint &endpoint, const QMap <QString, QVariant> &data)
{
    QMap <QString, QVariant> properties;

    for (auto it = data.begin(); it != data.end(); it++)
    {
        const QVariant &value = endpoint->properties().value(it.key());

        if (it.key() != "action" && it.key() != "scene")
            properties.insert(it.key(), it.value());

        if (value == it.value())
            continue;

        for (int i = 0; i < m_automations->count(); i++)
        {
            const Automation &automation = m_automations->at(i);

            if (!automation->active())
                continue;

            for (int j = 0; j < automation->triggers().count(); j++)
            {
                PropertyTrigger *trigger = reinterpret_cast <PropertyTrigger*> (automation->triggers().at(j).data());

                if (trigger->type() != TriggerObject::Type::property || trigger->endpoint() != endpoint->name() || trigger->property() != it.key() || !trigger->match(value, it.value()))
                    continue;

                checkConditions(automation.data(), automation->triggers().at(j));
            }
        }
    }

    endpoint->properties() = properties;
}

void Controller::checkConditions(AutomationObject *automation, const Trigger &trigger)
{
    if (!checkConditions(automation->conditions()))
    {
        logInfo << "Automation" << automation->name() << "conditions mismatch";
        return;
    }

    if (automation->debounce() * 1000 + automation->lastTriggered() > QDateTime::currentMSecsSinceEpoch())
    {
        logInfo << "Automation" << automation->name() << "debounced";
        return;
    }

    logInfo << "Automation" << automation->name() << "triggered";
    // TODO: publish "triggered" event here?

    automation->setLastTrigger(trigger);
    automation->updateLastTriggered();
    m_automations->store();

    if (automation->timer()->isActive() && !automation->restart())
    {
        logWarning << "Automation" << automation->name() << "timer already started";
        return;
    }

    automation->setActionList(&automation->actions());
    automation->actionList()->setIndex(0);
    runActions(automation);
}

bool Controller::checkConditions(const QList<Condition> &conditions, ConditionObject::Type type)
{
    QDateTime now = QDateTime::currentDateTime();
    quint16 count = 0;

    for (int i = 0; i < conditions.count(); i++)
    {
        const Condition &item = conditions.at(i);

        switch (item->type())
        {
            case ConditionObject::Type::property:
            {
                PropertyCondition *condition = reinterpret_cast <PropertyCondition*> (item.data());
                auto it = m_endpoints.find(condition->endpoint());

                if (it != m_endpoints.end() && condition->match(it.value()->properties().value(condition->property())))
                    count++;

                break;
            }

            case ConditionObject::Type::date:
            {
                DateCondition *condition = reinterpret_cast <DateCondition*> (item.data());

                if (condition->match(QDate(1900, now.date().month(), now.date().day())))
                    count++;

                break;
            }

            case ConditionObject::Type::time:
            {
                TimeCondition *condition = reinterpret_cast <TimeCondition*> (item.data());

                if (condition->match(QTime(now.time().hour(), now.time().minute()), m_sun))
                    count++;

                break;
            }

            case ConditionObject::Type::week:
            {
                WeekCondition *condition = reinterpret_cast <WeekCondition*> (item.data());

                if (condition->match(QDate::currentDate().dayOfWeek()))
                    count++;

                break;
            }

            case ConditionObject::Type::AND:
            case ConditionObject::Type::OR:
            case ConditionObject::Type::NOT:
            {
                NestedCondition *condition = reinterpret_cast <NestedCondition*> (item.data());

                if (checkConditions(condition->conditions(), condition->type()))
                    count++;

                break;
            }
        }
    }

    switch (type)
    {
        case ConditionObject::Type::AND: return count == conditions.count();
        case ConditionObject::Type::OR:  return count != 0;
        case ConditionObject::Type::NOT: return count == 0;
        default: return false;
    }
}

bool Controller::runActions(AutomationObject *automation)
{
    for (int i = automation->actionList()->index(); i < automation->actionList()->count(); i++)
    {
        const Action &item = automation->actionList()->at(i);

        if (!item->triggerName().isEmpty() && item->triggerName() != automation->lastTrigger()->name())
            continue;

        switch (item->type())
        {
            case ActionObject::Type::property:
            {
                PropertyAction *action = reinterpret_cast <PropertyAction*> (item.data());
                auto it = m_endpoints.find(action->endpoint());
                mqttPublish(mqttTopic("td/").append(action->endpoint()), {{action->property(), QJsonValue::fromVariant(action->value(it != m_endpoints.end() ? it.value()->properties().value(action->property()) : QVariant()))}});
                break;
            }

            case ActionObject::Type::mqtt:
            {
                MqttAction *action = reinterpret_cast <MqttAction*> (item.data());
                mqttPublishString(action->topic(), composeString(action->message(), automation->lastTrigger()), action->retain());
                break;
            }

            case ActionObject::Type::telegram:
            {
                TelegramAction *action = reinterpret_cast <TelegramAction*> (item.data());
                m_telegram->sendMessage(composeString(action->message(), automation->lastTrigger()), action->silent(), action->chats());
                break;
            }

            case ActionObject::Type::shell:
            {
                ShellAction *action = reinterpret_cast <ShellAction*> (item.data());
                system(QString("sh -c \"%1\" > /dev/null &").arg(composeString(action->command(), automation->lastTrigger())).toUtf8().constData());
                break;
            }

            case ActionObject::Type::condition:
            {
                ConditionAction *action = reinterpret_cast <ConditionAction*> (item.data());

                automation->actionList()->setIndex(++i);
                automation->setActionList(&action->actions(checkConditions(action->conditions())));
                automation->actionList()->setIndex(0);

                if (runActions(automation))
                    break;

                return false;
            }

            case ActionObject::Type::delay:
            {
                DelayAction *action = reinterpret_cast <DelayAction*> (item.data());

                connect(automation->timer(), &QTimer::timeout, this, &Controller::automationTimeout, Qt::UniqueConnection);
                automation->timer()->setSingleShot(true);
                automation->actionList()->setIndex(++i);

                logInfo << "Automation" << automation->name() << "timer" << (automation->timer()->isActive() ? "restarted" : "started");
                automation->timer()->start(action->delay() * 1000);
                return false;
            }
        }
    }

    if (automation->actionList()->parent())
    {
        automation->setActionList(automation->actionList()->parent());
        runActions(automation);
    }

    return true;
}

void Controller::publishEvent(const QString &name, Event event)
{
    mqttPublish(mqttTopic("event/automation"), {{"automation", name}, {"event", m_events.valueToKey(static_cast <int> (event))}});
}

void Controller::mqttConnected(void)
{
    logInfo << "MQTT connected";

    mqttSubscribe(mqttTopic("command/automation"));
    mqttSubscribe(mqttTopic("service/#"));

    for (int i = 0; i < m_subscriptions.count(); i++)
    {
        logInfo << "MQTT subscribed to" << m_subscriptions.at(i);
        mqttSubscribe(m_subscriptions.at(i));
    }

    m_automations->store();
}

void Controller::mqttReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString subTopic = topic.name().replace(mqttTopic(), QString());
    QJsonObject json = QJsonDocument::fromJson(message).object();

    if (m_subscriptions.contains(topic.name()))
    {
        for (int i = 0; i < m_automations->count(); i++)
        {
            const Automation &automation = m_automations->at(i);

            if (!automation->active())
                continue;

            for (int j = 0; j < automation->triggers().count(); j++)
            {
                MqttTrigger *trigger = reinterpret_cast <MqttTrigger*> (automation->triggers().at(j).data());

                if (trigger->type() != TriggerObject::Type::mqtt || trigger->topic() != topic.name() || !trigger->match(m_topics.value(topic.name()), message))
                    continue;

                checkConditions(automation.data(), automation->triggers().at(j));
            }
        }

        m_topics.insert(topic.name(), message);
    }

    if (subTopic == "command/automation")
    {
        QString action = json.value("action").toString();

        if (action == "updateAutomation")
        {
            int index = -1;
            QJsonObject data = json.value("data").toObject();
            QString name = data.value("name").toString();
            Automation automation = m_automations->byName(json.value("automation").toString(), &index), other = m_automations->byName(name);

            if (automation != other && !other.isNull())
            {
                logWarning << "Automation" << name << "update failed, name already in use";
                publishEvent(name, Event::nameDuplicate);
                return;
            }

            automation = m_automations->parse(data);

            if (automation.isNull())
            {
                logWarning << "Automation" << name << "update failed, data is incomplete";
                publishEvent(name, Event::incompleteData);
                return;
            }

            if (index < 0)
            {
                m_automations->append(automation);
                logInfo << "Automation" << automation->name() << "successfully added";
                publishEvent(automation->name(), Event::added);
            }
            else
            {
                m_automations->replace(index, automation);
                logInfo << "Automation" << automation->name() << "successfully updated";
                publishEvent(automation->name(), Event::updated);
            }
        }
        else if (action == "removeAutomation")
        {
            int index = -1;
            const Automation &automation = m_automations->byName(json.value("automation").toString(), &index);

            if (index < 0)
                return;

            m_automations->removeAt(index);
            logInfo << "Automation" << automation->name() << "removed";
            publishEvent(automation->name(), Event::removed);
        }
        else
            return;

        m_automations->store(true);
    }
    else if (subTopic.startsWith("service/"))
    {
        QList <QString> list = {"automation", "web"};
        QString service = subTopic.split('/').value(1);

        if (list.contains(service))
            return;

        if (json.value("status").toString() == "online")
        {
            logInfo << "Service" << service << "is online";
            mqttSubscribe(mqttTopic("status/").append(service));
            mqttSubscribe(mqttTopic("fd/%1/#").arg(service));
        }
        else
        {
            logWarning << "Service" << service << "is offline";
            mqttUnsubscribe(mqttTopic("status/").append(service));
            mqttUnsubscribe(mqttTopic("fd/%1/#").arg(service));
            m_services.removeAll(service);
        }
    }
    else if (subTopic.startsWith("status/"))
    {
        QString service = subTopic.split('/').value(1);
        QJsonArray array = json.value("devices").toArray();

        if (!m_services.contains(service))
            return;

        for (auto it = array.begin(); it != array.end(); it++)
        {
            QJsonObject item = it->toObject();

            if (item.value("removed").toBool() || (service == "zigbee" && !item.value("logicalType").toInt()))
                continue;

            if (service == "zigbee")
                mqttPublish(mqttTopic("command/zigbee"), {{"action", "getProperties"}, {"device", item.value("ieeeAddress")}});
        }

        m_services.append(service);
    }
    else if (subTopic.startsWith("fd/"))
    {
        QString endpoint = subTopic.split('/').mid(1).join('/');
        auto it = m_endpoints.find(endpoint);

        if (it == m_endpoints.end())
            it = m_endpoints.insert(endpoint, Endpoint(new EndpointObject(endpoint)));

        updateEndpoint(it.value(), json.toVariantMap());
    }
}

void Controller::statusUpdated(const QJsonObject &json)
{
    mqttPublish(mqttTopic("status/automation"), json, true);
}

void Controller::addSubscription(const QString &topic)
{
    if (m_subscriptions.contains(topic))
        return;

    m_subscriptions.append(topic);

    if (mqttStatus())
    {
        logInfo << "MQTT subscribed to" << topic;
        mqttSubscribe(topic);
    }
}

void Controller::telegramReceived(const QString &message, qint64 chat)
{
    for (int i = 0; i < m_automations->count(); i++)
    {
        const Automation &automation = m_automations->at(i);

        if (!automation->active())
            continue;

        for (int j = 0; j < automation->triggers().count(); j++)
        {
            TelegramTrigger *trigger = reinterpret_cast <TelegramTrigger*> (automation->triggers().at(j).data());

            if (trigger->type() != TriggerObject::Type::telegram || !trigger->match(message, chat))
                continue;

            checkConditions(automation.data(), automation->triggers().at(j));
        }
    }
}

void Controller::updateTime(void)
{
    QDateTime now = QDateTime::currentDateTime();

    if (m_date != now.date())
    {
        updateSun();
        m_date = now.date();
    }

    if (now.time().second())
        return;

    for (int i = 0; i < m_automations->count(); i++)
    {
        const Automation &automation = m_automations->at(i);

        if (!automation->active())
            continue;

        for (int j = 0; j < automation->triggers().count(); j++)
        {
            TimeTrigger *trigger = reinterpret_cast <TimeTrigger*> (automation->triggers().at(j).data());
            QTime time = QTime(now.time().hour(), now.time().minute());

            if (trigger->type() != TriggerObject::Type::time || !trigger->match(time, m_sun))
                continue;

            checkConditions(automation.data(), automation->triggers().at(j));
        }
    }
}

void Controller::automationTimeout(void)
{
    AutomationObject *automation = reinterpret_cast <AutomationObject*> (sender()->parent());
    logInfo << "Automation" << automation->name() << "timer stopped";
    runActions(automation);
}
