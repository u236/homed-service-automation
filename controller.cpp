#include "controller.h"
#include "expression.h"
#include "logger.h"

Controller::Controller(const QString &configFile) : HOMEd(configFile), m_automations(new AutomationList(getConfig(), this)), m_telegram(new Telegram(getConfig(), this)), m_timer(new QTimer(this)), m_commands(QMetaEnum::fromType <Command> ()), m_events(QMetaEnum::fromType <Event> ()), m_date(QDate::currentDate())
{
    logInfo << "Starting version" << SERVICE_VERSION;
    logInfo << "Configuration file is" << getConfig()->fileName();

    m_sun = new Sun(getConfig()->value("location/latitude").toDouble(), getConfig()->value("location/longitude").toDouble());
    m_types = {"zigbee", "modbus", "custom"};

    updateSun();

    connect(m_automations, &AutomationList::statusUpdated, this, &Controller::statusUpdated);
    connect(m_automations, &AutomationList::addSubscription, this, &Controller::addSubscription);

    connect(m_telegram, &Telegram::messageReceived, this, &Controller::telegramReceived);
    connect(m_timer, &QTimer::timeout, this, &Controller::updateTime);

    m_automations->init();
    m_timer->start(1000);
}

Device Controller::findDevice(const QString &search)
{
    QList <QString> list = search.split('/');

    for (auto it = m_devices.begin(); it != m_devices.end(); it++)
        if (search == it.value()->key() || search.startsWith(it.value()->key().append('/')) || search == it.value()->topic() || search.startsWith(it.value()->topic().append('/')) || (it.value()->key().split('/').first() == list.value(0) && it.value()->name() == list.value(1)))
            return it.value();

    return Device();
}

QVariant Controller::parseString(const QString &string)
{
    bool check;
    double value = string.toDouble(&check);

    if (check)
        return value;

    if (string != "true" && string != "false")
        return string;

    return string == "true" ? true : false;
}

