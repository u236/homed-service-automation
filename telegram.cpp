#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "logger.h"
#include "telegram.h"

Telegram::Telegram(QSettings *config, QObject *parent) : QObject(parent), m_process(new QProcess(this)), m_offset(0)
{
    m_token = config->value("telegram/token").toString();
    m_chat = config->value("telegram/chat").toLongLong();
    m_timeout = config->value("telegram/timeout", 60).toInt();

    if (m_token.isEmpty() || !m_chat)
        return;

    connect(m_process, static_cast <void (QProcess::*)(int, QProcess::ExitStatus)> (&QProcess::finished), this, &Telegram::finished);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Telegram::readyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, &Telegram::pollError);
    getUpdates();
}

Telegram::~Telegram(void)
{
    m_process->close();
}

void Telegram::sendMessage(const QString &message, const QString &photo, qint64 thread, bool silent, const QList <qint64> &chats)
{
    QList <qint64> list = chats;
    QJsonObject json = {{"disable_notification", silent}, {"parse_mode", "Markdown"}};
    QString method;

    if (m_token.isEmpty() || !m_chat)
        return;

    if (list.isEmpty())
        list.append(m_chat);

    if (thread)
        json.insert("message_thread_id", thread);

    if (photo.isEmpty())
    {
        json.insert("text", message);
        method = "sendMessage";
    }
    else
    {
        json.insert("caption", message);
        json.insert("photo", photo);
        method = "sendPhoto";
    }

    for (int i = 0; i < list.count(); i++)
    {
        json.insert("chat_id", list.at(i));
        system(QString("curl -X POST -H 'Content-Type: application/json' -d '%1' -s https://api.telegram.org/bot%2/%3 > /dev/null &").arg(QJsonDocument(json).toJson(QJsonDocument::Compact), m_token, method).toUtf8().constData());
    }
}

void Telegram::getUpdates(void)
{
    m_buffer.clear();
    m_process->start("curl", {"-X", "POST", "-H", "Content-Type: application/json", "-d", QJsonDocument(QJsonObject {{"timeout", m_timeout}, {"offset", m_offset}}).toJson(QJsonDocument::Compact), "-s", QString("https://api.telegram.org/bot%1/getUpdates").arg(m_token)});
}

void Telegram::finished(int, QProcess::ExitStatus)
{
    QJsonObject json = QJsonDocument::fromJson(m_buffer).object();
    QJsonArray array = json.value("result").toArray();

    if (!json.value("ok").toBool())
    {
        logWarning << "Telegram getUpdates request error, description:" << json.value("description").toString();
        return;
    }

    for (auto it = array.begin(); it != array.end(); it++)
    {
        QJsonObject item = it->toObject(), message = item.value("message").toObject();
        qint64 chat = message.value("chat").toObject().value("id").toVariant().toLongLong();

        m_offset = item.value("update_id").toVariant().toLongLong() + 1;

        if (message.contains("photo"))
        {
            sendMessage(QString("File ID:\n`%1`").arg(message.value("photo").toArray().last().toObject().value("file_id").toString()), QString(), 0, true, {chat});
            continue;
        }

        emit messageReceived(message.value("text").toString(), chat);
    }

    getUpdates();
}

void Telegram::readyRead(void)
{
    m_buffer.append(m_process->readAllStandardOutput());
}

void Telegram::pollError(void)
{
    logWarning << "Telegram getUpdates process error:" << m_process->readAllStandardError();
}
