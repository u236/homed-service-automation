#include <QRandomGenerator>
#include "controller.h"
#include "logger.h"

AutomationList::AutomationList(QSettings *config, QObject *parent) : QObject(parent), m_timer(new QTimer(this)), m_sync(false)
{
    m_automationModes = QMetaEnum::fromType <AutomationObject::Mode> ();

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
    QJsonObject json, messages;

    if (!m_file.open(QFile::ReadOnly))
        return;

    json = QJsonDocument::fromJson(m_file.readAll()).object();
    messages = json.value("messages").toObject();

    unserialize(json.value("automations").toArray());
    m_states = json.value("states").toObject().toVariantMap();

    for (auto it = messages.begin(); it != messages.end(); it++)
    {
        if (!m_telegramActions.contains(it.key().split(':').value(0)))
            continue;

        m_messages.insert(it.key(), it.value().toVariant().toLongLong());
    }

    m_file.close();
}

void AutomationList::store(bool sync)
{
    m_sync = sync;
    m_timer->start(STORE_DATABASE_DELAY);
}

AutomationObject::Mode AutomationList::getMode(const QJsonObject &json)
{
    int value = m_automationModes.keyToValue(json.value("mode").toString().toUtf8().constData());

    if (json.value("restart").toBool())
        return AutomationObject::Mode::restart;

    return value < 0 ? AutomationObject::Mode::single : static_cast <AutomationObject::Mode> (value);
}

Automation AutomationList::byUuid(const QString &uuid, int *index)
{
    for (int i = 0; i < count(); i++)
    {
        if (at(i)->uuid() != uuid)
            continue;

        if (index)
            *index = i;

        return at(i);
    }

    return Automation();
}

Automation AutomationList::byName(const QString &name)
{
    for (int i = 0; i < count(); i++)
        if (at(i)->name() == name)
            return at(i);

    return Automation();
}

Automation AutomationList::parse(const QJsonObject &json, bool add)
{
    QString uuid = json.value("uuid").toString().trimmed();
    Automation automation(new AutomationObject(getMode(json), add || uuid.isEmpty() ? randomData(16).toHex() : uuid, json.value("name").toString().trimmed(), json.value("note").toString(), json.value("active").toBool(), json.value("log").toBool(), json.value("debounce").toInt(), json.value("lastTriggered").toVariant().toLongLong()));
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

                    trigger = Trigger(new PropertyTrigger(endpoint, property, static_cast <TriggerObject::Statement> (m_triggerStatements.value(i)), value, item.value("force").toBool()));
                    break;
                }

                break;
            }

            case TriggerObject::Type::mqtt:
            {
                QString topic = item.value("topic").toString().trimmed(), property = item.value("property").toString().trimmed();

                if (topic.isEmpty() || topic == "#")
                    continue;

                for (int i = 0; i < m_triggerStatements.keyCount(); i++)
                {
                    QVariant value = item.value(m_triggerStatements.key(i)).toVariant();

                    if (!value.isValid())
                        continue;

                    trigger = Trigger(new MqttTrigger(topic, property, static_cast <TriggerObject::Statement> (m_triggerStatements.value(i)), value, item.value("force").toBool()));
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

                trigger = Trigger(new TelegramTrigger(message, m_telegramChat, chats));
                break;
            }

            case TriggerObject::Type::time:
            {
                trigger = Trigger(new TimeTrigger(item.value("time").toVariant()));
                break;
            }

            case TriggerObject::Type::interval:
            {
                trigger = Trigger(new IntervalTrigger(item.value("interval").toInt(), item.value("offset").toInt()));
                break;
            }

            case TriggerObject::Type::startup:
            {
                trigger = Trigger(new StartupTrigger);
                break;
            }
        }

        if (trigger.isNull())
            continue;

        trigger->setName(item.value("name").toString().trimmed());
        trigger->setActive(item.value("active").toBool(true));
        automation->triggers().append(trigger);
    }

    unserializeConditions(automation->conditions(), json.value("conditions").toArray());
    unserializeActions(automation->actions(), json.value("actions").toArray(), add);

    if (automation->name().isEmpty() || automation->triggers().isEmpty() || automation->actions().isEmpty())
        return Automation();

    return automation;
}

