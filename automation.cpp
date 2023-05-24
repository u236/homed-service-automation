#include "automation.h"
#include "logger.h"

AutomationList::AutomationList(QSettings *config, QObject *parent) : QObject(parent)
{
    m_databaseFile.setFileName(config->value("automation/database", "/opt/homed-automation/database.json").toString());

    m_triggerTypes        = QMetaEnum::fromType <TriggerObject::Type> ();
    m_conditionTypes      = QMetaEnum::fromType <ConditionObject::Type> ();
    m_actionTypes         = QMetaEnum::fromType <ActionObject::Type> ();

    m_triggerStatements   = QMetaEnum::fromType <TriggerObject::Statement> ();
    m_conditionStatements = QMetaEnum::fromType <ConditionObject::Statement> ();
    m_actionStatements    = QMetaEnum::fromType <ActionObject::Statement> ();
}

void AutomationList::init(void)
{
    QJsonObject json;

    if (!m_databaseFile.open(QFile::ReadOnly))
        return;

    json = QJsonDocument::fromJson(m_databaseFile.readAll()).object();
    unserialize(json.value("automations").toArray());
}

void AutomationList::unserialize(const QJsonArray &automations)
{
    quint16 count = 0;

    for (auto it = automations.begin(); it != automations.end(); it++)
    {
        QJsonObject json = it->toObject();
        QJsonArray triggers = json.value("triggers").toArray(), conditions = json.value("conditions").toArray(), actions = json.value("actions").toArray();
        Automation automation(new AutomationObject(json.value("name").toString()));

        if (!json.value("active").toBool() || automation->name().isEmpty())
            continue;

        for (auto it = triggers.begin(); it != triggers.end(); it++)
        {
            QJsonObject item = it->toObject();
            TriggerObject::Type type = static_cast <TriggerObject::Type> (m_triggerTypes.keyToValue(item.value("type").toString().toUtf8().constData()));

            switch (type)
            {
                case TriggerObject::Type::property:
                {
                    QString endpoint = item.value("endpoint").toString(), property = item.value("property").toString();

                    if (endpoint.isEmpty() || property.isEmpty())
                        continue;

                    for (int i = 0; i < m_triggerStatements.keyCount(); i++)
                    {
                        QVariant value = item.value(m_triggerStatements.key(i)).toVariant();

                        if (!value.isValid())
                            continue;

                        automation->triggers().append(Trigger(new PropertyTrigger(endpoint, property, static_cast <TriggerObject::Statement> (m_triggerStatements.value(i)), value)));
                        break;
                    }

                    break;
                }

                case TriggerObject::Type::telegram:
                {
                    QString message = item.value("message").toString();

                    if (message.isEmpty())
                        continue;

                    automation->triggers().append(Trigger(new TelegramTrigger(message)));
                    break;
                }

                case TriggerObject::Type::mqtt:
                {
                    QString topic = item.value("topic").toString(), message = item.value("message").toString();

                    if (topic.isEmpty() || message.isEmpty())
                        continue;

                    automation->triggers().append(Trigger(new MqttTrigger(topic, message)));
                    emit addSubscription(topic);
                    break;
                }

                case TriggerObject::Type::sunrise:
                {
                    automation->triggers().append(Trigger(new SunriseTrigger(static_cast <qint32> (item.value("offset").toInt()))));
                    break;
                }

                case TriggerObject::Type::sunset:
                {
                    automation->triggers().append(Trigger(new SunsetTrigger(static_cast <qint32> (item.value("offset").toInt()))));
                    break;
                }

                case TriggerObject::Type::time:
                {
                    automation->triggers().append(Trigger(new TimeTrigger(QTime::fromString(item.value("time").toString()))));
                    break;
                }
            }
        }

        for (auto it = conditions.begin(); it != conditions.end(); it++)
        {
            QJsonObject item = it->toObject();
            ConditionObject::Type type = static_cast <ConditionObject::Type> (m_conditionTypes.keyToValue(item.value("type").toString().toUtf8().constData()));

            switch (type)
            {
                case ConditionObject::Type::property:
                {
                    QString endpoint = item.value("endpoint").toString(), property = item.value("property").toString();

                    if (endpoint.isEmpty() || property.isEmpty())
                        continue;

                    for (int i = 0; i < m_conditionStatements.keyCount(); i++)
                    {
                        QVariant value = item.value(m_conditionStatements.key(i)).toVariant();

                        if (!value.isValid())
                            continue;

                        automation->conditions().append(Condition(new PropertyCondition(endpoint, property, static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value)));
                        break;
                    }

                    break;
                }

                default:    // TODO: remove it
                    break;
            }
        }

        for (auto it = actions.begin(); it != actions.end(); it++)
        {
            QJsonObject item = it->toObject();
            ActionObject::Type type = static_cast <ActionObject::Type> (m_actionTypes.keyToValue(item.value("type").toString().toUtf8().constData()));

            switch (type)
            {
                case ActionObject::Type::property:
                {
                    QString endpoint = item.value("endpoint").toString(), property = item.value("property").toString();

                    if (endpoint.isEmpty() || property.isEmpty())
                        continue;

                    for (int i = 0; i < m_actionStatements.keyCount(); i++)
                    {
                        QVariant value = item.value(m_actionStatements.key(i)).toVariant();

                        if (!value.isValid())
                            continue;

                        automation->actions().append(Action(new PropertyAction(endpoint, property, static_cast <ActionObject::Statement> (m_actionStatements.value(i)), value)));
                        break;
                    }

                    break;
                }

                case ActionObject::Type::telegram:
                {
                    QString message = item.value("message").toString();

                    if (message.isEmpty())
                        continue;

                    automation->actions().append(Action(new TelegramAction(message)));
                    break;
                }

                case ActionObject::Type::mqtt:
                {
                    QString topic = item.value("topic").toString(), message = item.value("message").toString();

                    if (topic.isEmpty() || message.isEmpty())
                        continue;

                    automation->actions().append(Action(new MqttAction(topic, message, item.value("retain").toBool())));
                    break;
                }
            }
        }

        if (automation->triggers().isEmpty() || automation->actions().isEmpty())
            continue;

        append(automation);
        count++;
    }

    if (count)
        logInfo << count << "automations loaded";
}
