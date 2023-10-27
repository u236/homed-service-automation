#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION     "1.2.1"

#include "automation.h"
#include "homed.h"
#include "sun.h"
#include "telegram.h"

class Controller : public HOMEd
{
    Q_OBJECT

public:

    Controller(const QString &configFile);

    enum class Event
    {
        nameDuplicate,
        incompleteData,
        added,
        updated,
        removed
    };

    Q_ENUM(Event)

private:

    AutomationList *m_automations;
    Telegram *m_telegram;
    Sun *m_sun;
    QTimer *m_timer;

    QMetaEnum m_events;
    QDate m_date;

    QList <QString> m_services, m_subscriptions;
    QMap <QString, Endpoint> m_endpoints;
    QMap <QString, QByteArray> m_topics;

    QString composeString(QString string, const Trigger &trigger);

    void updateSun(void);
    void updateEndpoint(const Endpoint &endpoint, const QMap <QString, QVariant> &data);

    void checkConditions(AutomationObject *automation, const Trigger &trigger);
    bool checkConditions(const QList <Condition> &conditions, ConditionObject::Type type = ConditionObject::Type::AND);
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
