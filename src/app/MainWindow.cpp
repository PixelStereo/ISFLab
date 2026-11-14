#include "MainWindow.h"
#include "AppStyle.h"
#include "DiagnosticsPanel.h"
#include "FindBar.h"
#include "HeaderInspector.h"
#include "MediaBin.h"
#include "ParameterPanel.h"
#include "PreviewPanel.h"
#include "SettingsKeys.h"
#include "ShaderDocument.h"
#include "ShaderPreview.h"
#include "SourceEditor.h"
#include "UiHelpers.h"
#include <QAudioDevice>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QMediaDevices>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

namespace {
constexpr int statusTimeout = 5000;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    resize(1280, 820);
    setWindowTitle("ISFLab — Pixel Stereo");
    setAcceptDrops(true);

    bin = new MediaBin;
    actions = new LibraryActions(this, bin, this);
    libraryDock = new QDockWidget("Library", this);
    libraryDock->setWidget(bin);
    addDockWidget(Qt::LeftDockWidgetArea, libraryDock);

    previewPanel = new PreviewPanel;
    preview = previewPanel->shaderPreview();
    setCentralWidget(previewPanel);

    parameters = new ParameterPanel(preview, previewPanel->transitionAnimationButton());
    parameters->browseFolder = [this] { return folder; };
    auto *scroll = new QScrollArea;
    scroll->setWidget(parameters);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(220);
    paramsDock = new QDockWidget("Preview Parameters", this);
    paramsDock->setWidget(scroll);
    splitDockWidget(libraryDock, paramsDock, Qt::Horizontal);

    editorDock = new QDockWidget("ISF Editor", this);
    editorDock->setWidget(createEditorArea());
    addDockWidget(Qt::BottomDockWidgetArea, editorDock);

    createMenusAndToolbar();
    connectPanels();
    parameters->rebuild({}, {});
    restoreSession();
}

QWidget *MainWindow::createEditorArea() {
    auto *codePanel = new QWidget;
    auto *codeLayout = new QVBoxLayout(codePanel);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    sourceTabs = new QTabWidget;
    codeLayout->addWidget(sourceTabs, 1);
    editor = new SourceEditor;
    new SourceHighlighter(editor->document());
    editor->setReadOnly(true);
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setPlaceholderText("Import a shader to edit its GLSL source.");
    vertexEditor = new SourceEditor;
    new SourceHighlighter(vertexEditor->document());
    vertexEditor->setFont(editor->font());
    sourceTabs->addTab(editor, "Fragment (.fs)");
    sourceTabs->addTab(vertexEditor, "Vertex (.vs)");
    sourceTabs->setTabVisible(1, false);
    document = new ShaderDocument(editor, vertexEditor, this);

    auto *findBar = new FindBar([this] { return qobject_cast<QPlainTextEdit *>(sourceTabs->currentWidget()); },
                                codePanel, codePanel);
    codeLayout->addWidget(findBar);
    connect(sourceTabs, &QTabWidget::currentChanged, findBar, &FindBar::editorChanged);

    inspector = new HeaderInspector(document);
    diagnostics = new DiagnosticsPanel(document);
    const int diagnosticTab = inspector->addTab(diagnostics, "Diagnostics");
    connect(diagnostics, &DiagnosticsPanel::summaryChanged, this, [this, diagnosticTab](const QString &caption, const QColor &color) {
        inspector->setTabText(diagnosticTab, caption);
        inspector->tabBar()->setTabTextColor(diagnosticTab, color);
    });
    connect(diagnostics, &DiagnosticsPanel::attentionRequested, this, [this] {
        editorDock->show();
        inspector->setCurrentWidget(diagnostics);
    });

    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    split->addWidget(codePanel);
    split->addWidget(inspector);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 1);
    QTimer::singleShot(0, split, [split] {
        split->setSizes({qRound(split->width() * 0.61), qRound(split->width() * 0.39)});
    });
    return split;
}

