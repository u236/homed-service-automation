#include "condition.h"

bool PropertyCondition::match(const QVariant &value)
{
    switch (m_statement)
    {
        case Statement::equals: return value == m_value;
        case Statement::above: return value.toDouble() >= m_value.toDouble();
        case Statement::below: return value.toDouble() <= m_value.toDouble();

        case Statement::between:
        {
            QList <QVariant> list = m_value.toList();
            return value.toDouble() >= list.value(0).toDouble() && value.toDouble() <= list.value(1).toDouble();
        }
    }

    return false;
}
