#include "controller.h"

Controller::Controller(const QString &configFile) : HOMEd(configFile), m_automations(new AutomationList(getConfig(), this))
{
    m_telegramToken = getConfig()->value("telegram/token").toString();
    m_telegramChat = getConfig()->value("telegram/chat").toInt();

    m_automations->init();
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
                PropertyTrigger *trigger = reinterpret_cast < PropertyTrigger*> (automation->triggers().at(j).data());

                if (trigger->type() != TriggerObject::Type::property || trigger->endpoint() != endpoint->name() || trigger->property() != it.key() || !trigger->match(value, it.value()))
                    continue;

                logInfo << "Automation" << automation->name() << "triggered"; // TODO: publish "triggered" event here?
                checkConditions(automation);
            }
        }
    }

    endpoint->properties() = properties;
}

void Controller::checkConditions(const Automation &automation)
{
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
                    logInfo << "Automation" << automation->name() << "conditions mismatch";
                    return;
                }

                break;
            }
        }
    }

    runActions(automation);
}

void Controller::runActions(const Automation &automation)
{
    for (int i = 0; i < automation->actions().count(); i++)
    {
        const Action &item = automation->actions().at(i);

        switch (item->type())
        {
            case ActionObject::Type::property:
            {
                PropertyAction *action = reinterpret_cast <PropertyAction*> (item.data());
                mqttPublish(mqttTopic("td/").append(action->endpoint()), {{action->property(), QJsonValue::fromVariant(action->value())}});
                break;
            }

            case ActionObject::Type::mqtt:
            {
                MqttAction *action = reinterpret_cast <MqttAction*> (item.data());
                mqttPublishString(action->topic(), action->message(), action->retain());
                break;
            }

            case ActionObject::Type::telegram:
            {
                TelegramAction *action = reinterpret_cast <TelegramAction*> (item.data());

                if (!m_telegramToken.isEmpty() && m_telegramChat)
                    system(QString("curl -X POST -H 'Content-Type: application/json' -d '%1' -s https://api.telegram.org/bot%2/sendMessage > /dev/null &").arg(QJsonDocument(QJsonObject {{"chat_id", m_telegramChat}, {"text", action->message()}, {"parse_mode", "Markdown"}}).toJson(QJsonDocument::Compact), m_telegramToken).toUtf8());

                break;
            }
        }
    }
}

void Controller::mqttConnected(void)
{
    logInfo << "MQTT connected";
    mqttSubscribe(mqttTopic("service/#"));
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
}
