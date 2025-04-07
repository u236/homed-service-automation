#include "controller.h"
#include "logger.h"
#include "parser.h"
#include "runner.h"

Controller::Controller(const QString &configFile) : HOMEd(configFile, true), m_subscribeTimer(new QTimer(this)), m_updateTimer(new QTimer(this)), m_automations(new AutomationList(getConfig(), this)), m_telegram(new Telegram(getConfig(), m_automations,  this)), m_commands(QMetaEnum::fromType <Command> ()), m_events(QMetaEnum::fromType <Event> ()), m_dateTime(QDateTime::currentDateTime()), m_startup(false)
{
    logInfo << "Starting version" << SERVICE_VERSION;
    logInfo << "Configuration file is" << getConfig()->fileName();

    m_sun = new Sun(getConfig()->value("location/latitude").toDouble(), getConfig()->value("location/longitude").toDouble());
    m_types = {"zigbee", "modbus", "custom"};

    updateSun();

    connect(m_automations, &AutomationList::statusUpdated, this, &Controller::statusUpdated);
    connect(m_automations, &AutomationList::addSubscription, this, &Controller::addSubscription);

    connect(m_telegram, &Telegram::messageReceived, this, &Controller::telegramReceived);
    connect(m_subscribeTimer, &QTimer::timeout, this, &Controller::updateSubscriptions);
    connect(m_updateTimer, &QTimer::timeout, this, &Controller::updateTime);

    m_automations->init();
    m_subscribeTimer->setSingleShot(true);
    m_updateTimer->start(1000);
}

Device Controller::findDevice(const QString &search)
{
    QList <QString> list = search.split('/');

    for (auto it = m_devices.begin(); it != m_devices.end(); it++)
        if (search == it.value()->key() || search.startsWith(it.value()->key().append('/')) || search == it.value()->topic() || search.startsWith(it.value()->topic().append('/')) || (it.value()->key().split('/').value(0) == list.value(0).toLower().trimmed() && it.value()->name().toLower() == list.value(1).toLower().trimmed()))
            return it.value();

    return Device();
}

quint8 Controller::getEndpointId(const QString &endpoint)
{
    QList <QString> list = endpoint.split('/');

    if (list.count() > 2)
        return static_cast <quint8> (list.last().toInt());

    return 0;
}

