#ifndef AUTOMATION_H
#define AUTOMATION_H

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QSettings>
#include <QTimer>
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

class AutomationObject : public QObject
{
    Q_OBJECT

public:

    AutomationObject(const QString &name, qint32 delay, bool restart) :
        QObject(nullptr), m_timer(new QTimer(this)), m_name(name), m_delay(delay), m_restart(restart) {}

    inline QTimer *timer(void) { return m_timer; }

    inline QString name(void) { return m_name; }
    inline qint32 delay(void) { return m_delay; }
    inline bool restart(void) { return m_restart; }

    inline QList <Trigger> &triggers(void) { return m_triggers; }
    inline QList <Condition> &conditions(void) { return m_conditions; }
    inline QList <Action> &actions(void) { return m_actions; }

private:

    QTimer *m_timer;

    QString m_name;
    qint32 m_delay;
    bool m_restart;

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

    QMetaEnum m_triggerTypes, m_conditionTypes, m_actionTypes, m_triggerStatements, m_conditionStatements, m_actionStatements;
    QFile m_databaseFile;
    qint64 m_telegramChat;

    void unserialize(const QJsonArray &automations);

signals:

    void addSubscription(const QString &topic);

};

#endif
