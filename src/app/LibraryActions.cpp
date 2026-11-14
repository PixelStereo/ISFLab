#include "LibraryActions.h"
#include "ShaderCheckWorker.h"
#include "UiHelpers.h"
#include "ZipExport.h"
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QProgressBar>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryFile>
#include <QUrl>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>

LibraryActions::LibraryActions(QWidget *window, MediaBin *bin, LibraryHost *host)
    : QObject(window), window(window), bin(bin), host(host) {}

void LibraryActions::exportFiltered(bool zip) {
    if (!host->resolveChanges()) return;
    QList<ShaderEntry> selected;
    QStringList unreadable;
    for (const auto &entry : bin->shaders()) {
        const auto current = ShaderEntry::read(entry.path);
        if (current && bin->matches(*current)) selected << *current;
        else if (!current && bin->matches(entry)) unreadable << "Cannot read: " + entry.path;
    }
    if (selected.isEmpty() && !unreadable.isEmpty()) {
        ExportReport result;
        result.errors = unreadable;
        report(result, {});
        return;
    }
    if (selected.isEmpty()) {
        QMessageBox::information(window, "Export shaders", "No shaders match the current filters.");
        return;
    }
    if (zip) {
        auto archive = QFileDialog::getSaveFileName(window, "Export filtered ZIP",
                                                    QDir(host->browseFolder()).filePath("ISFLab-Library.zip"), "ZIP archives (*.zip)");
        if (archive.isEmpty()) return;
        if (!archive.endsWith(".zip", Qt::CaseInsensitive)) {
            archive += ".zip";
            if (QFileInfo::exists(archive) &&
                QMessageBox::question(window, "Replace ZIP", "Replace the existing ZIP archive?") != QMessageBox::Yes)
                return;
        }
        QTemporaryDir staging;
        if (!staging.isValid()) {
            QMessageBox::warning(window, "Export filtered ZIP", "Cannot create a temporary folder.");
            return;
        }
        auto result = exportShaders(selected, staging.path());
        result.errors << unreadable;
        if (result.copied > 0) {
            const auto error = writeLibraryZip(staging.path(), archive);
            if (!error.isEmpty()) { result.errors << error; result.copied = 0; }
        }
        report(result, archive, true);
        return;
    }
    const auto destination = QFileDialog::getExistingDirectory(window, "Export filtered shaders to…", host->browseFolder());
    if (!destination.isEmpty()) {
        auto result = exportShaders(selected, destination);
        result.errors << unreadable;
        report(result, destination);
    }
}

