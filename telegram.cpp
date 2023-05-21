#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "telegram.h"

#include "logger.h"

Telegram::Telegram(QSettings *config, QObject *parent) : QObject(parent), m_process(new QProcess(this)), m_offset(0)
{
    m_token = config->value("telegram/token").toString();
    m_chat = config->value("telegram/chat").toInt();
    m_timeout = config->value("telegram/timeout", 60).toInt();

    if (m_token.isEmpty() || !m_chat)
        return;

    connect(m_process, &QProcess::readyReadStandardOutput, this, &Telegram::readyRead);
    getUpdates();
}

Telegram::~Telegram(void)
{
    m_process->close();
}

void Telegram::sendMessage(const QString &message)
{
    if (m_token.isEmpty() || !m_chat)
        return;

    system(QString("curl -X POST -H 'Content-Type: application/json' -d '%1' -s https://api.telegram.org/bot%2/sendMessage > /dev/null &").arg(QJsonDocument(QJsonObject {{"chat_id", m_chat}, {"text", message}, {"parse_mode", "Markdown"}}).toJson(QJsonDocument::Compact), m_token).toUtf8());
}

void Telegram::getUpdates(void)
{
    m_process->start("curl", {"-X", "POST", "-H", "Content-Type: application/json", "-d", QJsonDocument(QJsonObject {{"timeout", m_timeout}, {"offset", m_offset}}).toJson(QJsonDocument::Compact), "-s", QString("https://api.telegram.org/bot%1/getUpdates").arg(m_token)});
}

void Telegram::readyRead(void)
{
    QJsonArray array = QJsonDocument::fromJson(m_process->readAllStandardOutput()).object().value("result").toArray();

    for (auto it = array.begin(); it != array.end(); it++)
    {
        QJsonObject json = it->toObject(), message = json.value("message").toObject();

        if (message.value("chat").toObject().value("id").toInt() == m_chat)
            emit messageReceived(message.value("text").toString());

        m_offset = json.value("update_id").toInt() + 1;
    }

    m_process->close();
    getUpdates();
}