void MainWindow::connectPanels() {
    const auto showMessage = [this](const QString &text) { statusBar()->showMessage(text, statusTimeout); };
    connect(bin, &MediaBin::message, this, showMessage);
    connect(inspector, &HeaderInspector::message, this, showMessage);
    connect(diagnostics, &DiagnosticsPanel::message, this, showMessage);

    connect(bin, &MediaBin::activationRequested, this, [this](const QString &path) {
        if (resolveChanges()) select(path);
        else bin->revertCurrent();
    });
    connect(bin, &MediaBin::repairRequested, actions, &LibraryActions::repairErrors);
    connect(bin, &MediaBin::fileActionRequested, actions, &LibraryActions::actOnShaders);
    connect(bin, &MediaBin::duplicatesRequested, actions, &LibraryActions::findDuplicates);
    connect(bin, &MediaBin::checkRequested, actions, &LibraryActions::checkShaders);
    connect(bin, &MediaBin::clearRequested, this, [this] {
        if (!resolveChanges()) return;
        bin->clear();
        clearSelection();
    });

    connect(preview, &ShaderPreview::status, this, [this](const QString &status, bool compiled) {
        previewPanel->setStatusText(compiled ? status : "Preview error · See Diagnostics");
        if (!document->isOpen() || document->isModified()) return;
        if (compiled) bin->clearPreviewError(document->path());
        else bin->setPreviewError(document->path(), status);
    });
    connect(preview, &ShaderPreview::status, diagnostics, &DiagnosticsPanel::setPreviewStatus);
    connect(preview, &ShaderPreview::glslDiagnostic, diagnostics, &DiagnosticsPanel::setCompilerDiagnostic);
    connect(diagnostics, &DiagnosticsPanel::compilerErrorRequested, this, &MainWindow::showCompilerError);
    connect(diagnostics, &DiagnosticsPanel::headerErrorRequested, this, &MainWindow::showHeaderError);
    connect(parameters, &ParameterPanel::saveDefaultsRequested, this, &MainWindow::savePreviewDefaults);
}

void MainWindow::restoreSession() {
    const QSettings settings;
    folder = settings.value(SettingsKeys::BinLastFolder, folder).toString();
    bin->restoreSession(settings);
    bin->requestSelection(settings.value(SettingsKeys::BinActive).toString());
    if (!restoreGeometry(settings.value(SettingsKeys::WindowGeometry).toByteArray())) {
        if (const auto *display = screen()) {
            const auto available = display->availableGeometry();
            resize(available.width(), qMax(1, available.height() - 36));
            move(available.topLeft());
        }
    }
    QTimer::singleShot(0, this, [this] {
        resizeDocks({libraryDock, paramsDock}, {qRound(width() * 0.20), qRound(width() * 0.19)}, Qt::Horizontal);
        resizeDocks({editorDock}, {qRound(height() * 0.37)}, Qt::Vertical);
    });
}

