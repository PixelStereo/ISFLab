#pragma once
#include <QMap>
#include <QObject>
#include <QStringList>
#include <memory>
#include <vector>

class QProcess;
class QTemporaryFile;

// Checks shaders in worker processes (ShaderCheckWorker.h), several at once, and survives a worker that dies: some
// shaders crash the OpenGL driver while it compiles them. Each worker gets its share (every n-th shader, so heavy ones
// are spread out); when one dies on a shader, that shader is reported as crashed and the worker resumes after it.
class ShaderCheckRunner final : public QObject {
    Q_OBJECT
public:
    struct Result {
        QMap<QString, QString> errors; // path -> why it does not compile (or that it crashed the driver)
        QStringList passed;
        int crashes = 0;
        bool cancelled = false;
        QString failure; // a worker could not start or stopped without a reason: the rest was not checked
    };

    // `lanes` 0: as many as the machine and the driver allow (the driver does not scale linearly: 2 workers 1.7×, 8 workers
    // 2.6×, no more beyond)
    explicit ShaderCheckRunner(QStringList paths, QObject *parent = nullptr);
    ~ShaderCheckRunner() override;
    // The program that checks, with its arguments before the list file's name; the default is this application with
    // --check-shaders
    void setWorker(const QString &program, const QStringList &arguments);
    void setLanes(int lanes) { laneCount = lanes; }
    static int defaultLanes(qsizetype shaders);

    void start();
    void cancel();
    int checked() const { return done; }

signals:
    void progress(int checked, const QString &current); // current: the shader a worker has begun
    void finished(const ShaderCheckRunner::Result &result);

private:
    struct Lane {
        QStringList paths;
        std::unique_ptr<QTemporaryFile> list;
        QProcess *process = nullptr;
        QByteArray buffer;
        qsizetype current = -1, completed = 0;
    };
    void launch(Lane &lane);
    void read(Lane &lane);
    void ended(Lane &lane);
    void stopAll();
    void finish();

    QStringList targets;
    QString program;
    QStringList arguments;
    int laneCount = 0;
    std::vector<std::unique_ptr<Lane>> lanes;
    Result result;
    int running = 0, done = 0;
    bool over = false;
};
