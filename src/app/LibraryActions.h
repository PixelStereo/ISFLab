#pragma once
#include "Library.h"
#include "MediaBin.h"
#include <QObject>

// What the file operations need from the window that hosts the editor.
class LibraryHost {
public:
    virtual ~LibraryHost() = default;
    // Offers to save unsaved edits; false when the user cancels.
    virtual bool resolveChanges() = 0;
    virtual bool hasUnsavedChanges() const = 0;
    virtual QString activePath() const = 0;
    virtual void selectShader(const QString &path) = 0;
    virtual void clearSelection() = 0;
    // Folder that file dialogs start in.
    virtual QString browseFolder() const = 0;
};

// File operations on Media Bin shaders: export, organize, copy, move, trash, repair and duplicate removal.
// Each one checks that files did not change on disk since they were imported before it touches them.
class LibraryActions final : public QObject {
    Q_OBJECT
public:
    LibraryActions(QWidget *window, MediaBin *bin, LibraryHost *host);

    void exportFiltered(bool zip = false);
    void organize();
    void moveErrors();
    void repairErrors(const QStringList &paths);
    void findDuplicates();
    void actOnShaders(const QStringList &paths, MediaBin::FileAction action);

private:
    void report(const ExportReport &result, const QString &destination, bool zip = false);

    QWidget *window;
    MediaBin *bin;
    LibraryHost *host;
};