void LibraryActions::actOnShaders(const QStringList &paths, MediaBin::FileAction action) {
    const bool trash = action == MediaBin::FileAction::Trash;
    const bool copy = action == MediaBin::FileAction::Copy;
    const QString title = trash ? "Move to Trash" : copy ? "Copy to Folder" : "Move to Folder";
    QString destination;
    if (trash) {
        if (QMessageBox::question(window, title, QString("Move %1 selected shader file(s) to the Trash?").arg(paths.size()))
            != QMessageBox::Yes) return;
    } else {
        const auto chosen = QFileDialog::getExistingDirectory(window, title, host->browseFolder());
        if (chosen.isEmpty()) return;
        destination = QFileInfo(chosen).canonicalFilePath();
        if (destination.isEmpty() || !QFileInfo(destination).isDir()) {
            QMessageBox::warning(window, title, "The destination folder is no longer available.");
            return;
        }
    }
    if (paths.contains(host->activePath()) && !host->resolveChanges()) return;

    QStringList failures, removed;
    int done = 0;
    const int total = int(paths.size());
    std::atomic<int> processed{0};
    std::atomic<bool> cancelled{false};
    const auto snapshot = bin->shaders();
    class FileProgressDialog final : public QDialog {
    public:
        FileProgressDialog(QWidget *parent, std::atomic<bool> &cancelled)
            : QDialog(parent), cancelled(cancelled) {}
        void reject() override { cancelled.store(true); }
    private:
        std::atomic<bool> &cancelled;
    } progress(window, cancelled);
    progress.setWindowTitle(title);
    auto *progressLayout = new QVBoxLayout(&progress);
    auto *message = plainLabel(title + "…");
    auto *bar = new QProgressBar;
    bar->setRange(0, total);
    bar->setValue(0);
    bar->setFormat("%v / %m");
    auto *cancel = new QPushButton("Cancel");
    progressLayout->addWidget(message);
    progressLayout->addWidget(bar);
    progressLayout->addWidget(cancel);
    connect(cancel, &QPushButton::clicked, &progress, &QDialog::reject);

    // Keep the bin locked while filesystem work runs off the UI thread.
    auto worker = std::unique_ptr<QThread>(QThread::create([&] {
        for (const auto &path : paths) {
            if (cancelled.load()) break;
            const auto process = [&] {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly))
                    return file.errorString();
                const auto bytes = file.readAll();
                if (file.error() != QFileDevice::NoError)
                    return file.errorString();
                file.close();
                if (!copy && bytes != snapshot.value(path).source)
                    return QString("The file changed since it was imported.");
                QString created;
                if (!trash) {
                    if (!copy && QFileInfo(path).absolutePath() == destination) return QString();
                    const auto entry = ShaderEntry::fromSource(path, bytes);
                    const auto error = copyShaderToFolder(entry, destination, created);
                    if (!error.isEmpty()) return error;
                }
                if (!copy) {
                    if (!file.moveToTrash())
                        return file.errorString() + (created.isEmpty() ? QString() : "\nCopy kept at " + created);
                    removed << path;
                }
                ++done;
                return QString();
            };
            const auto error = process();
            if (!error.isEmpty()) failures << path + ": " + error;
            ++processed;
        }
    }));
    QTimer updateProgress;
    connect(&updateProgress, &QTimer::timeout, &progress, [&] {
        bar->setValue(processed.load());
        if (cancelled.load()) {
            message->setText("Stopping after the current file…");
            cancel->setEnabled(false);
        }
    });
    connect(worker.get(), &QThread::finished, &progress, &QDialog::accept);
    updateProgress.start(100);
    worker->start();
    progress.exec();
    worker->wait();
    updateProgress.stop();

    if (removed.contains(host->activePath())) host->clearSelection();
    bin->forget(removed);
    const QString verb = trash ? "sent to the Trash" : copy ? "copied" : "moved";
    QMessageBox result(failures.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
                       title, QString("%1 shader(s) %2. %3 error(s).").arg(done).arg(verb).arg(failures.size()) +
                           (cancelled.load() ? QString("\nOperation cancelled.") : QString()),
                       QMessageBox::Ok, window);
    result.setDetailedText(failures.join('\n'));
    result.exec();
}

