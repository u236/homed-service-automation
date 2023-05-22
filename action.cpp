#include "action.h"

QVariant PropertyAction::value(const QVariant &oldValue)
{
    switch (m_statement)
    {
        case Statement::increase: return oldValue.toDouble() + m_value.toDouble();
        case Statement::decrease: return oldValue.toDouble() - m_value.toDouble();
        default: return m_value;
    }
}
