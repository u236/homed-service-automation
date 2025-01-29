#include "condition.h"

bool ConditionObject::match(const QVariant &value, QVariant match, Statement statement)
{
    if (value.type() == QVariant::Bool && match.type() == QVariant::String)
    {
        QList <QString> list = {"detected", "low", "occupied", "on", "open", "wet"};
        match = list.contains(match.toString()) ? true : false;
    }

    switch (statement)
    {
        case Statement::equals:  return value == match;
        case Statement::differs: return value != match;
        case Statement::above:   return value.toDouble() >= match.toDouble();
        case Statement::below:   return value.toDouble() <= match.toDouble();

        case Statement::between:
        {
            QList <QVariant> list = match.toList();
            return value.toDouble() >= qMin(list.value(0).toDouble(), list.value(1).toDouble()) && value.toDouble() <= qMax(list.value(0).toDouble(), list.value(1).toDouble());
        }
    }

    return false;
}

bool DateCondition::match(const QDate &value)
{
    switch (m_statement)
    {
        case Statement::equals:  return value == QDate::fromString(m_value.toString(), "d.M");
        case Statement::differs: return value != QDate::fromString(m_value.toString(), "d.M");
        case Statement::above:   return value >= QDate::fromString(m_value.toString(), "d.M");
        case Statement::below:   return value <= QDate::fromString(m_value.toString(), "d.M");

        case Statement::between:
        {
            QList <QVariant> list = m_value.toList();
            QDate start = QDate::fromString(list.value(0).toString(), "d.M"), end = QDate::fromString(list.value(1).toString(), "d.M");
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
