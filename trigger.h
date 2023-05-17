#ifndef TRIGGER_H
#define TRIGGER_H

#include <QSharedPointer>
#include <QVariant>

class TriggerObject;
typedef QSharedPointer <TriggerObject> Trigger;

class TriggerObject : public QObject
{
    Q_OBJECT

public:

    enum class Type
    {
        property
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

    bool match(const QVariant &oldValue, const QVariant &newValue);

private:

    QString m_endpoint, m_property;
    Statement m_statement;
    QVariant m_value;

};

#endif
