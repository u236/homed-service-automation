#ifndef CONDITION_H
#define CONDITION_H

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedPointer>
#include <QVariant>
#include "sun.h"

class ConditionObject;
typedef QSharedPointer <ConditionObject> Condition;

class ConditionObject : public QObject
{
    Q_OBJECT

public:

    enum class Type
    {
        property,
        mqtt,
        state,
        date,
        time,
        week,
        AND,
        OR,
        NOT
    };

    enum class Statement
    {
        equals,
        differs,
        above,
        below,
        between
    };

    ConditionObject(Type type) :
        QObject(nullptr), m_type(type) {}

    inline Type type(void) { return m_type; }

    Q_ENUM(Type)
    Q_ENUM(Statement)

protected:

    bool match(const QVariant &value, const QVariant &match, Statement statement);

private:

    Type m_type;

};

class PropertyCondition : public ConditionObject
{

public:

    PropertyCondition(const QString &endpoint, const QString &property, Statement statement, const QVariant &value) :
        ConditionObject(Type::property), m_endpoint(endpoint), m_property(property), m_statement(statement), m_value(value) {}

    inline QString endpoint(void) { return m_endpoint; }
    inline QString property(void) { return m_property; }
    inline Statement statement(void) { return m_statement; }
    inline QVariant value(void) { return m_value; }

    inline bool match(const QVariant &value, const QVariant &match) {{ return ConditionObject::match(value, match, m_statement); }}

private:

    QString m_endpoint, m_property;
    Statement m_statement;
    QVariant m_value;

};

class MqttCondition : public ConditionObject
{

public:

    MqttCondition(const QString &topic, const QString &property, Statement statement, const QVariant &value) :
        ConditionObject(Type::mqtt), m_topic(topic), m_property(property), m_statement(statement), m_value(value) {}

    inline QString topic(void) { return m_topic; }
    inline QString property(void) { return m_property; }
    inline Statement statement(void) { return m_statement; }
    inline QVariant value(void) { return m_value; }

    inline bool match(const QByteArray &message, const QVariant &match) {{ return ConditionObject::match(m_property.isEmpty() ? message : QJsonDocument::fromJson(message).object().value(m_property).toVariant(), match, m_statement); }}

private:

    QString m_topic, m_property;
    Statement m_statement;
    QVariant m_value;

};

class StateCondition : public ConditionObject
{

public:

    StateCondition(const QString &name, Statement statement, const QVariant &value) :
        ConditionObject(Type::state), m_name(name), m_statement(statement), m_value(value) {}

    inline QString name(void) { return m_name; }
    inline Statement statement(void) { return m_statement; }
    inline QVariant value(void) { return m_value; }

    inline bool match(const QVariant &value, const QVariant &match) {{ return ConditionObject::match(value, match, m_statement); }}

private:

    QString m_name;
    Statement m_statement;
    QVariant m_value;

};

class DateCondition : public ConditionObject
{

public:

    DateCondition(Statement statement, const QVariant &value) :
        ConditionObject(Type::date), m_statement(statement), m_value(value) {}

    inline Statement statement(void) { return m_statement; }
    inline QVariant value(void) { return m_value; }

    bool match(const QDate &value);

private:

    Statement m_statement;
    QVariant m_value;

};

class TimeCondition : public ConditionObject
{

public:

    TimeCondition(Statement statement, const QVariant &value) :
        ConditionObject(Type::time), m_statement(statement), m_value(value) {}

    inline Statement statement(void) { return m_statement; }
    inline QVariant value(void) { return m_value; }

    bool match(const QTime &value, Sun *sun);

private:

    Statement m_statement;
    QVariant m_value;

};

class WeekCondition : public ConditionObject
{

public:

    WeekCondition(const QVariant &value) :
        ConditionObject(Type::week), m_value(value) {}

    inline QVariant value(void) { return m_value; }
    inline bool match(int value) { return m_value.toList().contains(value); }

private:

    QVariant m_value;

};

class NestedCondition : public ConditionObject
{

public:

    NestedCondition(Type type) :
        ConditionObject(type) {}

    inline QList <Condition> &conditions(void) { return m_conditions; }


private:

    QList <Condition> m_conditions;

};

#endif
