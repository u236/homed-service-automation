#include "automation.h"
#include "controller.h"
#include "logger.h"

AutomationList::AutomationList(QSettings *config, QObject *parent) : QObject(parent)
{
    m_triggerTypes        = QMetaEnum::fromType <TriggerObject::Type> ();
    m_conditionTypes      = QMetaEnum::fromType <ConditionObject::Type> ();
    m_actionTypes         = QMetaEnum::fromType <ActionObject::Type> ();

    m_triggerStatements   = QMetaEnum::fromType <TriggerObject::Statement> ();
    m_conditionStatements = QMetaEnum::fromType <ConditionObject::Statement> ();
    m_actionStatements    = QMetaEnum::fromType <ActionObject::Statement> ();

    m_file.setFileName(config->value("automation/database", "/opt/homed-automation/database.json").toString());
    m_telegramChat = config->value("telegram/chat").toLongLong();
}

AutomationList::~AutomationList(void)
{
    store(true);
}

void AutomationList::init(void)
{
    QJsonObject json;

    if (!m_file.open(QFile::ReadOnly))
        return;

    json = QJsonDocument::fromJson(m_file.readAll()).object();
    unserialize(json.value("automations").toArray());
    m_file.close();
}

void AutomationList::store(bool sync)
{
    QJsonObject json = {{"automations", serialize()}, {"timestamp", QDateTime::currentSecsSinceEpoch()}, {"version", SERVICE_VERSION}};
    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    bool check = true;

    emit statusUpdated(json);

    if (!sync)
        return;

    if (!m_file.open(QFile::WriteOnly))
    {
        logWarning << "Database not stored, file" << m_file.fileName() << "open error:" << m_file.errorString();
        return;
    }

    if (m_file.write(data) != data.length())
    {
        logWarning << "Database not stored, file" << m_file.fileName() << "open error:" << m_file.errorString();
        check = false;
    }

    m_file.close();

    if (!check)
        return;

    system("sync");
}

Automation AutomationList::byName(const QString &name, int *index)
{
    for (int i = 0; i < count(); i++)
    {
        if (at(i)->name() != name)
            continue;

        if (index)
            *index = i;

        return at(i);
    }

    return Automation();
}

