#include "MainWindow.h"
#include "AppStyle.h"
#include "ColorPicker.h"
#include "ShaderPreview.h"
#include "SliderField.h"
#include "ZipExport.h"
#include <QAction>
#include <QHeaderView>
#include <QToolBar>
#include <QStyledItemDelegate>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImageReader>
#include <QWidgetAction>
#include <QInputDialog>
#include <QToolButton>
#include <QMenu>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QMessageBox>
#include <QAbstractButton>
#include <QApplication>
#include <QKeySequence>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QPushButton>
#ifdef Q_OS_MACOS
#include <QProcess>
#endif
#include <QSaveFile>
#include <QSettings>
#include <QScreen>
#include <QTimer>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextCursor>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <algorithm>
#include <functional>
#include <limits>

namespace {
class PointPad final : public QWidget {
public:
    explicit PointPad(QWidget *parent) : QWidget(parent) {
        setMinimumHeight(104);
        setCursor(Qt::CrossCursor);
        setAccessibleName("XY position");
        setToolTip("Drag to adjust X and Y.");
    }

    void setPosition(const QPointF &value) {
        position = value;
        update();
    }

    std::function<void(const QPointF &)> positionEdited;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto area = plot();
        painter.setPen(palette().color(QPalette::Mid));
        painter.setBrush(palette().color(QPalette::Base));
        painter.drawRoundedRect(area, 6, 6);
        for (int i = 1; i < 4; ++i) {
            const auto x = area.left() + area.width() * i / 4;
            const auto y = area.top() + area.height() * i / 4;
            painter.drawLine(QPointF(x, area.top() + 3), QPointF(x, area.bottom() - 3));
            painter.drawLine(QPointF(area.left() + 3, y), QPointF(area.right() - 3, y));
        }
        const QPointF handle(area.left() + area.width() * position.x(),
                             area.bottom() - area.height() * position.y());
        painter.setPen(QPen(palette().color(QPalette::Base), 2));
        painter.setBrush(palette().color(QPalette::Highlight));
        painter.drawEllipse(handle, 6, 6);
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) editPosition(event->position());
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (event->buttons() & Qt::LeftButton) editPosition(event->position());
    }

private:
    QRectF plot() const { return QRectF(rect()).adjusted(8, 8, -8, -8); }

    void editPosition(const QPointF &point) {
        const auto area = plot();
        positionEdited({qBound(0., (point.x() - area.left()) / area.width(), 1.),
                        qBound(0., (area.bottom() - point.y()) / area.height(), 1.)});
    }

    QPointF position;
};

class SourceHighlighter final : public QSyntaxHighlighter {
public:
    explicit SourceHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}

protected:
    void highlightBlock(const QString &line) override {
        enum State { BeforeSource, GLSL, Header, Comment };
        int state = qMax(previousBlockState(), int(BeforeSource));
        int offset = 0;
        if (state == BeforeSource) {
            while (offset < line.size() && line.at(offset).isSpace()) ++offset;
            if (offset == line.size()) {
                setCurrentBlockState(BeforeSource);
                return;
            }
            state = GLSL;
            if (line.mid(offset, 2) == "/*") {
                setFormat(offset, 2, QColor("#83918d"));
                offset += 2;
                state = Header;
            }
        }

        static const QRegularExpression json(R"rx("(?:\\.|[^"\\])*"(?=\s*:)|"(?:\\.|[^"\\])*"|-?\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?|\b(?:true|false|null)\b)rx");
        static const QRegularExpression glsl(
            R"rx(//.*|/\*|^\s*#\s*\w+|\b(?:attribute|varying|uniform|const|in|out|inout|precision|highp|mediump|lowp|if|else|for|while|do|switch|case|default|break|continue|return|discard|struct|true|false)\b|\b(?:void|bool|int|uint|float|double|[biud]?vec[234]|mat[234](?:x[234])?|sampler\w+)\b|\b(?:gl_\w+|isf_\w+|IMG_\w+|TIME|TIMEDELTA|DATE|RENDERSIZE|PASSINDEX|FRAMEINDEX)\b|(?:\b0[xX][\da-fA-F]+|\b\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[fFuU]?|\b[A-Za-z_]\w*(?=\s*\())rx");
        while (offset < line.size()) {
            if (state == Header || state == Comment) {
                const int close = line.indexOf("*/", offset);
                const int end = close < 0 ? line.size() : close + 2;
                setFormat(offset, end - offset, QColor("#83918d"));
                if (state == Header) {
                    auto tokens = json.globalMatch(line.mid(offset, (close < 0 ? end : close) - offset));
                    while (tokens.hasNext()) {
                        const auto token = tokens.next();
                        const auto value = token.capturedView();
                        const auto after = line.mid(offset + token.capturedEnd()).trimmed();
                        const QColor color(value.startsWith('"')
                            ? (after.startsWith(':') ? "#86cbd0" : "#b8d895") : "#efbb83");
                        setFormat(offset + token.capturedStart(), token.capturedLength(), color);
                    }
                }
                offset = end;
                if (close >= 0) state = GLSL;
                continue;
            }

            const auto token = glsl.match(line, offset);
            if (!token.hasMatch()) break;
            const auto value = token.capturedView();
            offset = token.capturedEnd();
            QColor color("#c9a8ed");
            if (value.startsWith('/')) {
                color = QColor("#83918d");
                if (value == u"/*") state = Comment;
            } else if (value.trimmed().startsWith('#')) {
                color = QColor("#dfa6ca");
            } else if (value.front().isDigit() || value.startsWith('.')) {
                color = QColor("#efbb83");
            } else if (value.startsWith(u"gl_") || value.startsWith(u"isf_") ||
                       value.front().isUpper() || line.mid(offset).trimmed().startsWith('(')) {
                color = QColor("#86cbd0");
            }
            setFormat(token.capturedStart(), token.capturedLength(), color);
        }
        setCurrentBlockState(state);
    }
};

