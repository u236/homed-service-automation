#ifndef TRIGGER_H
#define TRIGGER_H

#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedPointer>

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
        sunrise,
        sunset,
        time
    };

    enum class Statement
    {
        equals,
        above,
        below,
        between,
        changes
    };

    TriggerObject(Type type) :
        QObject(nullptr), m_type(type) {}

    inline Type type(void) { return m_type; }

    inline QString name(void) { return m_name; }
    inline void setName(const QString &value) { m_name = value; }

    Q_ENUM(Type)
    Q_ENUM(Statement)

protected:

    bool match(const QVariant &oldValue, const QVariant &newValue, Statement statement, const QVariant &value);

private:

    Type m_type;
    QString m_name;

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

    inline bool match(const QVariant &oldValue, const QVariant &newValue) {{ return TriggerObject::match(oldValue, newValue, m_statement, m_value); }}

private:

    QString m_endpoint, m_property;
    Statement m_statement;
    QVariant m_value;

};

class MqttTrigger : public TriggerObject
{

public:

    MqttTrigger(const QString &topic, const QString &property, Statement statement, const QVariant &value) :
        TriggerObject(Type::mqtt), m_topic(topic), m_property(property), m_statement(statement), m_value(value) {}

    inline QString topic(void) { return m_topic; }
    inline QString property(void) { return m_property; }
    inline Statement statement(void) { return m_statement; }
    inline QVariant value(void) { return m_value; }

    inline bool match(const QByteArray &oldMessage, const QByteArray &newMessage) {{ return TriggerObject::match(parse(oldMessage), parse(newMessage), m_statement, m_value); }}

private:

    QString m_topic, m_property;
    Statement m_statement;
    QVariant m_value;

    inline QVariant parse(const QByteArray &message) { return m_property.isEmpty() ? message : QJsonDocument::fromJson(message).object().value(m_property).toVariant(); }

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

class SunriseTrigger : public TriggerObject
{

public:

    SunriseTrigger(qint32 offset) :
        TriggerObject(Type::sunrise), m_offset(offset * 60) {}

    inline qint32 offset(void) { return m_offset / 60; }
    inline bool match(const QTime &sunrise, const QTime &value) { return value == sunrise.addSecs(m_offset); }

private:

    qint32 m_offset;

};

class SunsetTrigger : public TriggerObject
{

public:

    SunsetTrigger(qint32 offset) :
        TriggerObject(Type::sunset), m_offset(offset * 60) {}

    inline qint32 offset(void) { return m_offset / 60; }
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