void LibraryActions::checkShaders(const QStringList &paths) {
    const QString title = "Check Shaders";
    QStringList targets;
    int skipped = 0;
    for (const auto &path : paths) {
        if (!bin->contains(path)) continue;
        const auto type = bin->shader(path).type();
        // Invalid headers are already in ERR and fragments without a header cannot be previewed.
        if (type == ISFType::Unknown || type == ISFType::NonISF) ++skipped;
        else targets << path;
    }
    if (targets.isEmpty()) {
        QMessageBox::information(window, title, "No shader with a valid header to check.");
        return;
    }
    QProgressDialog progress("Compiling shaders…", "Cancel", 0, int(targets.size()), window);
    progress.setWindowTitle(title);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QMap<QString, QString> errors;
    QStringList passed;
    int checked = 0, crashes = 0;
    bool cancelled = false;
    QString failure;
    // Each worker process checks the remaining shaders. When the OpenGL driver crashes on one, that
    // shader goes to ERR and a new worker resumes after it.
    QStringList remaining = targets;
    while (!remaining.isEmpty() && !cancelled && failure.isEmpty()) {
        QTemporaryFile list;
        if (!list.open() || list.write(remaining.join('\n').toUtf8()) < 0 || !list.flush()) {
            failure = "Cannot write the list of shaders to check.";
            break;
        }
        QProcess worker;
        worker.setProcessChannelMode(QProcess::SeparateChannels);
        worker.start(QCoreApplication::applicationFilePath(), {ShaderCheckWorkerFlag, list.fileName()});
        if (!worker.waitForStarted()) {
            failure = "Cannot start the shader check process: " + worker.errorString();
            break;
        }
        qsizetype current = -1, completed = 0;
        QByteArray buffer;
        for (bool finished = false; !finished;) {
            finished = worker.waitForFinished(50);
            QCoreApplication::processEvents(); // keeps Cancel responsive during long compiles
            buffer += worker.readAllStandardOutput();
            for (qsizetype end; (end = buffer.indexOf('\n')) >= 0; buffer.remove(0, end + 1)) {
                const auto line = buffer.left(end);
                const auto fields = line.split(' ');
                const auto index = fields.value(1).toLongLong();
                if (line.startsWith("FAIL ")) failure = QString::fromUtf8(line.mid(5));
                if (index < 0 || index >= remaining.size()) continue;
                if (line.startsWith("BEGIN ")) {
                    current = index;
                    progress.setLabelText(QString("Compiling %1…").arg(QFileInfo(remaining.at(index)).fileName()));
                } else if (line.startsWith("OK ") || line.startsWith("ERR ")) {
                    if (line.startsWith("OK ")) passed << remaining.at(index);
                    else errors.insert(remaining.at(index), QUrl::fromPercentEncoding(fields.value(2)));
                    current = -1;
                    completed = index + 1;
                    progress.setValue(++checked);
                }
            }
            if (progress.wasCanceled()) {
                worker.kill();
                worker.waitForFinished();
                cancelled = true;
                break;
            }
        }
        if (cancelled || !failure.isEmpty()) break;
        if (current >= 0) {
            errors.insert(remaining.at(current), "The OpenGL driver crashed while compiling this shader. "
                                                 "Selecting it would close ISFLab.");
            ++crashes;
            progress.setValue(++checked);
            completed = current + 1;
        } else if (completed < remaining.size()) {
            failure = "The shader check process stopped unexpectedly.";
            break;
        }
        remaining = remaining.mid(completed);
    }
    if (!failure.isEmpty()) QMessageBox::warning(window, title, failure);
    cancelled |= progress.wasCanceled();
    progress.setValue(int(targets.size()));
    bin->applyCheckResults(errors, passed);

    QMessageBox result(errors.isEmpty() ? QMessageBox::Information : QMessageBox::Warning, title,
                       QString("%1 shader(s) checked · %2 in error · %3 skipped (invalid header or Non-ISF).")
                           .arg(checked).arg(errors.size()).arg(skipped) +
                           (crashes ? QString("\n%1 shader(s) crashed the OpenGL driver.").arg(crashes) : QString()) +
                           (cancelled ? QString("\nCheck cancelled: the remaining shaders keep their state.") : QString()),
                       QMessageBox::Ok, window);
    QStringList details;
    for (auto it = errors.cbegin(); it != errors.cend(); ++it)
        details << it.key() + "\n    " + it.value().section('\n', 0, 0);
    result.setDetailedText(details.join('\n'));
    result.exec();
}