QByteArray AutomationList::randomData(int length)
{
    QByteArray data;

    for (int i = 0; i < length; i++)
        data.append(static_cast <char> (QRandomGenerator::global()->generate()));

    return data;
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
        Condition condition;
        bool nested = false;

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

                    condition = Condition(new PropertyCondition(endpoint, property, static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value));
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

                    condition = Condition(new MqttCondition(topic, property, static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value));
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

                    condition = Condition(new StateCondition(name, static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value));
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

                    condition = Condition(new DateCondition(static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value));
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

                    condition = Condition(new TimeCondition(static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value));
                    break;
                }

                break;
            }

            case ConditionObject::Type::week:
            {
                QVariant value = item.value("days").toVariant();

                if (!value.isValid())
                    continue;

                condition = Condition(new WeekCondition(value));
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

                    condition = Condition(new PatternCondition(pattern, static_cast <ConditionObject::Statement> (m_conditionStatements.value(i)), value));
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
                condition = Condition(new NestedCondition(type));
                unserializeConditions(reinterpret_cast <NestedCondition*> (condition.data())->conditions(), item.value("conditions").toArray());
                nested = true;
                break;
            }
        }

        condition->setActive(nested ? true : item.value("active").toBool(true));
        list.append(condition);
    }
}

void AutomationList::unserializeActions(ActionList &list, const QJsonArray &actions, bool add)
{
    QList <QString> uuidList;

    for (auto it = actions.begin(); it != actions.end(); it++)
    {
        QJsonObject item = it->toObject();
        QString uuid = item.value("uuid").toString().trimmed();
        ActionObject::Type type = static_cast <ActionObject::Type> (m_actionTypes.keyToValue(item.value("type").toString().toUtf8().constData()));
        Action action;

        if (add || uuid.isEmpty() || uuidList.contains(uuid))
            uuid = randomData(16).toHex();

        uuidList.append(uuid);

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
                QString message = item.value("message").toString().trimmed(), file = item.value("file").toString().trimmed();
                QJsonArray array = item.value("chats").toArray();
                QList <qint64> chats;

                if (message.isEmpty() && file.isEmpty())
                    continue;

                if (!m_telegramActions.contains(uuid))
                    m_telegramActions.append(uuid);

                for (auto it = array.begin(); it != array.end(); it++)
                    chats.append(it->toVariant().toLongLong());

                action = Action(new TelegramAction(message, file, item.value("keyboard").toString().trimmed(), item.value("thread").toVariant().toLongLong(), item.value("silent").toBool(), item.value("remove").toBool(), item.value("update").toBool(), chats));
                parsePattern(message);
                break;
            }

            case ActionObject::Type::shell:
            {
                QString command = item.value("command").toString().trimmed();

                if (command.isEmpty())
                    continue;

                action = Action(new ShellAction(command, static_cast <quint32> (item.value("timeout").toInt(30))));
                parsePattern(command);
                break;
            }

            case ActionObject::Type::condition:
            {
                ConditionObject::Type conditionType = static_cast <ConditionObject::Type> (m_conditionTypes.keyToValue(item.value("conditionType").toString().toUtf8().constData()));
                action = Action(new ConditionAction(static_cast <int> (conditionType) < 0 ? ConditionObject::Type::AND : conditionType, item.value("hideElse").toBool(), &list));
                unserializeConditions(reinterpret_cast <ConditionAction*> (action.data())->conditions(), item.value("conditions").toArray());
                unserializeActions(reinterpret_cast <ConditionAction*> (action.data())->actions(true), item.value("then").toArray(), add);
                unserializeActions(reinterpret_cast <ConditionAction*> (action.data())->actions(false), item.value("else").toArray(), add);
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

            case ActionObject::Type::exit:
            {
                action = Action(new ExitAction);
                break;
            }
        }

        if (action.isNull())
            continue;

        action->setUuid(uuid);
        action->setTriggerName(item.value("triggerName").toString().trimmed());
        action->setActive(type == ActionObject::Type::condition ? true : item.value("active").toBool(true));
        list.append(action);
    }
}

