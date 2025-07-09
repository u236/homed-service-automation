#include <unistd.h>
#include "controller.h"
#include "logger.h"

Runner::Runner(Controller *controller, const Automation &automation, const QString &triggerName) : QThread(nullptr), m_controller(controller), m_automation(automation), m_id(automation->counter()), m_triggerName(triggerName), m_actions(&automation->actions()), m_aborted(false)
{
    connect(this, &Runner::started, this, &Runner::threadStarted);
    connect(this, &Runner::finished, this, &Runner::threadFinished);

    moveToThread(this);
}

Runner::~Runner(void)
{
    if (m_aborted)
        return;

    logInfo << this << "completed";
}

void Runner::abort(void)
{
    logInfo << this << "aborted";
    m_aborted = true;

    if (m_process->isOpen())
        m_process->kill();

    quit();
}

QVariant Runner::parsePattern(QString string)
{
    return m_controller->parsePattern(string, m_triggerName, m_shellOutput, false);
}

void Runner::runActions(void)
{
    for (int i = m_actions->index(); i < m_actions->count(); i++)
    {
        const Action &item = m_actions->at(i);

        if (!item->triggerName().isEmpty() && item->triggerName() != m_triggerName)
            continue;

        switch (item->type())
        {
            case ActionObject::Type::property:
            {
                PropertyAction *action = reinterpret_cast <PropertyAction*> (item.data());
                const Device &device = m_controller->findDevice(action->endpoint());

                if (!device.isNull())
                {
                    quint8 endpointId = m_controller->getEndpointId(action->endpoint());
                    QVariant value = action->value(device->properties().value(endpointId).value(action->property()));
                    QString string;

                    if (value.type() == QVariant::String)
                    {
                        value = parsePattern(value.toString());
                        string = value.toString();
                    }

                    if (string.contains(','))
                    {
                        QList <QString> list = string.split(',');
                        QJsonArray array;

                        for (int i = 0; i < list.count(); i++)
                            array.append(QJsonValue::fromVariant(Parser::stringValue(list.at(i).trimmed())));

                        value = array;
                    }

                    emit publishMessage(m_controller->mqttTopic("td/").append(endpointId ? QString("%1/%2").arg(device->topic()).arg(endpointId) : device->topic()), QMap <QString, QVariant> {{action->property(), value}});
                }

                break;
            }

            case ActionObject::Type::mqtt:
            {
                MqttAction *action = reinterpret_cast <MqttAction*> (item.data());
                emit publishMessage(action->topic(), parsePattern(action->message()).toString(), action->retain());
                break;
            }

            case ActionObject::Type::state:
            {
                StateAction *action = reinterpret_cast <StateAction*> (item.data());
                emit updateState(action->name(), parsePattern(action->value().toString()));
                break;
            }

            case ActionObject::Type::telegram:
            {
                TelegramAction *action = reinterpret_cast <TelegramAction*> (item.data());
                emit telegramAction(parsePattern(action->message()).toString(), parsePattern(action->file()).toString(), parsePattern(action->keyboard()).toString(), action->uuid(), action->thread(), action->silent(), action->remove(), action->update(), &action->chats());;
                break;
            }

            case ActionObject::Type::shell:
            {
                ShellAction *action = reinterpret_cast <ShellAction*> (item.data());

                m_process->setProcessChannelMode(QProcess::MergedChannels);
                m_process->start("/bin/sh", {"-c", parsePattern(action->command()).toString()});
                setpgid(m_process->processId(), m_process->processId());

                if (!m_process->waitForFinished(action->timeout() * 1000))
                {
                    logWarning << this << "shell action process" << m_process->processId() << "timed out";
                    system(QString("kill -9 -%1").arg(m_process->processId()).toUtf8().constData());
                }

                if (m_aborted)
                    return;

                m_shellOutput = m_process->readAll();
                break;
            }

            case ActionObject::Type::condition:
            {
                ConditionAction *action = reinterpret_cast <ConditionAction*> (item.data());
                m_actions->setIndex(++i);
                m_actions = &action->actions(m_controller->checkConditions(action->conditions(), ConditionObject::Type::AND, m_triggerName));
                m_actions->setIndex(0);
                runActions();
                return;
            }

            case ActionObject::Type::delay:
            {
                int delay = parsePattern(reinterpret_cast <DelayAction*> (item.data())->value().toString()).toInt();
                logInfo << this << "timer started for" << delay << "seconds";
                m_actions->setIndex(++i);
                m_timer->start(delay * 1000);
                return;
            }
        }
    }

    if (m_actions->parent())
    {
        m_actions = m_actions->parent();
        runActions();
        return;
    }

    quit();
}

void Runner::threadStarted(void)
{
    logInfo << this << "started";

    m_process = new QProcess(this);
    m_timer = new QTimer(this);

    connect(m_timer, &QTimer::timeout, this, &Runner::timeout);

    m_timer->setSingleShot(true);
    m_actions->setIndex(0);
    runActions();
}

void Runner::threadFinished(void)
{
    m_timer->stop();
}

void Runner::timeout(void)
{
    logInfo << this << "timer stopped";
    runActions();
}