Automation AutomationList::parse(const QJsonObject &json)
{
    QJsonArray triggers = json.value("triggers").toArray(), actions = json.value("actions").toArray();
    Automation automation(new AutomationObject(json.value("name").toString(), json.value("active").toBool(), json.value("debounce").toInt(), json.value("delay").toInt(), json.value("restart").toBool(), json.value("lastTriggered").toVariant().toLongLong()));

    for (auto it = triggers.begin(); it != triggers.end(); it++)
    {
        QJsonObject item = it->toObject();
        TriggerObject::Type type = static_cast <TriggerObject::Type> (m_triggerTypes.keyToValue(item.value("type").toString().toUtf8().constData()));
        Trigger trigger;

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

                    trigger = Trigger(new PropertyTrigger(endpoint, property, static_cast <TriggerObject::Statement> (m_triggerStatements.value(i)), value));
                    break;
                }

                break;
            }

            case TriggerObject::Type::mqtt:
            {
                QString topic = item.value("topic").toString(), property = item.value("property").toString();

                if (topic.isEmpty())
                    continue;

                // TODO: remove this few versions later
                if (item.contains("message"))
                    item.insert("equals", item.value("message"));

                for (int i = 0; i < m_triggerStatements.keyCount(); i++)
                {
                    QVariant value = item.value(m_triggerStatements.key(i)).toVariant();

                    if (!value.isValid())
                        continue;

                    trigger = Trigger(new MqttTrigger(topic, property, static_cast <TriggerObject::Statement> (m_triggerStatements.value(i)), value));
                    emit addSubscription(topic);
                    break;
                }

                break;
            }

            case TriggerObject::Type::telegram:
            {
                QString message = item.value("message").toString();
                QJsonArray array = item.value("chats").toArray();
                QList <qint64> chats;

                if (message.isEmpty())
                    continue;

                for (auto it = array.begin(); it != array.end(); it++)
                    chats.append(it->toVariant().toLongLong());

                if (chats.isEmpty())
                    chats.append(m_telegramChat);

                trigger = Trigger(new TelegramTrigger(message, chats));
                break;
            }

            case TriggerObject::Type::sunrise:
            {
                trigger = Trigger(new SunriseTrigger(static_cast <qint32> (item.value("offset").toInt())));
                break;
            }

            case TriggerObject::Type::sunset:
            {
                trigger = Trigger(new SunsetTrigger(static_cast <qint32> (item.value("offset").toInt())));
                break;
            }

            case TriggerObject::Type::time:
            {
                trigger = Trigger(new TimeTrigger(QTime::fromString(item.value("time").toString())));
                break;
            }
        }

        if (trigger.isNull())
            continue;

        trigger->setName(item.value("name").toString());
        automation->triggers().append(trigger);
    }

    for (auto it = actions.begin(); it != actions.end(); it++)
    {
        QJsonObject item = it->toObject();
        ActionObject::Type type = static_cast <ActionObject::Type> (m_actionTypes.keyToValue(item.value("type").toString().toUtf8().constData()));
        Action action;

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

                    action = Action(new PropertyAction(endpoint, property, static_cast <ActionObject::Statement> (m_actionStatements.value(i)), value));
                    break;
                }

                break;
            }

            case ActionObject::Type::mqtt:
            {
                QString topic = item.value("topic").toString(), message = item.value("message").toString();

                if (topic.isEmpty() || message.isEmpty())
                    continue;

                action = Action(new MqttAction(topic, message, item.value("retain").toBool()));
                break;
            }

            case ActionObject::Type::telegram:
            {
                QString message = item.value("message").toString();
                QJsonArray array = item.value("chats").toArray();
                QList <qint64> chats;

                if (message.isEmpty())
                    continue;

                for (auto it = array.begin(); it != array.end(); it++)
                    chats.append(it->toVariant().toLongLong());

                action = Action(new TelegramAction(message, item.value("silent").toBool(), chats));
                break;
            }

            case ActionObject::Type::shell:
            {
                QString command = item.value("command").toString();

                if (command.isEmpty())
                    continue;

                action = Action(new ShellAction(command));
                break;
            }
        }

        if (action.isNull())
            continue;

        action->setTriggerName(item.value("triggerName").toString());
        automation->actions().append(action);
    }

    if (automation->name().isEmpty() || automation->triggers().isEmpty() || automation->actions().isEmpty())
        return Automation();

    unserializeConditions(automation->conditions(), json.value("conditions").toArray());
    return automation;
}

void AutomationList::unserializeConditions(QList <Condition> &list, const QJsonArray &conditions)
{
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

                    list.append(Condition(new PropertyCondition(endpoint, property, static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value)));
                    break;
                }

                break;
            }

            case ConditionObject::Type::date:
            {
                for (int i = 0; i < m_conditionStatements.keyCount(); i++)
                {
                    QVariant value = item.value(m_conditionStatements.key(i)).toVariant();

                    if (!value.isValid())
                        continue;

                    list.append(Condition(new DateCondition(static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value)));
                    break;
                }

                break;
            }

            case ConditionObject::Type::time:
            {
                for (int i = 0; i < m_conditionStatements.keyCount(); i++)
                {
                    QVariant value = item.value(m_conditionStatements.key(i)).toVariant();

                    if (!value.isValid())
                        continue;

                    list.append(Condition(new TimeCondition(static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value)));
                    break;
                }

                break;
            }

            case ConditionObject::Type::week:
            {
                QVariant value = item.value("days").toVariant();

                if (!value.isValid())
                    continue;

                list.append(Condition(new WeekCondition(value)));
                break;
            }

            case ConditionObject::Type::AND:
            case ConditionObject::Type::OR:
            case ConditionObject::Type::NOT:
            {
                Condition condition(new NestedCondition(type));
                unserializeConditions(reinterpret_cast <NestedCondition*> (condition.data())->conditions(), item.value("conditions").toArray());
                list.append(condition);
                break;
            }
        }
    }
}

