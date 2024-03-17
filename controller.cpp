#include "controller.h"
#include "expression.h"
#include "logger.h"

Controller::Controller(const QString &configFile) : HOMEd(configFile), m_automations(new AutomationList(getConfig(), this)), m_telegram(new Telegram(getConfig(), this)), m_timer(new QTimer(this)), m_commands(QMetaEnum::fromType <Command> ()), m_events(QMetaEnum::fromType <Event> ()), m_date(QDate::currentDate())
{
    logInfo << "Starting version" << SERVICE_VERSION;
    logInfo << "Configuration file is" << getConfig()->fileName();

    m_sun = new Sun(getConfig()->value("location/latitude").toDouble(), getConfig()->value("location/longitude").toDouble());
    m_services = {"zigbee", "modbus", "custom"};

    updateSun();

    connect(m_automations, &AutomationList::statusUpdated, this, &Controller::statusUpdated);
    connect(m_automations, &AutomationList::addSubscription, this, &Controller::addSubscription);

    connect(m_telegram, &Telegram::messageReceived, this, &Controller::telegramReceived);
    connect(m_timer, &QTimer::timeout, this, &Controller::updateTime);

    m_automations->init();
    m_timer->start(1000);
}

QString Controller::endpointName(const QString &endpoint)
{
    QList <QString> search = endpoint.split('/');

    for (auto it = m_devices.begin(); it != m_devices.end(); it++)
    {
        QList <QString> list = it.key().split('/');

        if (list.value(0) != search.value(0))
            continue;

        if (list.value(1) == search.value(1))
            return endpoint;

        if (it.value() == search.value(1))
        {
            search.replace(1, list.value(1));
            return search.join('/');
        }
    }

    return QString();
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

    property.clear();

    for (int i = 0; i < list.count(); i++)
    {
        QString item = list.at(i).toLower();

        if (i)
            item.front() = item.front().toUpper();

        property.append(item);
    }
}

QVariant Controller::parseTemplate(QString string, const Trigger &trigger)
{
    QRegExp calculate("\\[\\[([^\\]]*)\\]\\]"), replace("\\{\\{([^\\}]*)\\}\\}");
    QList <QString> valueList = {"property", "mqtt", "state", "timestamp", "triggerName"};
    bool check = false;
    int position = 0;
    double value;

    while ((position = calculate.indexIn(string, position)) != -1)
    {
        QString item = calculate.cap();
        Expression expression(parseTemplate(item.mid(2, item.length() - 4), trigger).toString());
        string.replace(position, item.length(), QString::number(expression.result()));
        check = true;
    }

    position = 0;

    while ((position = replace.indexIn(string, position)) != -1)
    {
        QString item = replace.cap(), value;
        QList <QString> itemList = item.mid(2, item.length() - 4).split('|');

        switch (valueList.lastIndexOf(itemList.value(0).trimmed()))
        {
            case 0: // property
            {
                auto it = m_endpoints.find(itemList.value(1).trimmed());

                if (it != m_endpoints.end())
                    value = it.value()->properties().value(itemList.value(2).trimmed()).toString();

                break;
            }

            case 1: // mqtt
            {
                auto it = m_topics.find(itemList.value(1).trimmed());

                if (it != m_topics.end())
                {
                    QString property = itemList.value(2).trimmed();
                    value = property.isEmpty() ? it.value() : QJsonDocument::fromJson(it.value()).object().value(property).toVariant().toString();
                }

                break;
            }

            case 2: // state
            {
                value = m_automations->states().value(itemList.value(1).trimmed()).toString();
                break;
            }

            case 3: // timestamp
            {
                QString format = itemList.value(1).trimmed();
                value = QDateTime::currentDateTime().toString(format.isEmpty() ? "hh:mm:ss" : format);
                break;
            }

            case 4: // triggerName
            {
                value = trigger->name();
                break;
            }
        }

        string.replace(position, item.length(), value);
    }

    value = string.toDouble(&check);

    if (check)
        return value;

    if (string != "true" && string != "false")
        return string;

    return string == "true" ? true : false;
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
    QMap <QString, QVariant> check = endpoint->properties();

    endpoint->properties() = data;

    for (auto it = endpoint->properties().begin(); it != endpoint->properties().end(); it++)
        handleTrigger(TriggerObject::Type::property, endpoint->name(), it.key(), check.value(it.key()), it.value());

    endpoint->properties().remove("action");
    endpoint->properties().remove("scene");
}

