#include "controller.h"

Controller::Controller(const QString &configFile) : HOMEd(configFile), m_automations(new AutomationList(getConfig(), this)), m_telegram(new Telegram(getConfig(), this)), m_timer(new QTimer(this)), m_date(QDate::currentDate())
{
    m_sun = new Sun(getConfig()->value("location/latitude").toDouble(), getConfig()->value("location/longitude").toDouble());
    updateSun();

    connect(m_automations, &AutomationList::addSubscription, this, &Controller::addSubscription);
    connect(m_telegram, &Telegram::messageReceived, this, &Controller::telegramReceived);
    connect(m_timer, &QTimer::timeout, this, &Controller::updateTime);

    m_automations->init();
    m_timer->start(1000);
}

QString Controller::composeMessage(QString message)
{
    QRegularExpressionMatchIterator match = QRegularExpression("{{(.+?)}}").globalMatch(message);

    while (match.hasNext())
    {
        QString item = match.next().captured(), value;
        QList <QString> list = item.mid(2, item.length() - 4).split('|');
        auto it = m_endpoints.find(list.value(0).trimmed());

        if (it != m_endpoints.end())
            value = it.value()->properties().value(list.value(1).trimmed()).toString();

        message.replace(item, value.isEmpty() ? "_unknown_" : value);
    }

    return message;
}

void Controller::updateSun(void)
{
    m_sun->setDate(QDate::currentDate());
    m_sun->setOffset(QDateTime::currentDateTime().offsetFromUtc());

    m_sunrise = m_sun->sunrise();
    m_sunset = m_sun->sunset();

    logInfo << "Sunrise set to" << m_sunrise.toString("hh:mm").toUtf8().constData() << "and sunset set to" << m_sunset.toString("hh:mm").toUtf8().constData();
}

void Controller::updateStatus(const Endpoint &endpoint, const QMap <QString, QVariant> &data)
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

            for (int j = 0; j < automation->triggers().count(); j++)
            {
                PropertyTrigger *trigger = reinterpret_cast <PropertyTrigger*> (automation->triggers().at(j).data());

                if (trigger->type() != TriggerObject::Type::property || trigger->endpoint() != endpoint->name() || trigger->property() != it.key() || !trigger->match(value, it.value()))
                    continue;

                checkConditions(automation.data());
            }
        }
    }

    endpoint->properties() = properties;
}

void Controller::checkConditions(AutomationObject *automation)
{
    QDateTime now = QDateTime::currentDateTime();

    logInfo << "Automation" << automation->name() << "triggered";
    // TODO: publish "triggered" event here?

    for (int i = 0; i < automation->conditions().count(); i++)
    {
        const Condition &item = automation->conditions().at(i);

        switch (item->type())
        {
            case ConditionObject::Type::property:
            {
                PropertyCondition *condition = reinterpret_cast <PropertyCondition*> (item.data());
                auto it = m_endpoints.find(condition->endpoint());

                if (it == m_endpoints.end() || !condition->match(it.value()->properties().value(condition->property())))
                {
                    logInfo << "Automation" << automation->name() << "properties mismatch";
                    return;
                }

                break;
            }

            case ConditionObject::Type::date:
            {
                DateCondition *condition = reinterpret_cast <DateCondition*> (item.data());

                if (!condition->match(QDate(1900, now.date().month(), now.date().day())))
                {
                    logInfo << "Automation" << automation->name() << "date mismatch";
                    return;
                }

                break;
            }

            case ConditionObject::Type::time:
            {
                TimeCondition *condition = reinterpret_cast <TimeCondition*> (item.data());

                if (!condition->match(QTime(now.time().hour(), now.time().minute())))
                {
                    logInfo << "Automation" << automation->name() << "time mismatch";
                    return;
                }

                break;
            }

            case ConditionObject::Type::week:
            {
                WeekCondition *condition = reinterpret_cast <WeekCondition*> (item.data());

                if (!condition->match(QDate::currentDate().dayOfWeek()))
                {
                    logInfo << "Automation" << automation->name() << "day of week mismatch";
                    return;
                }

                break;
            }
        }
    }

    if (!automation->delay())
    {
        runActions(automation);
        return;
    }

    connect(automation->timer(), &QTimer::timeout, this, &Controller::automationTimeout);
    automation->timer()->setSingleShot(true);

    if (!automation->timer()->isActive() || automation->restart())
    {
        logInfo << "Automation" << automation->name() << "timer" << (automation->timer()->isActive() ? "restarted" : "started");
        automation->timer()->start(automation->delay() * 1000);
        return;
    }

    logWarning << "Automation" << automation->name() << "timer already started";
}