void MainWindow::createMenusAndToolbar() {
    auto *file = menuBar()->addMenu("&File");
    auto *openFolder = file->addAction("Import folder…", this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, "Import ISF folder", folder);
        if (!path.isEmpty()) importPaths({path});
    });
    auto *openFiles = file->addAction("Import shaders…", QKeySequence::Open, this, [this] {
        importPaths(QFileDialog::getOpenFileNames(this, "Import ISF shaders", folder, ISFScanner::shaderFileFilter()));
    });
    file->addAction("Load examples", this, [this] {
#ifdef Q_OS_MACOS
        const auto path = QDir(QCoreApplication::applicationDirPath()).filePath("../Resources/examples");
#else
        const auto local = QDir(QCoreApplication::applicationDirPath());
        const auto path = local.exists("examples") ? local.filePath("examples") : local.filePath("../examples");
#endif
        if (QFileInfo(path).isDir()) importPaths({path});
        else QMessageBox::information(this, "Load examples", "Install ISFLab to load the bundled examples.");
    });
    file->addAction("Load community shaders…", this, [this] {
        QDir root(QCoreApplication::applicationDirPath());
        for (int depth = 0; depth < 6; ++depth) {
            if (root.exists("examples/community/manifest.json")) {
                importPaths({root.filePath("examples/community")}, false);
                return;
            }
            if (!root.cdUp()) break;
        }
        const auto path = QFileDialog::getExistingDirectory(this, "Open the community shader collection", folder);
        if (!path.isEmpty()) importPaths({path}, false);
    });
    file->addAction("New shader…", QKeySequence::New, this, [this] {
        if (!resolveChanges()) return;
        auto path = QFileDialog::getSaveFileName(this, "Create shader", QDir(folder).filePath("Untitled.fs"), ISFScanner::shaderFileFilter());
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += ".fs";
        QFile output(path);
        const QByteArray example = "/*{\n  \"ISFVSN\": \"2.0\",\n  \"CATEGORIES\": [\"Generator\"],\n"
            "  \"INPUTS\": []\n}*/\n\nvoid main() {\n"
            "    gl_FragColor = vec4(isf_FragNormCoord, 0.5 + 0.5 * sin(TIME), 1.0);\n}\n";
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            QMessageBox::warning(this, "Create shader", "Cannot create the file. Choose a new filename.");
            return;
        }
        if (output.write(example) != example.size() || !output.flush()) {
            output.close(); output.remove();
            QMessageBox::warning(this, "Create shader", "Cannot write the shader.");
            return;
        }
        output.close();
        bin->addFiles({path});
        select(QFileInfo(path).canonicalFilePath());
    });
    file->addAction("Save shader", QKeySequence::Save, this, &MainWindow::save);
    file->addSeparator();
    auto *exportAction = file->addAction("Export filtered shaders…", this, [this] { actions->exportFiltered(); });
    auto *zipAction = file->addAction("Export filtered ZIP…", this, [this] { actions->exportFiltered(true); });
    auto *organizeAction = file->addAction("Organize a folder…", this, [this] { actions->organize(); });
    file->addSeparator();
    auto *preferences = file->addAction("Preferences…", this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle("Preferences");
        auto *layout = new QFormLayout(&dialog);
        auto *input = new QComboBox;
        input->addItem("System default", QByteArray{});
        input->addItem("Disabled", QByteArray("disabled"));
        for (const auto &device : QMediaDevices::audioInputs())
            input->addItem(device.description(), device.id());
        QSettings settings;
        const auto saved = settings.value(SettingsKeys::AudioInputDevice, QByteArray{}).toByteArray();
        if (input->findData(saved) < 0) input->addItem("Previously selected device (unavailable)", saved);
        input->setCurrentIndex(input->findData(saved));
        layout->addRow("Audio input", input);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        const auto id = input->currentData().toByteArray();
        settings.setValue(SettingsKeys::AudioInputDevice, id);
        preview->setAudioDevice(id);
    });
    preferences->setMenuRole(QAction::PreferencesRole);
    preferences->setShortcut(QKeySequence::Preferences);
    file->addAction("Quit", QKeySequence::Quit, this, &QWidget::close);
    const QIcon brandLogo(":/assets/ISFLab-rounded.png");
    auto *about = menuBar()->addMenu("&Help")->addAction("About ISFLab", this, [this, brandLogo] {
        QDialog dialog(this);
        dialog.setWindowTitle("About ISFLab");
        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(30, 24, 30, 20);
        auto *logo = plainLabel();
        logo->setPixmap(brandLogo.pixmap(QSize(112, 112), devicePixelRatioF()));
        layout->addWidget(logo, 0, Qt::AlignHCenter);
        auto *title = plainLabel("ISFLab");
        auto font = title->font();
        font.setPointSize(font.pointSize() + 9);
        font.setBold(true);
        title->setFont(font);
        layout->addWidget(title, 0, Qt::AlignHCenter);
        layout->addWidget(plainLabel("Version " ISFLAB_VERSION), 0, Qt::AlignHCenter);
        layout->addWidget(plainLabel("Published by Pixel Stereo"), 0, Qt::AlignHCenter);
        layout->addSpacing(8);
        layout->addWidget(plainLabel("Browse, preview and edit Interactive Shader Format shaders."),
                          0, Qt::AlignHCenter);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog.exec();
    });
    about->setMenuRole(QAction::AboutRole);

    auto *toolbar = addToolBar("ISFLab");
    toolbar->setObjectName("commandBar");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(17, 17));
    auto *brandIcon = plainLabel();
    brandIcon->setPixmap(brandLogo.pixmap(QSize(24, 24), devicePixelRatioF()));
    toolbar->addWidget(brandIcon);
    auto *brand = plainLabel("ISFLab");
    brand->setObjectName("brand");
    toolbar->addWidget(brand);
    auto *version = plainLabel("v" ISFLAB_VERSION);
    version->setObjectName("version");
    toolbar->addWidget(version);
    toolbar->addSeparator();
    const auto command = [toolbar](QAction *action, const QString &label, AppStyle::Icon icon) {
        auto *button = new QToolButton;
        button->setObjectName("command");
        button->setDefaultAction(action);
        button->setText(label);
        button->setIcon(AppStyle::icon(icon));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toolbar->addWidget(button);
    };
    command(openFiles, "Import files", AppStyle::Icon::File);
    command(openFolder, "Import folder", AppStyle::Icon::Folder);
    toolbar->addSeparator();
    auto *exportMenu = new QMenu(toolbar);
    exportMenu->addAction(exportAction);
    exportMenu->addAction(zipAction);
    exportMenu->addSeparator();
    exportMenu->addAction(organizeAction);
    exportMenu->addAction("Repair Filtered ERR…", this, [this] { actions->repairErrors(bin->filteredErrors()); });
    exportMenu->addAction("Move ERR to Folder…", this, [this] { actions->moveErrors(); });
    auto *exportButton = new QToolButton;
    exportButton->setObjectName("command");
    exportButton->setText("Export");
    exportButton->setIcon(AppStyle::icon(AppStyle::Icon::Export));
    exportButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    exportButton->setPopupMode(QToolButton::InstantPopup);
    exportButton->setMenu(exportMenu);
    toolbar->addWidget(exportButton);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    QStringList paths;
    for (const auto &url : event->mimeData()->urls())
        if (url.isLocalFile()) paths << url.toLocalFile();
    if (!paths.isEmpty()) {
        importPaths(paths);
        event->acceptProposedAction();
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!resolveChanges()) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(SettingsKeys::WindowGeometry, saveGeometry());
    settings.setValue(SettingsKeys::BinLastFolder, folder);
    settings.setValue(SettingsKeys::BinActive, document->path());
    bin->saveSession(settings);
    event->accept();
}