const QString filter = "ISF shaders (*.fs *.isf *.FS *.ISF)";
std::optional<ShaderEntry> readShader(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const auto source = file.readAll();
    if (file.error() != QFile::NoError) return std::nullopt;
    return ShaderEntry{path, source, ISFScanner::header(source)};
}
QLabel *plainLabel(const QString &text = {}) {
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    return label;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    resize(1280, 820);
    setWindowTitle("ISFLab — Pixel Stereo");
    setAcceptDrops(true);
    auto *file = menuBar()->addMenu("&File");
    auto *openFolder = file->addAction("Import folder…", this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, "Import ISF folder", folder);
        if (!path.isEmpty()) importPaths({path});
    });
    auto *openFiles = file->addAction("Import shaders…", QKeySequence::Open, this, [this] {
        importPaths(QFileDialog::getOpenFileNames(this, "Import ISF shaders", folder, filter));
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
        auto path = QFileDialog::getSaveFileName(this, "Create shader", QDir(folder).filePath("Untitled.fs"), filter);
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
        addFiles({path});
        select(QFileInfo(path).canonicalFilePath());
        refresh();
    });
    file->addAction("Save shader", QKeySequence::Save, this, &MainWindow::save);
    file->addSeparator();
    auto *exportAction = file->addAction("Export filtered shaders…", this, [this] { exportFiltered(); });
    auto *zipAction = file->addAction("Export filtered ZIP…", this, [this] { exportFiltered(true); });
    auto *organizeAction = file->addAction("Organize a folder…", this, &MainWindow::organize);
    file->addSeparator();
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
    exportMenu->addAction("Move ERR to Folder…", this, &MainWindow::moveErrors);
    auto *exportButton = new QToolButton;
    exportButton->setObjectName("command");
    exportButton->setText("Export");
    exportButton->setIcon(AppStyle::icon(AppStyle::Icon::Export));
    exportButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    exportButton->setPopupMode(QToolButton::InstantPopup);
    exportButton->setMenu(exportMenu);
    toolbar->addWidget(exportButton);

    auto *library = new QWidget;
    library->setObjectName("libraryPanel");
    auto *left = new QVBoxLayout(library);
    left->setContentsMargins(10, 12, 10, 9);
    left->setSpacing(8);
    auto *mediaHeader = plainLabel("MEDIA BIN");
    mediaHeader->setObjectName("sectionTitle");
    left->addWidget(mediaHeader);
    search = new QLineEdit;
    search->setPlaceholderText("Filter shaders by name…");
    search->addAction(AppStyle::icon(AppStyle::Icon::Search), QLineEdit::LeadingPosition);
    search->setClearButtonEnabled(true);
    auto *typeRow = new QWidget;
    auto *typeLayout = new QHBoxLayout(typeRow);
    typeLayout->setContentsMargins(0, 0, 0, 0);
    typeLayout->setSpacing(4);
    allTypes = new QPushButton("All");
    allTypes->setObjectName("allTypes");
    allTypes->setProperty("filterTab", true);
    allTypes->setCheckable(true);
    allTypes->setChecked(true);
    typeLayout->addWidget(allTypes, 1);
    connect(allTypes, &QPushButton::clicked, this, [this] {
        for (auto *tab : typeTabs) {
            const QSignalBlocker block(tab);
            tab->setChecked(true);
        }
        refresh();
    });
    for (const auto kind : {ISFType::Generator, ISFType::FX, ISFType::Transition, ISFType::Unknown}) {
        auto *tab = new QPushButton(kind == ISFType::Unknown ? "ERR" : kind == ISFType::FX ? "Filter" : ISFScanner::typeToString(kind));
        tab->setProperty("filterTab", true);
        tab->setObjectName(kind == ISFType::Generator ? "generatorType" : kind == ISFType::FX ? "effectType" : kind == ISFType::Transition ? "transitionType" : "errorType");
        tab->setCheckable(true);
        tab->setChecked(true);
        tab->setToolTip(kind == ISFType::Unknown ? "Invalid headers and errors detected in preview." :
                        "Toggle this shader type. Several types can be active.");
        typeTabs.insert(kind, tab);
        typeLayout->addWidget(tab, 1);
        connect(tab, &QPushButton::toggled, this, &MainWindow::refresh);
    }
    categoryButton = new QToolButton;
    categoryButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    categoryButton->setCheckable(true);
    categoryButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    categoryButton->setArrowType(Qt::RightArrow);
    categoryButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *categoryPanel = new QWidget;
    categoryPanel->setObjectName("categoryPanel");
    categoryPanel->hide();
    auto *categoryLayout = new QVBoxLayout(categoryPanel);
    categorySearch = new QLineEdit;
    categorySearch->setPlaceholderText("Search categories…");
    categorySearch->setClearButtonEnabled(true);
    categoryLayout->addWidget(categorySearch);
    auto *selection = new QHBoxLayout;
    auto *selectAll = new QPushButton("Select all");
    auto *deselectAll = new QPushButton("Deselect all");
    selection->addWidget(selectAll);
    selection->addWidget(deselectAll);
    categoryLayout->addLayout(selection);
    auto *categoryScroll = new QScrollArea;
    auto *categoryRows = new QWidget;
    categoryItems = new QVBoxLayout(categoryRows);
    categoryItems->setAlignment(Qt::AlignTop);
    categoryItems->setContentsMargins(6, 3, 6, 3);
    categoryItems->setSpacing(3);
    categoryScroll->setWidget(categoryRows);
    categoryScroll->setWidgetResizable(true);
    categoryScroll->setMinimumSize(250, 88);
    categoryScroll->setMaximumHeight(150);
    categoryLayout->addWidget(categoryScroll);
    connect(categoryButton, &QToolButton::toggled, this, [=](bool expanded) {
        categoryButton->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        categoryPanel->setVisible(expanded);
        if (expanded) categorySearch->setFocus();
    });
    connect(categorySearch, &QLineEdit::textChanged, this, [this](const QString &query) {
        for (auto *check : categoryChecks)
            check->setVisible(check->text().contains(query.trimmed(), Qt::CaseInsensitive));
    });
    const auto setAll = [this](bool checked) {
        uncheckedCategories.clear();
        for (auto it = categoryChecks.cbegin(); it != categoryChecks.cend(); ++it) {
            const QSignalBlocker blocker(it.value());
            it.value()->setChecked(checked);
            if (!checked) uncheckedCategories.insert(it.key());
        }
        refresh();
    };
    connect(selectAll, &QPushButton::clicked, this, [=] { setAll(true); });
    connect(deselectAll, &QPushButton::clicked, this, [=] { setAll(false); });
    count = plainLabel();
    count->setObjectName("quiet");
    tree = new QTreeWidget;
    tree->setObjectName("shaderTree");
    tree->setColumnCount(2);
    tree->setHeaderHidden(true);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    tree->setColumnWidth(1, 66);
    tree->setRootIsDecorated(true);
    tree->setIndentation(14);
    tree->setMouseTracking(true);
    tree->setItemDelegate(AppStyle::libraryDelegate(tree));
    tree->setSelectionMode(QAbstractItemView::NoSelection);
    tree->setMinimumWidth(315);
    auto *flatToggle = new QCheckBox("Don’t show folder hierarchy");
    flatToggle->setToolTip("Show shaders in a flat list without folder rows.");
    flatBin = QSettings().value("mediaBin/shadersOnly", false).toBool();
    flatToggle->setChecked(flatBin);
    tree->setRootIsDecorated(!flatBin);
    connect(flatToggle, &QCheckBox::toggled, this, [this](bool enabled) {
        flatBin = enabled;
        QSettings().setValue("mediaBin/shadersOnly", enabled);
        tree->setRootIsDecorated(!enabled);
        refresh();
    });
    for (auto *widget : QList<QWidget *>{search, typeRow, categoryButton, categoryPanel}) left->addWidget(widget);
    auto *binOptions = new QHBoxLayout;
    binOptions->addWidget(count, 1);
    auto *binSettings = new QToolButton;
    binSettings->setText(QString::fromUtf8("⚙"));
    binSettings->setAccessibleName("Media Bin settings");
    binSettings->setToolTip("Media Bin settings");
    binSettings->setPopupMode(QToolButton::InstantPopup);
    auto *binMenu = new QMenu(binSettings);
    auto *option = new QWidgetAction(binMenu);
    auto *optionPanel = new QWidget;
    auto *optionLayout = new QVBoxLayout(optionPanel);
    optionLayout->setContentsMargins(12, 8, 12, 8);
    optionLayout->addWidget(flatToggle);
    option->setDefaultWidget(optionPanel);
    binMenu->addAction(option);
    binSettings->setMenu(binMenu);
    binOptions->addWidget(binSettings);
    left->addLayout(binOptions);
    left->addWidget(tree, 1);
    auto *footer = new QWidget;
    footer->setObjectName("libraryFooter");
    auto *stats = new QHBoxLayout(footer);
    stats->setContentsMargins(9, 5, 9, 5);
    for (const auto kind : {ISFType::Generator, ISFType::FX, ISFType::Transition}) {
        auto *label = plainLabel();
        label->setStyleSheet("color:" + AppStyle::typeColor(kind).name() + "; font-size:11px; font-weight:600;");
        typeCounts.insert(kind, label);
        stats->addWidget(label, 1);
    }
    left->addWidget(footer);
    auto *libraryDock = new QDockWidget("Library", this);
    libraryDock->setWidget(library);
    addDockWidget(Qt::LeftDockWidgetArea, libraryDock);

    auto *viewer = new QWidget;
    auto *viewerLayout = new QVBoxLayout(viewer);
    preview = new ShaderPreview;
    preview->setMinimumSize(320, 180);
    auto *animationBar = new QHBoxLayout;
    transitionAnimationButton = new QPushButton("Animate progress");
    transitionAnimationButton->setCheckable(true);
    transitionAnimationButton->setChecked(true);
    transitionAnimationButton->setToolTip("Loop from 0 to 1 and back in two seconds each way, with a one-second hold at each end.");
    connect(transitionAnimationButton, &QPushButton::toggled, this, [this](bool running) {
        if (!transitionAnimationButton->isEnabled()) return;
        preview->setTransitionAnimation(running);
    });
    animationBar->addWidget(transitionAnimationButton);
    animationBar->addStretch();
    viewerLayout->addLayout(animationBar);
    viewerLayout->addWidget(preview, 1);
    auto *resolutionBar = new QHBoxLayout;
    auto *resolution = plainLabel();
    auto *autoResolution = new QPushButton("Auto");
    auto *setResolution = new QPushButton("Set resolution…");
    resolutionBar->addWidget(resolution, 1);
    resolutionBar->addWidget(autoResolution);
    resolutionBar->addWidget(setResolution);
    viewerLayout->addLayout(resolutionBar);
    const auto showResolution = [resolution, autoResolution](const QSize &size) {
        resolution->setText(QString("%1: %2 × %3 px").arg(autoResolution->isEnabled() ? "Fixed" : "Auto").arg(size.width()).arg(size.height()));
    };
    connect(preview, &ShaderPreview::renderSizeChanged, this, showResolution);
    const auto setRenderSize = [this, showResolution, autoResolution](const QSize &size) {
        preview->setFixedRenderSize(size);
        QSettings().setValue("preview/fixedResolution", size);
        autoResolution->setEnabled(!size.isEmpty());
        showResolution(preview->renderSize());
    };
    const QSize defaultResolution(1280, 720);
    const auto storedResolution = QSettings().value("preview/fixedResolution", defaultResolution).toSize();
    setRenderSize(storedResolution.width() <= 8192 && storedResolution.height() <= 8192
                      ? storedResolution : defaultResolution);
    connect(autoResolution, &QPushButton::clicked, this, [setRenderSize] { setRenderSize({}); });
    connect(setResolution, &QPushButton::clicked, this, [this, setRenderSize] {
        QDialog dialog(this);
        dialog.setWindowTitle("Fixed render resolution");
        auto *form = new QFormLayout(&dialog);
        auto *width = new SliderField;
        auto *height = new SliderField;
        for (auto *field : {width, height}) {
            field->setDecimals(0);
            field->setRange(1, 8192);
            field->setSingleStep(1);
            field->setSuffix(" px");
        }
        const auto current = preview->renderSize();
        width->setValue(current.width());
        height->setValue(current.height());
        form->addRow("Width", width);
        form->addRow("Height", height);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) setRenderSize(QSize(int(width->value()), int(height->value())));
    });
    auto *transport = new QHBoxLayout;
    auto *playing = new QPushButton("Pause");
    auto *reset = new QPushButton("Reset");
    auto *timeline = new SliderField;
    timeline->setRange(0, 120);
    timeline->setDecimals(2);
    timeline->setTypedRange(0, 86400);
    timeline->setSingleStep(0.01);
    timeline->setSuffix(" s");
    timeline->setAccessibleName("Preview time");
    transport->addWidget(playing); transport->addWidget(reset); transport->addWidget(timeline, 1);
    viewerLayout->addLayout(transport);
    previewStatus = plainLabel("Select a shader to preview.");
    previewStatus->setWordWrap(true);
    previewStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    viewerLayout->addWidget(previewStatus);
    connect(preview, &ShaderPreview::status, this, [this](const QString &status) {
        previewStatus->setText(status);
        if (active.isEmpty() || editor->document()->isModified() || !entries.value(active).header) return;
        if (status == "Preview compiled") {
            if (previewErrors.remove(active)) refresh();
        } else if (previewErrors.value(active) != status) {
            previewErrors.insert(active, status);
            refresh();
        }
    });
    connect(preview, &ShaderPreview::timeChanged, this, [=](double seconds) {
        if (!timeline->isEditing()) timeline->setValue(seconds);
    });
    connect(timeline, &SliderField::valueEdited, preview, &ShaderPreview::seek);
    connect(reset, &QPushButton::clicked, this, [=] { preview->seek(0); });
    playing->setCheckable(true);
    connect(playing, &QPushButton::toggled, this, [=](bool paused) {
        preview->setPlaying(!paused);
        playing->setText(paused ? "Play" : "Pause");
    });
    setCentralWidget(viewer);

    auto *scroll = new QScrollArea;
    auto *panel = new QWidget;
    parameters = new QVBoxLayout(panel);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(220);
    auto *paramsDock = new QDockWidget("Preview Parameters", this);
    paramsDock->setWidget(scroll);
    splitDockWidget(libraryDock, paramsDock, Qt::Horizontal);

    auto *codePanel = new QWidget;
    auto *codeLayout = new QVBoxLayout(codePanel);
    editor = new QPlainTextEdit;
    new SourceHighlighter(editor->document());
    editor->setReadOnly(true);
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setPlaceholderText("Import a shader to edit its GLSL source.");
    compilePreviewButton = new QPushButton("Compile preview");
    compilePreviewButton->setObjectName("primary");
    compilePreviewButton->setEnabled(false);
    connect(compilePreviewButton, &QPushButton::clicked, this, &MainWindow::applyPreview);
    connect(editor, &QPlainTextEdit::textChanged, this, [this] {
        compilePreviewButton->setEnabled(!active.isEmpty() && editor->toPlainText() != previewedSource);
    });
    codeLayout->addWidget(compilePreviewButton, 0, Qt::AlignLeft);
    codeLayout->addWidget(editor);
    auto *inspector = new QTabWidget;
    auto *metadata = new QWidget;
    auto *metadataLayout = new QFormLayout(metadata);
    for (const auto &item : {std::pair{"DESCRIPTION", "Description"}, {"CREDIT", "Credit"},
                                    {"CATEGORIES", "Categories"}, {"VSN", "Version"}}) {
        auto *field = new QLineEdit;
        const QString key = QString::fromLatin1(item.first);
        metadataLayout->addRow(QString::fromLatin1(item.second), field);
        const bool categories = key == "CATEGORIES";
        const auto displayValue = [key, categories](const QJsonObject &header) {
            return categories ? ISFScanner::categories(header).join(", ") : header.value(key).toString();
        };
        const auto syncField = [this, field, displayValue] {
            const auto header = ISFScanner::header(editor->toPlainText().toUtf8());
            field->setEnabled(header.has_value() && !active.isEmpty());
            field->setText(header ? displayValue(*header) : QString());
        };
        syncField();
        connect(editor, &QPlainTextEdit::textChanged, field, syncField);
        connect(field, &QLineEdit::editingFinished, this, [this, field, key, categories, displayValue] {
            if (!field->isModified()) return;
            const auto source = editor->toPlainText();
            auto header = ISFScanner::header(source.toUtf8());
            if (!header || active.isEmpty() || field->text() == displayValue(*header)) return;
            const auto value = field->text().trimmed();
            if (value.isEmpty()) header->remove(key);
            else if (categories) {
                QJsonArray tags;
                for (const auto &tag : value.split(',')) {
                    const auto name = tag.trimmed();
                    if (!name.isEmpty()) tags.append(name);
                }
                header->insert(key, tags);
            } else {
                header->insert(key, value);
            }
            writeHeader(*header);
        });
    }

    const auto addHeaderTable = [this, inspector](const QString &key, const QStringList &columns,
                                                              QWidget *basicInfo = nullptr) {
        const bool passes = key == "PASSES";
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        if (basicInfo) layout->addWidget(basicInfo);
        auto *tools = new QHBoxLayout;
        auto *add = new QPushButton(passes ? "Add pass" : "Add input");
        auto *remove = new QPushButton("Remove");
        auto *up = new QPushButton("Move up");
        auto *down = new QPushButton("Move down");
        for (auto *button : {add, remove, up, down}) tools->addWidget(button);
        tools->addStretch();
        layout->addLayout(tools);
        auto *table = new QTableWidget(0, columns.size());
        table->setHorizontalHeaderLabels(columns);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        table->horizontalHeader()->setDefaultSectionSize(85);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setMinimumWidth(240);
        layout->addWidget(table, 1);
        const auto readHeader = [this] { return ISFScanner::header(editor->toPlainText().toUtf8()); };
        const auto showValue = [passes](const QJsonValue &value, int column) -> QString {
            if (value.isUndefined()) return {};
            if (value.isString() && (passes || column < 3)) return value.toString();
            const auto json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
            return QString::fromUtf8(json.mid(1, json.size() - 2));
        };
        const auto syncTable = [=] {
            const auto header = readHeader();
            const auto rows = header ? header->value(key).toArray() : QJsonArray{};
            const int selected = table->currentRow();
            const QSignalBlocker blocker(table);
            table->setEnabled(header.has_value());
            add->setEnabled(header.has_value());
            table->setRowCount(rows.size());
            for (int row = 0; row < rows.size(); ++row) {
                const auto object = rows.at(row).toObject();
                const bool finalPass = passes && row == rows.size() - 1 && !object.contains("TARGET");
                table->setVerticalHeaderItem(row, new QTableWidgetItem(finalPass ? "Output" : QString::number(row + 1)));
                for (int column = 0; column < columns.size(); ++column) {
                    auto *cell = table->item(row, column);
                    if (!cell) {
                        cell = new QTableWidgetItem;
                        table->setItem(row, column, cell);
                    }
                    const auto value = object.value(columns.at(column));
                    cell->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
                    if (passes && column >= 3) {
                        cell->setFlags((cell->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
                        cell->setCheckState(value.toBool() || value.toDouble() > 0 ? Qt::Checked : Qt::Unchecked);
                    } else {
                        cell->setText(showValue(value, column));
                    }
                    if (finalPass) cell->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                }
            }
            if (selected >= 0 && selected < rows.size()) table->selectRow(selected);
            const int row = table->currentRow();
            remove->setEnabled(row >= 0 && (!passes || row < rows.size() - 1));
            up->setEnabled(row > 0 && (!passes || row < rows.size() - 1));
            down->setEnabled(row >= 0 && row + 1 < rows.size() - int(passes));
        };
        const auto commit = [=](QJsonObject header, const QJsonArray &rows) {
            if (rows.isEmpty()) header.remove(key);
            else header.insert(key, rows);
            writeHeader(header);
        };
        connect(editor, &QPlainTextEdit::textChanged, page, syncTable);
        connect(table, &QTableWidget::currentCellChanged, page, [=](int row) {
            const int count = table->rowCount();
            remove->setEnabled(row >= 0 && (!passes || row < count - 1));
            up->setEnabled(row > 0 && (!passes || row < count - 1));
            down->setEnabled(row >= 0 && row + 1 < count - int(passes));
        });
        connect(table, &QTableWidget::itemChanged, page, [=](QTableWidgetItem *cell) {
            const auto header = readHeader();
            if (!header) return;
            auto rows = header->value(key).toArray();
            const int row = cell->row(), column = cell->column();
            auto object = rows.at(row).toObject();
            const auto field = columns.at(column);
            const auto input = cell->text().trimmed();
            if (passes && column >= 3) {
                object.insert(field, cell->checkState() == Qt::Checked);
            } else if (input.isEmpty() && (passes ? column > 0 : column > 1)) {
                object.remove(field);
            } else if (passes || column < 3) {
                const bool nameField = column == 0;
                static const QRegularExpression identifier("^[A-Za-z_][A-Za-z_0-9]*$");
                if (nameField && (!identifier.match(input).hasMatch() || (passes && row == rows.size() - 1))) {
                    syncTable();
                    return;
                }
                if (nameField) {
                    for (int other = 0; other < rows.size(); ++other) {
                        if (other != row && rows.at(other).toObject().value(field).toString() == input) {
                            syncTable();
                            return;
                        }
                    }
                }
                if (!passes && column == 1 && !QStringList{"event", "bool", "long", "float", "point2D",
                        "point3D", "color", "image", "audio", "audioFFT"}.contains(input)) {
                    syncTable();
                    return;
                }
                object.insert(field, input);
            } else {
                const auto value = QJsonDocument::fromJson("[" + input.toUtf8() + "]").array();
                if (value.size() != 1) {
                    statusBar()->showMessage("Enter a valid JSON value.", 4000);
                    syncTable();
                    return;
                }
                object.insert(field, value.first());
            }
            rows.replace(row, object);
            commit(*header, rows);
        });
        connect(add, &QPushButton::clicked, page, [=] {
            const auto header = readHeader();
            if (!header) return;
            auto rows = header->value(key).toArray();
            const auto field = columns.first();
            QSet<QString> names;
            for (const auto &row : rows) names.insert(row.toObject().value(field).toString());
            int number = 1;
            QString name;
            do { name = (passes ? "pass" : "input") + QString::number(number++); } while (names.contains(name));
            QJsonObject object{{field, name}};
            if (passes) {
                if (rows.isEmpty() || rows.last().toObject().contains("TARGET")) rows.append(QJsonObject{});
                rows.insert(rows.size() - 1, object);
            } else {
                object.insert("TYPE", "float");
                rows.append(object);
            }
            commit(*header, rows);
            table->selectRow(rows.size() - (passes ? 2 : 1));
        });
        connect(remove, &QPushButton::clicked, page, [=] {
            const auto header = readHeader();
            const int row = table->currentRow();
            if (!header || row < 0) return;
            auto rows = header->value(key).toArray();
            rows.removeAt(row);
            commit(*header, rows);
        });
        const auto move = [=](int offset) {
            const auto header = readHeader();
            const int row = table->currentRow();
            if (!header || row < 0) return;
            auto rows = header->value(key).toArray();
            const auto value = rows.takeAt(row);
            rows.insert(row + offset, value);
            commit(*header, rows);
            table->selectRow(row + offset);
        };
        connect(up, &QPushButton::clicked, page, [=] { move(-1); });
        connect(down, &QPushButton::clicked, page, [=] { move(1); });
        inspector->addTab(page, passes ? "Passes" : "Inputs");
        syncTable();
    };
    addHeaderTable("INPUTS", {"NAME", "TYPE", "LABEL", "DEFAULT", "IDENTITY", "MIN", "MAX", "VALUES", "LABELS"}, metadata);
    addHeaderTable("PASSES", {"TARGET", "WIDTH", "HEIGHT", "PERSISTENT", "FLOAT"});

    auto *editorSplit = new QSplitter(Qt::Horizontal);
    editorSplit->setChildrenCollapsible(false);
    editorSplit->addWidget(codePanel);
    editorSplit->addWidget(inspector);
    editorSplit->setStretchFactor(0, 2);
    editorSplit->setStretchFactor(1, 1);
    auto *editorDock = new QDockWidget("ISF Editor", this);
    editorDock->setWidget(editorSplit);
    addDockWidget(Qt::BottomDockWidgetArea, editorDock);

    const auto activateRow = [this](QTreeWidgetItem *item) {
        if (!item) return;
        const auto path = item->data(0, AppStyle::PathRole).toString();
        if (path.isEmpty() || path == active) return;
        if (resolveChanges()) select(path);
        else {
            const QSignalBlocker blocker(tree);
            tree->setCurrentItem(rows.value(active));
        }
    };
    connect(tree, &QTreeWidget::currentItemChanged, this, activateRow);
    connect(tree, &QTreeWidget::itemClicked, this, activateRow);
#ifdef Q_OS_MACOS
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        const auto *item = tree->itemAt(position);
        if (!item) return;
        const auto path = item->data(0, AppStyle::PathRole).toString();
        if (path.isEmpty()) return;

        QMenu menu(tree);
        auto *reveal = menu.addAction("Show in Finder");
        reveal->setEnabled(QFileInfo(path).isFile());
        if (menu.exec(tree->viewport()->mapToGlobal(position)) == reveal &&
            !QProcess::startDetached("/usr/bin/open", {"-R", QFileInfo(path).absoluteFilePath()}))
            statusBar()->showMessage("Could not show the shader in Finder.", 5000);
    });
