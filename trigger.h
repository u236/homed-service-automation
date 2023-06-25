#ifndef TRIGGER_H
#define TRIGGER_H

#include <QSharedPointer>
#include <QTime>
#include <QVariant>

class TriggerObject;
typedef QSharedPointer <TriggerObject> Trigger;

class TriggerObject : public QObject
{
    Q_OBJECT

public:

    enum class Type
    {
        property,
        telegram,
        mqtt,
        sunrise,
        sunset,
        time
    };

    enum class Statement
    {
        equals,
        above,
        below,
        between
    };

    TriggerObject(Type type) :
        QObject(nullptr), m_type(type) {}

    inline Type type(void) { return m_type; }

    Q_ENUM(Type)
    Q_ENUM(Statement)

private:

    Type m_type;

};

class PropertyTrigger : public TriggerObject
{

public:

    PropertyTrigger(const QString &endpoint, const QString &property, Statement statement, const QVariant &value) :
        TriggerObject(Type::property), m_endpoint(endpoint), m_property(property), m_statement(statement), m_value(value) {}

    inline QString endpoint(void) { return m_endpoint; }
    inline QString property(void) { return m_property; }
    inline Statement statement(void) { return m_statement; }
    inline QVariant value(void) { return m_value; }

    bool match(const QVariant &oldValue, const QVariant &newValue);

private:

    QString m_endpoint, m_property;
    Statement m_statement;
    QVariant m_value;

};

class TelegramTrigger : public TriggerObject
{

public:

    TelegramTrigger(const QString &message, const QList <qint64> &chats) :
        TriggerObject(Type::telegram), m_message(message), m_chats(chats) {}

    inline QString message(void) { return m_message; }
    inline QList <qint64> &chats(void) { return m_chats; }

    inline bool match(const QString &message, qint64 chat) { return message == m_message && m_chats.contains(chat); }

private:

    QString m_message;
    QList <qint64> m_chats;

};

class MqttTrigger : public TriggerObject
{

public:

    MqttTrigger(const QString &topic, const QString &message) :
        TriggerObject(Type::mqtt), m_topic(topic), m_message(message) {}

    inline QString topic(void) { return m_topic; }
    inline QString message(void) { return m_message; }

    inline bool match(const QString &topic, const QString &message) { return topic == m_topic && message == m_message; }

private:

    QString m_topic, m_message;

};

class SunriseTrigger : public TriggerObject
{

public:

    SunriseTrigger(qint32 offset) :
        TriggerObject(Type::sunrise), m_offset(offset * 60) {}

    inline quint32 offset(void) { return m_offset / 60; }
    inline bool match(const QTime &sunrise, const QTime &value) { return value == sunrise.addSecs(m_offset); }

private:

    qint32 m_offset;

};

class SunsetTrigger : public TriggerObject
{

public:

    SunsetTrigger(qint32 offset) :
        TriggerObject(Type::sunset), m_offset(offset * 60) {}

    inline quint32 offset(void) { return m_offset / 60; }
    inline bool match(const QTime &sunset, const QTime &value) { return value == sunset.addSecs(m_offset); }

private:

    qint32 m_offset;

};

class TimeTrigger : public TriggerObject
{

public:

    TimeTrigger(const QTime &time) :
        TriggerObject(Type::time), m_time(time) {}

    inline QTime time(void) { return m_time; }
    inline bool match(const QTime &value) { return value == m_time; }

private:

    QTime m_time;

};

#endif