void LibraryActions::findDuplicates() {
    if (bin->shaders().isEmpty()) {
        QMessageBox::information(window, "Find Duplicates", "Import shaders to look for duplicates.");
        return;
    }
    const auto snapshot = bin->shaders();
    QMap<QByteArray, QStringList> groupsByContent;
    std::atomic<bool> cancelled{false};
    int uncertain = 0;

    QDialog scan(window);
    scan.setWindowTitle("Find Duplicates");
    auto *scanLayout = new QVBoxLayout(&scan);
    scanLayout->addWidget(plainLabel("Comparing shader files and their dependencies…"));
    auto *progress = new QProgressBar;
    progress->setRange(0, 0);
    scanLayout->addWidget(progress);

    auto worker = std::unique_ptr<QThread>(QThread::create([&] {
        QHash<QByteArray, QStringList> bySource;
        for (const auto &entry : snapshot) {
            if (cancelled.load()) return;
            bySource[QCryptographicHash::hash(shaderComparisonSource(entry), QCryptographicHash::Sha256)].append(entry.path);
        }
        for (const auto &candidates : bySource) {
            if (candidates.size() < 2) continue;
            for (const auto &path : candidates) {
                if (cancelled.load()) return;
                const auto fingerprint = shaderFingerprint(snapshot.value(path));
                if (fingerprint) groupsByContent[*fingerprint] << path;
                else ++uncertain;
            }
        }
    }));
    connect(worker.get(), &QThread::finished, &scan, &QDialog::accept);
    connect(&scan, &QDialog::rejected, &scan, [&] { cancelled.store(true); });
    worker->start();
    const auto scanned = scan.exec();
    worker->wait();
    if (scanned != QDialog::Accepted) return;

    QDialog review(window);
    review.setWindowTitle("Find Duplicates");
    auto *layout = new QVBoxLayout(&review);
    auto *summary = plainLabel();
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto *results = new QTreeWidget;
    results->setHeaderLabels({"Shader", "Path"});
    results->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layout->addWidget(results);
    for (auto paths : groupsByContent) {
        if (paths.size() < 2) continue;
        paths.sort(Qt::CaseInsensitive);
        if (paths.removeOne(host->activePath())) paths.prepend(host->activePath());
        const auto preferred = std::find_if(paths.cbegin(), paths.cend(), [&](const QString &path) {
            const auto &entry = snapshot[path];
            return entry.source.contains("isf_FragNormCoord") && shaderComparisonSource(entry) == entry.source;
        });
        if (preferred != paths.cend()) {
            const auto keeper = *preferred;
            paths.removeOne(keeper);
            paths.prepend(keeper);
        }
        auto *group = new QTreeWidgetItem(results, {QString("%1 matching shaders").arg(paths.size())});
        for (qsizetype i = 0; i < paths.size(); ++i) {
            auto *row = new QTreeWidgetItem(group, {QFileInfo(paths.at(i)).fileName(), paths.at(i)});
            row->setCheckState(0, i == 0 ? Qt::Unchecked : Qt::Checked);
        }
        group->setExpanded(true);
    }
    const int count = results->topLevelItemCount();
    summary->setText(QString("%1 duplicate group(s). Leave at least one file unchecked in each group.\n"
                             "%2 candidate(s) could not be compared safely.").arg(count).arg(uncertain));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *remove = buttons->addButton("Remove from Media Bin", QDialogButtonBox::ActionRole);
    auto *trash = buttons->addButton("Move to Trash…", QDialogButtonBox::ActionRole);
    remove->setEnabled(count > 0);
    trash->setEnabled(count > 0);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &review, &QDialog::reject);
    const auto process = [&](bool deleteFiles) {
        QStringList selected;
        for (int i = 0; i < count; ++i) {
            const auto *group = results->topLevelItem(i);
            QStringList checked;
            QString keeper;
            for (int child = 0; child < group->childCount(); ++child) {
                const auto *row = group->child(child);
                if (row->checkState(0) == Qt::Checked) checked << row->text(1);
                else if (keeper.isEmpty()) keeper = row->text(1);
            }
            if (checked.isEmpty()) continue;
            if (keeper.isEmpty()) {
                QMessageBox::warning(&review, "Find Duplicates", "Leave at least one file unchecked in each group.");
                return;
            }
            if (deleteFiles) {
                const auto fingerprint = shaderFingerprint(snapshot.value(keeper));
                for (const auto &path : checked) {
                    if (!fingerprint || shaderFingerprint(snapshot.value(path)) != fingerprint) {
                        QMessageBox::warning(&review, "Find Duplicates", "Files or dependencies changed. Run the scan again.");
                        return;
                    }
                }
            }
            selected << checked;
        }
        if (selected.isEmpty()) return;
        if (!deleteFiles && selected.contains(host->activePath()) && !host->resolveChanges()) return;
        review.accept();
        if (deleteFiles) {
            actOnShaders(selected, MediaBin::FileAction::Trash);
            return;
        }
        if (selected.contains(host->activePath())) host->clearSelection();
        bin->forget(selected);
    };
    connect(remove, &QPushButton::clicked, &review, [&] { process(false); });
    connect(trash, &QPushButton::clicked, &review, [&] { process(true); });
    review.resize(820, 460);
    review.exec();
}

