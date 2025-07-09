#ifndef TELEGRAM_H
#define TELEGRAM_H

#define CURL_OPERATION_TIMEOUT_EXIT_CODE    28
#define GET_UPDATES_RETRY_TIMEOUT           15000

#include <QProcess>
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
    QTimer *m_timer;

    QString m_token;
    qint64 m_chat, m_offset;
    qint32 m_timeout;

    void deleteMessage(qint64 chatId, qint64 messageId);

private slots:

    void getUpdates(void);
    void finished(int exitCode, QProcess::ExitStatus exitStatus);
    void readyRead(void);
    void pollError(void);

signals:

    void messageReceived(const QString &message, qint64 chat);

};

#endif
