#include "automation.h"
#include "controller.h"
#include "logger.h"

AutomationList::AutomationList(QSettings *config, QObject *parent) : QObject(parent), m_timer(new QTimer(this)), m_sync(false)
{
    m_triggerTypes = QMetaEnum::fromType <TriggerObject::Type> ();
    m_conditionTypes = QMetaEnum::fromType <ConditionObject::Type> ();
    m_actionTypes = QMetaEnum::fromType <ActionObject::Type> ();

    m_triggerStatements = QMetaEnum::fromType <TriggerObject::Statement> ();
    m_conditionStatements = QMetaEnum::fromType <ConditionObject::Statement> ();
    m_actionStatements = QMetaEnum::fromType <ActionObject::Statement> ();

    m_file.setFileName(config->value("automation/database", "/opt/homed-automation/database.json").toString());
    m_telegramChat = config->value("telegram/chat").toLongLong();

    connect(m_timer, &QTimer::timeout, this, &AutomationList::writeDatabase);
    m_timer->setSingleShot(true);
}

AutomationList::~AutomationList(void)
{
    m_sync = true;
    writeDatabase();
}

void AutomationList::init(void)
{
    QJsonObject json;

    if (!m_file.open(QFile::ReadOnly))
        return;

    json = QJsonDocument::fromJson(m_file.readAll()).object();
    m_states = json.value("states").toObject().toVariantMap();
    unserialize(json.value("automations").toArray());
    m_file.close();
}

