#ifndef AUTOMATION_H
#define AUTOMATION_H

#define STORE_DATABASE_DELAY    20

#include <QFile>
#include <QMetaEnum>
#include <QSettings>
#include <QTimer>
#include "action.h"
#include "condition.h"
#include "trigger.h"

class DeviceObject;
typedef QSharedPointer <DeviceObject> Device;

class AutomationObject;
typedef QSharedPointer <AutomationObject> Automation;

class DeviceObject
{

public:

    DeviceObject(const QString &key, const QString &topic, const QString &name) :
        m_key(key), m_topic(topic), m_name(name) {}

    inline QString key(void) { return m_key; }

    inline QString topic(void) { return m_topic; }
    inline void setTopic(const QString &value) { m_topic = value; }
    inline void clearTopic(void) { m_topic.clear(); }

    inline QString name(void) { return m_name; }
    inline void setName(const QString &value) { m_name = value; }

    inline QMap <quint8, QVariantMap> &properties(void) { return m_properties; }

private:

    QString m_key, m_topic, m_name;
    QMap <quint8, QVariantMap> m_properties;

};

class AutomationObject : public QObject
{
    Q_OBJECT

public:

    AutomationObject(const QString &name, const QString &note, bool active, qint32 debounce, bool restart, qint64 lastTriggered) :
        QObject(nullptr), m_name(name), m_note(note), m_active(active), m_debounce(debounce), m_restart(restart), m_lastTriggered(lastTriggered), m_runner(nullptr) {}

    inline QString name(void) { return m_name; }
    inline QString note(void) { return m_note; }
    inline bool active(void) { return m_active; }

    inline qint32 debounce(void) { return m_debounce; }
    inline bool restart(void) { return m_restart; }

    inline Trigger lastTrigger(void) { return m_lastTrigger; }
    inline void setLastTrigger(const Trigger &value) { m_lastTrigger = value; }

    inline qint64 lastTriggered(void) { return m_lastTriggered; }
    inline void updateLastTriggered(void) { m_lastTriggered = QDateTime::currentMSecsSinceEpoch(); }

    inline ActionList *actionList(void) { return m_actionList; }
    inline void setActionList(ActionList *value) { m_actionList = value; }

    inline void *runner(void) { return m_runner; }
    inline void setRunner(void *value) { m_runner = value; }

    inline QString shellOutput(void) { return m_shellOutput; }
    inline void setShellOutput(const QString &value) { m_shellOutput = value; }

    inline QList <Trigger> &triggers(void) { return m_triggers; }
    inline QList <Condition> &conditions(void) { return m_conditions; }
    inline ActionList &actions(void) { return m_actions; }

private:

    QString m_name, m_note;
    bool m_active;

    qint32 m_debounce;
    bool m_restart;

    QWeakPointer <TriggerObject> m_lastTrigger;
    qint64 m_lastTriggered;

    ActionList *m_actionList;
    void *m_runner;

    QString m_shellOutput;

    QList <Trigger> m_triggers;
    QList <Condition> m_conditions;
    ActionList m_actions;

};

class AutomationList : public QObject, public QList <Automation>
{
    Q_OBJECT

public:

    AutomationList(QSettings *config, QObject *parent);
    ~AutomationList(void);

    inline QMap <QString, QVariant> &states(void) { return m_states; }

    void init(void);
    void store(bool sync = false);

    Automation byName(const QString &name, int *index = nullptr);
    Automation parse(const QJsonObject &json);

private:

    QTimer *m_timer;

    QMetaEnum m_triggerTypes, m_conditionTypes, m_actionTypes, m_triggerStatements, m_conditionStatements, m_actionStatements;
    QFile m_file;
    qint64 m_telegramChat;
    bool m_sync;

    QMap <QString, QVariant> m_states;

    void parsePattern(const QString &string);
    void unserializeConditions(QList <Condition> &list, const QJsonArray &conditions);
    void unserializeActions(ActionList &list, const QJsonArray &actions);
    void unserialize(const QJsonArray &automations);

    QJsonArray serializeConditions(const QList <Condition> &list);    
    QJsonArray serializeActions(const ActionList &list);
    QJsonArray serialize(void);

private slots:

    void writeDatabase(void);

signals:

    void statusUpdated(const QJsonObject &json);
    void addSubscription(const QString &topic);

};

inline QDebug operator << (QDebug debug, const Automation &automation) { return debug << "automation" << automation->name(); }

#endif
