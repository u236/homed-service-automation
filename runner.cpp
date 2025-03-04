#include "logger.h"
#include "runner.h"

Runner::Runner(Controller *controller, const Automation &automation) : QThread(nullptr), m_controller(controller), m_automation(automation)
{
    connect(this, &Runner::started, this, &Runner::threadStarted);
    moveToThread(this);
    start();
}

Runner::~Runner(void)
{
    automation()->setRunner(nullptr);
    m_timer->stop();
}

void Runner::runActions(void)
{
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
                        value = m_controller->parsePattern(value.toString(), automation()->lastTrigger());
                        string = value.toString();
                    }

                    if (string.contains(','))
                    {
                        QList <QString> list = string.split(',');
                        QJsonArray array;

                        for (int i = 0; i < list.count(); i++)
                            array.append(QJsonValue::fromVariant(Parser::stringValue(list.at(i))));

                        value = array;
                    }

                    emit publishData(m_controller->mqttTopic("td/").append(endpointId ? QString("%1/%2").arg(device->topic()).arg(endpointId) : device->topic()), QMap <QString, QVariant> {{action->property(), value}});
                }

                break;
            }

            case ActionObject::Type::mqtt:
            {
                MqttAction *action = reinterpret_cast <MqttAction*> (item.data());
                emit publishData(action->topic(), m_controller->parsePattern(action->message(), automation()->lastTrigger()).toString(), action->retain());
                break;
            }

            case ActionObject::Type::state:
            {
                StateAction *action = reinterpret_cast <StateAction*> (item.data());
                QMap <QString, QVariant> &states = m_controller->automations()->states();
                QVariant check = states.value(action->name());

                if (action->value().isValid() && !action->value().isNull())
                    states.insert(action->name(), m_controller->parsePattern(action->value().toString(), automation()->lastTrigger()));
                else
                    states.remove(action->name());

                if (check != states.value(action->name()))
                    emit storeAutomations();

                break;
            }

            case ActionObject::Type::telegram:
            {
                TelegramAction *action = reinterpret_cast <TelegramAction*> (item.data());

                if (!action->file().isEmpty())
                    m_controller->telegram()->sendFile(m_controller->parsePattern(action->message(), automation()->lastTrigger()).toString(), m_controller->parsePattern(action->file(), automation()->lastTrigger()).toString(), m_controller->parsePattern(action->keyboard(), automation()->lastTrigger()).toString(), action->thread(), action->silent(), action->chats());
                else
                    m_controller->telegram()->sendMessage(m_controller->parsePattern(action->message(), automation()->lastTrigger()).toString(), action->photo(), m_controller->parsePattern(action->keyboard(), automation()->lastTrigger()).toString(), action->thread(), action->silent(), action->chats());

                break;
            }

            case ActionObject::Type::shell:
            {
                ShellAction *action = reinterpret_cast <ShellAction*> (item.data());
                FILE *file = popen(m_controller->parsePattern(action->command(), automation()->lastTrigger()).toString().append(0x20).append("2>&1").toUtf8().constData(), "r");
                char buffer[32];
                QByteArray data;

                if (!file)
                    break;

                memset(buffer, 0, sizeof(buffer));

                while (fgets(buffer, sizeof(buffer), file))
                    data.append(buffer, strlen(buffer));

                automation()->setShellOutput(data.trimmed());
                pclose(file);
                break;
            }

            case ActionObject::Type::condition:
            {
                ConditionAction *action = reinterpret_cast <ConditionAction*> (item.data());
                automation()->actionList()->setIndex(++i);
                automation()->setActionList(&action->actions(m_controller->checkConditions(action->conditions(), ConditionObject::Type::AND, automation()->lastTrigger())));
                automation()->actionList()->setIndex(0);
                runActions();
                return;
            }

            case ActionObject::Type::delay:
            {
                DelayAction *action = reinterpret_cast <DelayAction*> (item.data());
                automation()->actionList()->setIndex(++i);
                logInfo << automation() << "timer" << (m_timer->isActive() ? "restarted" : "started");
                m_timer->start(m_controller->parsePattern(action->value().toString(), automation()->lastTrigger()).toInt() * 1000);
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

    exit();
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
