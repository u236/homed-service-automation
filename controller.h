#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION                 "1.0.0"

#include "automation.h"
#include "homed.h"

class Controller : public HOMEd
{
    Q_OBJECT

public:

    Controller(const QString &configFile);

private:

    QString m_telegramToken;
    qint64 m_telegramChat;

    QMap <QString, Endpoint> m_endpoints;
    AutomationList *m_automations;

    void updateStatus(const Endpoint &endpoint, const QMap<QString, QVariant> &data);
    void checkConditions(const Automation &automation);
    void runActions(const Automation &automation);

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

};

#endif
