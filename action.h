#ifndef ACTION_H
#define ACTION_H

#include <QSharedPointer>
#include <QVariant>
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
        telegram,
        shell,
        condition,
        delay
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

    inline QString triggerName(void) { return m_triggerName; }
    inline void setTriggerName(const QString &value) { m_triggerName = value; }

    Q_ENUM(Type)
    Q_ENUM(Statement)

private:

    Type m_type;
    QString m_triggerName;

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

class TelegramAction : public ActionObject
{

public:

    TelegramAction(const QString &message, bool silent, const QList <qint64> &chats) :
        ActionObject(Type::telegram), m_message(message), m_silent(silent), m_chats(chats) {}

    inline QString message(void) { return m_message; }
    inline bool silent(void) { return m_silent; }
    inline QList <qint64> &chats(void) { return m_chats; }

private:

    QString m_message;
    bool m_silent;

    QList <qint64> m_chats;
};

class ShellAction : public ActionObject
{

public:

    ShellAction(const QString &command) :
        ActionObject(Type::shell), m_command(command) {}

    inline QString command(void) { return m_command; }

private:

    QString m_command;

};

class ConditionAction : public ActionObject
{

public:

    ConditionAction(ActionList *parent) :
        ActionObject(Type::condition) { m_then.setParent(parent); m_else.setParent(parent); }

    inline QList <Condition> &conditions(void) { return m_conditions; }
    inline ActionList &actions(bool match) { return match ? m_then : m_else; }

private:

    QList <Condition> m_conditions;
    ActionList m_then, m_else;

};

class DelayAction : public ActionObject
{

public:

    DelayAction(quint32 delay) :
        ActionObject(Type::delay), m_delay(delay) {}

    inline quint32 delay(void) { return m_delay; }

private:

    quint32 m_delay;

};

#endif
