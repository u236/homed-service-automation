#ifndef AUTOMATION_H
#define AUTOMATION_H

#define STORE_DATABASE_DELAY    20

#include <QFile>
#include <QMetaEnum>
#include <QSettings>
#include <QTimer>
#include "action.h"
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

    enum class Mode
    {
        single,
        restart,
        queued,
        parallel
    };

    AutomationObject(Mode mode, const QString &uuid, const QString &name, const QString &note, bool active, qint32 debounce, qint64 lastTriggered) :
        QObject(nullptr), m_mode(mode), m_uuid(uuid), m_name(name), m_note(note), m_active(active), m_debounce(debounce), m_lastTriggered(lastTriggered), m_counter(1) {}

    inline Mode mode(void) { return m_mode; }
    inline QString uuid(void) { return m_uuid; }
    inline QString name(void) { return m_name; }
    inline QString note(void) { return m_note; }

    inline bool active(void) { return m_active; }
    inline qint32 debounce(void) { return m_debounce; }

    inline qint64 lastTriggered(void) { return m_lastTriggered; }
    inline void updateLastTriggered(void) { m_lastTriggered = QDateTime::currentMSecsSinceEpoch(); }

    inline qint64 counter(void) { return m_counter; }
    inline void updateCounter(void) { m_counter++; }

    inline QList <Trigger> &triggers(void) { return m_triggers; }
    inline QList <Condition> &conditions(void) { return m_conditions; }
    inline ActionList &actions(void) { return m_actions; }

    Q_ENUM(Mode)

private:

    Mode m_mode;
    QString m_uuid, m_name, m_note;

    bool m_active;
    qint32 m_debounce;

    QWeakPointer <TriggerObject> m_lastTrigger;
    qint64 m_lastTriggered, m_counter;

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

    inline QMap <QString, qint64> &messages(void) { return m_messages; }
    inline QMap <QString, QVariant> &states(void) { return m_states; }

    void init(void);
    void store(bool sync = false);

    AutomationObject::Mode getMode(const QJsonObject &json);

    Automation byUuid(const QString &uuid, int *index = nullptr);
    Automation byName(const QString &name);
    Automation parse(const QJsonObject &json, bool add = false);

private:

    QTimer *m_timer;

    QMetaEnum m_automationModes, m_triggerTypes, m_conditionTypes, m_actionTypes, m_triggerStatements, m_conditionStatements, m_actionStatements;
    QFile m_file;
    qint64 m_telegramChat;
    bool m_sync;

    QList <QString> m_telegramActions;
    QMap <QString, qint64> m_messages;
    QMap <QString, QVariant> m_states;

    QByteArray randomData(int length);
    void parsePattern(const QString &string);

    void unserializeConditions(QList <Condition> &list, const QJsonArray &conditions);
    void unserializeActions(ActionList &list, const QJsonArray &actions, bool add);
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
