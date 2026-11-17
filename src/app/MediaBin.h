#pragma once
#include "Library.h"
#include <QMap>
#include <QHash>
#include <QSet>
#include <QWidget>
#include <atomic>
#include <memory>
#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSettings;
class QThread;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

// The Media Bin: the imported shaders, their type and category filters, and the shader list.
// It owns the entries and reports what the user asks for; the window decides what happens.
class MediaBin final : public QWidget {
    Q_OBJECT
public:
    enum class FileAction { Copy, Move, Trash };

    explicit MediaBin(QWidget *parent = nullptr);
    ~MediaBin() override;
    // The saved shaders are being read in the background (restoreSession): the list fills in as they come
    bool isLoading() const { return loading; }

    const QMap<QString, ShaderEntry> &shaders() const { return entries; }
    bool contains(const QString &path) const { return entries.contains(path); }
    ShaderEntry shader(const QString &path) const { return entries.value(path); }
    void setSource(const QString &path, const QByteArray &source);

    void addFiles(const QStringList &files, const QString &group = {});
    void forget(const QStringList &paths);
    void clear();

    // Type shown in the bin: a shader whose preview failed counts as an error.
    ISFType binType(const ShaderEntry &entry) const;
    bool matches(const ShaderEntry &entry) const;
    QStringList filteredErrors() const;

    void setActive(const QString &path);
    void revertCurrent();
    void selectFirstVisible();
    void requestSelection(const QString &path);
    void setPreviewError(const QString &path, const QString &status);
    bool clearPreviewError(const QString &path);
    void clearPreviewErrors() { previewErrors.clear(); }
    // Results of a batch check: shaders in errors move to ERR, those in passed leave it.
    void applyCheckResults(const QMap<QString, QString> &errors, const QStringList &passed);

    void restoreSession(const QSettings &settings);
    void saveSession(QSettings &settings) const;

public slots:
    // Rebuilds the list: rows, their data and the categories, then applies the filters. For what changes the shaders;
    // the filters alone only need applyFilter()
    void refresh();
    // Shows and hides rows by the type tabs, the categories and the name filter, and updates the counts
    void applyFilter();

signals:
    void activationRequested(const QString &path);
    void repairRequested(const QStringList &paths);
    void fileActionRequested(const QStringList &paths, MediaBin::FileAction action);
    void duplicatesRequested();
    void checkRequested(const QStringList &paths);
    void clearRequested();
    void message(const QString &text);

private:
    void showContextMenu(const QPoint &position);
    // What the filters read of a shader, worked out once (not on every keystroke)
    struct Cached {
        QString foldedName;
        QStringList categoryKeys; // case folded; empty: uncategorized
        QString tooltip;
    };
    const Cached &cached(const ShaderEntry &entry) const;
    void updateRow(const QString &path); // after a preview error came or went: its kind, badge and tooltip only

    // What applyFilter walks, built by refresh(): every shader with its row and cache, so that a pass over thousands of
    // rows does no lookup by path (they share long prefixes: comparing them is what costs)
    struct RowRef {
        const ShaderEntry *entry;
        QTreeWidgetItem *item;
        const Cached *cached;
        int group; // index into groupKeys
    };
    bool passes(const ShaderEntry &entry, ISFType kind, const Cached &c) const;
    std::vector<RowRef> refs;
    QStringList groupKeys;

    mutable QHash<QString, Cached> cache;
    bool categoriesDirty = true; // the shaders changed: the category list is built again
    QString appliedQuery; // the name filter, trimmed and case folded

    // Background loading of the saved session: only the reading and parsing run on the worker; the list is the GUI
    // thread's. Shaders not read yet stay in pendingRestore, which saveSession() keeps, so quitting mid-load does not
    // shrink the bin.
    void startLoading();
    void cancelLoading();
    void takeBatch(int generation, std::vector<ShaderEntry> batch, const QStringList &missing);
    void finishLoading(int generation);
    QMap<QString, QString> pendingRestore; // path -> folder row, not read yet
    QString pendingSelection;               // the shader to select when it arrives
    std::unique_ptr<QThread> loader;
    std::shared_ptr<std::atomic<bool>> loaderCancel;
    int loadGeneration = 0;
    bool loading = false;

    QMap<QString, ShaderEntry> entries;
    QMap<QString, QString> entryGroups; // shader path -> folder row it is listed under
    QMap<QString, QString> previewErrors;
    QString active;
    QTreeWidget *tree;
    QMap<QString, QTreeWidgetItem *> rows, groups;
    bool flatBin = false;
    QMap<ISFType, QPushButton *> typeTabs;
    QMap<ISFType, QLabel *> typeCounts;
    QPushButton *allTypes;
    QVBoxLayout *categoryItems;
    QMap<QString, QCheckBox *> categoryChecks;
    QToolButton *categoryButton;
    QSet<QString> uncheckedCategories;
    QStringList categoryNames;
    QLineEdit *search, *categorySearch;
    QLabel *count;
};
