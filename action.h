#ifndef ACTION_H
#define ACTION_H

#include <QSharedPointer>
#include <QVariant>

class ActionObject;
typedef QSharedPointer <ActionObject> Action;

class ActionObject : public QObject
{
    Q_OBJECT

public:

    enum class Type
    {
        property,
        telegram,
        mqtt
    };

    enum class Statement
    {
        increase,
        decrease,
        value
    };

    ActionObject(Type type) :
        QObject(nullptr), m_type(type) {}

    inline Type type(void) { return m_type; }

    Q_ENUM(Type)
    Q_ENUM(Statement)

private:

    Type m_type;

};

class PropertyAction : public ActionObject
{

public:

    PropertyAction(const QString &endpoint, const QString &property, Statement statement, const QVariant &value) :
        ActionObject(Type::property), m_endpoint(endpoint), m_property(property), m_statement(statement), m_value(value) {}

    inline QString endpoint(void) { return m_endpoint; }
    inline QString property(void) { return m_property; }

    QVariant value(const QVariant &oldValue);

private:

    QString m_endpoint, m_property;
    Statement m_statement;
    QVariant m_value;

};

class TelegramAction : public ActionObject
{

public:

    TelegramAction(const QString &message, const QList <qint64> &chats) :
        ActionObject(Type::telegram), m_message(message), m_chats(chats) {}

    inline QString message(void) { return m_message; }
    inline QList <qint64> &chats(void) { return m_chats; }

private:

    QString m_message;
    QList <qint64> m_chats;
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

#endif