QVariant Controller::parsePattern(const Automation &automation, QString string, bool condition)
{
    QRegExp calculate("\\[\\[([^\\]]*)\\]\\]"), replace("\\{\\{[^\\{\\}]*\\}\\}"), split("\\s+(?=(?:[^']*['][^']*['])*[^']*$)");
    QList <QString> valueList = {"colorTemperature", "file", "mqtt", "property", "shellOutput", "state", "sunrise", "sunset", "timestamp", "triggerName"}, operatList = {"is", "==", "!=", ">", ">=", "<", "<="};
    int position;

    if (!string.startsWith("#!"))
    {
        while ((position = calculate.indexIn(string)) != -1)
        {
            QString item = calculate.cap();
            double number = Expression(parsePattern(automation, item.mid(2, item.length() - 4), condition).toString()).result();
            string.replace(position, item.length(), QString::number(number, 'f').remove(QRegExp("0+$")).remove(QRegExp("\\.$")));
        }
    }

    while ((position = replace.indexIn(string)) != -1)
    {
        QString capture = replace.cap(), item = capture.mid(2, capture.length() - 4).trimmed(), value;
        QList <QString> itemList = item.split('|');
        int index = valueList.indexOf(itemList.value(0).trimmed());

        switch (index)
        {
            case 0: // colorTemperature
            {
                double sunrise = m_sun->sunrise().msecsSinceStartOfDay(), sunset = m_sun->sunset().msecsSinceStartOfDay(), position = 1 - sin(M_PI * (QDateTime::currentDateTime().time().msecsSinceStartOfDay() - sunrise) / (sunset - sunrise));
                int min = itemList.value(1).toInt(), max = itemList.value(2).toInt();

                if (!min)
                    min = 153;

                if (!max)
                    max = 500;

                value = QString::number(round(position < 1 ? min + (max - min) * position : max));
                break;
            }

            case 1: // file
            {
                QFile file(itemList.value(1).trimmed());

                if (file.open(QFile::ReadOnly | QFile::Text))
                {
                    value = QString(file.readAll());
                    file.close();
                }

                break;
            }

            case 2: // mqtt
            {
                auto it = m_topics.find(itemList.value(1).trimmed());

                if (it != m_topics.end())
                {
                    QString property = itemList.value(2).trimmed();
                    value = property.isEmpty() ? it.value() : Parser::jsonValue(it.value(), property).toString();
                }

                break;
            }

            case 3: // property
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
                        endpointId = getEndpointId(endpoint);
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

                        value = it.value().type() == QVariant::List ? it.value().toStringList().join(',') : it.value().toString();
                        break;
                    }
                }

                if (value.isEmpty())
                    value = itemList.value(3).trimmed();

                break;
            }

            case 4: // shellOutput
            {
                value = automation->shellOutput();
                break;
            }

            case 5: // state
            {
                value = m_automations->states().value(itemList.value(1).trimmed()).toString();
                break;
            }

            case 6: // sunrise
            case 7: // sunset
            case 8: // timestamp
            {
                QDateTime dateTime = QDateTime::currentDateTime();
                QString format = itemList.value(1).trimmed();

                switch (index)
                {
                    case 6: dateTime.setTime(m_sun->sunrise()); break;
                    case 7: dateTime.setTime(m_sun->sunset()); break;
                }

                value = format.isEmpty() ? QString::number(dateTime.toSecsSinceEpoch()) : dateTime.toString(format);
                break;
            }

            case 9: // triggerName
            {
                value = automation->lastTrigger()->name();
                break;
            }

            default:
            {
                QList <QString> list = item.split(split);

                for (int i = 0; i < list.count(); i++)
                {
                    QString item = list.at(i);

                    if (!item.startsWith('\'') || !item.endsWith('\''))
                        continue;

                    list.replace(i, item.mid(1, item.length() - 2));
                }

                while (list.count() >= 7 && list.at(1) == "if" && list.at(5) == "else")
                {
                    bool check = false;

                    switch (operatList.indexOf(list.at(3)))
                    {
                        case 0: check = list.at(4) == "defined" ? list.at(2) != EMPTY_PATTERN_VALUE : list.at(4) == "undefined" ? list.at(2) == EMPTY_PATTERN_VALUE : false; break;
                        case 1: check = list.at(2) == list.at(4); break;
                        case 2: check = list.at(2) != list.at(4); break;
                        case 3: check = list.at(2).toDouble() > list.at(4).toDouble(); break;
                        case 4: check = list.at(2).toDouble() >= list.at(4).toDouble(); break;
                        case 5: check = list.at(2).toDouble() < list.at(4).toDouble(); break;
                        case 6: check = list.at(2).toDouble() <= list.at(4).toDouble(); break;
                    }

                    list = check ? list.mid(0, 1) : list.mid(6);
                }

                value = list.join(0x20);
                break;
            }

        }

        string.replace(position, capture.length(), value.isEmpty() && !condition ? EMPTY_PATTERN_VALUE : value);
    }

    return Parser::stringValue(string);
}