void LibraryActions::repairErrors(const QStringList &paths) {
    const QString title = "Repair ERR";
    QMap<QString, QList<qsizetype>> repairs;
    int errorCount = 0;
    for (const auto &path : paths) {
        if (!bin->contains(path)) continue;
        const auto entry = bin->shader(path);
        if (bin->binType(entry) != ISFType::Unknown) continue;
        ++errorCount;
        if (path == host->activePath() && host->hasUnsavedChanges()) continue;
        const auto commas = ISFScanner::trailingCommaFix(entry.source);
        if (!commas.isEmpty()) repairs.insert(path, commas);
    }
    if (repairs.isEmpty()) {
        QMessageBox::information(window, title, QString(
            "No safe automatic repair found for %1 shader(s) in error. Files with unsaved edits are skipped.")
            .arg(errorCount));
        return;
    }
    if (QMessageBox::question(window, title, QString(
            "Remove trailing JSON commas and save %1 of %2 shader(s) in error?")
            .arg(repairs.size()).arg(errorCount)) != QMessageBox::Yes) return;

    QStringList failures;
    int repaired = 0;
    bool reloadActive = false;
    for (auto it = repairs.cbegin(); it != repairs.cend(); ++it) {
        const auto &path = it.key();
        const auto disk = ShaderEntry::read(path);
        if (!disk || QFileInfo(path).isSymLink() || disk->source != bin->shader(path).source) {
            failures << path + ": The original file is no longer available or has changed.";
            continue;
        }
        auto source = disk->source;
        for (auto comma = it.value().crbegin(); comma != it.value().crend(); ++comma)
            source.remove(*comma, 1);
        const auto header = ISFScanner::header(source);
        if (!header) {
            failures << path + ": The repaired header did not validate.";
            continue;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(source) != source.size() || !file.commit()) {
            failures << path + ": " + file.errorString();
            continue;
        }
        bin->setSource(path, source);
        bin->clearPreviewError(path);
        reloadActive |= host->activePath() == path;
        ++repaired;
    }
    if (reloadActive) host->selectShader(host->activePath());
    bin->refresh();

    QMessageBox report(window);
    report.setWindowTitle(title);
    report.setText(QString("%1 repaired · %2 skipped · %3 failed")
        .arg(repaired).arg(errorCount - repairs.size()).arg(failures.size()));
    if (!failures.isEmpty()) report.setDetailedText(failures.join('\n'));
    report.exec();
}

void LibraryActions::moveErrors() {
    if (!host->resolveChanges()) return;
    QStringList selected;
    for (const auto &entry : bin->shaders())
        if (bin->binType(entry) == ISFType::Unknown && bin->matches(entry)) selected << entry.path;
    if (selected.isEmpty()) {
        QMessageBox::information(window, "Move ERR to Folder", "No shaders in error match the current filters.");
        return;
    }
    const auto destination = QFileDialog::getExistingDirectory(
        window, QString("Move %1 shader(s) in error to…").arg(selected.size()), host->browseFolder());
    if (destination.isEmpty()) return;
    const auto destinationPath = QFileInfo(destination).canonicalFilePath();
    if (destinationPath.isEmpty() || !QFileInfo(destinationPath).isDir()) {
        QMessageBox::warning(window, "Move ERR to Folder", "The destination folder is no longer available.");
        return;
    }
    const QDir target(destinationPath);
    QStringList failures;
    int moved = 0;
    QStringList movedPaths;
    for (const auto &path : selected) {
        const QFileInfo source(path);
        if (source.absolutePath() == target.absolutePath()) continue;
        const auto current = ShaderEntry::read(path);
        if (!current || current->source != bin->shader(path).source) {
            failures << path + ": The file changed or could not be read.";
            continue;
        }
        QString output = target.filePath(source.fileName());
        for (int suffix = 2; QFileInfo::exists(output) || QFileInfo(output).isSymLink(); ++suffix)
            output = target.filePath(QString("%1_%2.%3").arg(source.completeBaseName()).arg(suffix).arg(source.suffix()));
        QFile file(path);
        if (!file.rename(output)) {
            failures << path + ": " + file.errorString();
            continue;
        }
        ++moved;
        movedPaths << path;
    }
    if (movedPaths.contains(host->activePath())) host->clearSelection();
    bin->forget(movedPaths);
    QMessageBox result(failures.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
                       "Move ERR to Folder", QString("%1 shader(s) moved. %2 error(s).").arg(moved).arg(failures.size()),
                       QMessageBox::Ok, window);
    result.setDetailedText(failures.join('\n'));
    result.exec();
}

