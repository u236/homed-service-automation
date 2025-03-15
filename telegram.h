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

    void sendFile(const QString &message, const QString &file, const QString &keyboard, const QString &uuid, qint64 thread, bool silent, bool remove, bool update, const QList <qint64> &chats);
    void sendMessage(const QString &message, const QString &photo, const QString &keyboard, const QString &uuid, qint64 thread, bool silent, bool remove, bool update,const QList <qint64> &chats);


private:

    QByteArray m_buffer;
    QProcess *m_process;

    QString m_token;
    qint64 m_chat, m_offset;
    qint32 m_timeout;

    QJsonObject inllineKeyboard(const QString &keyboard);

    void deleteMessage(qint64 chatId, qint64 messageId);
    void getUpdates(void);

    QMap <QString, quint64> m_messages;

private slots:

    void requestFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void pollFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void readyRead(void);
    void pollError(void);

signals:

    void messageReceived(const QString &message, qint64 chat);

};

#endif
