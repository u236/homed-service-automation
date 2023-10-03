#include "trigger.h"

bool PropertyTrigger::match(const QVariant &oldValue, const QVariant &newValue)
{
    switch (m_statement)
    {
        case Statement::equals: return oldValue != m_value && newValue == m_value;
        case Statement::above:  return oldValue.toDouble() < m_value.toDouble() && newValue.toDouble() >= m_value.toDouble();
        case Statement::below:  return oldValue.toDouble() > m_value.toDouble() && newValue.toDouble() <= m_value.toDouble();

        case Statement::between:
        {
            QList <QVariant> list = m_value.toList();
            double check = oldValue.toDouble(), value = newValue.toDouble(), min = qMin(list.value(0).toDouble(), list.value(1).toDouble()), max = qMax(list.value(0).toDouble(), list.value(1).toDouble());
            return (check < min || check > max) && value >= min && value <= max;
        }

        case Statement::changes:
        {
            double check = oldValue.toDouble(), value = newValue.toDouble(), change = m_value.toDouble();
            return value != check && (value <= check - change || value >= check + change);
        }
    }

    return false;
}