void LibraryActions::organize() {
    if (!host->resolveChanges()) return;
    const auto source = QFileDialog::getExistingDirectory(window, "Choose a folder to organize", host->browseFolder());
    if (source.isEmpty()) return;
    QMessageBox mode(window);
    mode.setWindowTitle("Organize a folder");
    mode.setText("Sort shaders into Generators, FX and Transitions.");
    mode.setInformativeText("Copy keeps the originals. Move sends each original shader to the Trash only after its organized copy and dependencies have been written.");
    auto *copy = mode.addButton("Copy shaders", QMessageBox::AcceptRole);
    auto *move = mode.addButton("Move shaders", QMessageBox::ActionRole);
    mode.addButton(QMessageBox::Cancel);
    mode.setDefaultButton(copy);
    mode.exec();
    const bool moveOriginals = mode.clickedButton() == move;
    if (mode.clickedButton() != copy && !moveOriginals) return;
    QList<ShaderEntry> selected;
    QStringList unreadable;
    QHash<QString, QByteArray> sources;
    for (const auto &file : shaderFiles(source, true)) {
        const auto path = QFileInfo(file).canonicalFilePath();
        const auto entry = ShaderEntry::read(path);
        if (entry) {
            selected << *entry;
            if (moveOriginals) sources.insert(path, entry->source);
        } else unreadable << "Cannot read: " + file;
    }
    auto result = exportShaders(selected, source);
    result.errors << unreadable;
    if (moveOriginals) {
        QString selectedCopy;
        QStringList importedCopies, trashed;
        for (auto it = result.copiedFiles.cbegin(); it != result.copiedFiles.cend(); ++it) {
            const auto &path = it.key();
            const auto original = ShaderEntry::read(path);
            if (!original || original->source != sources.value(path) || !QFile::moveToTrash(path)) {
                result.errors << "Original kept: " + path;
                continue;
            }
            ++result.moved;
            if (bin->contains(path)) {
                trashed << path;
                importedCopies << it.value();
            }
            if (host->activePath() == path) selectedCopy = QFileInfo(it.value()).canonicalFilePath();
        }
        const bool activeMoved = trashed.contains(host->activePath());
        bin->forget(trashed);
        if (!importedCopies.isEmpty()) bin->addFiles(importedCopies, source);
        if (!selectedCopy.isEmpty() && bin->contains(selectedCopy)) host->selectShader(selectedCopy);
        else if (activeMoved) host->clearSelection();
    }
    report(result, source);
}

void LibraryActions::report(const ExportReport &result, const QString &destination, bool zip) {
    QMessageBox message(window);
    message.setWindowTitle("ISFLab export");
    message.setTextFormat(Qt::PlainText);
    message.setIcon(result.errors.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
    auto summary = QString("%1 shader(s) %5 to Generators, FX and Transitions in:\n%2\n\n"
                           "%3 unsupported or invalid file(s) skipped. %4 error(s).")
                   .arg(result.copied).arg(QDir::toNativeSeparators(destination)).arg(result.skipped)
                   .arg(result.errors.size()).arg(zip ? "archived" : "copied");
    if (result.moved) summary += QString("\n%1 original shader(s) moved to the Trash.").arg(result.moved);
    message.setText(summary);
    if (!result.errors.isEmpty()) message.setDetailedText(result.errors.join("\n"));
    message.exec();
}