QJsonArray AutomationList::serializeConditions(const QList <Condition> &list)
{
    QJsonArray array;

    for (int j = 0; j < list.count(); j++)
    {
        ConditionObject::Type type = list.at(j)->type();
        QJsonObject json = {{"type", m_conditionTypes.valueToKey(static_cast <int> (type))}};

        switch (type)
        {
            case ConditionObject::Type::property:
            {
                PropertyCondition *condition = reinterpret_cast <PropertyCondition*> (list.at(j).data());
                json.insert("endpoint", condition->endpoint());
                json.insert("property", condition->property());
                json.insert(m_conditionStatements.valueToKey(static_cast <int> (condition->statement())), QJsonValue::fromVariant(condition->value()));
                break;
            }

            case ConditionObject::Type::date:
            {
                DateCondition *condition = reinterpret_cast <DateCondition*> (list.at(j).data());
                json.insert(m_conditionStatements.valueToKey(static_cast <int> (condition->statement())), QJsonValue::fromVariant(condition->value()));
                break;
            }

            case ConditionObject::Type::time:
            {
                TimeCondition *condition = reinterpret_cast <TimeCondition*> (list.at(j).data());
                json.insert(m_conditionStatements.valueToKey(static_cast <int> (condition->statement())), QJsonValue::fromVariant(condition->value()));
                break;
            }

            case ConditionObject::Type::week:
            {
                WeekCondition *condition = reinterpret_cast <WeekCondition*> (list.at(j).data());
                json.insert("days", QJsonValue::fromVariant(condition->value()));
                break;
            }

            case ConditionObject::Type::AND:
            case ConditionObject::Type::OR:
            case ConditionObject::Type::NOT:
            {
                NestedCondition *condition = reinterpret_cast <NestedCondition*> (list.at(j).data());
                json.insert("conditions", serializeConditions(condition->conditions()));
                break;
            }
        }

        array.append(json);
    }

    return array;
}

void AutomationList::unserialize(const QJsonArray &automations)
{
    quint16 count = 0;

    for (auto it = automations.begin(); it != automations.end(); it++)
    {
        QJsonObject json = it->toObject();
        Automation automation = byName(json.value("name").toString());

        if (!automation.isNull())
            continue;

        automation = parse(json);

        if (automation.isNull())
            continue;

        append(automation);
        count++;
    }

    if (count)
        logInfo << count << "automations loaded";
}

