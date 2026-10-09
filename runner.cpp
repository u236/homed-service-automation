#include <csignal>
#include <unistd.h>
#include <QDir>
#include "controller.h"
#include "logger.h"

Runner::Runner(Controller *controller, const Automation &automation, const QMap <QString, QString> &meta) : QThread(nullptr), m_controller(controller), m_automation(automation), m_id(automation->counter()), m_processId(0), m_aborted(false), m_actions(&automation->actions()), m_meta(meta)
{
    connect(this, &Runner::started, this, &Runner::threadStarted);
    connect(this, &Runner::finished, this, &Runner::threadFinished);

    moveToThread(this);
}

Runner::~Runner(void)
{
    if (m_aborted)
        return;

    logDebug(automation()->log()) << this << "completed";
}

void Runner::abort(void)
{
    if (m_processId)
        killpg(m_processId, SIGKILL);

    if (m_aborted)
        return;

    logDebug(automation()->log()) << this << "aborted";
    m_aborted = true;
    quit();
}

void Runner::propertyMessage(PropertyAction *action, QString &topic, QVariant &message)
{
    QMutexLocker locker(m_controller->mutex());
    QString endpoint = action->endpoint() == "triggerEndpoint" ? m_meta.value("triggerEndpoint") : action->endpoint(), property = action->property() == "triggerProperty" ? m_meta.value("triggerProperty") : action->property();
    const Device &device = m_controller->findDevice(endpoint);

    if (!device.isNull())
    {
        quint8 endpointId = m_controller->getEndpointId(endpoint);
        QVariant value = action->value(device->properties().value(endpointId).value(property));
        QString string;

        if (value.type() == QVariant::String)
        {
            value = m_controller->parsePattern(value.toString(), m_meta, false);
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

        topic = m_controller->mqttTopic("td/").append(endpointId ? QString("%1/%2").arg(device->topic()).arg(endpointId) : device->topic());
        message = QMap <QString, QVariant> {{property, value}};
    }
}

QString Runner::parseFrame(QString string)
{
    QRegExp replace("\\{\\{\\s*camera\\s*\\|([^\\{\\}]*)\\}\\}");
    int position;

    while ((position = replace.indexIn(string)) != -1)
        string.replace(position, replace.cap().length(), requestFrame(replace.cap(1).trimmed()));

    return string;
}

QVariant Runner::parsePattern(QString string)
{
    QString data = parseFrame(string);
    QMutexLocker locker(m_controller->mutex());
    return m_controller->parsePattern(data, m_meta, false);
}

bool Runner::checkConditions(ConditionObject::Type type, const QList <Condition> &conditions)
{
    QMutexLocker locker(m_controller->mutex());
    return m_controller->checkConditions(type, conditions, m_meta);
}

QString Runner::requestFrame(const QString &device)
{
    QFile file(QString("%1/homed-automation-%2.jpg").arg(QDir::tempPath(), QString(device).replace(QRegExp("[^0-9a-zA-Z]"), "_")));
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray frame;
    QEventLoop loop;
    QTimer timer;

    if (m_frames.contains(device))
        return m_frames.value(device);

    connect(m_controller, &Controller::frameReceived, &loop, [&] (const QString &id, const QByteArray &data) { if (id != uuid) return; frame = data; loop.quit(); });
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    emit frameRequest(uuid, device);

    timer.start(FRAME_REQUEST_TIMEOUT);
    loop.exec();

    if (frame.isEmpty() || !file.open(QFile::WriteOnly))
    {
        logWarning << this << device << "frame request failed";
        return m_controller->basePath().append("share/homed-automation/camera.jpg");
    }

    file.write(frame);
    file.close();

    m_frames.insert(device, file.fileName());
    return file.fileName();
}

void Runner::runActions(void)
{
    while (!m_aborted)
    {
        int index = m_index.value(m_actions);
        const Action &item = m_actions->value(index);

        if (item.isNull())
        {
            if (m_actions->parent())
            {
                m_actions = m_actions->parent();
                continue;
            }

            quit();
            return;
        }

        m_index.insert(m_actions, index + 1);

        if (!item->active() || (!item->triggerName().isEmpty() && item->triggerName() != m_meta.value("triggerName")))
            continue;

        if (!m_loops.isEmpty())
        {
            ActionList *list = m_actions;

            while (list && !m_loops.contains(list))
                list = list->parent();

            m_meta.insert("loopIndex", list ? QString::number(m_loops.value(list).index) : QString());
        }

        switch (item->type())
        {
            case ActionObject::Type::property:
            {
                QString topic;
                QVariant message;

                propertyMessage(reinterpret_cast <PropertyAction*> (item.data()), topic, message);

                if (!topic.isEmpty())
                    emit publishMessage(topic, message);

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
                emit telegramAction(parsePattern(action->message()).toString(), parsePattern(action->file()).toString(), parsePattern(action->keyboard()).toString(), action->uuid(), action->thread(), action->rich(), action->silent(), action->remove(), action->update(), &action->chats());
                break;
            }

            case ActionObject::Type::shell:
            {
                QProcess process;
                ShellAction *action = reinterpret_cast <ShellAction*> (item.data());

                process.setProcessChannelMode(QProcess::MergedChannels);
                process.start("/bin/sh", {"-c", parsePattern(action->command()).toString()});

                m_processId = process.processId();
                setpgid(m_processId, 0);

                if (!process.waitForFinished(action->timeout() * 1000))
                {
                    logDebug(automation()->log()) << this << "shell action process" << m_processId << "timed out";
                    killpg(m_processId, SIGKILL);
                }

                m_meta.insert("shellOutput", process.readAll());
                break;
            }

            case ActionObject::Type::condition:
            {
                ConditionAction *action = reinterpret_cast <ConditionAction*> (item.data());
                m_actions = &action->actions(checkConditions(action->conditionType(), action->conditions()));
                m_index.insert(m_actions, 0);
                break;
            }

            case ActionObject::Type::loop:
            {
                LoopAction *action = reinterpret_cast <LoopAction*> (item.data());
                auto it = m_loops.find(&action->actions());

                if (it == m_loops.end())
                {
                    int count = parsePattern(action->count().toString()).toInt();

                    if (count < 1 && !action->atLeastOnce())
                        break;

                    it = m_loops.insert(&action->actions(), {static_cast <quint32> (qMax(count, 1)), 0});
                }

                if ((!action->atLeastOnce() || it->index) && (it->count == it->index || !checkConditions(action->conditionType(), action->conditions())))
                {
                    m_loops.erase(it);
                    break;
                }

                it->index++;

                m_index.insert(m_actions, index);
                m_actions = &action->actions();
                m_index.insert(m_actions, 0);
                break;
            }

            case ActionObject::Type::delay:
            {
                int delay = parsePattern(reinterpret_cast <DelayAction*> (item.data())->value().toString()).toInt();
                logDebug(automation()->log()) << this << "timer started for" << delay << "seconds";
                m_timer->start(delay * 1000);
                m_frames.clear();
                return;
            }

            case ActionObject::Type::exit:
            {
                quit();
                return;
            }
        }
    }
}

void Runner::threadStarted(void)
{
    logDebug(automation()->log()) << this << "started";

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Runner::timeout);

    m_timer->setSingleShot(true);
    runActions();
}

void Runner::threadFinished(void)
{
    m_timer->stop();
}

void Runner::timeout(void)
{
    logDebug(automation()->log()) << this << "timer stopped";
    runActions();
}
