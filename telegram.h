#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <QProcess>
#include <QSettings>
#include "automation.h"

class Telegram : public QObject
{
    Q_OBJECT

public:

    Telegram(QSettings *config, AutomationList *automations, QObject *parent);
    ~Telegram(void);

    void sendMessage(const QString &message, const QString &file, const QString &keyboard, const QString &uuid, qint64 thread, bool silent, bool remove, bool update, const QList <qint64> &chats);

private:

    AutomationList *m_automations;
    QByteArray m_buffer;
    QProcess *m_process;

    QString m_token;
    qint64 m_chat, m_offset;
    qint32 m_timeout;

    void deleteMessage(qint64 chatId, qint64 messageId);
    void getUpdates(void);

private slots:

    void finished(int exitCode, QProcess::ExitStatus exitStatus);
    void readyRead(void);
    void pollError(void);

signals:

    void messageReceived(const QString &message, qint64 chat);

};

#endif
