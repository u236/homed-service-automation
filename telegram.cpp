#include "logger.h"
#include "telegram.h"

Telegram::Telegram(QSettings *config, AutomationList *automations, QObject *parent) : QObject(parent), m_automations(automations), m_process(new QProcess(this)), m_timer(new QTimer(this)), m_offset(0)
{
    m_token = config->value("telegram/token").toString();
    m_chat = config->value("telegram/chat").toLongLong();
    m_timeout = config->value("telegram/timeout", 60).toInt();

    if (m_token.isEmpty() || !m_chat || !config->value("telegram/update", true).toBool())
        return;

    connect(m_process, static_cast <void (QProcess::*)(int, QProcess::ExitStatus)> (&QProcess::finished), this, &Telegram::finished);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Telegram::readyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, &Telegram::pollError);
    connect(m_timer, &QTimer::timeout, this, &Telegram::getUpdates);

    m_timer->setSingleShot(true);
    getUpdates();
}

Telegram::~Telegram(void)
{
    m_process->close();
}

void Telegram::sendMessage(const QString &message, const QString &file, const QString &keyboard, const QString &uuid, qint64 thread, bool silent, bool remove, bool update, const QList <qint64> &chats)
{
    QList <qint64> chatList = chats.isEmpty() ? QList <qint64> {m_chat} : chats;
    QList <QString> typeList = {"animation", "audio", "message", "photo", "video"}, itemList = file.split('|'), formList, messageList;
    QString document = itemList.value(0).trimmed(), type = file.isEmpty() ? "message" : itemList.value(1).trimmed();
    QJsonArray array;

    if (m_token.isEmpty() || !m_chat)
        return;

    if (!typeList.contains(type))
        type = "document";

    if (!file.isEmpty())
        formList.append(QString("-F %1=%2'%3'").arg(type, QFile::exists(document) ? "@" : QString(), document));

    if (!message.isEmpty())
    {
        messageList.append(QString("-F %1='%2'").arg(file.isEmpty() ? "text" : "caption", message));
        messageList.append("-F parse_mode=Markdown");
    }

    if (!keyboard.isEmpty())
    {
        QList <QString> lines = keyboard.trimmed().split('\n');

        for (int i = 0; i < lines.count(); i++)
        {
            QList <QString> items = lines.at(i).split(',');
            QJsonArray line;

            for (int j = 0; j < items.count(); j++)
            {
                QList <QString> item = items.at(j).split(':');

                if (item.value(0).isEmpty())
                    continue;

                line.append(QJsonObject{{"text", item.value(0).trimmed()}, {"callback_data", item.value(item.count() > 1 ? 1 : 0).trimmed()}});
            }

            if (line.isEmpty())
                continue;

            array.append(line);
        }
    }

    if (!array.isEmpty())
        formList.append(QString("-F reply_markup='%1'").arg(QString(QJsonDocument(QJsonObject {{"inline_keyboard", array}}).toJson(QJsonDocument::Compact))));

    if (!update)
    {
        if (thread)
            formList.append(QString("-F message_thread_id=%1").arg(thread));

        if (silent)
            formList.append("-F disable_notification=true");
    }

    for (int i = 0; i < chatList.count(); i++)
    {
        QList <QString> list = formList;
        QProcess *process(new QProcess(this));
        QString method = QString("send%1").arg(QString(type).replace(0, 1, type.at(0).toUpper())), id;
        qint64 chatId = chatList.at(i);

        connect(process, static_cast <void (QProcess::*)(int, QProcess::ExitStatus)> (&QProcess::finished), this, &Telegram::finished);
        list.append(QString("-F chat_id=%1").arg(chatId));
        id = QString("%1:%2").arg(uuid).arg(chatId);

        if (m_automations->messages().contains(id))
        {
            if (update)
            {
                list.append(QString("-F message_id=%1").arg(m_automations->messages().value(id)));
                method = file.isEmpty() ? "editMessageText" : "editMessageMedia";

                if (!file.isEmpty())
                {
                    QJsonObject json = {{"type", type}, {"media", QFile::exists(document) ? QString("attach://%1").arg(type) : document}};

                    if (!message.isEmpty())
                    {
                        json.insert("caption", message);
                        json.insert("parse_mode", "Markdown");
                    }

                    list.append(QString("-F media='%1'").arg(QString(QJsonDocument(json).toJson(QJsonDocument::Compact))));
                }
            }
            else if (remove)
            {
                QProcess *process(new QProcess(this));
                connect(process, static_cast <void (QProcess::*)(int, QProcess::ExitStatus)> (&QProcess::finished), this, &Telegram::finished);
                process->start("sh", {"-c", QString("curl -X POST -H 'Content-Type: application/json' -d '%1' -s https://api.telegram.org/bot%2/deleteMessage").arg(QJsonDocument({{"chat_id", chatId}, {"message_id", m_automations->messages().value(id)}}).toJson(QJsonDocument::Compact), m_token)});
            }
            else
            {
                m_automations->messages().remove(id);
                m_automations->store(true);
            }
        }

        if (method != "editMessageMedia")
            list.append(messageList);

        if (remove || update)
            process->setProperty("id", id);

        process->start("sh", {"-c", QString("curl -X POST %1 -s https://api.telegram.org/bot%2/%3").arg(list.join(0x20), m_token, method)});
    }
}

