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
    inline bool aborted(void) { return m_aborted; }

    void abort(void);

private:

    QTimer *m_timer;
    Controller *m_controller;
    QWeakPointer <AutomationObject> m_automation;
    bool m_aborted;

    inline QVariant parsePattern(QString string) { return m_controller->parsePattern(m_automation, string); }

private slots:

    void runActions(void);
    void threadStarted(void);
    void timeout(void);

signals:

    void publishMessage(const QString &topic, const QVariant &data, bool retain = false);
    void updateState(const QString &name, const QVariant &value);
    void telegramAction(const QString &message, const QString &file, const QString &keyboard, const QString &uuid, qint64 thread, bool silent, bool remove, bool update, QList <qint64> *chats);

};

#endif