void AutomationList::store(bool sync)
{
    m_sync = sync;
    m_timer->start(STORE_DATABASE_DELAY);
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
    Automation automation(new AutomationObject(json.value("name").toString().trimmed(), json.value("note").toString(), json.value("active").toBool(), json.value("debounce").toInt(), json.value("restart").toBool(), json.value("lastTriggered").toVariant().toLongLong()));
    QJsonArray triggers = json.value("triggers").toArray();

    for (auto it = triggers.begin(); it != triggers.end(); it++)
    {
        QJsonObject item = it->toObject();
        TriggerObject::Type type = static_cast <TriggerObject::Type> (m_triggerTypes.keyToValue(item.value("type").toString().toUtf8().constData()));
        Trigger trigger;

        switch (type)
        {
            case TriggerObject::Type::property:
            {
                QString endpoint = item.value("endpoint").toString().trimmed(), property = item.value("property").toString().trimmed();

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
                QString topic = item.value("topic").toString().trimmed(), property = item.value("property").toString().trimmed();

                if (topic.isEmpty())
                    continue;

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
                QString message = item.value("message").toString().trimmed();
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

            case TriggerObject::Type::time:
            {
                trigger = Trigger(new TimeTrigger(item.value("time").toVariant()));
                break;
            }

            case TriggerObject::Type::interval:
            {
                trigger = Trigger(new IntervalTrigger(item.value("interval").toInt()));
                break;
            }
        }

        if (trigger.isNull())
            continue;

        trigger->setParent(automation.data());
        trigger->setName(item.value("name").toString().trimmed());

        automation->triggers().append(trigger);
    }

    unserializeConditions(automation->conditions(), json.value("conditions").toArray());
    unserializeActions(automation->actions(), json.value("actions").toArray());

    if (automation->name().isEmpty() || automation->triggers().isEmpty() || automation->actions().isEmpty())
        return Automation();

    return automation;
}

void AutomationList::parsePattern(const QString &string)
{
    QRegExp pattern("\\{\\{[^\\{\\}]*\\}\\}");
    int position = 0;

    while ((position = pattern.indexIn(string, position)) != -1)
    {
        QString item = pattern.cap();
        QList <QString> list = item.mid(2, item.length() - 4).split('|');

        if (list.value(0).trimmed() == "mqtt")
            emit addSubscription(list.value(1).trimmed());

        position += item.length();
    }
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
                QString endpoint = item.value("endpoint").toString().trimmed(), property = item.value("property").toString().trimmed();

                if (endpoint.isEmpty() || property.isEmpty())
                    continue;

                for (int i = 0; i < m_conditionStatements.keyCount(); i++)
                {
                    QVariant value = item.value(m_conditionStatements.key(i)).toVariant();

                    if (!value.isValid())
                        continue;

                    list.append(Condition(new PropertyCondition(endpoint, property, static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value)));
                    parsePattern(value.toString());
                    break;
                }

                break;
            }

            case ConditionObject::Type::mqtt:
            {
                QString topic = item.value("topic").toString().trimmed(), property = item.value("property").toString().trimmed();

                if (topic.isEmpty())
                    continue;

                for (int i = 0; i < m_conditionStatements.keyCount(); i++)
                {
                    QVariant value = item.value(m_conditionStatements.key(i)).toVariant();

                    if (!value.isValid())
                        continue;

                    list.append(Condition(new MqttCondition(topic, property, static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value)));
                    parsePattern(value.toString());
                    emit addSubscription(topic);
                    break;
                }

                break;
            }

            case ConditionObject::Type::state:
            {
                QString name = item.value("name").toString().trimmed();

                if (name.isEmpty())
                    continue;

                for (int i = 0; i < m_conditionStatements.keyCount(); i++)
                {
                    QVariant value = item.value(m_conditionStatements.key(i)).toVariant();

                    if (!value.isValid())
                        continue;

                    list.append(Condition(new StateCondition(name, static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value)));
                    parsePattern(value.toString());
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

            case ConditionObject::Type::pattern:
            {
                QString pattern = item.value("pattern").toString().trimmed();

                if (pattern.isEmpty())
                    continue;

                for (int i = 0; i < m_conditionStatements.keyCount(); i++)
                {
                    QVariant value = item.value(m_conditionStatements.key(i)).toVariant();

                    if (!value.isValid())
                        continue;

                    list.append(Condition(new PatternCondition(pattern, static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value)));
                    parsePattern(pattern);
                    parsePattern(value.toString());
                    break;
                }

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

void AutomationList::unserializeActions(ActionList &list, const QJsonArray &actions)
{
    for (auto it = actions.begin(); it != actions.end(); it++)
    {
        QJsonObject item = it->toObject();
        ActionObject::Type type = static_cast <ActionObject::Type> (m_actionTypes.keyToValue(item.value("type").toString().toUtf8().constData()));
        Action action;

        switch (type)
        {
            case ActionObject::Type::property:
            {
                QString endpoint = item.value("endpoint").toString().trimmed(), property = item.value("property").toString().trimmed();

                if (endpoint.isEmpty() || property.isEmpty())
                    continue;

                for (int i = 0; i < m_actionStatements.keyCount(); i++)
                {
                    QVariant value = item.value(m_actionStatements.key(i)).toVariant();

                    if (!value.isValid())
                        continue;

                    action = Action(new PropertyAction(endpoint, property, static_cast <ActionObject::Statement> (m_actionStatements.value(i)), value));
                    parsePattern(value.toString());
                    break;
                }

                break;
            }

            case ActionObject::Type::mqtt:
            {
                QString topic = item.value("topic").toString().trimmed(), message = item.value("message").toString().trimmed();

                if (topic.isEmpty() || message.isEmpty())
                    continue;

                action = Action(new MqttAction(topic, message, item.value("retain").toBool()));
                parsePattern(message);
                break;
            }

            case ActionObject::Type::state:
            {
                QString name = item.value("name").toString().trimmed();
                QVariant value = item.value("value").toVariant();

                if (name.isEmpty())
                    continue;

                action = Action(new StateAction(name, value));
                parsePattern(value.toString());
                break;
            }

            case ActionObject::Type::telegram:
            {
                QString message = item.value("message").toString().trimmed(), photo = item.value("photo").toString().trimmed();
                QJsonArray array = item.value("chats").toArray();
                QList <qint64> chats;

                if (message.isEmpty() && photo.isEmpty())
                    continue;

                for (auto it = array.begin(); it != array.end(); it++)
                    chats.append(it->toVariant().toLongLong());

                action = Action(new TelegramAction(message, photo, item.value("keyboard").toString().trimmed(), item.value("thread").toVariant().toLongLong(), item.value("silent").toBool(), chats));
                parsePattern(message);
                break;
            }

            case ActionObject::Type::shell:
            {
                QString command = item.value("command").toString().trimmed();

                if (command.isEmpty())
                    continue;

                action = Action(new ShellAction(command));
                parsePattern(command);
                break;
            }

            case ActionObject::Type::condition:
            {
                action = Action(new ConditionAction(&list));
                unserializeConditions(reinterpret_cast <ConditionAction*> (action.data())->conditions(), item.value("conditions").toArray());
                unserializeActions(reinterpret_cast <ConditionAction*> (action.data())->actions(true), item.value("then").toArray());
                unserializeActions(reinterpret_cast <ConditionAction*> (action.data())->actions(false), item.value("else").toArray());
                break;
            }

            case ActionObject::Type::delay:
            {
                QVariant value = item.value("delay").toVariant();

                if (!value.isValid())
                    continue;

                action = Action(new DelayAction(value));
                break;
            }
        }

        if (action.isNull())
            continue;

        action->setTriggerName(item.value("triggerName").toString().trimmed());
        list.append(action);
    }
}

void AutomationList::unserialize(const QJsonArray &automations)
{
    quint16 count = 0;

    for (auto it = automations.begin(); it != automations.end(); it++)
    {
        QJsonObject json = it->toObject();
        Automation automation = byName(json.value("name").toString().trimmed());

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

QJsonArray AutomationList::serializeConditions(const QList <Condition> &list)
{
    QJsonArray array;

    for (int i = 0; i < list.count(); i++)
    {
        ConditionObject::Type type = list.at(i)->type();
        QJsonObject json = {{"type", m_conditionTypes.valueToKey(static_cast <int> (type))}};

        switch (type)
        {
            case ConditionObject::Type::property:
            {
                PropertyCondition *condition = reinterpret_cast <PropertyCondition*> (list.at(i).data());
                json.insert("endpoint", condition->endpoint());
                json.insert("property", condition->property());
                json.insert(m_conditionStatements.valueToKey(static_cast <int> (condition->statement())), QJsonValue::fromVariant(condition->value()));
                break;
            }

            case ConditionObject::Type::mqtt:
            {
                MqttCondition *condition = reinterpret_cast <MqttCondition*> (list.at(i).data());

                json.insert("topic", condition->topic());
                json.insert(m_conditionStatements.valueToKey(static_cast <int> (condition->statement())), QJsonValue::fromVariant(condition->value()));

                if (!condition->property().isEmpty())
                    json.insert("property", condition->property());

                break;
            }

            case ConditionObject::Type::state:
            {
                StateCondition *condition = reinterpret_cast <StateCondition*> (list.at(i).data());
                json.insert("name", condition->name());
                json.insert(m_conditionStatements.valueToKey(static_cast <int> (condition->statement())), QJsonValue::fromVariant(condition->value()));
                break;
            }

            case ConditionObject::Type::date:
            {
                DateCondition *condition = reinterpret_cast <DateCondition*> (list.at(i).data());
                json.insert(m_conditionStatements.valueToKey(static_cast <int> (condition->statement())), QJsonValue::fromVariant(condition->value()));
                break;
            }

            case ConditionObject::Type::time:
            {
                TimeCondition *condition = reinterpret_cast <TimeCondition*> (list.at(i).data());
                json.insert(m_conditionStatements.valueToKey(static_cast <int> (condition->statement())), QJsonValue::fromVariant(condition->value()));
                break;
            }

            case ConditionObject::Type::week:
            {
                WeekCondition *condition = reinterpret_cast <WeekCondition*> (list.at(i).data());
                json.insert("days", QJsonValue::fromVariant(condition->value()));
                break;
            }

            case ConditionObject::Type::pattern:
            {
                PatternCondition *condition = reinterpret_cast <PatternCondition*> (list.at(i).data());
                json.insert("pattern", condition->pattern());
                json.insert(m_conditionStatements.valueToKey(static_cast <int> (condition->statement())), QJsonValue::fromVariant(condition->value()));
                break;
            }

            case ConditionObject::Type::AND:
            case ConditionObject::Type::OR:
            case ConditionObject::Type::NOT:
            {
                NestedCondition *condition = reinterpret_cast <NestedCondition*> (list.at(i).data());
                json.insert("conditions", serializeConditions(condition->conditions()));
                break;
            }
        }

        array.append(json);
    }

    return array;
}

QJsonArray AutomationList::serializeActions(const ActionList &list)
{
    QJsonArray array;

    for (int i = 0; i < list.count(); i++)
    {
        ActionObject::Type type = list.at(i)->type();
        QJsonObject json = {{"type", m_actionTypes.valueToKey(static_cast <int> (type))}};

        switch (type)
        {
            case ActionObject::Type::property:
            {
                PropertyAction *action = reinterpret_cast <PropertyAction*> (list.at(i).data());
                json.insert("endpoint", action->endpoint());
                json.insert("property", action->property());
                json.insert(m_actionStatements.valueToKey(static_cast <int> (action->statement())), QJsonValue::fromVariant(action->value()));
                break;
            }

            case ActionObject::Type::mqtt:
            {
                MqttAction *action = reinterpret_cast <MqttAction*> (list.at(i).data());
                json.insert("topic", action->topic());
                json.insert("message", action->message());
                json.insert("retain", action->retain());
                break;
            }

            case ActionObject::Type::state:
            {
                StateAction *action = reinterpret_cast <StateAction*> (list.at(i).data());
                json.insert("name", action->name());

                if (action->value().isValid())
                    json.insert("value", QJsonValue::fromVariant(action->value()));

                break;
            }

            case ActionObject::Type::telegram:
            {
                TelegramAction *action = reinterpret_cast <TelegramAction*> (list.at(i).data());
                QList <QVariant> chats;

                for (int j = 0; j < action->chats().count(); j++)
                    chats.append(action->chats().at(j));

                if (!action->message().isEmpty())
                    json.insert("message", action->message());

                if (!action->photo().isEmpty())
                    json.insert("photo", action->photo());

                if (!action->keyboard().isEmpty())
                    json.insert("keyboard", action->keyboard());

                if (action->thread())
                    json.insert("thread", action->thread());

                if (action->silent())
                    json.insert("silent", action->silent());

                if (!chats.isEmpty())
                    json.insert("chats", QJsonArray::fromVariantList(chats));

                break;
            }

            case ActionObject::Type::shell:
            {
                ShellAction *action = reinterpret_cast <ShellAction*> (list.at(i).data());
                json.insert("command", action->command());
                break;
            }

            case ActionObject::Type::condition:
            {
                ConditionAction *action = reinterpret_cast <ConditionAction*> (list.at(i).data());
                json.insert("conditions", serializeConditions(action->conditions()));
                json.insert("then", serializeActions(action->actions(true)));
                json.insert("else", serializeActions(action->actions(false)));
                break;
            }

            case ActionObject::Type::delay:
            {
                DelayAction *action = reinterpret_cast <DelayAction*> (list.at(i).data());
                json.insert("delay", QJsonValue::fromVariant(action->value()));
                break;
            }
        }

        if (!list.at(i)->triggerName().isEmpty())
            json.insert("triggerName", list.at(i)->triggerName());

        array.append(json);
    }

    return array;
}

QJsonArray AutomationList::serialize(void)
{
    QJsonArray array;

    for (int i = 0; i < count(); i++)
    {
        const Automation &automation = at(i);
        QJsonObject json = {{"name", automation->name()}, {"active", automation->active()}, {"restart", automation->restart()}};
        QJsonArray triggers;

        if (!automation->note().isEmpty())
            json.insert("note", automation->note());

        if (automation->debounce())
            json.insert("debounce", automation->debounce());

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

                case TriggerObject::Type::time:
                {
                    TimeTrigger *trigger = reinterpret_cast <TimeTrigger*> (automation->triggers().at(j).data());
                    item.insert("time", QJsonValue::fromVariant(trigger->value()));
                    break;
                }

                case TriggerObject::Type::interval:
                {
                    IntervalTrigger *trigger = reinterpret_cast <IntervalTrigger*> (automation->triggers().at(j).data());
                    item.insert("interval", QJsonValue::fromVariant(trigger->value()));
                    break;
                }
            }

            if (!automation->triggers().at(j)->name().isEmpty())
                item.insert("name", automation->triggers().at(j)->name());

            triggers.append(item);
        }

        if (!triggers.isEmpty())
            json.insert("triggers", triggers);

        if (!automation->conditions().isEmpty())
            json.insert("conditions", serializeConditions(automation->conditions()));

        if (!automation->actions().isEmpty())
            json.insert("actions", serializeActions(automation->actions()));

        array.append(json);
    }

    return array;
}

void AutomationList::writeDatabase(void)
{
    QJsonObject json = {{"automations", serialize()}, {"states", QJsonObject::fromVariantMap(m_states)}, {"timestamp", QDateTime::currentSecsSinceEpoch()}, {"version", SERVICE_VERSION}};
    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    bool check = true;

    emit statusUpdated(json);

    if (!m_sync)
        return;

    m_sync = false;

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
