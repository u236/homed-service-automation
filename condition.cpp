#include "condition.h"

bool PropertyCondition::match(const QVariant &value)
{
    switch (m_statement)
    {
        case Statement::equal: return value == m_value;
        case Statement::above: return value.toDouble() >= m_value.toDouble();
        case Statement::below: return value.toDouble() <= m_value.toDouble();
    }

    return false;
}
