#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION         "2.3.0"
#define EMPTY_PATTERN_VALUE     "_NULL_"
#define SUBSCRIPTION_DELAY      1000
#define RUNNER_STARTUP_DELAY    10

#include <QMutex>
#include "homed.h"
#include "runner.h"
#include "telegram.h"

class Controller : public HOMEd
{
    Q_OBJECT

public:

    enum class Command
    {
        restartService,
        updateAutomation,
        removeAutomation,
        removeState
    };

    enum class Event
    {
        nameDuplicate,
        incompleteData,
        added,
        updated,
        removed
    };

    Controller(const QString &configFile);

    inline QMutex *mutex(void) { return m_mutex; }
    inline Telegram *telegram(void) { return m_telegram; }

    Device findDevice(const QString &search);
    quint8 getEndpointId(const QString &endpoint);

    QVariant parsePattern(QString string, const QMap <QString, QString> &meta, bool condition = true);
    bool checkConditions(ConditionObject::Type type, const QList <Condition> &conditions, const QMap <QString, QString> &meta);

    Q_ENUM(Command)
    Q_ENUM(Event)

private:

    QTimer *m_subscribeTimer, *m_updateTimer;
    QMutex *m_mutex;

    AutomationList *m_automations;
    Telegram *m_telegram;
    Sun *m_sun;

    QMetaEnum m_commands, m_events;
    QDateTime m_dateTime;
    bool m_startup;

    QList <QString> m_types, m_subscriptions;
    QList <Runner*> m_runners;

    QMap <QString, Device> m_devices;
    QMap <QString, QByteArray> m_topics;

    Runner *findRunner(const Automation &automation, bool pending = false);
    void abortRunners(const Automation &automation);
    void addRunner(const Automation &automation, const QMap <QString, QString> &meta, bool start);

    void handleTrigger(TriggerObject::Type type, const QVariant &a = QVariant(), const QVariant &b = QVariant(), const QVariant &c = QVariant(), const QVariant &d = QVariant());
    void publishEvent(const QString &name, Event event);
    void updateSun(void);

public slots:

    void quit(void) override;

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

    void statusUpdated(const QJsonObject &json);
    void addSubscription(const QString &topic);
    void telegramReceived(const QString &message, qint64 chat);

    void publishMessage(const QString &topic, const QVariant &data, bool retain);
    void updateState(const QString &name, const QVariant &value);
    void telegramAction(const QString &message, const QString &file, const QString &keyboard, const QString &uuid, qint64 thread, bool silent, bool remove, bool update, QList <qint64> *chats);
    void finished(void);

    void updateSubscriptions(void);
    void updateTime(void);

};

#endif