#endif
    connect(search, &QLineEdit::textChanged, this, &MainWindow::refresh);
    controls({});
    refresh();
    if (!restoreGeometry(QSettings().value("mainWindow/geometry").toByteArray())) {
        if (const auto *display = screen()) {
            const auto available = display->availableGeometry();
            resize(available.width(), qMax(1, available.height() - 36));
            move(available.topLeft());
        }
    }
    QTimer::singleShot(0, this, [this, libraryDock, paramsDock, editorDock, editorSplit] {
        resizeDocks({libraryDock, paramsDock}, {qRound(width() * 0.20), qRound(width() * 0.19)}, Qt::Horizontal);
        resizeDocks({editorDock}, {qRound(height() * 0.37)}, Qt::Vertical);
        editorSplit->setSizes({qRound(editorSplit->width() * 0.61), qRound(editorSplit->width() * 0.39)});
    });
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    QStringList paths;
    for (const auto &url : event->mimeData()->urls())
        if (url.isLocalFile()) paths << url.toLocalFile();
    if (!paths.isEmpty()) { importPaths(paths); event->acceptProposedAction(); }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!resolveChanges()) {
        event->ignore();
        return;
    }
    QSettings().setValue("mainWindow/geometry", saveGeometry());
    event->accept();
}

void MainWindow::importPaths(const QStringList &paths, bool autoPreview) {
    QStringList files;
    for (const auto &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            folder = info.absoluteFilePath();
            addFiles(shaderFiles(folder), folder);
        } else if (QStringList{"fs", "isf"}.contains(info.suffix(), Qt::CaseInsensitive)) {
            files << path;
        }
    }
    if (!files.isEmpty()) addFiles(files);
    if (autoPreview && active.isEmpty()) {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            auto *item = *it;
            if (!item->isHidden() && !item->data(0, AppStyle::PathRole).toString().isEmpty()) {
                tree->setCurrentItem(item);
                break;
            }
        }
    }
}