void MainWindow::importPaths(const QStringList &paths, bool autoPreview) {
    QStringList files;
    for (const auto &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            folder = info.absoluteFilePath();
            bin->addFiles(shaderFiles(folder), folder);
        } else if (ISFScanner::isShaderFile(path)) {
            files << path;
        }
    }
    if (!files.isEmpty()) bin->addFiles(files);
    if (autoPreview && !document->isOpen()) bin->selectFirstVisible();
}

void MainWindow::select(const QString &path) {
    if (!bin->contains(path)) return;
    bin->setActive(path);
    const auto vertexPath = ISFScanner::companionVertexPath(path);
    document->setPaths(path, vertexPath);
    const auto vertex = vertexPath.isEmpty() ? std::optional<ShaderEntry>{} : ShaderEntry::read(vertexPath);
    vertexEditor->setPlaceholderText(vertexPath.isEmpty() ? QString{} : "The vertex shader could not be read.");
    vertexBaseline = vertex ? vertex->source : QByteArray{};
    vertexEditor->setPlainText(QString::fromUtf8(vertexBaseline));
    vertexEditor->document()->setModified(false);
    vertexEditor->setReadOnly(!vertex);
    sourceTabs->setTabVisible(1, !vertexPath.isEmpty());
    sourceTabs->setCurrentIndex(0);
    editor->setReadOnly(false);
    editor->setPlainText(QString::fromUtf8(bin->shader(path).source));
    editor->document()->setModified(false);
    applyPreview();
}

bool MainWindow::hasUnsavedChanges() const { return document->isModified(); }
QString MainWindow::activePath() const { return document->path(); }

