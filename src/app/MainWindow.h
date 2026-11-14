#pragma once
#include "LibraryActions.h"
#include <QMainWindow>

class DiagnosticsPanel;
class HeaderInspector;
class LibraryActions;
class MediaBin;
class ParameterPanel;
class PreviewPanel;
class QDockWidget;
class QTabWidget;
class ShaderDocument;
class ShaderPreview;
class SourceEditor;

// Composes the Media Bin, the preview, the parameter panel and the editor, and keeps them on the same
// shader: selecting, saving and closing go through here so that unsaved edits are never lost.
class MainWindow final : public QMainWindow, private LibraryHost {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenusAndToolbar();
    QWidget *createEditorArea();
    void connectPanels();
    void restoreSession();

    void importPaths(const QStringList &paths, bool autoPreview = true);
    void select(const QString &path);
    void applyPreview();
    void updateImageTabs(const QJsonObject &header);
    void savePreviewDefaults();
    bool save();
    void showCompilerError(const QString &file, int line);
    void showHeaderError(int position);

    // LibraryHost
    bool resolveChanges() override;
    bool hasUnsavedChanges() const override;
    QString activePath() const override;
    void selectShader(const QString &path) override { select(path); }
    void clearSelection() override;
    QString browseFolder() const override { return folder; }

    QString folder;
    MediaBin *bin;
    LibraryActions *actions;
    PreviewPanel *previewPanel;
    ShaderPreview *preview;
    ParameterPanel *parameters;
    SourceEditor *editor;
    SourceEditor *vertexEditor;
    ShaderDocument *document;
    QTabWidget *sourceTabs;
    HeaderInspector *inspector;
    DiagnosticsPanel *diagnostics;
    QDockWidget *libraryDock, *paramsDock, *editorDock;
    QByteArray vertexBaseline; // vertex source as last read or saved
};