void Controller::handleTrigger(TriggerObject::Type type, const QVariant &a, const QVariant &b, const QVariant &c, const QVariant &d)
{
    for (int i = 0; i < m_automations->count(); i++)
    {
        const Automation &automation = m_automations->at(i);

        if (!automation->active())
            continue;

        for (int j = 0; j < automation->triggers().count(); j++)
        {
            const Trigger &trigger = automation->triggers().at(j);

            if (trigger->type() != type)
                continue;

            switch (type)
            {
                case TriggerObject::Type::property:
                {
                    PropertyTrigger *item = reinterpret_cast <PropertyTrigger*> (trigger.data());
                    QString endpoint = item->endpoint(), property = item->property();

                    parseProperty(endpoint, property);

                    if (endpointName(endpoint) != a.toString() || property != b.toString() || !item->match(c, d))
                        continue;

                    break;
                }

                case TriggerObject::Type::mqtt:
                {
                    MqttTrigger *item = reinterpret_cast <MqttTrigger*> (trigger.data());

                    if (item->topic() != a.toString() || !item->match(b.toByteArray(), c.toByteArray()))
                        continue;

                    break;
                }

                case TriggerObject::Type::telegram:
                {
                    TelegramTrigger *item = reinterpret_cast <TelegramTrigger*> (trigger.data());

                    if (!item->match(a.toString(), b.toLongLong()))
                        continue;

                    break;
                }

                case TriggerObject::Type::time:
                {
                    TimeTrigger *item = reinterpret_cast <TimeTrigger*> (trigger.data());

                    if (!item->match(a.toTime(), m_sun))
                        continue;

                    break;
                }

                case TriggerObject::Type::interval:
                {
                    IntervalTrigger *item = reinterpret_cast <IntervalTrigger*> (trigger.data());

                    if (!item->match(a.toTime().msecsSinceStartOfDay() / 60000))
                        continue;

                    break;
                }
            }

            if (!checkConditions(automation->conditions()))
            {
                logInfo << "Automation" << automation->name() << "conditions mismatch";
                continue;
            }

            if (automation->debounce() * 1000 + automation->lastTriggered() > QDateTime::currentMSecsSinceEpoch())
            {
                logInfo << "Automation" << automation->name() << "debounced";
                continue;
            }

            logInfo << "Automation" << automation->name() << "triggered";

            automation->setLastTrigger(trigger);
            automation->updateLastTriggered();
            m_automations->store();

            if (automation->timer()->isActive() && !automation->restart())
            {
                logWarning << "Automation" << automation->name() << "timer already started";
                continue;
            }

            automation->setActionList(&automation->actions());
            automation->actionList()->setIndex(0);
            runActions(automation.data());
        }
    }
}