void MainWindow::addFiles(const QStringList &files, const QString &group) {
    int added = 0, failed = 0;
    for (const auto &path : files) {
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (canonical.isEmpty()) { ++failed; continue; }
        if (entries.contains(canonical)) continue;
        const auto entry = readShader(canonical);
        if (entry) {
            entries.insert(canonical, *entry);
            QString groupPath = group.isEmpty() ? QFileInfo(canonical).absolutePath() : group;
            if (QDir(groupPath).exists("manifest.json") && QFileInfo(groupPath).fileName() == "community") {
                const auto segments = QDir(groupPath).relativeFilePath(canonical).split('/');
                if (segments.size() >= 3) groupPath = QDir(groupPath).filePath(segments[0] + "/" + segments[1]);
            }
            entryGroups.insert(canonical, groupPath);
            ++added;
        }
        else ++failed;
    }
    refresh();
    statusBar()->showMessage(QString("Imported %1 shader(s); %2 could not be read.").arg(added).arg(failed), 5000);
}

ISFType MainWindow::binType(const ShaderEntry &entry) const {
    return previewErrors.contains(entry.path) ? ISFType::Unknown : entry.type();
}

bool MainWindow::matches(const ShaderEntry &entry) const {
    if (!typeTabs.value(binType(entry))->isChecked()) return false;
    const auto categories = entry.header ? ISFScanner::categories(*entry.header) : QStringList{};
    if (!uncheckedCategories.isEmpty()) {
        bool checked = false;
        if (categories.isEmpty()) checked = !uncheckedCategories.contains(QString());
        for (const auto &tag : categories) checked |= !uncheckedCategories.contains(tag.toCaseFolded());
        if (!checked) return false;
    }
    const auto query = search->text().trimmed();
    return query.isEmpty() || QFileInfo(entry.path).fileName().contains(query, Qt::CaseInsensitive);
}

