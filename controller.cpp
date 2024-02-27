#include "controller.h"
#include "logger.h"

Controller::Controller(const QString &configFile) : HOMEd(configFile), m_automations(new AutomationList(getConfig(), this)), m_telegram(new Telegram(getConfig(), this)), m_timer(new QTimer(this)), m_commands(QMetaEnum::fromType <Command> ()), m_events(QMetaEnum::fromType <Event> ()), m_date(QDate::currentDate())
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

void Controller::parseProperty(QString &endpointName, QString &property)
{
    QList <QString> list = property.split(0x20);
    int number = list.last().toInt();

    if (number)
    {
        endpointName.append(QString("/%1").arg(number));
        list.removeLast();
    }

    switch (list.count())
    {
        case 0: return;
        case 1: property = list.value(0); return;
    }

    property.clear();

    for (int i = 0; i < list.count(); i++)
    {
        QString item = list.at(i).toLower();

        if (i)
            item.front() = item.front().toUpper();

        property.append(item);
    }
}

QString Controller::composeString(QString string, const Trigger &trigger)
{
    static QRegularExpression regex("{{(.+?)}}");

    QRegularExpressionMatchIterator match = regex.globalMatch(string);
    QList <QString> valueList = {"property", "mqtt", "timestamp", "triggerName"};

    while (match.hasNext())
    {
        QString item = match.next().captured(), value;
        QList <QString> itemList = item.mid(2, item.length() - 4).split('|');

        switch (valueList.lastIndexOf(itemList.value(0).trimmed()))
        {
            case 0:
            {
                auto it = m_endpoints.find(itemList.value(1).trimmed());

                if (it != m_endpoints.end())
                    value = it.value()->properties().value(itemList.value(2).trimmed()).toString();

                break;
            }

            case 1:
            {
                auto it = m_topics.find(itemList.value(1).trimmed());

                if (it != m_topics.end())
                {
                    QString property = itemList.value(2).trimmed();
                    value = property.isEmpty() ? it.value() : QJsonDocument::fromJson(it.value()).object().value(property).toString();
                }

                break;
            }

            case 2:
            {
                QString format = itemList.value(1).trimmed();
                value = QDateTime::currentDateTime().toString(format.isEmpty() ? "hh:mm:ss" : format);
                break;
            }

            case 3:
            {
                value = trigger->name();
                break;
            }
        }

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
    QMap <QString, QVariant> properties, check = endpoint->properties();

    for (auto it = data.begin(); it != data.end(); it++)
        properties.insert(it.key(), it.value());

    endpoint->properties() = properties;

    for (auto it = endpoint->properties().begin(); it != endpoint->properties().end(); it++)
    {
        for (int i = 0; i < m_automations->count(); i++)
        {
            const Automation &automation = m_automations->at(i);

            if (!automation->active())
                continue;

            for (int j = 0; j < automation->triggers().count(); j++)
            {
                PropertyTrigger *trigger = reinterpret_cast <PropertyTrigger*> (automation->triggers().at(j).data());

                if (trigger->type() == TriggerObject::Type::property)
                {
                    QString endpointName = trigger->endpoint(), property = trigger->property();

                    parseProperty(endpointName, property);

                    if (endpoint->name() != endpointName || it.key() != property || !trigger->match(check.value(it.key()), it.value()))
                        continue;

                    checkConditions(automation.data(), automation->triggers().at(j));
                }
            }
        }
    }

    endpoint->properties().remove("action");
    endpoint->properties().remove("scene");
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
                QString endpointName = condition->endpoint(), property = condition->property();
                Endpoint endpoint;

                parseProperty(endpointName, property);
                endpoint = m_endpoints.value(endpointName);

                if (!endpoint.isNull() && condition->match(endpoint->properties().value(property)))
                    count++;

                break;
            }

            case ConditionObject::Type::mqtt:
            {
                MqttCondition *condition = reinterpret_cast <MqttCondition*> (item.data());

                if (condition->match(m_topics.value(condition->topic())))
                    count++;

                break;
            }

            case ConditionObject::Type::state:
            {
                StateCondition *condition = reinterpret_cast <StateCondition*> (item.data());

                if (condition->match(m_automations->states().value(condition->name())))
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
                QString endpointName = action->endpoint(), property = action->property();
                Endpoint endpoint;

                parseProperty(endpointName, property);
                endpoint = m_endpoints.value(endpointName);

                mqttPublish(mqttTopic("td/").append(endpointName), {{property, QJsonValue::fromVariant(action->value(endpoint.isNull() ? QVariant() : endpoint->properties().value(property)))}});
                break;
            }

            case ActionObject::Type::mqtt:
            {
                MqttAction *action = reinterpret_cast <MqttAction*> (item.data());
                mqttPublishString(action->topic(), composeString(action->message(), automation->lastTrigger()), action->retain());
                break;
            }

            case ActionObject::Type::state:
            {
                StateAction *action = reinterpret_cast <StateAction*> (item.data());

                if (action->value().isValid() && !action->value().isNull())
                    m_automations->states().insert(action->name(), action->value().type() == QVariant::String ? composeString(action->value().toString(), automation->lastTrigger()) : action->value());
                else
                    m_automations->states().remove(action->name());

                m_automations->store();
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
    mqttSubscribe(mqttTopic("command/automation"));
    mqttSubscribe(mqttTopic("service/#"));

    for (int i = 0; i < m_subscriptions.count(); i++)
    {
        logInfo << "MQTT subscribed to" << m_subscriptions.at(i);
        mqttSubscribe(m_subscriptions.at(i));
    }

    m_automations->store();
    mqttPublishStatus();
}

void Controller::mqttReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString subTopic = topic.name().replace(mqttTopic(), QString());
    QJsonObject json = QJsonDocument::fromJson(message).object();

    if (m_subscriptions.contains(topic.name()))
    {
        QByteArray check = m_topics.value(topic.name());

        m_topics.insert(topic.name(), message);

        for (int i = 0; i < m_automations->count(); i++)
        {
            const Automation &automation = m_automations->at(i);

            if (!automation->active())
                continue;

            for (int j = 0; j < automation->triggers().count(); j++)
            {
                MqttTrigger *trigger = reinterpret_cast <MqttTrigger*> (automation->triggers().at(j).data());

                if (trigger->type() != TriggerObject::Type::mqtt || trigger->topic() != topic.name() || !trigger->match(check, message))
                    continue;

                checkConditions(automation.data(), automation->triggers().at(j));
            }
        }
    }

    if (subTopic == "command/automation")
    {
        switch (static_cast <Command> (m_commands.keyToValue(json.value("action").toString().toUtf8().constData())))
        {
            case Command::restartService:
            {
                logWarning << "Restart request received...";
                mqttPublish(mqttTopic("command/automation"), QJsonObject(), true);
                QCoreApplication::exit(EXIT_RESTART);
                break;
            }

            case Command::updateAutomation:
            {
                int index = -1;
                QJsonObject data = json.value("data").toObject();
                QString name = data.value("name").toString().trimmed();
                Automation automation = m_automations->byName(json.value("automation").toString(), &index), other = m_automations->byName(name);

                if (automation != other && !other.isNull())
                {
                    logWarning << "Automation" << name << "update failed, name already in use";
                    publishEvent(name, Event::nameDuplicate);
                    break;
                }

                automation = m_automations->parse(data);

                if (automation.isNull())
                {
                    logWarning << "Automation" << name << "update failed, data is incomplete";
                    publishEvent(name, Event::incompleteData);
                    break;
                }

                if (index >= 0)
                {
                    m_automations->replace(index, automation);
                    logInfo << "Automation" << automation->name() << "successfully updated";
                    publishEvent(automation->name(), Event::updated);
                }
                else
                {
                    m_automations->append(automation);
                    logInfo << "Automation" << automation->name() << "successfully added";
                    publishEvent(automation->name(), Event::added);
                }

                m_automations->store(true);
                break;
            }

            case Command::removeAutomation:
            {
                int index = -1;
                const Automation &automation = m_automations->byName(json.value("automation").toString(), &index);

                if (index >= 0)
                {
                    m_automations->removeAt(index);
                    logInfo << "Automation" << automation->name() << "removed";
                    publishEvent(automation->name(), Event::removed);
                    m_automations->store(true);
                }

                break;
            }
        }
    }
    else if (subTopic.startsWith("service/"))
    {
        QList <QString> list = {"automation", "cloud", "web"};
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

        if (m_services.contains(service))
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
            const Trigger item = automation->triggers().at(j);
            QTime time = QTime(now.time().hour(), now.time().minute());

            switch (item->type())
            {
                case TriggerObject::Type::time:
                {
                    TimeTrigger *trigger = reinterpret_cast <TimeTrigger*> (item.data());

                    if (trigger->match(time, m_sun))
                        checkConditions(automation.data(), automation->triggers().at(j));

                    break;
                }

                case TriggerObject::Type::interval:

                {
                    IntervalTrigger *trigger = reinterpret_cast <IntervalTrigger*> (item.data());

                    if (trigger->match(time.msecsSinceStartOfDay() / 60000))
                        checkConditions(automation.data(), automation->triggers().at(j));

                    break;
                }

                default:
                    break;
            }
        }
    }
}

void Controller::automationTimeout(void)
{
    AutomationObject *automation = reinterpret_cast <AutomationObject*> (sender()->parent());
    logInfo << "Automation" << automation->name() << "timer stopped";
    runActions(automation);
}
