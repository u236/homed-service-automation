#include "condition.h"

bool PropertyCondition::match(const QVariant &value)
{
    switch (m_statement)
    {
        case Statement::equals:  return value == m_value;
        case Statement::differs: return value != m_value;
        case Statement::above:   return value.toDouble() >= m_value.toDouble();
        case Statement::below:   return value.toDouble() <= m_value.toDouble();

        case Statement::between:
        {
            QList <QVariant> list = m_value.toList();
            return value.toDouble() >= list.value(0).toDouble() && value.toDouble() <= list.value(1).toDouble();
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
            return value >= QDate::fromString(list.value(0).toString(), "dd.MM") && value <= QDate::fromString(list.value(1).toString(), "dd.MM");
        }
    }

    return false;
}

bool TimeCondition::match(const QTime &value)
{
    switch (m_statement)
    {
        case Statement::equals:  return value == QTime::fromString(m_value.toString(), "hh:mm");
        case Statement::differs: return value != QTime::fromString(m_value.toString(), "hh:mm");
        case Statement::above:   return value >= QTime::fromString(m_value.toString(), "hh:mm");
        case Statement::below:   return value <= QTime::fromString(m_value.toString(), "hh:mm");

        case Statement::between:
        {
            QList <QVariant> list = m_value.toList();
            return value >= QTime::fromString(list.value(0).toString(), "hh:mm") && value <= QTime::fromString(list.value(1).toString(), "hh:mm");
        }
    }

    return false;
}
