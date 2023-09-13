#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <QProcess>
#include <QSettings>

class Telegram : public QObject
{
    Q_OBJECT

public:

    Telegram(QSettings *config, QObject *parent);
    ~Telegram(void);

    void sendMessage(const QString &message, bool silent, const QList <qint64> &chats);

private:

    QProcess *m_process;

    QString m_token;
    qint64 m_chat, m_offset;
    qint32 m_timeout;

    void getUpdates(void);

private slots:

    void finished(int exitCode, QProcess::ExitStatus exitStatus);
    void readyRead(void);
    void pollError(void);

signals:

    void messageReceived(const QString &message, qint64 chat);

};

#endif