void AutomationList::unserialize(const QJsonArray &automations)
{
    quint16 count = 0;

    for (auto it = automations.begin(); it != automations.end(); it++)
    {
        QJsonObject json = it->toObject();
        Automation automation = byUuid(json.value("uuid").toString().trimmed());

        if (!automation.isNull())
            json.remove("uuid");

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
        bool nested = false;

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
                nested = true;
                break;
            }
        }

        if (!nested)
            json.insert("active", list.at(i)->active());

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
        QJsonObject json = {{"type", m_actionTypes.valueToKey(static_cast <int> (type))}, {"uuid", list.at(i)->uuid()}};

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

                if (!action->file().isEmpty())
                    json.insert("file", action->file());

                if (!action->keyboard().isEmpty())
                    json.insert("keyboard", action->keyboard());

                if (action->thread())
                    json.insert("thread", action->thread());

                if (action->silent())
                    json.insert("silent", true);

                if (action->remove())
                    json.insert("remove", true);

                if (action->update())
                    json.insert("update", true);

                if (!chats.isEmpty())
                    json.insert("chats", QJsonArray::fromVariantList(chats));

                break;
            }

            case ActionObject::Type::shell:
            {
                ShellAction *action = reinterpret_cast <ShellAction*> (list.at(i).data());
                json.insert("command", action->command());
                json.insert("timeout", QJsonValue::fromVariant(action->timeout()));
                break;
            }

            case ActionObject::Type::condition:
            {
                ConditionAction *action = reinterpret_cast <ConditionAction*> (list.at(i).data());
                json.insert("conditionType", m_conditionTypes.valueToKey(static_cast <int> (action->conditionType())));
                json.insert("hideElse", action->hideElse());
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

            case ActionObject::Type::exit: break;
        }

        if (!list.at(i)->triggerName().isEmpty())
            json.insert("triggerName", list.at(i)->triggerName());

        if (type != ActionObject::Type::condition)
            json.insert("active", list.at(i)->active());

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
        QJsonObject json = {{"mode", m_automationModes.valueToKey(static_cast <int> (automation->mode()))}, {"uuid", automation->uuid()}, {"name", automation->name()}, {"active", automation->active()}, {"log", automation->log()}};
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
            QJsonObject item = {{"type", m_triggerTypes.valueToKey(static_cast <int> (type))}, {"active", automation->triggers().at(j)->active()}};

            switch (type)
            {
                case TriggerObject::Type::property:
                {
                    PropertyTrigger *trigger = reinterpret_cast <PropertyTrigger*> (automation->triggers().at(j).data());
                    item.insert("endpoint", trigger->endpoint());
                    item.insert("property", trigger->property());
                    item.insert(m_triggerStatements.valueToKey(static_cast <int> (trigger->statement())), QJsonValue::fromVariant(trigger->value()));

                    if (trigger->force())
                        item.insert("force", true);

                    break;
                }

                case TriggerObject::Type::mqtt:
                {
                    MqttTrigger *trigger = reinterpret_cast <MqttTrigger*> (automation->triggers().at(j).data());

                    item.insert("topic", trigger->topic());
                    item.insert(m_triggerStatements.valueToKey(static_cast <int> (trigger->statement())), QJsonValue::fromVariant(trigger->value()));

                    if (!trigger->property().isEmpty())
                        item.insert("property", trigger->property());

                    if (trigger->force())
                        item.insert("force", true);

                    break;
                }

                case TriggerObject::Type::telegram:
                {
                    TelegramTrigger *trigger = reinterpret_cast <TelegramTrigger*> (automation->triggers().at(j).data());
                    QList <QVariant> chats;

                    item.insert("message", trigger->message());

                    for (int k = 0; k < trigger->chats().count(); k++)
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
                    item.insert("interval", QJsonValue::fromVariant(trigger->interval()));
                    item.insert("offset", QJsonValue::fromVariant(trigger->offset()));
                    break;
                }

                case TriggerObject::Type::startup: break;
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
    QJsonObject json = {{"automations", serialize()}, {"states", QJsonObject::fromVariantMap(m_states)}, {"timestamp", QDateTime::currentSecsSinceEpoch()}, {"version", SERVICE_VERSION}}, messages;

    emit statusUpdated(json);

    if (!m_sync)
        return;

    m_sync = false;

    for (auto it = m_messages.begin(); it != m_messages.end(); it++)
        messages.insert(it.key(), QJsonValue::fromVariant(it.value()));

    if (!messages.isEmpty())
        json.insert("messages", messages);

    if (reinterpret_cast <Controller*> (parent())->writeFile(m_file, QJsonDocument(json).toJson(QJsonDocument::Compact)))
        return;

    logWarning << "Database not stored";
}
