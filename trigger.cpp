#include "trigger.h"

bool PropertyTrigger::match(const QVariant &oldValue, const QVariant &newValue)
{
    switch (m_statement)
    {
        case Statement::equal: return oldValue != m_value && newValue == m_value;
        case Statement::above: return oldValue.toDouble() < m_value.toDouble() && newValue.toDouble() >= m_value.toDouble();
        case Statement::below: return oldValue.toDouble() > m_value.toDouble() && newValue.toDouble() <= m_value.toDouble();
    }

    return false;
}