void MainWindow::refresh() {
    const QSignalBlocker blockTree(tree);
    QStringList categories;
    bool hasUncategorized = false;
    for (const auto &entry : std::as_const(entries)) {
        const auto tags = entry.header ? ISFScanner::categories(*entry.header) : QStringList{};
        hasUncategorized |= tags.isEmpty();
        for (const auto &tag : tags)
            if (!categories.contains(tag, Qt::CaseInsensitive)) categories << tag;
    }
    categories.sort(Qt::CaseInsensitive);
    if (hasUncategorized) categories << QString();
    if (categoryNames != categories) {
        categoryNames = categories;
        const auto query = categorySearch->text().trimmed();
        QSet<QString> current;
        while (auto *item = categoryItems->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        categoryChecks.clear();
        for (const auto &tag : categories) {
            const auto key = tag.toCaseFolded();
            current.insert(key);
            auto *check = new QCheckBox(tag.isEmpty() ? "(Uncategorized)" : tag);
            check->setChecked(!uncheckedCategories.contains(key));
            categoryItems->addWidget(check);
            check->setVisible(check->text().contains(query, Qt::CaseInsensitive));
            categoryChecks.insert(key, check);
            connect(check, &QCheckBox::toggled, this, [this, key](bool enabled) {
                if (enabled) uncheckedCategories.remove(key);
                else uncheckedCategories.insert(key);
                refresh();
            });
        }
        uncheckedCategories.intersect(current);
    }
    categoryButton->setText(uncheckedCategories.isEmpty() ? "Categories: all" :
        QString("Categories: %1 of %2").arg(categories.size() - uncheckedCategories.size()).arg(categories.size()));
    bool all = true;
    for (auto *tab : typeTabs) all &= tab->isChecked();
    allTypes->setChecked(all);
    int visible = 0;
    bool inserted = false;
    QMap<ISFType, int> shownByType, totalByType;
    QMap<QString, int> shownByGroup;
    QHash<QString, int> nameCounts;
    if (flatBin)
        for (const auto &entry : std::as_const(entries))
            ++nameCounts[QFileInfo(entry.path).fileName().toCaseFolded()];
    for (const auto &entry : std::as_const(entries)) {
        auto *item = rows.value(entry.path);
        const auto group = entryGroups.value(entry.path);
        QTreeWidgetItem *parent = nullptr;
        if (!flatBin) {
            parent = groups.value(group);
            if (!parent) {
                parent = new QTreeWidgetItem(tree);
                auto label = QFileInfo(group).fileName();
                auto owner = QFileInfo(group).dir();
                QDir community = owner;
                const bool communityGroup = community.cdUp() && community.dirName() == "community" && community.exists("manifest.json");
                if (communityGroup) label = owner.dirName() + "/" + label;
                parent->setText(0, label == "examples" ? "Demo Shaders" : (label.isEmpty() ? group : label));
                parent->setData(0, AppStyle::GroupRole, true);
                parent->setData(1, AppStyle::GroupRole, true);
                parent->setToolTip(0, group);
                parent->setExpanded(!communityGroup);
                groups.insert(group, parent);
            }
        }
        const QFileInfo file(entry.path);
        if (!item) {
            item = new QTreeWidgetItem;
            item->setText(0, file.fileName());
            item->setData(0, AppStyle::PathRole, entry.path);
            rows.insert(entry.path, item);
        } else if (item->parent() != parent) {
            if (auto *previous = item->parent()) previous->removeChild(item);
            else tree->takeTopLevelItem(tree->indexOfTopLevelItem(item));
        }
        if (!item->treeWidget()) {
            if (parent) parent->addChild(item);
            else tree->addTopLevelItem(item);
            inserted = true;
        }
        QString origin;
        if (flatBin && nameCounts.value(file.fileName().toCaseFolded()) > 1) {
            QDir community = QFileInfo(group).dir();
            if (community.cdUp() && community.dirName() == "community" && community.exists("manifest.json"))
                origin = community.relativeFilePath(file.absolutePath());
            else origin = QDir::toNativeSeparators(file.absolutePath());
        }
        item->setData(0, AppStyle::OriginRole, origin);
        const auto kind = binType(entry);
        item->setData(0, AppStyle::KindRole, int(kind));
        item->setData(1, AppStyle::KindRole, int(kind));
        switch (kind) {
        case ISFType::Generator: item->setText(1, "GEN"); break;
        case ISFType::FX: item->setText(1, "FX"); break;
        case ISFType::Transition: item->setText(1, "TRANS"); break;
        default: item->setText(1, "ERR"); break;
        }
        item->setToolTip(1, previewErrors.value(entry.path, ISFScanner::typeToString(kind)));
        item->setToolTip(0, entry.path + (entry.header ? "\n" + ISFScanner::categories(*entry.header).join(", ") : QString()));
        const bool shown = matches(entry);
        item->setHidden(!shown);
        ++totalByType[kind];
        if (shown) { ++visible; ++shownByType[kind]; ++shownByGroup[group]; }
    }
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const int shown = shownByGroup.value(it.key());
        it.value()->setText(1, QString::number(shown));
        it.value()->setHidden(flatBin || !shown);
    }
    for (auto it = typeCounts.cbegin(); it != typeCounts.cend(); ++it) {
        const auto kind = it.key();
        const int shown = shownByType.value(kind), total = totalByType.value(kind);
        const auto caption = kind == ISFType::Generator ? "Gen" : kind == ISFType::FX ? "FX" : "Trans";
        it.value()->setText(QString("●  %1 %2").arg(shown == total ? QString::number(total) :
                            QString("%1/%2").arg(shown).arg(total)).arg(caption));
        it.value()->setToolTip(QString("%1 visible · %2 total").arg(shown).arg(total));
    }
    if (inserted) tree->sortItems(0, Qt::AscendingOrder);
    if (!active.isEmpty() && rows.contains(active) && !rows.value(active)->isHidden())
        tree->setCurrentItem(rows.value(active));
    count->setText(QString("%1 of %2 shaders").arg(visible).arg(entries.size()));
}

void MainWindow::select(const QString &path) {
    if (!entries.contains(path)) return;
    if (auto *previous = rows.value(active)) {
        previous->setData(0, AppStyle::ActiveRole, false);
        previous->setData(1, AppStyle::ActiveRole, false);
    }
    active = path;
    if (auto *item = rows.value(active)) {
        item->setData(0, AppStyle::ActiveRole, true);
        item->setData(1, AppStyle::ActiveRole, true);
    }
    editor->setReadOnly(false);
    editor->setPlainText(QString::fromUtf8(entries.value(active).source));
    editor->document()->setModified(false);
    applyPreview();
}

