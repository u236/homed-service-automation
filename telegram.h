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

    void sendMessage(const QString &message);

private:

    QProcess *m_process;

    QString m_token;
    qint64 m_chat, m_timeout, m_offset;

    void getUpdates(void);

private slots:

    void readyRead(void);

signals:

    void messageReceived(const QString &message);

};

#endif
