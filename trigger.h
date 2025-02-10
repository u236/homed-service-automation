#ifndef TRIGGER_H
#define TRIGGER_H

#include <QSharedPointer>
#include "parser.h"
#include "sun.h"

class TriggerObject;
typedef QSharedPointer <TriggerObject> Trigger;

class TriggerObject : public QObject
{
    Q_OBJECT

public:

    enum class Type
    {
        property,
        mqtt,
        telegram,
        time,
        interval
    };

    enum class Statement
    {
        equals,
        above,
        below,
        between,
        changes,
        updates
    };

    TriggerObject(Type type) :
        QObject(nullptr), m_type(type) {}

    inline Type type(void) { return m_type; }

    inline QString name(void) { return m_name; }
    inline void setName(const QString &value) { m_name = value; }

    Q_ENUM(Type)
    Q_ENUM(Statement)

protected:

    bool match(const QVariant &oldValue, const QVariant &newValue, Statement statement, QVariant value, bool force);

private:

    Type m_type;
    QString m_name;

};

class PropertyTrigger : public TriggerObject
{

public:

    PropertyTrigger(const QString &endpoint, const QString &property, Statement statement, const QVariant &value, bool force) :
        TriggerObject(Type::property), m_endpoint(endpoint), m_property(property), m_statement(statement), m_value(value), m_force(force) {}

    inline QString endpoint(void) { return m_endpoint; }
    inline QString property(void) { return m_property; }
    inline Statement statement(void) { return m_statement; }
    inline QVariant value(void) { return m_value; }
    inline bool force(void) { return m_force; }

    inline bool match(const QVariant &oldValue, const QVariant &newValue) {{ return TriggerObject::match(oldValue, newValue, m_statement, m_value, m_force); }}

private:

    QString m_endpoint, m_property;
    Statement m_statement;
    QVariant m_value;
    bool m_force;

};

class MqttTrigger : public TriggerObject
{

public:

    MqttTrigger(const QString &topic, const QString &property, Statement statement, const QVariant &value, bool force) :
        TriggerObject(Type::mqtt), m_topic(topic), m_property(property), m_statement(statement), m_value(value), m_force(force) {}

    inline QString topic(void) { return m_topic; }
    inline QString property(void) { return m_property; }
    inline Statement statement(void) { return m_statement; }
    inline QVariant value(void) { return m_value; }
    inline bool force(void) { return m_force; }

    inline bool match(const QByteArray &oldMessage, const QByteArray &newMessage) {{ return TriggerObject::match(parse(oldMessage), parse(newMessage), m_statement, m_value, m_force); }}

private:

    QString m_topic, m_property;
    Statement m_statement;
    QVariant m_value;
    bool m_force;

    inline QVariant parse(const QByteArray &message) { return m_property.isEmpty() ? message : Parser::jsonValue(QJsonDocument::fromJson(message).object(), m_property); }

};

class TelegramTrigger : public TriggerObject
{

public:

    TelegramTrigger(const QString &message, const QList <qint64> &chats) :
        TriggerObject(Type::telegram), m_message(message), m_chats(chats) {}

    inline QString message(void) { return m_message; }
    inline QList <qint64> &chats(void) { return m_chats; }

    inline bool match(const QString &message, qint64 chat) { return message.toLower() == m_message.toLower() && m_chats.contains(chat); }

private:

    QString m_message;
    QList <qint64> m_chats;

};

class TimeTrigger : public TriggerObject
{

public:

    TimeTrigger(const QVariant &value) :
        TriggerObject(Type::time), m_value(value) {}

    inline QVariant value(void) { return m_value; }
    inline bool match(const QTime &value, Sun *sun) { return value == sun->fromString(m_value.toString()); }

private:

    QVariant m_value;

};

class IntervalTrigger : public TriggerObject
{

public:

    IntervalTrigger(int value) :
        TriggerObject(Type::interval), m_value(value) {}

    inline int value(void) { return m_value; }
    inline bool match(int value) { return !m_value || value % m_value ? false : true; }

private:

    int m_value;

};

#endif
