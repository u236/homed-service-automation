#include <unistd.h>
#include <QProcess>
#include "logger.h"
#include "runner.h"

Runner::Runner(Controller *controller, const Automation &automation) : QThread(nullptr), m_controller(controller), m_automation(automation), m_aborted(false)
{
    connect(this, &Runner::started, this, &Runner::threadStarted);
    moveToThread(this);
}

Runner::~Runner(void)
{
    logInfo << automation() << "completed";
    automation()->setRunner(nullptr);
}

void Runner::abort(void)
{
    logInfo << automation() << "aborted";
    m_aborted = true;
    quit();
}

void Runner::runActions(void)
{
    m_timer->stop();

    for (int i = automation()->actionList()->index(); i < automation()->actionList()->count(); i++)
    {
        const Action &item = automation()->actionList()->at(i);

        if (!item->triggerName().isEmpty() && item->triggerName() != automation()->lastTrigger()->name())
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
                QProcess process;

                process.setProcessChannelMode(QProcess::MergedChannels);
                process.start("/bin/sh", {"-c", parsePattern(action->command()).toString()});
                setpgid(process.processId(), process.processId());

                if (!process.waitForFinished(action->timeout() * 1000))
                {
                    logWarning << automation() << "shell action process" << process.processId() << "timed out";
                    system(QString("kill -9 -%1").arg(process.processId()).toUtf8().constData());
                }

                if (m_aborted)
                    return;

                automation()->setShellOutput(process.readAll());
                break;
            }

            case ActionObject::Type::condition:
            {
                ConditionAction *action = reinterpret_cast <ConditionAction*> (item.data());
                automation()->actionList()->setIndex(++i);
                automation()->setActionList(&action->actions(m_controller->checkConditions(automation(), action->conditions(), ConditionObject::Type::AND)));
                automation()->actionList()->setIndex(0);
                runActions();
                return;
            }

            case ActionObject::Type::delay:
            {
                int delay = parsePattern(reinterpret_cast <DelayAction*> (item.data())->value().toString()).toInt();
                automation()->actionList()->setIndex(++i);
                logInfo << automation() << "timer started for" << delay << "seconds";
                m_timer->start(delay * 1000);
                return;
            }
        }
    }

    if (automation()->actionList()->parent())
    {
        automation()->setActionList(automation()->actionList()->parent());
        runActions();
        return;
    }

    quit();
}

void Runner::threadStarted(void)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Runner::timeout);

    m_timer->setSingleShot(true);
    automation()->setRunner(this);
    runActions();
}

void Runner::timeout(void)
{
    logInfo << automation() << "timer stopped";
    runActions();
}