void Controller::runActions(AutomationObject *automation)
{
    for (int i = 0; i < automation->actions().count(); i++)
    {
        const Action &item = automation->actions().at(i);

        switch (item->type())
        {
            case ActionObject::Type::property:
            {
                PropertyAction *action = reinterpret_cast <PropertyAction*> (item.data());
                auto it = m_endpoints.find(action->endpoint());
                mqttPublish(mqttTopic("td/").append(action->endpoint()), {{action->property(), QJsonValue::fromVariant(action->value(it != m_endpoints.end() ? it.value()->properties().value(action->property()) : QVariant()))}});
                break;
            }

            case ActionObject::Type::telegram:
            {
                TelegramAction *action = reinterpret_cast <TelegramAction*> (item.data());
                m_telegram->sendMessage(composeMessage(action->message()), action->silent(), action->chats());
                break;
            }

            case ActionObject::Type::mqtt:
            {
                MqttAction *action = reinterpret_cast <MqttAction*> (item.data());
                mqttPublishString(action->topic(), composeMessage(action->message()), action->retain());
                break;
            }
        }
    }
}

void Controller::mqttConnected(void)
{
    logInfo << "MQTT connected";
    mqttSubscribe(mqttTopic("service/#"));

    for (int i = 0; i < m_subscriptions.count(); i++)
    {
        logInfo << "MQTT subscribed to" << m_subscriptions.at(i);
        mqttSubscribe(m_subscriptions.at(i));
    }
}

void Controller::mqttReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString subTopic = topic.name().replace(mqttTopic(), QString());
    QJsonObject json = QJsonDocument::fromJson(message).object();

    if (subTopic.startsWith("service/"))
    {
        QString service = subTopic.split('/').value(1);

        if (service == "automation") // TODO: make it smarter
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
        }
    }
    else if (subTopic.startsWith("status/"))
    {
        QString service = subTopic.split('/').value(1);
        QJsonArray array = json.value("devices").toArray();

        for (auto it = array.begin(); it != array.end(); it++)
        {
            QJsonObject item = it->toObject();

            if (item.value("removed").toBool() || (service == "zigbee" && !item.value("logicalType").toInt()))
                continue;

            if (service == "zigbee" && item.value("logicalType").toInt())
                mqttPublish(mqttTopic("command/zigbee"), {{"action", "getProperties"}, {"device", item.value("ieeeAddress")}});
        }

        mqttUnsubscribe(topic.name());
    }
    else if (subTopic.startsWith("fd/"))
    {
        QString endpoint = subTopic.split('/').mid(1).join('/');
        auto it = m_endpoints.find(endpoint);

        if (it == m_endpoints.end())
            it = m_endpoints.insert(endpoint, Endpoint(new EndpointObject(endpoint)));

        updateStatus(it.value(), json.toVariantMap());
    }
    else if (m_subscriptions.contains(topic.name()))
    {
        for (int i = 0; i < m_automations->count(); i++)
        {
            const Automation &automation = m_automations->at(i);

            for (int j = 0; j < automation->triggers().count(); j++)
            {
                MqttTrigger *trigger = reinterpret_cast <MqttTrigger*> (automation->triggers().at(j).data());

                if (trigger->type() != TriggerObject::Type::mqtt || !trigger->match(topic.name(), message))
                    continue;

                checkConditions(automation.data());
            }
        }
    }
}

void Controller::addSubscription(const QString &topic)
{
    if (m_subscriptions.contains(topic))
        return;

    m_subscriptions.append(topic);
}

void Controller::telegramReceived(const QString &message, qint64 chat)
{
    for (int i = 0; i < m_automations->count(); i++)
    {
        const Automation &automation = m_automations->at(i);

        for (int j = 0; j < automation->triggers().count(); j++)
        {
            TelegramTrigger *trigger = reinterpret_cast <TelegramTrigger*> (automation->triggers().at(j).data());

            if (trigger->type() != TriggerObject::Type::telegram || !trigger->match(message, chat))
                continue;

            checkConditions(automation.data());
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

        for (int j = 0; j < automation->triggers().count(); j++)
        {
            const Trigger &trigger = automation->triggers().at(j);
            QTime time = QTime(now.time().hour(), now.time().minute());

            switch (trigger->type())
            {
                case TriggerObject::Type::sunrise:

                    if (reinterpret_cast <SunriseTrigger*> (trigger.data())->match(m_sunrise, time))
                        checkConditions(automation.data());

                    break;

                case TriggerObject::Type::sunset:

                    if (reinterpret_cast <SunsetTrigger*> (trigger.data())->match(m_sunset, time))
                        checkConditions(automation.data());

                    break;

                case TriggerObject::Type::time:

                    if (reinterpret_cast <TimeTrigger*> (trigger.data())->match(time))
                        checkConditions(automation.data());

                    break;

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