bool MainWindow::resolveChanges() {
    if (!document->isModified()) return true;
    QMessageBox dialog(QMessageBox::Warning, "Unsaved shader", "Save changes to " + QFileInfo(document->path()).fileName() + "?",
                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    dialog.button(QMessageBox::Discard)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    const auto choice = dialog.exec();
    if (choice == QMessageBox::Save) return save();
    if (choice != QMessageBox::Discard) return false;
    select(document->path());
    return true;
}

bool MainWindow::save() {
    if (!document->isOpen()) return true;
    const auto path = document->path();
    const bool saveFragment = document->isFragmentModified();
    const bool saveVertex = document->isVertexModified();
    const auto source = document->bytes();
    if (saveFragment && !document->header() && !(document->isMaterial() && document->previewHeader())) {
        QMessageBox::warning(this, "Save shader", "The fragment source must begin with a valid shader header.");
        return false;
    }
    const auto unchanged = [&](const QString &file, const QByteArray &baseline) {
        const auto disk = ShaderEntry::read(file);
        if (disk && disk->source == baseline) return true;
        QMessageBox::warning(this, "Save shader", QFileInfo(file).fileName() + " changed outside ISFLab or could not be read.");
        return false;
    };
    if ((saveFragment && !unchanged(path, bin->shader(path).source)) ||
        (saveVertex && !unchanged(document->vertexPath(), vertexBaseline))) return false;
    const auto write = [&](const QString &file, const QByteArray &bytes) {
        QSaveFile output(file);
        if (output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit()) return true;
        QMessageBox::warning(this, "Save shader", QFileInfo(file).fileName() + " · " + output.errorString());
        return false;
    };
    if (saveFragment) {
        if (!write(path, source)) return false;
        bin->setSource(path, source);
        editor->document()->setModified(false);
    }
    if (saveVertex) {
        const auto bytes = vertexEditor->toPlainText().toUtf8();
        if (!write(document->vertexPath(), bytes)) return false;
        vertexBaseline = bytes;
        vertexEditor->document()->setModified(false);
    }
    if (saveFragment || saveVertex) bin->clearPreviewError(path);
    applyPreview();
    bin->refresh();
    return true;
}

void MainWindow::clearSelection() {
    bin->setActive({});
    document->setPaths({}, {});
    document->clearPreviewed();
    vertexBaseline.clear();
    vertexEditor->clear();
    vertexEditor->document()->setModified(false);
    sourceTabs->setCurrentIndex(0);
    sourceTabs->setTabVisible(1, false);
    updateImageTabs({});
    editor->clear();
    editor->document()->setModified(false);
    editor->setReadOnly(true);
    preview->load({}, {}, {});
    parameters->rebuild({}, {});
    previewPanel->setStatusText("Select a shader to preview.");
}

void MainWindow::applyPreview() {
    if (!document->isOpen()) return;
    document->markPreviewed();
    const auto &header = document->previewHeader();
    previewPanel->setStatusText(header ? "Compiling preview…" :
        document->isMaterial() ? "Invalid MadMapper Material header." :
        bin->shader(document->path()).type() == ISFType::NonISF ? "No ISF JSON header." : "Invalid ISF JSON header.");
    const auto metadata = header.value_or(QJsonObject{});
    preview->load(header ? document->bytes() : QByteArray{}, metadata, document->path());
    parameters->rebuild(metadata, document->path());
    updateImageTabs(metadata);
}

void MainWindow::showCompilerError(const QString &file, int line) {
    if (file == document->path()) {
        sourceTabs->setCurrentIndex(0);
        editor->goToLine(line);
        return;
    }
    if (file == document->vertexPath() && !document->isVertexModified()) {
        sourceTabs->setCurrentIndex(1);
        vertexEditor->goToLine(line);
        return;
    }
    // A Material include: show it read-only.
    const auto include = ShaderEntry::read(file);
    if (!include) {
        QMessageBox::warning(this, "Shader Source", "The shader source could not be read.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QFileInfo(file).fileName());
    auto *layout = new QVBoxLayout(&dialog);
    auto *view = new SourceEditor;
    view->setPlainText(QString::fromUtf8(include->source));
    view->setReadOnly(true);
    view->setFont(editor->font());
    layout->addWidget(view);
    dialog.resize(700, 420);
    view->goToLine(line);
    dialog.exec();
}

void MainWindow::showHeaderError(int position) {
    sourceTabs->setCurrentIndex(0);
    QTextCursor cursor(editor->document());
    cursor.setPosition(position);
    editor->setTextCursor(cursor);
    editor->setFocus();
    editor->ensureCursorVisible();
}

void MainWindow::savePreviewDefaults() {
    if (!document->isOpen()) return;
    if (!document->isPreviewCurrent()) {
        statusBar()->showMessage("Save the edited source before saving preview defaults.", 5000);
        return;
    }
    auto header = document->header();
    if (!header) return;
    auto inputs = header->value("INPUTS").toArray();
    bool changed = false;
    for (qsizetype i = 0; i < inputs.size(); ++i) {
        auto input = inputs.at(i).toObject();
        const auto type = input.value("TYPE").toString();
        const auto current = preview->value(input.value("NAME").toString());
        if (!current.isValid()) continue;
        QJsonValue value;
        if (type == "float" || type == "long" || type == "bool") {
            value = QJsonValue::fromVariant(current);
        } else if (type == "point2D" || type == "floatRange") {
            const auto point = current.value<QVector2D>();
            value = QJsonArray{point.x(), point.y()};
        } else if (type == "point3D") {
            const auto point = current.value<QVector3D>();
            value = QJsonArray{point.x(), point.y(), point.z()};
        } else if (type == "color") {
            const auto color = current.value<QVector4D>();
            value = QJsonArray{color.x(), color.y(), color.z(), color.w()};
        } else {
            continue;
        }
        if (input.value("DEFAULT") == value) continue;
        input.insert("DEFAULT", value);
        inputs.replace(i, input);
        changed = true;
    }
    if (changed) {
        header->insert("INPUTS", inputs);
        document->writeHeader(*header);
    }
    if (save()) statusBar()->showMessage("Shader defaults saved.", 4000);
}

void MainWindow::updateImageTabs(const QJsonObject &header) {
    const auto previous = sourceTabs->currentWidget()->property("imagePath").toString();
    while (sourceTabs->count() > 2) {
        auto *page = sourceTabs->widget(2);
        sourceTabs->removeTab(2);
        delete page;
    }

    const auto imported = header.value("IMPORTED").toObject();
    QSet<QString> seen;
    for (auto it = imported.constBegin(); it != imported.constEnd(); ++it) {
        const auto relative = it.value().toObject().value("PATH").toString();
        if (relative.isEmpty() || !QDir::isRelativePath(relative)) continue;
        const auto path = QDir::cleanPath(QFileInfo(document->path()).dir().filePath(relative));
        if (seen.contains(path)) continue;
        seen.insert(path);

        auto *page = new QScrollArea;
        page->setProperty("imagePath", path);
        page->setAlignment(Qt::AlignCenter);
        auto *image = plainLabel("Loading image…");
        image->setAlignment(Qt::AlignCenter);
        page->setWidget(image);
        const auto loadImage = [=, loaded = false](int index) mutable {
            if (loaded || sourceTabs->widget(index) != page) return;
            loaded = true;
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const auto size = reader.size();
            if (size.width() > 2048 || size.height() > 2048)
                reader.setScaledSize(size.scaled(2048, 2048, Qt::KeepAspectRatio));
            const auto pixels = reader.read();
            if (pixels.isNull())
                image->setText(QString("Cannot open image: %1\n%2").arg(relative, reader.errorString()));
            else
                image->setPixmap(QPixmap::fromImage(pixels));
            image->adjustSize();
        };
        connect(sourceTabs, &QTabWidget::currentChanged, page, loadImage);
        const int index = sourceTabs->addTab(page, QFileInfo(path).fileName());
        sourceTabs->setTabToolTip(index, relative);
        if (previous == path) sourceTabs->setCurrentIndex(index);
    }
}
