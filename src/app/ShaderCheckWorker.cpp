#include "ShaderCheckWorker.h"
#include "ISFScanner.h"
#include "Library.h"
#include "ShaderChecker.h"
#include "ShaderPreview.h"
#include <QFile>
#include <QGuiApplication>
#include <QUrl>
#include <cstdio>

#ifdef Q_OS_MACOS
void hideMacApplicationFromDock();
#endif

namespace {
void emitLine(const QByteArray &line) {
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout); // the parent must see BEGIN before a driver crash
}
}

int runShaderCheckWorker(int argc, char **argv) {
    ShaderPreview::configureDefaultFormat(); // same GLSL version as the preview
    QGuiApplication app(argc, argv);
#ifdef Q_OS_MACOS
    hideMacApplicationFromDock();
#endif
    const auto arguments = app.arguments();
    const auto flag = arguments.indexOf(ShaderCheckWorkerFlag);
    QFile list(flag >= 0 && flag + 1 < arguments.size() ? arguments.at(flag + 1) : QString());
    if (!list.open(QIODevice::ReadOnly)) {
        emitLine("FAIL Cannot read the list of shaders to check.");
        return 1;
    }
    const auto paths = QString::fromUtf8(list.readAll()).split('\n', Qt::SkipEmptyParts);
    ShaderChecker checker;
    if (const auto error = checker.initialize(); !error.isEmpty()) {
        emitLine("FAIL " + error.toUtf8());
        return 1;
    }
    for (qsizetype index = 0; index < paths.size(); ++index) {
        const auto number = QByteArray::number(index);
        emitLine("BEGIN " + number);
        const auto entry = ShaderEntry::read(paths.at(index));
        QString error;
        if (!entry) error = "The file could not be read.";
        else {
            const auto header = entry->madMapper ? ISFScanner::materialHeader(entry->source) : entry->header;
            error = header ? checker.check(entry->source, *header, entry->path) : QString("Invalid shader header.");
        }
        emitLine(error.isEmpty() ? "OK " + number : "ERR " + number + " " + QUrl::toPercentEncoding(error));
    }
    return 0;
}
