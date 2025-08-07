#ifndef RUNNER_H
#define RUNNER_H

#include <QProcess>
#include <QThread>
#include "automation.h"

class Controller;

class Runner : public QThread
{
    Q_OBJECT

public:

    Runner(Controller *controller, const Automation &automation, const QMap <QString, QString> &meta);
    ~Runner(void);

    inline Automation automation(void) { return m_automation; }
    inline qint64 id(void) { return m_id; }

    void abort(void);

private:

    QProcess *m_process;
    QTimer *m_timer;
    Controller *m_controller;

    QWeakPointer <AutomationObject> m_automation;
    qint64 m_id;

    ActionList *m_actions;
    bool m_aborted;

    QMap <QString, QString> m_meta;

    QVariant parsePattern(QString string);
    void killProcess(void);

private slots:

    void runActions(void);
    void threadStarted(void);
    void threadFinished(void);
    void timeout(void);

signals:

    void publishMessage(const QString &topic, const QVariant &data, bool retain = false);
    void updateState(const QString &name, const QVariant &value);
    void telegramAction(const QString &message, const QString &file, const QString &keyboard, const QString &uuid, qint64 thread, bool silent, bool remove, bool update, QList <qint64> *chats);

};

inline QDebug operator << (QDebug debug, Runner *runner) { return debug << runner->automation() << QString("#%1").arg(runner->id()).toUtf8().constData(); }

#endif