bool Controller::checkConditions(const Automation &automation, const QList <Condition> &conditions, ConditionObject::Type type)
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

                if (!device.isNull() && condition->match(device->properties().value(getEndpointId(condition->endpoint())).value(condition->property()), condition->value().type() == QVariant::String ? parsePattern(automation, condition->value().toString(), true) : condition->value()))
                    count++;

                break;
            }

            case ConditionObject::Type::mqtt:
            {
                MqttCondition *condition = reinterpret_cast <MqttCondition*> (item.data());

                if (condition->match(m_topics.value(condition->topic()), condition->value().type() == QVariant::String ? parsePattern(automation, condition->value().toString(), true) : condition->value()))
                    count++;

                break;
            }

            case ConditionObject::Type::state:
            {
                StateCondition *condition = reinterpret_cast <StateCondition*> (item.data());

                if (condition->match(m_automations->states().value(condition->name()), condition->value().type() == QVariant::String ? parsePattern(automation, condition->value().toString(), true) : condition->value()))
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

            case ConditionObject::Type::pattern:
            {
                PatternCondition *condition = reinterpret_cast <PatternCondition*> (item.data());

                if (condition->match(parsePattern(automation, condition->pattern(), true), condition->value().type() == QVariant::String ? parsePattern(automation, condition->value().toString(), true) : condition->value()))
                    count++;

                break;
            }

            case ConditionObject::Type::AND:
            case ConditionObject::Type::OR:
            case ConditionObject::Type::NOT:
            {
                NestedCondition *condition = reinterpret_cast <NestedCondition*> (item.data());

                if (checkConditions(automation, condition->conditions(), condition->type()))
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
            Runner *runner = reinterpret_cast <Runner*> (automation->runner());

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

                case TriggerObject::Type::startup: break;
            }

            if (trigger->name().isEmpty())
                logInfo << automation << "triggered by" << QString("[%1]").arg(j + 1).toUtf8().constData();
            else
                logInfo << automation << "triggered by" << trigger->name();

            automation->setLastTrigger(trigger);

            if (!checkConditions(automation, automation->conditions(), ConditionObject::Type::AND))
            {
                logInfo << automation << "conditions mismatch";
                continue;
            }

            if (automation->debounce() * 1000 + automation->lastTriggered() > QDateTime::currentMSecsSinceEpoch())
            {
                logInfo << automation << "debounced";
                continue;
            }

            automation->updateLastTriggered();
            m_automations->store();

            if (runner && runner->timer()->isActive() && !automation->restart())
            {
                logWarning << automation << "timer already started";
                continue;
            }

            automation->setActionList(&automation->actions());
            automation->actionList()->setIndex(0);

            if (!runner)
            {
                runner = new Runner(this, automation);
                connect(runner, &Runner::publishMessage, this, &Controller::publishMessage, Qt::BlockingQueuedConnection);
                connect(runner, &Runner::updateState, this, &Controller::updateState, Qt::BlockingQueuedConnection);
                connect(runner, &Runner::telegramAction, this, &Controller::telegramAction, Qt::BlockingQueuedConnection);
                connect(runner, &Runner::finished, this, &Controller::finished);
                runner->start();
                continue;
            }

            QMetaObject::invokeMethod(runner, "runActions");
        }
    }
}

void Controller::publishEvent(const QString &name, Event event)
{
    mqttPublish(mqttTopic("event/%1").arg(serviceTopic()), {{"automation", name}, {"event", m_events.valueToKey(static_cast <int> (event))}});
}

void Controller::updateSun(void)
{
    m_sun->setDate(QDate::currentDate());
    m_sun->setOffset(QDateTime::currentDateTime().offsetFromUtc());

    m_sun->updateSunrise();
    m_sun->updateSunset();

    logInfo << "Sunrise set to" << m_sun->sunrise().toString("hh:mm").toUtf8().constData() << "and sunset set to" << m_sun->sunset().toString("hh:mm").toUtf8().constData();
}

void Controller::mqttConnected(void)
{
    mqttSubscribe(mqttTopic("command/%1").arg(serviceTopic()));
    mqttSubscribe(mqttTopic("service/#"));

    m_devices.clear();
    m_automations->store();

    m_subscribeTimer->start(SUBSCRIPTION_DELAY);
    mqttPublishStatus();
}

