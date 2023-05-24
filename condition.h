#ifndef CONDITION_H
#define CONDITION_H

#include <QDateTime>
#include <QSharedPointer>
#include <QVariant>

class ConditionObject;
typedef QSharedPointer <ConditionObject> Condition;

class ConditionObject : public QObject
{
    Q_OBJECT

public:

    enum class Type
    {
        property,
        date,
        week,
        time
    };

    enum class Statement
    {
        equals,
        above,
        below,
        between
    };

    ConditionObject(Type type) :
        QObject(nullptr), m_type(type) {}

    inline Type type(void) { return m_type; }

    Q_ENUM(Type)
    Q_ENUM(Statement)

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

    bool match(const QVariant &value);

private:

    QString m_endpoint, m_property;
    Statement m_statement;
    QVariant m_value;

};

class DateCondition : public ConditionObject
{

public:

    DateCondition(Statement statement, const QVariant &value) :
        ConditionObject(Type::date), m_statement(statement), m_value(value) {}

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

    bool match(const QTime &value);

private:

    Statement m_statement;
    QVariant m_value;

};

class WeekCondition : public ConditionObject
{

public:

    WeekCondition(const QVariant &value) :
        ConditionObject(Type::week), m_value(value) {}

    inline bool match(int value) { return m_value.toList().contains(value); }

private:

    QVariant m_value;

};

#endif
