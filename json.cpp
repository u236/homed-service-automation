#include "json.h"

QVariant JSON::getValue(const QJsonObject &json, const QString &search)
{
    QList <QString> list = search.split('.');
    QJsonObject object = json;

    for (int i = 0; i < list.count(); i++)
    {
        QString key = list.at(i);
        int index = -1;

        if (key.endsWith(']'))
        {
            int position = key.indexOf('[');
            index = key.mid(position + 1, key.length() - position - 2).toInt();
            key = key.mid(0, position);
        }

        if (!object.contains(key))
            break;

        if (i < list.length() - 1)
        {
            object = index < 0 ? object.value(key).toObject() : object.value(key).toArray().at(index).toObject();
            continue;
        }

        return index < 0 ? object.value(key).toVariant() : object.value(key).toArray().at(index).toVariant();
    }

    return QVariant();
}
