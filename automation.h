#ifndef AUTOMATION_H
#define AUTOMATION_H

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QSettings>
#include "action.h"
#include "condition.h"
#include "trigger.h"

class EndpointObject;
typedef QSharedPointer <EndpointObject> Endpoint;

class AutomationObject;
typedef QSharedPointer <AutomationObject> Automation;

class EndpointObject
{

public:

    EndpointObject(const QString &name) :
        m_name(name) {}

    inline QString name(void) { return m_name; }
    inline QMap <QString, QVariant> &properties(void) { return m_properties; }

private:

    QString m_name;
    QMap <QString, QVariant> m_properties;

};

class AutomationObject
{

public:

    AutomationObject(const QString &name) :
        m_name(name) {}

    inline QString name(void) { return m_name; }

    inline QList <Trigger> &triggers(void) { return m_triggers; }
    inline QList <Condition> &conditions(void) { return m_conditions; }
    inline QList <Action> &actions(void) { return m_actions; }

private:

    QString m_name;

    QList <Trigger> m_triggers;
    QList <Condition> m_conditions;
    QList <Action> m_actions;

};

class AutomationList : public QObject, public QList <Automation>
{
    Q_OBJECT

public:

    AutomationList(QSettings *config, QObject *parent);

    void init(void);

private:

    QFile m_databaseFile;
    QMetaEnum m_triggerTypes, m_conditionTypes, m_actionTypes, m_triggerStatements, m_conditionStatements, m_actionStatements;

    void unserialize(const QJsonArray &automations);

};

#endif
