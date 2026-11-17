// The Media Bin's filters: what the list shows for each state of the type tabs, the categories, the name filter and the
// flat mode, and that the filter state and the list never disagree. Runs offscreen.
#include "MediaBin.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSettings>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <algorithm>
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char *why) {
    if (!ok) { ++failures; std::printf("FAIL mediabin: %s\n", why); }
}
void write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) std::abort();
}
QStringList shown(QTreeWidget *tree) {
    QStringList names;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if (!(*it)->isHidden() && !(*it)->data(0, Qt::UserRole).toString().isEmpty()) names << (*it)->text(0);
    names.sort();
    return names;
}
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setOrganizationName("ISFLabTest");
    QApplication::setApplicationName("MediaBinTest");
    QTemporaryDir dir;
    const QString d = QFileInfo(dir.path()).canonicalFilePath(); // paths in the bin are canonical (/private/var on macOS)
    write(d + "/a/WaveGen.fs", R"(/*{"CATEGORIES":["Noise","Generator"],"INPUTS":[]}*/ void main() {})");
    write(d + "/a/wavefx.fs", R"(/*{"CATEGORIES":["Noise"],"INPUTS":[{"NAME":"inputImage","TYPE":"image"}]}*/ void main() {})");
    write(d + "/a/Plain.fs", R"(/*{"INPUTS":[]}*/ void main() {})");
    write(d + "/b/Blur.fs", R"(/*{"CATEGORIES":["noise","Blur"],"INPUTS":[{"NAME":"inputImage","TYPE":"image"}]}*/ void main() {})");
    write(d + "/b/Plain.fs", R"(/*{"CATEGORIES":["Blur"],"INPUTS":[]}*/ void main() {})");
    write(d + "/b/NoHeader.fs", "void main() {}");

    MediaBin bin;
    bin.resize(400, 700);
    bin.show();
    bin.addFiles(shaderFiles(d), d);
    auto *tree = bin.findChild<QTreeWidget *>();
    auto *search = bin.findChild<QLineEdit *>();
    QPushButton *generators = nullptr, *filters = nullptr;
    for (auto *b : bin.findChildren<QPushButton *>()) {
        if (b->objectName() == "generatorType") generators = b;
        if (b->objectName() == "effectType") filters = b;
    }
    QCheckBox *flat = nullptr;
    QMap<QString, QCheckBox *> categories;
    for (auto *c : bin.findChildren<QCheckBox *>()) {
        if (c->text().startsWith("Don")) flat = c;
        else categories.insert(c->text().toCaseFolded(), c);
    }
    check(tree && search && generators && filters && flat, "the widgets are there");
    check(categories.size() == 3 + 1, "categories are merged without regard to case, with Uncategorized: Noise, Generator, Blur, (Uncategorized)");

    const QStringList all{"Blur.fs", "NoHeader.fs", "Plain.fs", "Plain.fs", "WaveGen.fs", "wavefx.fs"};
    check(shown(tree) == all, "every shader is shown at first");

    search->setText("WAVE");
    check(shown(tree) == QStringList({"WaveGen.fs", "wavefx.fs"}), "the name filter ignores case and applies as it is typed");
    check(bin.matches(bin.shader(d + "/a/wavefx.fs")) && !bin.matches(bin.shader(d + "/a/Plain.fs")),
          "matches() reads the filter the list shows");
    search->setText("");
    check(shown(tree) == all, "an empty filter shows everything again");

    generators->setChecked(false);
    check(shown(tree) == QStringList({"Blur.fs", "NoHeader.fs", "wavefx.fs"}), "a type tab off hides that type (the Non-ISF file stays)");
    generators->setChecked(true);

    categories.value("noise")->setChecked(false);
    check(shown(tree) == QStringList({"Blur.fs", "NoHeader.fs", "Plain.fs", "Plain.fs", "WaveGen.fs"}),
          "a category off hides the shaders that have only it (wavefx); WaveGen and Blur have another one and stay");
    categories.value("noise")->setChecked(true);

    flat->setChecked(true);
    search->setText("plain");
    check(shown(tree) == QStringList({"Plain.fs", "Plain.fs"}), "flat mode with a name filter");
    search->setText("");
    flat->setChecked(false);
    check(shown(tree) == all, "back to the hierarchy");

    // A preview error moves a shader to ERR, and out of its type
    bin.setPreviewError(d + "/a/WaveGen.fs", "Fragment shader failed");
    check(shown(tree).contains("WaveGen.fs"), "an ERR shader shows while ERR is on");
    generators->setChecked(false);
    check(shown(tree).contains("WaveGen.fs") && !shown(tree).contains("Plain.fs"), "an errored generator is no longer a generator");
    generators->setChecked(true);
    check(bin.clearPreviewError(d + "/a/WaveGen.fs"), "the error goes");

    // Rewriting a shader changes its categories (the cache follows)
    bin.setSource(d + "/a/Plain.fs", R"(/*{"CATEGORIES":["Brand New"],"INPUTS":[]}*/ void main() {})");
    bin.refresh();
    const auto boxes = bin.findChildren<QCheckBox *>();
    check(std::any_of(boxes.begin(), boxes.end(), [](QCheckBox *c) { return c->text() == "Brand New"; }),
          "a new category appears after a shader's source changes");

    // A shader with a preview error whose source changes (a repair, a save), then a filter: nothing the filter walks may be
    // stale. Meant for an AddressSanitizer build; it also checks the outcome
    search->setText("blur"); // the filter reads the cached names and categories of every row
    bin.setPreviewError(d + "/b/Blur.fs", "Fragment shader failed");
    bin.setSource(d + "/b/Blur.fs", R"(/*{"CATEGORIES":["Brand New"],"INPUTS":[]}*/ void main() {})");
    bin.clearPreviewError(d + "/b/Blur.fs"); // runs the filter before anyone called refresh()
    generators->setChecked(false);
    check(!shown(tree).contains("Blur.fs"), "a repaired shader is a generator now: a type tab off hides it");
    generators->setChecked(true);
    check(shown(tree) == QStringList({"Blur.fs"}), "and it still matches the name filter");
    search->setText("");
    {
        auto snapshot = bin.shaders(); // a copy of the list that outlives a change to it, as in the file operations
        bin.setSource(d + "/a/Plain.fs", R"(/*{"CATEGORIES":["Another"],"INPUTS":[]}*/ void main() {})");
        snapshot.clear();
        generators->setChecked(false);
        generators->setChecked(true);
    }
    bin.refresh();

    // Forgetting shaders
    bin.forget({d + "/b/NoHeader.fs"});
    check(shown(tree).size() == 5, "a forgotten shader leaves the list");
    // The saved session loads in the background
    {
        const QStringList paths = shaderFiles(d);
        const QString missing = d + "/gone/Missing.fs";
        QSettings saved(d + "/saved.ini", QSettings::IniFormat);
        QVariantMap entries;
        for (const auto &path : paths) entries.insert(QFileInfo(path).canonicalFilePath(), d);
        entries.insert(missing, d);
        saved.setValue("mediaBin/entries", entries);
        const auto spin = [&](MediaBin &b) {
            QElapsedTimer t;
            t.start();
            while (b.isLoading() && t.elapsed() < 10000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        };
        {
            MediaBin loaded;
            loaded.show();
            loaded.restoreSession(saved);
            check(loaded.isLoading() && loaded.shaders().isEmpty(), "restoring returns at once: the shaders come in afterwards");
            const QString wanted = QFileInfo(d + "/b/Blur.fs").canonicalFilePath();
            loaded.requestSelection(wanted); // before it has been read
            check(loaded.pendingSelectionPath() == wanted, "the selection asked for waits for its shader (and is saved if we quit)");
            QSettings midway(d + "/midway.ini", QSettings::IniFormat);
            loaded.saveSession(midway);
            check(midway.value("mediaBin/entries").toMap().size() == int(paths.size()) + 1,
                  "quitting mid-load keeps every saved shader, read or not");
            spin(loaded);
            check(!loaded.isLoading() && loaded.shaders().size() == paths.size(), "every shader that exists is loaded");
            auto *view = loaded.findChild<QTreeWidget *>();
            check(view->currentItem() && view->currentItem()->data(0, Qt::UserRole).toString() == wanted,
                  "the shader asked for before it arrived is selected when it has");
            QSettings after(d + "/after.ini", QSettings::IniFormat);
            loaded.saveSession(after);
            check(after.value("mediaBin/entries").toMap().size() == int(paths.size()), "a file that is gone is not kept");
        }
        {
            MediaBin cleared;
            cleared.restoreSession(saved);
            cleared.clear();
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 200) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            check(!cleared.isLoading() && cleared.shaders().isEmpty(), "clearing mid-load drops what was still being read");
        }
        {
            MediaBin early; // destroyed while the worker reads: no crash, no wait for the whole list
            early.restoreSession(saved);
        }
    }
    if (failures == 0) std::printf("ALL OK\n");
    return failures ? 1 : 0;
}
