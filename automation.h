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

    AutomationObject(const QString &name, bool active, qint32 debounce, qint32 delay, bool restart, qint64 lastTriggered) :
        QObject(nullptr), m_timer(new QTimer(this)), m_name(name), m_active(active), m_debounce(debounce), m_delay(delay), m_restart(restart), m_lastTriggered(lastTriggered) {}

    inline QTimer *timer(void) { return m_timer; }

    inline QString name(void) { return m_name; }
    inline bool active(void) { return m_active; }

    inline qint32 debounce(void) { return m_debounce; }
    inline qint32 delay(void) { return m_delay; }
    inline bool restart(void) { return m_restart; }

    inline qint64 lastTriggered(void) { return m_lastTriggered; }
    inline void updateLastTriggered(void) { m_lastTriggered = QDateTime::currentMSecsSinceEpoch(); }

    inline QList <Trigger> &triggers(void) { return m_triggers; }
    inline QList <Condition> &conditions(void) { return m_conditions; }
    inline QList <Action> &actions(void) { return m_actions; }

private:

    QTimer *m_timer;

    QString m_name;
    bool m_active;

    qint32 m_debounce, m_delay;
    bool m_restart;

    qint64 m_lastTriggered;

    QList <Trigger> m_triggers;
    QList <Condition> m_conditions;
    QList <Action> m_actions;

};

class AutomationList : public QObject, public QList <Automation>
{
    Q_OBJECT

public:

    AutomationList(QSettings *config, QObject *parent);
    ~AutomationList(void);

    void init(void);
    void store(bool sync = false);

    Automation byName(const QString &name, int *index = nullptr);
    Automation parse(const QJsonObject &json);

private:

    QMetaEnum m_triggerTypes, m_conditionTypes, m_actionTypes, m_triggerStatements, m_conditionStatements, m_actionStatements;
    QFile m_file;
    qint64 m_telegramChat;

    void unserialize(const QJsonArray &automations);
    QJsonArray serialize(void);

signals:

    void statusUpdated(const QJsonObject &json);
    void addSubscription(const QString &topic);

};

#endif
