#include "trigger.h"

bool TriggerObject::match(const QVariant &oldValue, const QVariant &newValue, Statement statement, const QVariant &value)
{
    switch (statement)
    {
        case Statement::equals:  return oldValue != value && newValue == value;
        case Statement::above:   return (!oldValue.isValid() || oldValue.toDouble() < value.toDouble()) && newValue.toDouble() >= value.toDouble();
        case Statement::below:   return (!oldValue.isValid() || oldValue.toDouble() > value.toDouble()) && newValue.toDouble() <= value.toDouble();

        case Statement::between:
        {
            QList <QVariant> list = value.toList();
            double a = oldValue.toDouble(), b = newValue.toDouble(), min = qMin(list.value(0).toDouble(), list.value(1).toDouble()), max = qMax(list.value(0).toDouble(), list.value(1).toDouble());
            return (!oldValue.isValid() || a < min || a > max) && b >= min && b <= max;
        }

        case Statement::changes:
        {
            double a = oldValue.toDouble(), b = newValue.toDouble(), change = value.toDouble();
            return a != b && (b <= a - change || b >= a + change);
        }

        case Statement::updates: return oldValue != newValue;
    }

    return false;
}
