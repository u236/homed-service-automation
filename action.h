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
        telegram
    };

    ActionObject(Type type) :
        QObject(nullptr), m_type(type) {}

    inline Type type(void) { return m_type; }

    Q_ENUM(Type)

private:

    Type m_type;

};

class PropertyAction : public ActionObject
{

public:

    PropertyAction(const QString &endpoint, const QString &property, const QVariant &value) :
        ActionObject(Type::property), m_endpoint(endpoint), m_property(property), m_value(value) {}

    inline QString endpoint(void) { return m_endpoint; }
    inline QString property(void) { return m_property; }
    inline QVariant value(void) { return m_value; }

private:

    QString m_endpoint, m_property;
    QVariant m_value;

};

class TelegramAction : public ActionObject
{

public:

    TelegramAction(const QString &message) :
        ActionObject(Type::telegram), m_message(message) {}

    inline QString message(void) { return m_message; }

private:

    QString m_message;

};

#endif
