#ifndef ACTION_H
#define ACTION_H

#include "condition.h"

class ActionObject;
typedef QSharedPointer <ActionObject> Action;

class ActionList : public QList <Action>
{

public:

    ActionList(void) : m_parent(nullptr) {}

    inline ActionList *parent(void) { return m_parent; }
    inline void setParent(ActionList *value) { m_parent = value; }

    inline quint32 index(void) { return m_index; }
    inline void setIndex(quint32 value) { m_index = value; }

private:

    ActionList *m_parent;
    quint32 m_index;

};

class ActionObject : public QObject
{
    Q_OBJECT

public:

    enum class Type
    {
        property,
        mqtt,
        state,
        telegram,
        shell,
        condition,
        delay,
        exit
    };

    enum class Statement
    {
        value,
        increase,
        decrease
    };

    ActionObject(Type type) :
        QObject(nullptr), m_type(type) {}

    inline Type type(void) { return m_type; }

    inline QString uuid(void) { return m_uuid; }
    inline void setUuid(const QString &value) { m_uuid = value; }

    inline QString triggerName(void) { return m_triggerName; }
    inline void setTriggerName(const QString &value) { m_triggerName = value; }

    Q_ENUM(Type)
    Q_ENUM(Statement)

private:

    Type m_type;
    QString m_uuid, m_triggerName;

};

class PropertyAction : public ActionObject
{

public:

    PropertyAction(const QString &endpoint, const QString &property, Statement statement, const QVariant &value) :
        ActionObject(Type::property), m_endpoint(endpoint), m_property(property), m_statement(statement), m_value(value) {}

    inline QString endpoint(void) { return m_endpoint; }
    inline QString property(void) { return m_property; }
    inline Statement statement(void) { return m_statement; }
    inline QVariant value(void) { return m_value; }

    QVariant value(const QVariant &oldValue);

private:

    QString m_endpoint, m_property;
    Statement m_statement;
    QVariant m_value;

};

class MqttAction : public ActionObject
{

public:

    MqttAction(const QString &topic, const QString &message, bool retain) :
        ActionObject(Type::mqtt), m_topic(topic), m_message(message), m_retain(retain) {}

    inline QString topic(void) { return m_topic; }
    inline QString message(void) { return m_message; }
    inline bool retain(void) { return m_retain; }

private:

    QString m_topic, m_message;
    bool m_retain;

};

class StateAction : public ActionObject
{

public:

    StateAction(const QString &name, const QVariant &value) :
        ActionObject(Type::state), m_name(name), m_value(value) {}

    inline QString name(void) { return m_name; }
    inline QVariant value(void) { return m_value; }

private:

    QString m_name;
    QVariant m_value;

};

class TelegramAction : public ActionObject
{

public:

    TelegramAction(const QString &message, const QString &file, const QString &keyboard, qint64 thread, bool silent, bool remove, bool update, const QList <qint64> &chats) :
        ActionObject(Type::telegram), m_message(message), m_file(file), m_keyboard(keyboard), m_thread(thread), m_silent(silent), m_remove(remove), m_update(update), m_chats(chats) {}

    inline QString message(void) { return m_message; }
    inline QString file(void) { return m_file; }
    inline QString keyboard(void) { return m_keyboard; }
    inline qint64 thread(void) { return m_thread; }
    inline bool silent(void) { return m_silent; }
    inline bool remove(void) { return m_remove; }
    inline bool update(void) { return m_update; }

    inline QList <qint64> &chats(void) { return m_chats; }

private:

    QString m_message, m_file, m_keyboard;
    qint64 m_thread;
    bool m_silent, m_remove, m_update;

    QList <qint64> m_chats;
};

class ShellAction : public ActionObject
{

public:

    ShellAction(const QString &command, quint32 timeout) :
        ActionObject(Type::shell), m_command(command), m_timeout(timeout) {}

    inline QString command(void) { return m_command; }
    inline quint32 timeout(void) { return m_timeout; }

private:

    QString m_command;
    quint32 m_timeout;

};

class ConditionAction : public ActionObject
{

public:

    ConditionAction(ConditionObject::Type conditionType, bool hideElse, ActionList *parent) :
        ActionObject(Type::condition), m_conditionType(conditionType), m_hideElse(hideElse) { m_then.setParent(parent); m_else.setParent(parent); }

    inline ConditionObject::Type conditionType(void) { return m_conditionType; }
    inline bool hideElse(void) { return m_hideElse; }

    inline QList <Condition> &conditions(void) { return m_conditions; }
    inline ActionList &actions(bool match) { return match ? m_then : m_else; }

private:

    ConditionObject::Type m_conditionType;
    bool m_hideElse;

    QList <Condition> m_conditions;
    ActionList m_then, m_else;

};

class DelayAction : public ActionObject
{

public:

    DelayAction(const QVariant &value) :
        ActionObject(Type::delay), m_value(value) {}

    inline QVariant value(void) { return m_value; }

private:

    QVariant m_value;

};

class ExitAction : public ActionObject
{

public:

    ExitAction(void) : ActionObject(Type::exit) {}

};

#endif
