#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION     "1.0.6"

#include "automation.h"
#include "homed.h"
#include "sun.h"
#include "telegram.h"

class Controller : public HOMEd
{
    Q_OBJECT

public:

    Controller(const QString &configFile);

private:

    AutomationList *m_automations;
    Telegram *m_telegram;
    Sun *m_sun;

    QTimer *m_timer;

    QTime m_sunrise, m_sunset;
    QDate m_date;

    QList <QString> m_subscriptions;
    QMap <QString, Endpoint> m_endpoints;

    QString composeString(QString string);

    void updateSun(void);
    void updateStatus(const Endpoint &endpoint, const QMap<QString, QVariant> &data);

    void checkConditions(AutomationObject *automation);
    void runActions(AutomationObject *automation);

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

    void addSubscription(const QString &topic);
    void telegramReceived(const QString &message, qint64 chat);

    void updateTime(void);
    void automationTimeout(void);

};

#endif
