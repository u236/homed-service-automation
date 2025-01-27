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

    if (m_token.isEmpty() || !m_chat || !config->value("telegram/update", true).toBool())
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

void Telegram::sendFile(const QString &message, const QString &file, qint64 thread, bool silent, const QList<qint64> &chats)
{
    QList <QString> items = {QString("-F document=@'%1'").arg(file)};
    QList <qint64> list = chats;

    if (m_token.isEmpty() || !m_chat)
        return;

    if (list.isEmpty())
        list.append(m_chat);

    if (!message.isEmpty())
    {
        items.append(QString("-F caption='%1'").arg(message));
        items.append("-F parse_mode=markdown");
    }

    if (thread)
        items.append(QString("-F message_thread_id=%1").arg(thread));

    if (silent)
        items.append("-F disable_notification=true");

    for (int i = 0; i < list.count(); i++)
        system(QString("curl -X POST -F chat_id=%1 %2 -s https://api.telegram.org/bot%3/sendDocument > /dev/null &").arg(list.at(i)).arg(items.join(0x20), m_token).toUtf8().constData());
}

void Telegram::sendMessage(const QString &message, const QString &photo, const QString &keyboard, qint64 thread, bool silent, const QList <qint64> &chats)
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

    if (!keyboard.isEmpty())
    {
        QList <QString> lines = keyboard.split('\n');
        QJsonArray array;

        for (int i = 0; i < lines.length(); i++)
        {
            QList <QString> items = lines.at(i).split(',');
            QJsonArray line;

            for (int j = 0; j < items.length(); j++)
            {
                QList <QString> item = items.at(j).split(':');
                line.append(QJsonObject{{"text", item.value(0).trimmed()}, {"callback_data", item.value(item.length() > 1 ? 1 : 0).trimmed()}});
            }

            array.append(line);
        }

        json.insert("reply_markup", QJsonObject {{"inline_keyboard", array}, {"one_time_keyboard",true}});
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
        logWarning << "Telegram getUpdates request error, description:" << (json.contains("description") ? json.value("description").toString() : "(empty)");

    for (auto it = array.begin(); it != array.end(); it++)
    {
        QJsonObject item = it->toObject(), data;
        qint64 chat;
        bool check = item.contains("message");

        m_offset = item.value("update_id").toVariant().toLongLong() + 1;

        data = item.value(check ? "message" : "callback_query").toObject();
        chat = check ? data.value("chat").toObject().value("id").toVariant().toLongLong() : data.value("message").toObject().value("chat").toObject().value("id").toVariant().toLongLong();

        if (check && data.contains("photo"))
        {
            sendMessage(QString("File ID:\n`%1`").arg(data.value("photo").toArray().last().toObject().value("file_id").toString()), QString(), QString(), 0, true, {chat});
            continue;
        }

        emit messageReceived(data.value(check ? "text" : "data").toString(), chat);
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