void Telegram::getUpdates(void)
{
    m_buffer.clear();
    m_process->start("sh", {"-c", QString("curl -X POST -H 'Content-Type: application/json' -d '%1' -s https://api.telegram.org/bot%2/getUpdates").arg(QJsonDocument({{"timeout", m_timeout}, {"offset", m_offset}}).toJson(QJsonDocument::Compact), m_token)});
}

void Telegram::finished(int exitCode, QProcess::ExitStatus)
{
    QProcess *process = reinterpret_cast <QProcess*> (sender());

    if (process != m_process)
    {
        QJsonObject json = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        QString id = process->property("id").toString();

        process->deleteLater();

        if (!json.value("ok").toBool())
        {
            logWarning << "Telegram message request error, description:" << (json.contains("description") ? json.value("description").toString() : "(empty)");
            return;
        }

        if (!id.isEmpty())
        {
            m_automations->messages().insert(id, json.value("result").toObject().value("message_id").toVariant().toLongLong());
            m_automations->store(true);
        }
    }
    else if (!exitCode || exitCode == CURL_OPERATION_TIMEOUT_EXIT_CODE)
    {
        QList <QString> list = {"audio", "document", "photo", "video"};
        QJsonObject json = QJsonDocument::fromJson(m_buffer).object();
        QJsonArray array = json.value("result").toArray();

        if (!json.value("ok").toBool())
            logWarning << "Telegram updates request error, description:" << (json.contains("description") ? json.value("description").toString() : "(empty)");

        for (auto it = array.begin(); it != array.end(); it++)
        {
            QJsonObject item = it->toObject(), data;
            qint64 chat;
            bool check = item.contains("message");

            m_offset = item.value("update_id").toVariant().toLongLong() + 1;
            data = item.value(check ? "message" : "callback_query").toObject();
            chat = check ? data.value("chat").toObject().value("id").toVariant().toLongLong() : data.value("message").toObject().value("chat").toObject().value("id").toVariant().toLongLong();

            if (check)
            {
                for (int i = 0; i < list.count(); i++)
                {
                    QString type = list.at(i);

                    if (!data.contains(type))
                        continue;

                    sendMessage(QString("File ID:\n`%1`\n\nType:\n`%2`").arg(type != "photo" ? data.value(type).toObject().value("file_id").toString() : data.value("photo").toArray().last().toObject().value("file_id").toString(), type), QString(), QString(), QString(), 0, false, false, false, {chat});
                }
            }

            if (!data.contains(check ? "text" : "data"))
                continue;

            emit messageReceived(data.value(check ? "text" : "data").toString(), chat);
        }

        getUpdates();
    }
    else
    {
        logWarning << "Telegram getUpdates process failed, exit code:" << exitCode;
        m_timer->start(GET_UPDATES_RETRY_TIMEOUT);
    }
}

void Telegram::readyRead(void)
{
    m_buffer.append(m_process->readAllStandardOutput());
}

void Telegram::pollError(void)
{
    logWarning << "Telegram getUpdates process error:" << m_process->readAllStandardError();
}
