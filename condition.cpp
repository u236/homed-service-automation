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
            return value.toDouble() >= qMin(list.value(0).toDouble(), list.value(1).toDouble()) && value.toDouble() <= qMax(list.value(0).toDouble(), list.value(1).toDouble());
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

bool TimeCondition::match(const QTime &value, const QTime &sunrise, const QTime &sunset)
{
    switch (m_statement)
    {
        case Statement::equals:  return value == time(m_value.toString(), sunrise, sunset);
        case Statement::differs: return value != time(m_value.toString(), sunrise, sunset);
        case Statement::above:   return value >= time(m_value.toString(), sunrise, sunset);
        case Statement::below:   return value <= time(m_value.toString(), sunrise, sunset);

        case Statement::between:
        {
            QList <QVariant> list = m_value.toList();
            QTime start = time(list.value(0).toString(), sunrise, sunset), end = time(list.value(1).toString(), sunrise, sunset);
            return start > end ? value >= start || value <= end : value >= start && value <= end;
        }
    }

    return false;
}

QTime TimeCondition::time(const QString &string, const QTime &sunrise, const QTime &sunset)
{
    QList <QString> itemList = string.split(QRegExp("[(\\-|\\+)]")), valueList = {"sunrise", "sunset"};
    QString value = itemList.value(0).trimmed();
    qint32 offset = itemList.value(1).toInt();

    if (string.mid(itemList.value(0).length(), 1) == "-")
        offset *= -1;

    switch (valueList.indexOf(value))
    {
        case 0:  return sunrise.addSecs(offset * 60);
        case 1:  return sunset.addSecs(offset * 60);
        default: return QTime::fromString(value, "hh:mm");
    }
}
