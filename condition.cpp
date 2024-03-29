#include "condition.h"

bool ConditionObject::match(const QVariant &newValue, Statement statement, const QVariant &value)
{
    switch (statement)
    {
        case Statement::equals:  return newValue == value;
        case Statement::differs: return newValue != value;
        case Statement::above:   return newValue.toDouble() >= value.toDouble();
        case Statement::below:   return newValue.toDouble() <= value.toDouble();

        case Statement::between:
        {
            QList <QVariant> list = value.toList();
            return newValue.toDouble() >= qMin(list.value(0).toDouble(), list.value(1).toDouble()) && newValue.toDouble() <= qMax(list.value(0).toDouble(), list.value(1).toDouble());
        }
    }

    return false;
}

bool DateCondition::match(const QDate &value)
{
    switch (m_statement)
    {
        case Statement::equals:  return value == QDate::fromString(m_value.toString(), "dd.MM");
        case Statement::differs: return value != QDate::fromString(m_value.toString(), "dd.MM");
        case Statement::above:   return value >= QDate::fromString(m_value.toString(), "dd.MM");
        case Statement::below:   return value <= QDate::fromString(m_value.toString(), "dd.MM");

        case Statement::between:
        {
            QList <QVariant> list = m_value.toList();
            QDate start = QDate::fromString(list.value(0).toString(), "dd.MM"), end = QDate::fromString(list.value(1).toString(), "dd.MM");
            return start > end ? value >= start || value <= end : value >= start && value <= end;
        }
    }

    return false;
}

bool TimeCondition::match(const QTime &value, Sun *sun)
{
    switch (m_statement)
    {
        case Statement::equals:  return value == sun->fromString(m_value.toString());
        case Statement::differs: return value != sun->fromString(m_value.toString());
        case Statement::above:   return value >= sun->fromString(m_value.toString());
        case Statement::below:   return value <= sun->fromString(m_value.toString());

        case Statement::between:
        {
            QList <QVariant> list = m_value.toList();
            QTime start = sun->fromString(list.value(0).toString()), end = sun->fromString(list.value(1).toString());
            return start > end ? value >= start || value <= end : value >= start && value <= end;
        }
    }

    return false;
}