bool MainWindow::resolveChanges() {
    if (!editor->document()->isModified()) return true;
    QMessageBox dialog(QMessageBox::Warning, "Unsaved shader", "Save changes to " + QFileInfo(active).fileName() + "?",
                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    dialog.button(QMessageBox::Discard)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    const auto choice = dialog.exec();
    if (choice == QMessageBox::Save) return save();
    if (choice != QMessageBox::Discard) return false;
    editor->setPlainText(QString::fromUtf8(entries.value(active).source));
    editor->document()->setModified(false);
    applyPreview();
    return true;
}

bool MainWindow::save() {
    if (active.isEmpty()) return true;
    if (!editor->document()->isModified()) return true;
    auto &entry = entries[active];
    const auto disk = readShader(active);
    if (!disk || disk->source != entry.source) {
        QMessageBox::warning(this, "Save shader", "The file changed outside ISFLab. Your edits are still in the editor.");
        return false;
    }
    const auto source = editor->toPlainText().toUtf8();
    const auto header = ISFScanner::header(source);
    if (!header) {
        QMessageBox::warning(this, "Save shader", "The source must begin with a valid ISF JSON header.");
        return false;
    }
    QSaveFile file(active);
    if (!file.open(QIODevice::WriteOnly) || file.write(source) != source.size() || !file.commit()) {
        QMessageBox::warning(this, "Save shader", file.errorString());
        return false;
    }
    entry.source = source;
    entry.header = header;
    previewErrors.remove(active);
    editor->document()->setModified(false);
    applyPreview();
    refresh();
    return true;
}

void MainWindow::writeHeader(const QJsonObject &header) {
    const auto source = editor->toPlainText();
    const auto start = source.indexOf("/*");
    const auto end = source.indexOf("*/", start + 2);
    auto json = QString::fromUtf8(QJsonDocument(header).toJson(QJsonDocument::Indented));
    json.replace("*/", "*\\/");
    QTextCursor cursor(editor->document());
    cursor.setPosition(start + 2);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.insertText(json);
}

void MainWindow::savePreviewDefaults() {
    if (active.isEmpty()) return;
    const auto source = editor->toPlainText();
    if (source != previewedSource) {
        statusBar()->showMessage("Compile the edited source before saving preview defaults.", 5000);
        return;
    }
    auto header = ISFScanner::header(source.toUtf8());
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
        } else if (type == "point2D") {
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
        writeHeader(*header);
    }
    if (save()) statusBar()->showMessage("Shader defaults saved.", 4000);
}

void MainWindow::applyPreview() {
    if (active.isEmpty()) return;
    previewedSource = editor->toPlainText();
    compilePreviewButton->setEnabled(false);
    const auto source = previewedSource.toUtf8();
    const auto header = ISFScanner::header(source);
    previewStatus->setText(header ? "Compiling preview…" : "Invalid ISF JSON header.");
    preview->load(header ? source : QByteArray{}, header.value_or(QJsonObject{}), active);
    controls(header.value_or(QJsonObject{}));
}

