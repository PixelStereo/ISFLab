#include "ShaderCheckRunner.h"
#include "ShaderCheckWorker.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryFile>
#include <QThread>
#include <QUrl>
#include <algorithm>

ShaderCheckRunner::ShaderCheckRunner(QStringList paths, QObject *parent)
    : QObject(parent), targets(std::move(paths)), program(QCoreApplication::applicationFilePath()),
      arguments{ShaderCheckWorkerFlag} {}

ShaderCheckRunner::~ShaderCheckRunner() { stopAll(); }

void ShaderCheckRunner::setWorker(const QString &worker, const QStringList &workerArguments) {
    program = worker;
    arguments = workerArguments;
}

int ShaderCheckRunner::defaultLanes(qsizetype shaders) {
    return std::min(std::clamp(QThread::idealThreadCount() / 2, 1, 8), std::max(1, int(shaders / 40)));
}

void ShaderCheckRunner::start() {
    const int count = laneCount > 0 ? laneCount : defaultLanes(targets.size());
    for (int k = 0; k < count; ++k) lanes.push_back(std::make_unique<Lane>());
    for (qsizetype i = 0; i < targets.size(); ++i) lanes[size_t(i % count)]->paths << targets.at(i);
    for (auto &lane : lanes) {
        if (lane->paths.isEmpty()) continue;
        ++running;
        launch(*lane);
        if (over) return;
    }
    if (running == 0) finish();
}

void ShaderCheckRunner::cancel() {
    if (over) return;
    result.cancelled = true;
    stopAll();
    finish();
}

void ShaderCheckRunner::stopAll() {
    for (auto &lane : lanes)
        if (lane->process) {
            lane->process->disconnect(this);
            if (lane->process->state() != QProcess::NotRunning) {
                lane->process->kill();
                lane->process->waitForFinished();
            }
            delete lane->process;
            lane->process = nullptr;
        }
}

void ShaderCheckRunner::finish() {
    if (over) return;
    over = true;
    emit finished(result);
}

void ShaderCheckRunner::launch(Lane &lane) {
    lane.list = std::make_unique<QTemporaryFile>();
    if (!lane.list->open() || lane.list->write(lane.paths.join('\n').toUtf8()) < 0 || !lane.list->flush()) {
        result.failure = "Cannot write the list of shaders to check.";
        stopAll();
        finish();
        return;
    }
    lane.process = new QProcess(this);
    lane.process->setProcessChannelMode(QProcess::SeparateChannels);
    Lane *l = &lane;
    connect(lane.process, &QProcess::readyReadStandardOutput, this, [this, l] { read(*l); });
    connect(lane.process, &QProcess::finished, this, [this, l] { ended(*l); });
    lane.process->start(program, arguments + QStringList{lane.list->fileName()});
    if (!lane.process->waitForStarted()) {
        result.failure = "Cannot start the shader check process: " + lane.process->errorString();
        stopAll();
        finish();
    }
}

// A slot of `progress` may run the event loop (a modal progress dialog does, in setValue()), which can deliver more
// output or a worker's end while we are still here. So every state change is made first and the signals are sent
// afterwards, from a state that is whole; nothing is read from a lane after a signal was sent.
void ShaderCheckRunner::read(Lane &lane) {
    lane.buffer += lane.process->readAllStandardOutput();
    QList<QByteArray> lines; // every complete line out of the buffer before any is handled
    for (qsizetype end; (end = lane.buffer.indexOf('\n')) >= 0;) {
        lines << lane.buffer.left(end);
        lane.buffer.remove(0, end + 1);
    }
    QList<QPair<int, QString>> news;
    for (const auto &line : lines) {
        const auto fields = line.split(' ');
        const auto index = fields.value(1).toLongLong();
        if (line.startsWith("FAIL ") && result.failure.isEmpty()) result.failure = QString::fromUtf8(line.mid(5));
        if (index < 0 || index >= lane.paths.size()) continue;
        if (line.startsWith("BEGIN ")) {
            lane.current = index;
            news << qMakePair(done, lane.paths.at(index));
        } else if (line.startsWith("OK ") || line.startsWith("ERR ")) {
            if (line.startsWith("OK ")) result.passed << lane.paths.at(index);
            else result.errors.insert(lane.paths.at(index), QUrl::fromPercentEncoding(fields.value(2)));
            lane.current = -1;
            lane.completed = index + 1;
            news << qMakePair(++done, lane.paths.at(index));
        }
    }
    for (const auto &[checked, current] : news) emit progress(checked, current);
}

void ShaderCheckRunner::ended(Lane &lane) {
    if (over) return;
    read(lane); // (its signals may have run the event loop: look at the state as it is now)
    if (over) return;
    QString crashed;
    if (result.failure.isEmpty()) {
        if (lane.current >= 0) {
            crashed = lane.paths.at(lane.current);
            result.errors.insert(crashed, "The OpenGL driver crashed while compiling this shader. "
                                          "Selecting it would close ISFLab.");
            ++result.crashes;
            ++done;
            lane.completed = lane.current + 1;
        } else if (lane.completed < lane.paths.size()) {
            result.failure = "The shader check process stopped unexpectedly.";
        }
        lane.paths = lane.paths.mid(lane.completed);
    }
    lane.process->deleteLater();
    lane.process = nullptr;
    lane.list.reset();
    lane.current = -1;
    lane.completed = 0;
    lane.buffer.clear();
    const bool stop = !result.failure.isEmpty(), laneDone = lane.paths.isEmpty();
    if (stop) stopAll();
    else if (laneDone) --running;
    else launch(lane); // the rest of its share, after the shader that crashed
    const bool all = stop || (laneDone && running == 0);
    if (!crashed.isEmpty()) emit progress(done, crashed);
    if (all) finish();
}