bool Controller::checkConditions(const QList <Condition> &conditions, ConditionObject::Type type)
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
                QVariant value;

                parseProperty(endpointName, property);
                endpoint = m_endpoints.value(endpointName);
                value = action->value(endpoint.isNull() ? QVariant() : endpoint->properties().value(property));

                mqttPublish(mqttTopic("td/").append(endpointName), {{property, QJsonValue::fromVariant(value.type() == QVariant::String ? parseTemplate(value.toString(), automation->lastTrigger()) : value)}});
                break;
            }

            case ActionObject::Type::mqtt:
            {
                MqttAction *action = reinterpret_cast <MqttAction*> (item.data());
                mqttPublishString(action->topic(), parseTemplate(action->message(), automation->lastTrigger()).toString(), action->retain());
                break;
            }

            case ActionObject::Type::state:
            {
                StateAction *action = reinterpret_cast <StateAction*> (item.data());
                QVariant check = m_automations->states().value(action->name());

                if (action->value().isValid() && !action->value().isNull())
                    m_automations->states().insert(action->name(), action->value().type() == QVariant::String ? parseTemplate(action->value().toString(), automation->lastTrigger()) : action->value());
                else
                    m_automations->states().remove(action->name());

                if (check != m_automations->states().value(action->name()))
                    m_automations->store(true);

                break;
            }

            case ActionObject::Type::telegram:
            {
                TelegramAction *action = reinterpret_cast <TelegramAction*> (item.data());
                m_telegram->sendMessage(parseTemplate(action->message(), automation->lastTrigger()).toString(), action->silent(), action->chats());
                break;
            }

            case ActionObject::Type::shell:
            {
                ShellAction *action = reinterpret_cast <ShellAction*> (item.data());
                system(QString("sh -c \"%1\" > /dev/null &").arg(parseTemplate(action->command(), automation->lastTrigger()).toString()).toUtf8().constData());
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
    mqttSubscribe(mqttTopic("status/#"));

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
        handleTrigger(TriggerObject::Type::mqtt, topic.name(), check, message);
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
    else if (subTopic.startsWith("status/"))
    {
        QString service = subTopic.split('/').value(1);
        QJsonArray devices = json.value("devices").toArray();

        for (auto it = devices.begin(); it != devices.end(); it++)
        {
            QJsonObject device = it->toObject();
            QString name = device.value("name").toString(), id, key, item;
            bool names = json.value("names").toBool();

            switch (m_services.indexOf(service))
            {
                case 0: id = device.value("ieeeAddress").toString(); break; // zigbee
                case 1: id = QString("%1.%2").arg(device.value("portId").toInt()).arg(device.value("slaveId").toInt()); break; // modbus
                case 2: id = device.value("id").toString(); break; // custom
            }

            if (name.isEmpty())
                name = id;

            key = QString("%1/%2").arg(service, id);
            item =  names ? name : id;

            if (m_devices.contains(key) && m_devices.value(key) != name)
            {
                mqttUnsubscribe(mqttTopic("fd/%1/%2").arg(service, item));
                mqttUnsubscribe(mqttTopic("fd/%1/%2/#").arg(service, item));
                m_devices.remove(key);
            }

            if (!m_devices.contains(key))
            {
                mqttSubscribe(mqttTopic("fd/%1/%2").arg(service, item));
                mqttSubscribe(mqttTopic("fd/%1/%2/#").arg(service, item));
                mqttPublish(mqttTopic("command/%1").arg(service), {{"action", "getProperties"}, {"device", item}, {"service", "automation"}});
                m_devices.insert(key, name);
            }
        }
    }
    else if (subTopic.startsWith("fd/"))
    {
        QString endpoint = endpointName(subTopic.mid(subTopic.indexOf('/') + 1));

        if (!endpoint.isEmpty())
        {
            auto it = m_endpoints.find(endpoint);

            if (it == m_endpoints.end())
                it = m_endpoints.insert(endpoint, Endpoint(new EndpointObject(endpoint)));

            updateEndpoint(it.value(), json.toVariantMap());
        }
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
    handleTrigger(TriggerObject::Type::telegram, message, chat);
}

void Controller::updateTime(void)
{
    QDateTime now = QDateTime::currentDateTime();

    if (m_date != now.date())
    {
        updateSun();
        m_date = now.date();
    }

    if (!now.time().second())
    {
        QTime time = QTime(now.time().hour(), now.time().minute());
        handleTrigger(TriggerObject::Type::time, time);
        handleTrigger(TriggerObject::Type::interval, time);
    }
}

void Controller::automationTimeout(void)
{
    AutomationObject *automation = reinterpret_cast <AutomationObject*> (sender()->parent());
    logInfo << "Automation" << automation->name() << "timer stopped";
    runActions(automation);
}
