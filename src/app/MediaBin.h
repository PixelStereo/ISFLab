#pragma once
#include "Library.h"
#include <QMap>
#include <QSet>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSettings;
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

    void restoreSession(const QSettings &settings);
    void saveSession(QSettings &settings) const;

public slots:
    void refresh();

signals:
    void activationRequested(const QString &path);
    void repairRequested(const QStringList &paths);
    void fileActionRequested(const QStringList &paths, MediaBin::FileAction action);
    void duplicatesRequested();
    void clearRequested();
    void message(const QString &text);

private:
    void showContextMenu(const QPoint &position);

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