QVariant Controller::parseTemplate(QString string, const Trigger &trigger)
{
    QRegExp calculate("\\[\\[([^\\]]*)\\]\\]"), replace("\\{\\{[^\\{\\}]*\\}\\}");
    QList <QString> valueList = {"property", "mqtt", "file", "state", "timestamp", "triggerName"};
    int position;

    while ((position = calculate.indexIn(string)) != -1)
    {
        QString item = calculate.cap();
        Expression expression(parseTemplate(item.mid(2, item.length() - 4), trigger).toString());
        string.replace(position, item.length(), QString::number(expression.result(), 'f').remove(QRegExp("0+$")).remove(QRegExp("\\.$")));
    }

    while ((position = replace.indexIn(string)) != -1)
    {
        QString item = replace.cap(), value;
        QList <QString> itemList = item.mid(2, item.length() - 4).split('|');

        switch (valueList.lastIndexOf(itemList.value(0).trimmed()))
        {
            case 0: // property
            {
                QString endpoint = itemList.value(1).trimmed();
                const Device &device = findDevice(endpoint);

                if (!device.isNull())
                {
                    QString property;
                    QMap <QString, QVariant> map;
                    QList <QString> list = itemList.value(2).trimmed().split(0x20);
                    quint8 endpointId = static_cast <quint8> (list.last().toInt());

                    if (!endpointId)
                        endpointId = static_cast <quint8> (endpoint.split('/').last().toInt());
                    else
                        list.removeLast();

                    property = list.join(QString());

                    if (!device->properties().contains(endpointId))
                    {
                        property.append(QString("_%1").arg(endpointId));
                        endpointId = 0;
                    }

                    map = device->properties().value(endpointId);

                    for (auto it = map.begin(); it != map.end(); it++)
                    {
                        if (it.key().compare(property, Qt::CaseInsensitive))
                            continue;

                        value = it.value().toString();
                        break;
                    }
                }

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

            case 2: // file
            {
                QFile file(itemList.value(1).trimmed());

                if (file.open(QFile::ReadOnly | QFile::Text))
                {
                    value = QString(file.readAll());
                    file.close();
                }

                break;
            }

            case 3: // state
            {
                value = m_automations->states().value(itemList.value(1).trimmed()).toString();
                break;
            }

            case 4: // timestamp
            {
                QString format = itemList.value(1).trimmed();
                value = format.isEmpty() ? QString::number(QDateTime::currentSecsSinceEpoch()) : QDateTime::currentDateTime().toString(format);
                break;
            }

            case 5: // triggerName
            {
                value = trigger->name();
                break;
            }
        }

        string.replace(position, item.length(), value);
    }

    return parseString(string);
}

void Controller::updateSun(void)
{
    m_sun->setDate(QDate::currentDate());
    m_sun->setOffset(QDateTime::currentDateTime().offsetFromUtc());

    m_sun->updateSunrise();
    m_sun->updateSunset();

    logInfo << "Sunrise set to" << m_sun->sunrise().toString("hh:mm").toUtf8().constData() << "and sunset set to" << m_sun->sunset().toString("hh:mm").toUtf8().constData();
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

                    if (item->endpoint() != a.toString() || item->property() != b.toString() || !item->match(c, d))
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
                logInfo << automation << "conditions mismatch";
                continue;
            }

            if (automation->debounce() * 1000 + automation->lastTriggered() > QDateTime::currentMSecsSinceEpoch())
            {
                logInfo << automation << "debounced";
                continue;
            }

            logInfo << automation << "triggered";

            automation->setLastTrigger(trigger);
            automation->updateLastTriggered();
            m_automations->store();

            if (automation->timer()->isActive() && !automation->restart())
            {
                logWarning << automation << "timer already started";
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
                const Device &device = findDevice(condition->endpoint());

                if (!device.isNull() && condition->match(device->properties().value(condition->endpoint().split('/').last().toInt()).value(condition->property())))
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
                 const Device &device = findDevice(action->endpoint());

                 if (!device.isNull())
                 {
                     quint8 endpointId = static_cast <quint8> (action->endpoint().split('/').last().toInt());
                     QVariant value = action->value(device->properties().value(endpointId).value(action->property()));
                     QString string;

                     if (value.type() == QVariant::String)
                     {
                         value = parseTemplate(value.toString(), automation->lastTrigger());
                         string = value.toString();
                     }

                     if (string.contains(','))
                     {
                         QList <QString> list = string.split(',');
                         QJsonArray array;

                         for (int i = 0; i < list.count(); i++)
                             array.append(QJsonValue::fromVariant(parseString(list.at(i))));

                         value = array;
                     }

                     mqttPublish(mqttTopic("td/").append(endpointId ? QString("%1/%2").arg(device->topic()).arg(endpointId) : device->topic()), {{action->property(), QJsonValue::fromVariant(value)}});
                 }

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

                logInfo << automation << "timer" << (automation->timer()->isActive() ? "restarted" : "started");
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

    m_devices.clear();
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
                mqttPublish(topic.name(), QJsonObject(), true);
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
                    logInfo << automation << "successfully updated";
                    publishEvent(automation->name(), Event::updated);
                }
                else
                {
                    m_automations->append(automation);
                    logInfo << automation << "successfully added";
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
                    logInfo << automation << "removed";
                    publishEvent(automation->name(), Event::removed);
                    m_automations->store(true);
                }

                break;
            }
        }
    }
    else if (subTopic.startsWith("status/"))
    {
        QString type = subTopic.split('/').value(1), service = subTopic.mid(subTopic.indexOf('/') + 1);
        QJsonArray devices = json.value("devices").toArray();
        bool names = json.value("names").toBool();

        if (!m_types.contains(type))
           return;

        for (auto it = devices.begin(); it != devices.end(); it++)
        {
            QJsonObject device = it->toObject();
            QString name = device.value("name").toString(), id, key, topic;
            bool check = false;

            if (type == "zigbee" && (device.value("removed").toBool() || !device.value("logicalType").toInt()))
                continue;

            switch (m_types.indexOf(type))
            {
                case 0: id = device.value("ieeeAddress").toString(); break; // zigbee
                case 1: id = QString("%1.%2").arg(device.value("portId").toInt()).arg(device.value("slaveId").toInt()); break; // modbus
                case 2: id = device.value("id").toString(); break; // custom
            }

            if (name.isEmpty())
                name = id;

            key = QString("%1/%2").arg(type, id);
            topic = QString("%1/%2").arg(service, names ? name : id);

            if (m_devices.contains(key) && m_devices.value(key)->topic() != topic)
            {
                const Device &device = m_devices.value(key);
                mqttUnsubscribe(mqttTopic("fd/%1").arg(device->topic()));
                mqttUnsubscribe(mqttTopic("fd/%1/#").arg(device->topic()));
                device->setTopic(topic);
                device->setName(name);
                check = true;
            }

            if (!m_devices.contains(key))
            {
                m_devices.insert(key, Device(new DeviceObject(key, topic, name)));
                check = true;
            }

            if (check)
            {
                mqttSubscribe(mqttTopic("fd/%1").arg(topic));
                mqttSubscribe(mqttTopic("fd/%1/#").arg(topic));
                mqttPublish(mqttTopic("command/%1").arg(service), {{"action", "getProperties"}, {"device", names ? name : id}, {"service", "automation"}});
            }
        }
    }
    else if (subTopic.startsWith("fd/"))
    {
        const Device &device = findDevice(subTopic.mid(subTopic.indexOf('/') + 1));

        if (!device.isNull())
        {
            quint8 endpointId = subTopic.split('/').last().toInt();
            QMap <QString, QVariant> properties = json.toVariantMap(), check = device->properties().value(endpointId);
            QList <QString> list = {"action", "scene"};

            device->properties().insert(endpointId, properties);

            for (auto it = properties.begin(); it != properties.end(); it++)
                handleTrigger(TriggerObject::Type::property, endpointId ? QString("%1/%2").arg(device->key()).arg(endpointId) : device->key(), it.key(), list.contains(it.key()) ? QVariant() : check.value(it.key()), it.value());
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
    logInfo << automation << "timer stopped";
    runActions(automation);
}