void MainWindow::controls(const QJsonObject &header) {
    preview->setMouseTracking(false);
    while (auto *item = parameters->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto inputs = header.value("INPUTS").toArray();
    const auto shaderType = ISFScanner::classify(header);
    const bool isTransition = shaderType == ISFType::Transition;
    transitionAnimationButton->setEnabled(isTransition);
    QVBoxLayout *sources = nullptr;
    if (std::any_of(inputs.begin(), inputs.end(), [](const QJsonValue &entry) {
            return entry.toObject().value("TYPE").toString() == "image";
        })) {
        auto *sourcePanel = new QWidget;
        sources = new QVBoxLayout(sourcePanel);
        sources->setContentsMargins(0, 0, 0, 0);
        auto *heading = plainLabel("Preview Sources");
        heading->setObjectName("sectionTitle");
        sources->addWidget(heading);
        parameters->addWidget(sourcePanel);
    }
    if (ISFScanner::classify(header) == ISFType::FX) {
        auto *enabled = new QCheckBox("Enable filter");
        enabled->setChecked(true);
        enabled->setToolTip("Turn off to preview the unfiltered input.");
        connect(enabled, &QCheckBox::toggled, preview, &ShaderPreview::setFilterEnabled);
        parameters->addWidget(enabled);
    }
    const auto at = [](const QJsonArray &array, int index) {
        return index < array.size() ? array.at(index) : QJsonValue();
    };
    const auto description = header.value("DESCRIPTION").toString();
    if (!description.isEmpty()) {
        auto *label = plainLabel(description);
        label->setWordWrap(true);
        parameters->addWidget(label);
    }
    const auto imported = header.value("IMPORTED").toObject();
    if (!imported.isEmpty()) {
        auto *heading = plainLabel(QString("Linked images (%1)").arg(imported.size()));
        heading->setObjectName("sectionTitle");
        parameters->addWidget(heading);
        for (auto it = imported.constBegin(); it != imported.constEnd(); ++it) {
            const auto relative = it.value().toObject().value("PATH").toString();
            const auto path = QFileInfo(active).dir().filePath(relative);
            auto *row = new QWidget;
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 2, 0, 2);
            QImage image;
            QSize dimensions;
            if (!relative.isEmpty() && QDir::isRelativePath(relative) && !active.isEmpty()) {
                QImageReader reader(path);
                dimensions = reader.size();
                if (dimensions.isValid())
                    reader.setScaledSize(dimensions.scaled(56, 56, Qt::KeepAspectRatio));
                image = reader.read();
            }
            if (!image.isNull()) {
                auto *thumbnail = plainLabel();
                thumbnail->setPixmap(QPixmap::fromImage(image.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                layout->addWidget(thumbnail);
            }
            auto *label = plainLabel(it.key() + "\n" + (relative.isEmpty() ? QString("No path") : relative)
                + (image.isNull() ? QString("\nMissing or unreadable") :
                   dimensions.isValid() ? QString("\n%1 × %2 px").arg(dimensions.width()).arg(dimensions.height()) : QString()));
            label->setWordWrap(true);
            label->setToolTip(path);
            layout->addWidget(label, 1);
            parameters->addWidget(row);
        }
    }
    if (inputs.isEmpty()) parameters->addWidget(plainLabel("No input parameters."));
    for (const auto &entry : inputs) {
        const auto input = entry.toObject();
        const auto name = input.value("NAME").toString();
        const auto kind = input.value("TYPE").toString();
        const auto value = input.value("DEFAULT");
        if (name.isEmpty()) continue;
        auto *layout = kind == "image" ? sources : parameters;
        layout->addWidget(plainLabel(input.value("LABEL").toString(name) + " (" + kind + ")"));
        if (kind == "float") {
            auto *spin = new SliderField;
            spin->setDecimals(5);
            const bool transitionProgress = name == "progress" && isTransition;
            const auto initial = transitionProgress && transitionAnimationButton->isChecked() ? 0.0 : value.toDouble();
            spin->setRange(input.value("MIN").toDouble(qMin(0., initial)), input.value("MAX").toDouble(qMax(1., initial)));
            spin->setTypedRange(input.value("MIN").toDouble(qMin(-1e6, initial)),
                                input.value("MAX").toDouble(qMax(1e6, initial)));
            spin->setSingleStep(0.01);
            spin->setValue(initial);
            parameters->addWidget(spin);
            preview->setValue(name, spin->value());
            if (transitionProgress) {
                preview->setTransitionAnimation(transitionAnimationButton->isChecked());
                connect(preview, &ShaderPreview::timeChanged, spin, [=] {
                    if (transitionAnimationButton->isChecked() && !spin->isEditing())
                        spin->setValue(preview->value(name).toDouble());
                });
                connect(spin, &SliderField::valueEdited, this, [=](double v) {
                    transitionAnimationButton->setChecked(false);
                    preview->setValue(name, v);
                });
            } else {
                connect(spin, &SliderField::valueEdited, this, [=](double v) { preview->setValue(name, v); });
            }
        } else if (kind == "bool") {
            auto *box = new QCheckBox("Enabled");
            box->setChecked(value.toBool(value.toInt() != 0));
            parameters->addWidget(box);
            preview->setValue(name, box->isChecked());
            connect(box, &QCheckBox::toggled, this, [=](bool v) { preview->setValue(name, v); });
        } else if (kind == "event") {
            auto *button = new QPushButton("Trigger");
            parameters->addWidget(button);
            connect(button, &QPushButton::clicked, this, [=] { preview->setValue(name, true); });
        } else if (kind == "long") {
            const auto values = input.value("VALUES").toArray(), labels = input.value("LABELS").toArray();
            if (!values.isEmpty()) {
                auto *combo = new QComboBox;
                for (int i = 0; i < values.size(); ++i)
                    combo->addItem((i < labels.size() ? labels.at(i).toString(QString::number(values.at(i).toInt())) : QString::number(values.at(i).toInt())), values.at(i).toInt());
                combo->setCurrentIndex(qMax(0, combo->findData(value.toInt())));
                preview->setValue(name, combo->currentData());
                connect(combo, &QComboBox::currentIndexChanged, this, [=] { preview->setValue(name, combo->currentData()); });
                parameters->addWidget(combo);
            } else {
                auto *spin = new SliderField;
                const int minimum = input.value("MIN").toInt(std::numeric_limits<int>::min());
                const int maximum = input.value("MAX").toInt(std::numeric_limits<int>::max());
                const int initial = value.toInt();
                spin->setDecimals(0);
                spin->setRange(input.contains("MIN") ? minimum : qMin(0., double(initial) - 100.),
                               input.contains("MAX") ? maximum : qMax(100., double(initial) + 100.));
                spin->setTypedRange(minimum, maximum);
                spin->setValue(initial);
                preview->setValue(name, int(spin->value()));
                connect(spin, &SliderField::valueEdited, this, [=](double v) { preview->setValue(name, int(v)); });
                parameters->addWidget(spin);
            }
        } else if (kind == "point2D" || kind == "point3D") {
            auto *row = new QWidget;
            auto *layout = new QFormLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            auto *pad = new PointPad(row);
            layout->addRow(pad);
            auto *x = new SliderField, *y = new SliderField;
            auto *z = kind == "point3D" ? new SliderField : nullptr;
            const auto min = input.value("MIN").toArray(), max = input.value("MAX").toArray(), initial = value.toArray();
            const auto lower = [=](int axis) { return at(min, axis).toDouble(-1); };
            const auto upper = [=](int axis) { return qMax(lower(axis), at(max, axis).toDouble(1)); };
            const auto configureAxis = [&](SliderField *field, int axis, const QString &label) {
                field->setDecimals(4);
                field->setRange(lower(axis), upper(axis));
                field->setTypedRange(at(min, axis).toDouble(-10000), at(max, axis).toDouble(10000));
                field->setSingleStep(0.01);
                field->setValue(at(initial, axis).toDouble());
                layout->addRow(label, field);
            };
            configureAxis(x, 0, "X");
            configureAxis(y, 1, "Y");
            if (z) configureAxis(z, 2, "Z");
            const auto updatePoint = [=] {
                const auto normalize = [=](int axis, double coordinate) {
                    const double span = upper(axis) - lower(axis);
                    return span > 0 ? qBound(0., (coordinate - lower(axis)) / span, 1.) : 0.5;
                };
                pad->setPosition({normalize(0, x->value()), normalize(1, y->value())});
                const auto point = z
                    ? QVariant::fromValue(QVector3D(x->value(), y->value(), z->value()))
                    : QVariant::fromValue(QVector2D(x->value(), y->value()));
                preview->setValue(name, point);
            };
            updatePoint();
            pad->positionEdited = [=](const QPointF &point) {
                x->setValue(lower(0) + point.x() * (upper(0) - lower(0)));
                y->setValue(lower(1) + point.y() * (upper(1) - lower(1)));
                updatePoint();
            };
            connect(x, &SliderField::valueEdited, row, updatePoint);
            connect(y, &SliderField::valueEdited, row, updatePoint);
            if (z) connect(z, &SliderField::valueEdited, row, updatePoint);
            if (!z && name.compare("mouse", Qt::CaseInsensitive) == 0) {
                auto *trackMouse = new QCheckBox("Use mouse in preview");
                trackMouse->setToolTip("Drive this input by moving the pointer over the rendered image.");
                trackMouse->setChecked(QSettings().value("preview/trackMouse", false).toBool());
                preview->setMouseTracking(trackMouse->isChecked());
                connect(trackMouse, &QCheckBox::toggled, row, [this](bool enabled) {
                    QSettings().setValue("preview/trackMouse", enabled);
                    preview->setMouseTracking(enabled);
                });
                connect(preview, &ShaderPreview::renderPointerMoved, row,
                        [=](const QPointF &normalized, const QPointF &pixels) {
                    if (!trackMouse->isChecked()) return;
                    const bool hasRange = min.size() >= 2 && max.size() >= 2;
                    const auto coordinate = [&](int axis, double unit, double pixel) {
                        if (!hasRange) return pixel;
                        const double lower = min.at(axis).toDouble();
                        return lower + unit * (max.at(axis).toDouble() - lower);
                    };
                    x->setValue(coordinate(0, normalized.x(), pixels.x()));
                    y->setValue(coordinate(1, normalized.y(), pixels.y()));
                    updatePoint();
                });
                layout->addRow(trackMouse);
            }
            parameters->addWidget(row);
        } else if (kind == "color") {
            const auto rgba = value.toArray();
            auto *picker = new ColorPicker;
            picker->setSettingsKey("preview/colorPicker");
            const auto applyColor = [this, name](const QColor &color) {
                preview->setValue(name, QVariant::fromValue(QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF())));
            };
            picker->setColor(QColor::fromRgbF(
                qBound(0., at(rgba, 0).toDouble(1), 1.), qBound(0., at(rgba, 1).toDouble(1), 1.),
                qBound(0., at(rgba, 2).toDouble(1), 1.), qBound(0., at(rgba, 3).toDouble(1), 1.)));
            applyColor(picker->color());
            connect(picker, &ColorPicker::colorEdited, this, applyColor);
            parameters->addWidget(picker);
        } else if (kind == "image") {
            const bool fxSource = name == "inputImage" && shaderType == ISFType::FX;
            const bool transitionSource = shaderType == ISFType::Transition &&
                (name == "startImage" || name == "endImage");
            const QString defaultName = fxSource ? "Animated test card" :
                transitionSource ? (name == "startImage" ? "BEFORE" : "AFTER") : "Default image";
            auto *sourceStatus = plainLabel(defaultName);
            sourceStatus->setWordWrap(true);
            auto *button = new QPushButton("Choose image…");
            auto *chooseShader = new QPushButton("Choose ISF generator…");
            auto *defaultButton = fxSource ? nullptr : new QPushButton;
            const auto showDefault = [defaultButton, defaultName](bool active) {
                if (!defaultButton) return;
                defaultButton->setChecked(active);
                defaultButton->setText(active ? defaultName + " · On" : "Use " + defaultName);
            };
            if (defaultButton) {
                defaultButton->setObjectName("testCard");
                defaultButton->setCheckable(true);
                showDefault(true);
            }
            connect(button, &QPushButton::clicked, this, [=] {
                const auto path = QFileDialog::getOpenFileName(this, "Choose image", folder);
                if (path.isEmpty()) return;
                const QImage image(path);
                if (image.isNull()) {
                    QMessageBox::warning(this, "Choose image", "Cannot read this image.");
                    return;
                }
                preview->setImage(name, image);
                showDefault(false);
                sourceStatus->setText("Image: " + QFileInfo(path).fileName());
            });
            connect(chooseShader, &QPushButton::clicked, this, [=] {
                const auto path = QFileDialog::getOpenFileName(this, "Choose ISF generator", folder, filter);
                if (path.isEmpty()) return;
                const auto shader = readShader(path);
                if (!shader || !shader->header || shader->type() != ISFType::Generator) {
                    QMessageBox::warning(this, "Choose ISF generator", "Choose a valid ISF generator.");
                    return;
                }
                preview->setShaderInput(name, shader->source, *shader->header, path);
                showDefault(false);
                sourceStatus->setText("ISF: " + QFileInfo(path).fileName());
            });
            connect(preview, &ShaderPreview::shaderInputError, sourceStatus, [=](const QString &slot, const QString &error) {
                if (slot == name) {
                    showDefault(true);
                    sourceStatus->setText(defaultName + " · ISF source unavailable");
                    sourceStatus->setToolTip(error);
                }
            });
            if (fxSource) {
                connect(preview, &ShaderPreview::inputStatus, sourceStatus, &QLabel::setText);
                auto *testCard = new QPushButton;
                testCard->setObjectName("testCard");
                testCard->setCheckable(true);
                const auto showTestCard = [testCard](bool active) {
                    testCard->setChecked(active);
                    testCard->setText(active ? "Animated test card · On" : "Use animated test card");
                };
                showTestCard(true);
                connect(preview, &ShaderPreview::testCardActiveChanged, testCard, showTestCard);
                connect(testCard, &QPushButton::clicked, this, [=] {
                    showTestCard(true);
                    preview->useTestCard();
                    sourceStatus->setText("Animated test card");
                });
                layout->addWidget(testCard);
#ifdef Q_OS_MACOS
                const QString liveName = "Syphon";
#elif defined(Q_OS_WIN)
                const QString liveName = "Spout";
#else
                const QString liveName = "GStreamer shared memory";
#endif
                auto *live = new QPushButton("Choose " + liveName + " input…");
                connect(live, &QPushButton::clicked, this, [=] {
                    QString sender;
                    QSize size;
                    bool accepted = false;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
                    auto discovery = createLiveInput({});
                    const auto discoveryError = discovery->error();
                    if (!discoveryError.isEmpty()) { QMessageBox::information(this, liveName, discoveryError); return; }
                    const auto senders = discovery->senders();
                    if (senders.isEmpty()) {
                        QMessageBox::information(this, liveName, "No sender was found. Start a sender and try again.");
                        return;
                    }
                    QStringList labels;
                    for (int i = 0; i < senders.size(); ++i)
                        labels << QString("%1. %2").arg(i + 1).arg(senders.at(i).label);
                    const auto selected = QInputDialog::getItem(this, liveName, "Sender:", labels, 0, false, &accepted);
                    if (!accepted) return;
                    const auto index = labels.indexOf(selected);
                    if (index < 0) return;
                    sender = senders.at(index).id;
#else
                    sender = QInputDialog::getText(this, liveName, "Socket path (RGBA video):",
                                                  QLineEdit::Normal, "/tmp/isflab-video", &accepted).trimmed();
                    if (!accepted || sender.isEmpty()) return;
                    QDialog dimensions(this);
                    dimensions.setWindowTitle("Shared video dimensions");
                    auto *form = new QFormLayout(&dimensions);
                    auto *width = new SliderField;
                    auto *height = new SliderField;
                    for (auto *field : {width, height}) {
                        field->setDecimals(0);
                        field->setRange(1, 3840);
                        field->setTypedRange(1, 8192);
                        field->setSuffix(" px");
                        field->setSingleStep(1);
                    }
                    width->setValue(1280);
                    height->setValue(720);
                    form->addRow("Width", width);
                    form->addRow("Height", height);
                    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
                    form->addRow(buttons);
                    connect(buttons, &QDialogButtonBox::accepted, &dimensions, &QDialog::accept);
                    connect(buttons, &QDialogButtonBox::rejected, &dimensions, &QDialog::reject);
                    if (dimensions.exec() != QDialog::Accepted) return;
                    size = QSize(int(width->value()), int(height->value()));
#endif
                    auto input = createLiveInput(sender, size);
                    const auto issue = input->error();
                    if (!issue.isEmpty()) { QMessageBox::information(this, liveName, issue); return; }
                    preview->setLiveInput(std::move(input));
                    sourceStatus->setText("Waiting for " + liveName + "…");
                });
                layout->addWidget(live);
            } else {
                connect(defaultButton, &QPushButton::clicked, this, [=] {
                    showDefault(true);
                    preview->setImage(name, {});
                    sourceStatus->setText(defaultName);
                });
                layout->addWidget(defaultButton);
            }
            layout->addWidget(button);
            layout->addWidget(chooseShader);
            layout->addWidget(sourceStatus);
        } else if (kind == "audio" || kind == "audioFFT") {
            parameters->addWidget(plainLabel("Live audio is unavailable in preview."));
        }
    }
    const bool hasDefaults = std::any_of(inputs.begin(), inputs.end(), [](const QJsonValue &entry) {
        const auto type = entry.toObject().value("TYPE").toString();
        return QStringList{"float", "long", "bool", "point2D", "point3D", "color"}.contains(type);
    });
    auto *saveDefaults = new QPushButton("Save as Default");
    saveDefaults->setEnabled(hasDefaults);
    saveDefaults->setToolTip("Save the current parameter values as defaults in the shader file.");
    connect(saveDefaults, &QPushButton::clicked, this, &MainWindow::savePreviewDefaults);
    parameters->addWidget(saveDefaults);
    parameters->addStretch();
}

void MainWindow::exportFiltered(bool zip) {
    if (!resolveChanges()) return;
    QList<ShaderEntry> selected;
    QStringList unreadable;
    for (const auto &entry : std::as_const(entries)) {
        const auto current = readShader(entry.path);
        if (current && matches(*current)) selected << *current;
        else if (!current && matches(entry)) unreadable << "Cannot read: " + entry.path;
    }
    if (selected.isEmpty() && !unreadable.isEmpty()) {
        ExportReport result;
        result.errors = unreadable;
        report(result, {});
        return;
    }
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Export shaders", "No shaders match the current filters.");
        return;
    }
    if (zip) {
        auto archive = QFileDialog::getSaveFileName(this, "Export filtered ZIP",
                                                    QDir(folder).filePath("ISFLab-Library.zip"), "ZIP archives (*.zip)");
        if (archive.isEmpty()) return;
        if (!archive.endsWith(".zip", Qt::CaseInsensitive)) {
            archive += ".zip";
            if (QFileInfo::exists(archive) &&
                QMessageBox::question(this, "Replace ZIP", "Replace the existing ZIP archive?") != QMessageBox::Yes)
                return;
        }
        QTemporaryDir staging;
        if (!staging.isValid()) {
            QMessageBox::warning(this, "Export filtered ZIP", "Cannot create a temporary folder.");
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
    const auto destination = QFileDialog::getExistingDirectory(this, "Export filtered shaders to…", folder);
    if (!destination.isEmpty()) {
        auto result = exportShaders(selected, destination);
        result.errors << unreadable;
        report(result, destination);
    }
}

void MainWindow::clearSelection() {
    active.clear();
    previewedSource.clear();
    compilePreviewButton->setEnabled(false);
    editor->clear();
    editor->document()->setModified(false);
    editor->setReadOnly(true);
    preview->load({}, {}, {});
    controls({});
    previewStatus->setText("Select a shader to preview.");
}

void MainWindow::moveErrors() {
    if (!resolveChanges()) return;
    QStringList selected;
    for (const auto &entry : std::as_const(entries))
        if (binType(entry) == ISFType::Unknown && matches(entry)) selected << entry.path;
    if (selected.isEmpty()) {
        QMessageBox::information(this, "Move ERR to Folder", "No shaders in error match the current filters.");
        return;
    }
    const auto destination = QFileDialog::getExistingDirectory(
        this, QString("Move %1 shader(s) in error to…").arg(selected.size()), folder);
    if (destination.isEmpty()) return;
    const auto destinationPath = QFileInfo(destination).canonicalFilePath();
    if (destinationPath.isEmpty() || !QFileInfo(destinationPath).isDir()) {
        QMessageBox::warning(this, "Move ERR to Folder", "The destination folder is no longer available.");
        return;
    }
    const QDir target(destinationPath);
    QStringList failures;
    int moved = 0;
    const QSignalBlocker blocker(tree);
    for (const auto &path : selected) {
        const QFileInfo source(path);
        if (source.absolutePath() == target.absolutePath()) continue;
        const auto current = readShader(path);
        if (!current || current->source != entries.value(path).source) {
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
        if (active == path) clearSelection();
        entries.remove(path);
        delete rows.take(path);
        entryGroups.remove(path);
        previewErrors.remove(path);
    }
    refresh();
    QMessageBox result(failures.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
                       "Move ERR to Folder", QString("%1 shader(s) moved. %2 error(s).").arg(moved).arg(failures.size()),
                       QMessageBox::Ok, this);
    result.setDetailedText(failures.join('\n'));
    result.exec();
}

void MainWindow::organize() {
    if (!resolveChanges()) return;
    const auto source = QFileDialog::getExistingDirectory(this, "Choose a folder to organize", folder);
    if (source.isEmpty()) return;
    QMessageBox mode(this);
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
        const auto entry = readShader(path);
        if (entry) {
            selected << *entry;
            if (moveOriginals) sources.insert(path, entry->source);
        } else unreadable << "Cannot read: " + file;
    }
    auto result = exportShaders(selected, source);
    result.errors << unreadable;
    if (moveOriginals) {
        const QSignalBlocker blocker(tree);
        QString selectedCopy;
        QStringList importedCopies;
        for (auto it = result.copiedFiles.cbegin(); it != result.copiedFiles.cend(); ++it) {
            const auto &path = it.key();
            const auto original = readShader(path);
            if (!original || original->source != sources.value(path) || !QFile::moveToTrash(path)) {
                result.errors << "Original kept: " + path;
                continue;
            }
            ++result.moved;
            if (entries.remove(path)) {
                delete rows.take(path);
                entryGroups.remove(path);
                previewErrors.remove(path);
                importedCopies << it.value();
            }
            if (active == path) {
                active.clear();
                selectedCopy = QFileInfo(it.value()).canonicalFilePath();
            }
        }
        if (!importedCopies.isEmpty()) addFiles(importedCopies, source);
        if (!selectedCopy.isEmpty() && entries.contains(selectedCopy)) select(selectedCopy);
        else if (active.isEmpty()) clearSelection();
        refresh();
    }
    report(result, source);
}

void MainWindow::report(const ExportReport &result, const QString &destination, bool zip) {
    QMessageBox message(this);
    message.setWindowTitle("ISFLab export");
    message.setTextFormat(Qt::PlainText);
    message.setIcon(result.errors.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
    auto summary = QString("%1 shader(s) %5 to Generators, FX and Transitions in:\n%2\n\n"
                           "%3 invalid header(s) skipped. %4 error(s).")
                   .arg(result.copied).arg(QDir::toNativeSeparators(destination)).arg(result.skipped)
                   .arg(result.errors.size()).arg(zip ? "archived" : "copied");
    if (result.moved) summary += QString("\n%1 original shader(s) moved to the Trash.").arg(result.moved);
    message.setText(summary);
    if (!result.errors.isEmpty()) message.setDetailedText(result.errors.join("\n"));
    message.exec();
}