void Controller::mqttReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString subTopic = topic.name().replace(0, mqttTopic().length(), QString());
    QJsonObject json = QJsonDocument::fromJson(message).object();

    if (m_subscriptions.contains(topic.name()))
    {
        QByteArray check = m_topics.value(topic.name());
        m_topics.insert(topic.name(), message);
        handleTrigger(TriggerObject::Type::mqtt, topic.name(), check, message);
    }

    if (subTopic == QString("command/%1").arg(serviceTopic()))
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

            case Command::removeState:
            {
                if (m_automations->states().remove(json.value("state").toString()))
                    m_automations->store(true);

                break;
            }
        }
    }
    else if (subTopic.startsWith("service/"))
    {
        QString type = subTopic.split('/').value(1), service = subTopic.mid(subTopic.indexOf('/') + 1);

        if (!m_types.contains(type))
            return;

        if (json.value("status").toString() == "online")
        {
            mqttSubscribe(mqttTopic("status/%1").arg(service));
            return;
        }

        for (auto it = m_devices.begin(); it != m_devices.end(); it++)
        {
            const Device &device = it.value();

            if (!device->topic().startsWith(QString("%1/").arg(service)))
                continue;

            mqttUnsubscribe(mqttTopic("fd/%1").arg(device->topic()));
            mqttUnsubscribe(mqttTopic("fd/%1/#").arg(device->topic()));
            device->clearTopic();
        }

        mqttUnsubscribe(mqttTopic("status/%1").arg(service));
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

                if (!device->topic().isEmpty())
                {
                    mqttUnsubscribe(mqttTopic("fd/%1").arg(device->topic()));
                    mqttUnsubscribe(mqttTopic("fd/%1/#").arg(device->topic()));
                }

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
        QString endpoint = subTopic.mid(subTopic.indexOf('/') + 1);
        const Device &device = findDevice(endpoint);

        if (!device.isNull())
        {
            quint8 endpointId = getEndpointId(endpoint);
            QMap <QString, QVariant> data = json.toVariantMap(), properties = device->properties().value(endpointId), check = properties;
            QList <QString> list = {"action", "event", "scene"};

            properties.insert(data);

            for (int i = 0; i < list.count(); i++)
                properties.remove(list.at(i));

            device->properties().insert(endpointId, properties);

            for (auto it = data.begin(); it != data.end(); it++)
                handleTrigger(TriggerObject::Type::property, endpointId ? QString("%1/%2").arg(device->key()).arg(endpointId) : device->key(), it.key(), check.value(it.key()), it.value());
        }
    }
}

void Controller::statusUpdated(const QJsonObject &json)
{
    mqttPublish(mqttTopic("status/%1").arg(serviceTopic()), json, true);
}

void Controller::addSubscription(const QString &topic)
{
    if (m_subscriptions.contains(topic))
        return;

    m_subscriptions.append(topic);

    if (!mqttStatus())
        return;

    QTimer::singleShot(SUBSCRIPTION_DELAY, this, [this, topic] () { logInfo << "MQTT subscribed to" << topic; mqttSubscribe(topic); });
}

void Controller::telegramReceived(const QString &message, qint64 chat)
{
    handleTrigger(TriggerObject::Type::telegram, message, chat);
}

void Controller::publishMessage(const QString &topic, const QVariant &data, bool retain)
{
    if (data.type() == QVariant::Map)
    {
        mqttPublish(topic, QJsonObject::fromVariantMap(data.toMap()), retain);
        return;
    }

    mqttPublishString(topic, data.toString(), retain);
}

void Controller::updateState(const QString &name, const QVariant &value)
{
    QVariant check = m_automations->states().value(name);

    if (value.isValid())
        m_automations->states().insert(name, value);
    else
        m_automations->states().remove(name);

    if (check == m_automations->states().value(name))
        return;

    m_automations->store(true);
}

void Controller::telegramAction(const QString &message, const QString &file, const QString &keyboard, const QString &uuid, qint64 thread, bool silent, bool remove, bool update, QList <qint64> *chats)
{
    m_telegram->sendMessage(message, file, keyboard, uuid, thread, silent, remove, update, *chats);
}

void Controller::finished(void)
{
    Runner *runner = reinterpret_cast <Runner*> (sender());
    runner->wait();
    delete runner;
}

void Controller::updateSubscriptions(void)
{
    for (int i = 0; i < m_subscriptions.count(); i++)
    {
        logInfo << "MQTT subscribed to" << m_subscriptions.at(i);
        mqttSubscribe(m_subscriptions.at(i));
    }

    if (!m_startup)
    {
        handleTrigger(TriggerObject::Type::startup);
        m_startup = true;
    }
}

void Controller::updateTime(void)
{
    QDateTime now = QDateTime::currentDateTime();

    if (m_dateTime.date() != now.date())
        updateSun();

    if (m_dateTime.time().minute() != now.time().minute())
    {
        QTime time = QTime(now.time().hour(), now.time().minute());
        handleTrigger(TriggerObject::Type::time, time);
        handleTrigger(TriggerObject::Type::interval, time);
        m_dateTime = now;
    }
}
