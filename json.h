#ifndef JSON_H
#define JSON_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

class JSON
{

public:

    static QVariant getValue(const QJsonObject &json, const QString &path);

};

#endif
