#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION     "1.5.5"

#include "automation.h"
#include "homed.h"
#include "sun.h"
#include "telegram.h"

class Controller : public HOMEd
{
    Q_OBJECT

public:

    Controller(const QString &configFile);

    enum class Command
    {
        restartService,
        updateAutomation,
        removeAutomation
    };

    enum class Event
    {
        nameDuplicate,
        incompleteData,
        added,
        updated,
        removed
    };

    Q_ENUM(Command)
    Q_ENUM(Event)

private:

    AutomationList *m_automations;
    Telegram *m_telegram;
    Sun *m_sun;
    QTimer *m_timer;

    QMetaEnum m_commands, m_events;
    QDate m_date;

    QList <QString> m_types, m_subscriptions;
    QMap <QString, Device> m_devices;
    QMap <QString, QByteArray> m_topics;

    Device findDevice(const QString &search);

    QVariant parseString(const QString &string);
    QVariant parseTemplate(QString string, const Trigger &trigger);

    void updateSun(void);
    void handleTrigger(TriggerObject::Type type, const QVariant &a = QVariant(), const QVariant &b = QVariant(), const QVariant &c = QVariant(), const QVariant &d = QVariant());
    bool checkConditions(const QList <Condition> &conditions, ConditionObject::Type type, const Trigger &trigger);
    bool runActions(AutomationObject *automation);

    void publishEvent(const QString &name, Event event);

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

    void statusUpdated(const QJsonObject &json);
    void addSubscription(const QString &topic);
    void telegramReceived(const QString &message, qint64 chat);

    void updateTime(void);
    void automationTimeout(void);

};

#endif
