// The batch shader check's orchestration: several workers, a worker that dies on a shader, cancelling, and workers that
// cannot start or stop without saying why. The worker is this executable (--fake-worker), so it runs everywhere.
#include "ShaderCheckRunner.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
void check(bool ok, const char *why) {
    if (!ok) { ++failures; std::printf("FAIL shadercheck: %s\n", why); }
}

// What a worker says (see ShaderCheckWorker.h): BEGIN i, then OK i or ERR i <message>; a path with "crash" in it kills it
int fakeWorker(const QString &listFile) {
    QFile list(listFile);
    if (!list.open(QIODevice::ReadOnly)) return 2;
    const auto paths = QString::fromUtf8(list.readAll()).split('\n', Qt::SkipEmptyParts);
    const bool slow = qEnvironmentVariableIsSet("SHADERCHECK_SLOW");
    for (qsizetype i = 0; i < paths.size(); ++i) {
        std::printf("BEGIN %d\n", int(i));
        std::fflush(stdout);
        if (slow) QThread::msleep(40);
        if (paths.at(i).contains("crash")) std::_Exit(139); // the driver took the process down
        if (paths.at(i).contains("bad")) std::printf("ERR %d Fragment%%20shader%%20failed\n", int(i));
        else std::printf("OK %d\n", int(i));
        std::fflush(stdout);
    }
    return 0;
}

ShaderCheckRunner::Result run(const QStringList &paths, int lanes, const QStringList &args = {}, int cancelAfterMs = -1,
                              const QString &program = {}) {
    ShaderCheckRunner runner(paths);
    runner.setLanes(lanes);
    runner.setWorker(program.isEmpty() ? QCoreApplication::applicationFilePath() : program, args.isEmpty() ? QStringList{"--fake-worker"} : args);
    QEventLoop loop;
    ShaderCheckRunner::Result result;
    int finishedSignals = 0;
    QObject::connect(&runner, &ShaderCheckRunner::finished, &loop, [&](const ShaderCheckRunner::Result &r) {
        result = r;
        ++finishedSignals;
        loop.quit(); // (before exec() it does nothing: finished can come from start() itself)
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { check(false, "the check did not finish in time"); loop.quit(); });
    timeout.start(30000);
    if (cancelAfterMs >= 0) QTimer::singleShot(cancelAfterMs, &runner, &ShaderCheckRunner::cancel);
    runner.start();
    if (finishedSignals == 0) loop.exec();
    QCoreApplication::processEvents();
    check(finishedSignals == 1, "finished is emitted exactly once");
    return result;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == "--fake-worker") return fakeWorker(app.arguments().value(2));
    if (app.arguments().value(1) == "--silent-worker") return 0; // stops without saying anything

    QStringList paths;
    for (int i = 0; i < 30; ++i) paths << QString("/shaders/s%1.fs").arg(i);
    paths[4] = "/shaders/bad4.fs";
    paths[11] = "/shaders/crash11.fs";
    paths[12] = "/shaders/bad12.fs";
    paths[20] = "/shaders/crash20.fs";
    paths[21] = "/shaders/crash21.fs"; // two in a row on the same worker (3 lanes: 20, 21 and 22 are on three)
    for (const int lanes : {1, 3, 5}) {
        const auto r = run(paths, lanes);
        check(r.failure.isEmpty() && !r.cancelled, "no failure");
        check(r.crashes == 3, "each crash is counted once");
        check(r.errors.size() == 5 && r.errors.contains("/shaders/bad4.fs") && r.errors.contains("/shaders/crash11.fs") &&
                  r.errors.value("/shaders/bad12.fs") == "Fragment shader failed",
              "errors: 2 that do not compile (message decoded), 3 crashes");
        check(r.errors.value("/shaders/crash11.fs").contains("crashed"), "a crash says so");
        check(r.passed.size() == 25, "every other shader is checked, after the crashes too");
        QStringList all = r.passed;
        all << r.errors.keys();
        all.sort();
        QStringList expected = paths;
        expected.sort();
        check(all == expected, "every shader is accounted for exactly once");
    }

    qputenv("SHADERCHECK_SLOW", "1");
    {
        QStringList many;
        for (int i = 0; i < 200; ++i) many << QString("/shaders/m%1.fs").arg(i);
        const auto r = run(many, 2, {}, 300);
        check(r.cancelled, "cancel is reported");
        check(r.passed.size() + r.errors.size() < 200, "cancel stops the workers before the end");
    }
    qunsetenv("SHADERCHECK_SLOW");

    {
        const auto r = run({"/shaders/a.fs"}, 1, {}, -1, "/nonexistent/worker");
        check(r.failure.contains("Cannot start"), "a worker that cannot start is a failure");
    }
    {
        const auto r = run({"/shaders/a.fs", "/shaders/b.fs"}, 1, {"--silent-worker"});
        check(r.failure.contains("stopped unexpectedly"), "a worker that stops without a word is a failure");
    }
    check(ShaderCheckRunner::defaultLanes(10) == 1 && ShaderCheckRunner::defaultLanes(100000) >= 1 &&
              ShaderCheckRunner::defaultLanes(100000) <= 8, "the default number of workers is between 1 and 8, one for few shaders");
    if (failures == 0) std::printf("ALL OK\n");
    return failures ? 1 : 0;
}
