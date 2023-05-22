#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION                 "1.0.2"

#include "automation.h"
#include "homed.h"
#include "telegram.h"

class Controller : public HOMEd
{
    Q_OBJECT

public:

    Controller(const QString &configFile);

private:

    Telegram *m_telegram;
    AutomationList *m_automations;

    QList <QString> m_subscriptions;
    QMap <QString, Endpoint> m_endpoints;

    void updateStatus(const Endpoint &endpoint, const QMap<QString, QVariant> &data);
    void checkConditions(const Automation &automation);
    void runActions(const Automation &automation);

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

    void telegramReceived(const QString &message);
    void addSubscription(const QString &topic);

};

#endif
