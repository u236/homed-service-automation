#ifndef RUNNER_H
#define RUNNER_H

#include <QThread>
#include "automation.h"
#include "controller.h"

class Runner : public QThread
{
    Q_OBJECT

public:

    Runner(Controller *controller, const Automation &automation);
    ~Runner(void);

    inline QTimer *timer(void) { return m_timer; }
    inline Automation automation(void) { return m_automation; }

private:

    QTimer *m_timer;
    Controller *m_controller;
    QWeakPointer <AutomationObject> m_automation;

    QVariant parsePattern(QString string);

private slots:

    void runActions(void);
    void threadStarted(void);
    void timeout(void);

signals:

    void publishData(const QString &topic, const QVariant &data, bool retain = false);
    void storeAutomations(void);

};

#endif