QJsonArray AutomationList::serialize(void)
{
    QJsonArray array;

    for (int i = 0; i < count(); i++)
    {
        const Automation &automation = at(i);
        QJsonObject json = {{"name", automation->name()}, {"active", automation->active()}};
        QJsonArray triggers, actions;

        if (automation->debounce())
            json.insert("debounce", automation->debounce());

        if (automation->delay())
            json.insert("delay", automation->delay());

        if (automation->restart())
            json.insert("restart", automation->restart());

        if (automation->lastTriggered())
            json.insert("lastTriggered", automation->lastTriggered());

        for (int j = 0; j < automation->triggers().count(); j++)
        {
            TriggerObject::Type type = automation->triggers().at(j)->type();
            QJsonObject item = {{"type", m_triggerTypes.valueToKey(static_cast <int> (type))}};

            switch (type)
            {
                case TriggerObject::Type::property:
                {
                    PropertyTrigger *trigger = reinterpret_cast <PropertyTrigger*> (automation->triggers().at(j).data());
                    item.insert("endpoint", trigger->endpoint());
                    item.insert("property", trigger->property());
                    item.insert(m_triggerStatements.valueToKey(static_cast <int> (trigger->statement())), QJsonValue::fromVariant(trigger->value()));
                    break;
                }

                case TriggerObject::Type::mqtt:
                {
                    MqttTrigger *trigger = reinterpret_cast <MqttTrigger*> (automation->triggers().at(j).data());

                    item.insert("topic", trigger->topic());
                    item.insert(m_triggerStatements.valueToKey(static_cast <int> (trigger->statement())), QJsonValue::fromVariant(trigger->value()));

                    if (!trigger->property().isEmpty())
                        item.insert("property", trigger->property());

                    break;
                }

                case TriggerObject::Type::telegram:
                {
                    TelegramTrigger *trigger = reinterpret_cast <TelegramTrigger*> (automation->triggers().at(j).data());
                    QList <QVariant> chats;

                    item.insert("message", trigger->message());

                    for (int k = 0; k < trigger->chats().count(); k++)
                         if (trigger->chats().at(k) != m_telegramChat || trigger->chats().count() > 1)
                            chats.append(trigger->chats().at(k));

                    if (!chats.isEmpty())
                        item.insert("chats", QJsonArray::fromVariantList(chats));

                    break;
                }

                case TriggerObject::Type::sunrise:
                {
                    SunriseTrigger *trigger = reinterpret_cast <SunriseTrigger*> (automation->triggers().at(j).data());
                    item.insert("offset", QJsonValue::fromVariant(trigger->offset()));

                    break;
                }

                case TriggerObject::Type::sunset:
                {
                    SunsetTrigger *trigger = reinterpret_cast <SunsetTrigger*> (automation->triggers().at(j).data());
                    item.insert("offset", QJsonValue::fromVariant(trigger->offset()));
                    break;
                }

                case TriggerObject::Type::time:
                {
                    TimeTrigger *trigger = reinterpret_cast <TimeTrigger*> (automation->triggers().at(j).data());
                    item.insert("time", trigger->time().toString("hh:mm"));
                    break;
                }
            }

            if (!automation->triggers().at(j)->name().isEmpty())
                item.insert("name", automation->triggers().at(j)->name());

            triggers.append(item);
        }

        for (int j = 0; j < automation->actions().count(); j++)
        {
            ActionObject::Type type = automation->actions().at(j)->type();
            QJsonObject item = {{"type", m_actionTypes.valueToKey(static_cast <int> (type))}};

            switch (type)
            {

                case ActionObject::Type::property:
                {
                    PropertyAction *action = reinterpret_cast <PropertyAction*> (automation->actions().at(j).data());
                    item.insert("endpoint", action->endpoint());
                    item.insert("property", action->property());
                    item.insert(m_actionStatements.valueToKey(static_cast <int> (action->statement())), QJsonValue::fromVariant(action->value()));
                    break;
                }

                case ActionObject::Type::mqtt:
                {
                    MqttAction *action = reinterpret_cast <MqttAction*> (automation->actions().at(j).data());
                    item.insert("topic", action->topic());
                    item.insert("message", action->message());
                    item.insert("retain", action->retain());
                    break;
                }

                case ActionObject::Type::telegram:
                {
                    TelegramAction *action = reinterpret_cast <TelegramAction*> (automation->actions().at(j).data());
                    QList <QVariant> chats;

                    item.insert("message", action->message());
                    item.insert("silent", action->silent());

                    for (int k = 0; k < action->chats().count(); k++)
                        chats.append(action->chats().at(k));

                    if (!chats.isEmpty())
                        item.insert("chats", QJsonArray::fromVariantList(chats));

                    break;
                }

                case ActionObject::Type::shell:
                {
                    ShellAction *action = reinterpret_cast <ShellAction*> (automation->actions().at(j).data());
                    item.insert("command", action->command());
                    break;
                }
            }

            if (!automation->actions().at(j)->triggerName().isEmpty())
                item.insert("triggerName", automation->actions().at(j)->triggerName());

            actions.append(item);
        }

        if (!triggers.isEmpty())
            json.insert("triggers", triggers);

        if (!actions.isEmpty())
            json.insert("actions", actions);

        if (!automation->conditions().isEmpty())
            json.insert("conditions", serializeConditions(automation->conditions()));

        array.append(json);
    }

    return array;
}
