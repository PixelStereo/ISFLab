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
#include <QCryptographicHash>
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
#include <QPaintEvent>
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
#include <QThread>
#include <QProgressBar>
#include <atomic>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QTextCursor>
#include <QTextBlock>
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

class SourceEditor final : public QPlainTextEdit {
public:
    SourceEditor() : gutter(new LineNumbers(this)) {
        connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { layoutGutter(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
            if (dy) gutter->scroll(0, dy);
            else gutter->update(0, rect.y(), gutter->width(), rect.height());
        });
        connect(this, &QPlainTextEdit::cursorPositionChanged, gutter, [this] { gutter->update(); });
        layoutGutter();
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QPlainTextEdit::resizeEvent(event);
        layoutGutter();
    }

    void changeEvent(QEvent *event) override {
        QPlainTextEdit::changeEvent(event);
        if (event->type() == QEvent::FontChange) layoutGutter();
    }

private:
    class LineNumbers final : public QWidget {
    public:
        explicit LineNumbers(SourceEditor *editor) : QWidget(editor), editor(editor) {}
    protected:
        void paintEvent(QPaintEvent *event) override { editor->paintGutter(event->rect()); }
    private:
        SourceEditor *editor;
    };

    void layoutGutter() {
        const int digits = QString::number(qMax(1, blockCount())).size();
        const int width = 14 + digits * fontMetrics().horizontalAdvance('9');
        if (viewportMargins().left() != width) setViewportMargins(width, 0, 0, 0);
        const auto rect = contentsRect();
        gutter->setGeometry(rect.left(), rect.top(), width, rect.height());
    }

    void paintGutter(const QRect &area) {
        QPainter painter(gutter);
        painter.fillRect(area, palette().color(QPalette::Window));
        painter.setFont(font());
        const auto current = textCursor().blockNumber();
        for (auto block = firstVisibleBlock(); block.isValid(); block = block.next()) {
            const int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
            if (top > area.bottom()) break;
            if (!block.isVisible() || top + blockBoundingRect(block).height() < area.top()) continue;
            painter.setPen(palette().color(block.blockNumber() == current ? QPalette::Text : QPalette::PlaceholderText));
            painter.drawText(QRect(0, top, gutter->width() - 7, fontMetrics().height()),
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(block.blockNumber() + 1));
        }
    }

    LineNumbers *gutter;
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
            if (line.mid(offset, 2) == "//") {
                setFormat(offset, line.size() - offset, QColor("#83918d"));
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
    exportMenu->addAction("Repair Filtered ERR…", this, [this] {
        QStringList paths;
        for (const auto &entry : std::as_const(entries))
            if (binType(entry) == ISFType::Unknown && matches(entry)) paths << entry.path;
        repairErrors(paths);
    });
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
    auto *filterRows = new QVBoxLayout(typeRow);
    filterRows->setContentsMargins(0, 0, 0, 0);
    filterRows->setSpacing(4);
    auto *typeLayout = new QHBoxLayout;
    auto *otherTypes = new QHBoxLayout;
    typeLayout->setSpacing(4);
    otherTypes->setSpacing(4);
    filterRows->addLayout(typeLayout);
    filterRows->addLayout(otherTypes);
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
    for (const auto kind : {ISFType::Generator, ISFType::FX, ISFType::Transition, ISFType::Unknown, ISFType::NonISF, ISFType::MadMapper}) {
        auto *tab = new QPushButton(kind == ISFType::MadMapper ? "MadMapper" : kind == ISFType::NonISF ? "Non-ISF" : kind == ISFType::Unknown ? "ERR" : kind == ISFType::FX ? "Filter" : ISFScanner::typeToString(kind));
        tab->setProperty("filterTab", true);
        tab->setObjectName(kind == ISFType::Generator ? "generatorType" : kind == ISFType::FX ? "effectType" : kind == ISFType::Transition ? "transitionType" : kind == ISFType::Unknown ? "errorType" : kind == ISFType::NonISF ? "nonIsfType" : "madMapperType");
        tab->setCheckable(true);
        tab->setChecked(true);
        tab->setToolTip(kind == ISFType::Unknown ? "Invalid headers and errors detected in preview." :
                        kind == ISFType::NonISF ? "Fragment files without an ISF JSON header." :
                        kind == ISFType::MadMapper ? "Materials with a MadMapper fragment entry point." :
                        "Toggle this shader type. Several types can be active.");
        typeTabs.insert(kind, tab);
        auto *row = kind == ISFType::Generator || kind == ISFType::FX || kind == ISFType::Transition
            ? typeLayout : otherTypes;
        row->addWidget(tab, 1);
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
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
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
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(9, 5, 9, 5);
    footerLayout->setSpacing(2);
    auto *stats = new QHBoxLayout;
    auto *diagnosticCounts = new QHBoxLayout;
    footerLayout->addLayout(stats);
    footerLayout->addLayout(diagnosticCounts);
    for (const auto kind : {ISFType::Generator, ISFType::FX, ISFType::Transition, ISFType::Unknown, ISFType::NonISF, ISFType::MadMapper}) {
        auto *label = plainLabel();
        label->setStyleSheet("color:" + AppStyle::typeColor(kind).name() + "; font-size:11px; font-weight:600;");
        typeCounts.insert(kind, label);
        auto *row = kind == ISFType::Unknown || kind == ISFType::NonISF || kind == ISFType::MadMapper ? diagnosticCounts : stats;
        row->addWidget(label, 1);
    }
    left->addWidget(footer);
    auto *clearBin = new QPushButton("Clear");
    clearBin->setToolTip("Remove all items from the Media Bin.");
    connect(clearBin, &QPushButton::clicked, this, [this] {
        if (entries.isEmpty() || !resolveChanges()) return;
        const QSignalBlocker blocker(tree);
        tree->clear();
        rows.clear();
        groups.clear();
        entries.clear();
        entryGroups.clear();
        previewErrors.clear();
        clearSelection();
        refresh();
    });
    auto *duplicates = new QPushButton("Find Duplicates…");
    connect(duplicates, &QPushButton::clicked, this, &MainWindow::findDuplicates);
    auto *binActions = new QHBoxLayout;
    binActions->addWidget(duplicates, 1);
    binActions->addWidget(clearBin);
    left->addLayout(binActions);
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
    previewStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    viewerLayout->addWidget(previewStatus);
    connect(preview, &ShaderPreview::status, this, [this](const QString &status) {
        previewStatus->setText(status == "Preview compiled" ? status : "Preview error · See Diagnostics");
        if (active.isEmpty() || editor->document()->isModified() || vertexEditor->document()->isModified()) return;
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

    auto *codePanel = sourceTabs = new QTabWidget;
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
    auto *editorDock = new QDockWidget("ISF Editor", this);
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

    auto *diagnostics = new QWidget;
    auto *diagnosticLayout = new QVBoxLayout(diagnostics);
    auto *diagnosticMessage = plainLabel();
    diagnosticMessage->setWordWrap(true);
    diagnosticMessage->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *jumpToError = new QPushButton("Go to error");
    auto *fixCommas = new QPushButton("Remove trailing commas");
    auto *fixGlsl = new QPushButton("Apply suggested fix");
    fixGlsl->hide();
    diagnosticLayout->addWidget(diagnosticMessage);
    diagnosticLayout->addWidget(jumpToError, 0, Qt::AlignLeft);
    diagnosticLayout->addWidget(fixCommas, 0, Qt::AlignLeft);
    diagnosticLayout->addWidget(fixGlsl, 0, Qt::AlignLeft);
    diagnosticLayout->addStretch();
    const int diagnosticTab = inspector->addTab(diagnostics, "Diagnostics");

    struct CompilerDiagnostic {
        QString stage, file, log;
        QString shader;
        int line = 0;
    };
    const auto compiler = std::make_shared<CompilerDiagnostic>();
    auto *compilerLog = new QPlainTextEdit;
    compilerLog->setReadOnly(true);
    compilerLog->setFont(editor->font());
    compilerLog->setVisible(false);
    diagnosticLayout->insertWidget(1, compilerLog, 1);
    const auto goToLine = [](QPlainTextEdit *view, int line) {
        const auto block = view->document()->findBlockByNumber(line - 1);
        if (!block.isValid()) return;
        QTextCursor cursor(block);
        view->setTextCursor(cursor);
        view->setFocus();
        view->ensureCursorVisible();
    };

    struct GlslFix {
        int position;
        int length;
        QString from, to;
    };
    const auto suggestedFix = [this, compiler]() -> std::optional<GlslFix> {
        if (compiler->stage != "Fragment shader" || compiler->file != active || compiler->line <= 0 ||
            editor->toPlainText() != previewedSource)
            return {};
        const auto log = compiler->log.toLower();
        if (!log.contains("undeclared") && !log.contains("not declared") && !log.contains("unknown identifier"))
            return {};
        const auto block = editor->document()->findBlockByNumber(compiler->line - 1);
        if (!block.isValid()) return {};
        const auto line = block.text().section("//", 0, 0);
        const auto tokens = {
            std::pair{"gl_FragColour", "gl_FragColor"},
            std::pair{"isf_FragNormCoords", "isf_FragNormCoord"},
            std::pair{"vv_FragNormCoords", "vv_FragNormCoord"},
        };
        for (const auto &token : tokens) {
            const QString from = QString::fromLatin1(token.first);
            const QRegularExpression word("\\b" + from + "\\b");
            if (!word.match(compiler->log).hasMatch()) continue;
            auto occurrences = word.globalMatch(line);
            if (!occurrences.hasNext()) continue;
            const auto match = occurrences.next();
            if (occurrences.hasNext()) continue;
            return GlslFix{block.position() + int(match.capturedStart()), int(match.capturedLength()),
                           from, QString::fromLatin1(token.second)};
        }
        return {};
    };

    const auto updateDiagnostics = [=] {
        const auto bytes = editor->toPlainText().toUtf8();
        ISFScanner::HeaderError error;
        const bool material = !active.isEmpty() && ISFScanner::isMadMapperMaterial(bytes);
        const bool invalid = !active.isEmpty() && !ISFScanner::header(bytes, &error) &&
                             !(material && ISFScanner::materialHeader(bytes));
        const auto fixes = invalid ? ISFScanner::trailingCommaFix(bytes) : QList<qsizetype>{};
        const bool showCompiler = !invalid && !active.isEmpty() && compiler->shader == active && !compiler->stage.isEmpty();
        inspector->setTabText(diagnosticTab, invalid || showCompiler ? "Diagnostics (1)" : "Diagnostics");
        const bool warning = showCompiler && compiler->stage == "Material warning";
        inspector->tabBar()->setTabTextColor(diagnosticTab, warning ? QColor("#e6bd8d") :
                                             invalid || showCompiler ? QColor("#f17979") : QColor{});
        const bool hasError = (invalid && (material || ISFScanner::headerStart(bytes) >= 0)) ||
                              (showCompiler && !warning);
        if (hasError) {
            editorDock->show();
            inspector->setCurrentIndex(diagnosticTab);
        }
        compilerLog->setVisible(showCompiler);
        const auto glslFix = showCompiler ? suggestedFix() : std::optional<GlslFix>{};
        fixGlsl->setVisible(glslFix.has_value());
        jumpToError->setEnabled(invalid || (showCompiler && compiler->line > 0 &&
                                           editor->toPlainText() == previewedSource &&
                                           !(compiler->file == vertexPath && vertexEditor->document()->isModified())));
        fixCommas->setEnabled(!fixes.isEmpty());
        if (showCompiler) {
            QString heading = compiler->stage;
            if (!compiler->file.isEmpty()) heading += " · " + QFileInfo(compiler->file).fileName();
            if (compiler->line > 0) heading += QString(" · line %1").arg(compiler->line);
            if (editor->toPlainText() != previewedSource)
                heading += "\nSave the current edits to refresh this diagnostic.";
            const auto log = compiler->log.toLower();
            if (log.contains("undeclared") || log.contains("not declared"))
                heading += "\nCheck the identifier spelling, its declaration, and its scope.";
            else if (log.contains("no matching") || log.contains("cannot convert") || log.contains("type mismatch"))
                heading += "\nCheck the types of the arguments and the expected GLSL signature.";
            else if (log.contains("syntax error"))
                heading += "\nCheck for missing punctuation or unmatched brackets near this line.";
            if (glslFix)
                heading += "\nSuggested fix: replace " + glslFix->from + " with " + glslFix->to + ".";
            diagnosticMessage->setText(heading);
            compilerLog->setPlainText(compiler->log.isEmpty() ? "The driver did not provide an error log." : compiler->log);
            return;
        }
        if (!invalid) {
            diagnosticMessage->setText(active.isEmpty() ? "Select a shader to see its diagnostics." :
                                        material ? "MadMapper Material · 2D preview" : "No ISF header errors.");
            return;
        }
        const auto position = fixes.isEmpty() ? error.byteOffset : fixes.first();
        const auto prefix = QString::fromUtf8(bytes.left(position));
        const auto line = prefix.count('\n') + 1;
        const auto column = prefix.size() - prefix.lastIndexOf('\n');
        const auto detail = fixes.isEmpty() ? error.message :
            QString("%1 trailing comma(s) can be removed to repair the header.").arg(fixes.size());
        diagnosticMessage->setText(QString("Line %1, column %2\n%3").arg(line).arg(column).arg(detail));
    };
    connect(editor, &QPlainTextEdit::textChanged, diagnostics, updateDiagnostics);
    connect(vertexEditor->document(), &QTextDocument::modificationChanged, diagnostics, updateDiagnostics);
    connect(preview, &ShaderPreview::glslDiagnostic, diagnostics,
            [=](const QString &stage, const QString &file, const QString &log, int line) {
        *compiler = {stage, file, log, active, line};
        updateDiagnostics();
    });
    connect(preview, &ShaderPreview::status, diagnostics, [=](const QString &status) {
        if (status == "Preview compiled") {
            if (compiler->stage != "Material warning") *compiler = {};
        } else if (compiler->stage.isEmpty() || compiler->stage == "Preview error" ||
                   compiler->stage == "Material warning") {
            *compiler = {"Preview error", active, status, active, 0};
        }
        updateDiagnostics();
    });
    connect(jumpToError, &QPushButton::clicked, diagnostics, [this, compiler, goToLine] {
        const auto bytes = editor->toPlainText().toUtf8();
        ISFScanner::HeaderError error;
        if (ISFScanner::header(bytes, &error) ||
            (ISFScanner::isMadMapperMaterial(bytes) && ISFScanner::materialHeader(bytes))) {
            if (compiler->shader != active || compiler->line <= 0 || editor->toPlainText() != previewedSource) return;
            if (compiler->file == active || compiler->file.isEmpty()) {
                sourceTabs->setCurrentIndex(0);
                goToLine(editor, compiler->line);
            } else if (compiler->file == vertexPath && !vertexEditor->document()->isModified()) {
                sourceTabs->setCurrentIndex(1);
                goToLine(vertexEditor, compiler->line);
            } else {
                const auto vertex = readShader(compiler->file);
                if (!vertex) {
                    QMessageBox::warning(this, "Shader Source", "The shader source could not be read.");
                    return;
                }
                QDialog dialog(this);
                dialog.setWindowTitle(QFileInfo(compiler->file).fileName());
                auto *layout = new QVBoxLayout(&dialog);
                auto *view = new SourceEditor;
                view->setPlainText(QString::fromUtf8(vertex->source));
                view->setReadOnly(true);
                view->setFont(editor->font());
                layout->addWidget(view);
                dialog.resize(700, 420);
                goToLine(view, compiler->line);
                dialog.exec();
            }
            return;
        }
        const auto fixes = ISFScanner::trailingCommaFix(bytes);
        sourceTabs->setCurrentIndex(0);
        QTextCursor cursor(editor->document());
        cursor.setPosition(QString::fromUtf8(bytes.left(fixes.isEmpty() ? error.byteOffset : fixes.first())).size());
        editor->setTextCursor(cursor);
        editor->setFocus();
        editor->ensureCursorVisible();
    });
    connect(fixGlsl, &QPushButton::clicked, diagnostics, [this, suggestedFix] {
        const auto fix = suggestedFix();
        if (!fix) return;
        QTextCursor cursor(editor->document());
        cursor.setPosition(fix->position);
        cursor.setPosition(fix->position + fix->length, QTextCursor::KeepAnchor);
        sourceTabs->setCurrentIndex(0);
        cursor.insertText(fix->to);
        statusBar()->showMessage("Suggested fix applied in the editor. Save to update the file.", 5000);
    });
    connect(fixCommas, &QPushButton::clicked, diagnostics, [this] {
        const auto bytes = editor->toPlainText().toUtf8();
        const auto fixes = ISFScanner::trailingCommaFix(bytes);
        if (fixes.isEmpty()) return;
        QTextCursor cursor(editor->document());
        cursor.beginEditBlock();
        for (auto it = fixes.crbegin(); it != fixes.crend(); ++it) {
            cursor.setPosition(QString::fromUtf8(bytes.left(*it)).size());
            cursor.deleteChar();
        }
        cursor.endEditBlock();
        statusBar()->showMessage("Trailing commas removed from the editor. Save to update the file.", 5000);
    });
    updateDiagnostics();

    auto *editorSplit = new QSplitter(Qt::Horizontal);
    editorSplit->setChildrenCollapsible(false);
    editorSplit->addWidget(codePanel);
    editorSplit->addWidget(inspector);
    editorSplit->setStretchFactor(0, 2);
    editorSplit->setStretchFactor(1, 1);
    editorDock->setWidget(editorSplit);
    addDockWidget(Qt::BottomDockWidgetArea, editorDock);

    const auto activateRow = [this](QTreeWidgetItem *item) {
        const auto modifiers = QApplication::keyboardModifiers();
        if (!item || (modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier)) ||
            QApplication::mouseButtons().testFlag(Qt::RightButton)) return;
        const auto path = item->data(0, AppStyle::PathRole).toString();
        if (path.isEmpty() || path == active) return;
        if (resolveChanges()) select(path);
        else {
            const QSignalBlocker blocker(tree);
            tree->setCurrentItem(rows.value(active), 0, QItemSelectionModel::NoUpdate);
        }
    };
    connect(tree, &QTreeWidget::currentItemChanged, this, activateRow);
    connect(tree, &QTreeWidget::itemClicked, this, activateRow);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        auto *item = tree->itemAt(position);
        if (!item || item->data(0, AppStyle::PathRole).toString().isEmpty()) return;
        if (!item->isSelected()) {
            const QSignalBlocker blocker(tree);
            tree->setCurrentItem(item, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        QStringList paths;
        for (const auto *row : tree->selectedItems()) {
            const auto path = row->data(0, AppStyle::PathRole).toString();
            if (!path.isEmpty() && !row->isHidden()) paths << path;
        }
        if (paths.isEmpty()) return;

        QMenu menu(tree);
        if (std::any_of(paths.cbegin(), paths.cend(), [this](const QString &path) {
                return binType(entries.value(path)) == ISFType::Unknown;
            })) {
            menu.addAction("Repair Selected ERR…", this, [this, paths] { repairErrors(paths); });
            menu.addSeparator();
        }
        menu.addAction("Copy to Folder…", this, [this, paths] { actOnShaders(paths, FileAction::Copy); });
        menu.addAction("Move to Folder…", this, [this, paths] { actOnShaders(paths, FileAction::Move); });
#ifdef Q_OS_MACOS
        if (paths.size() == 1) {
            menu.addAction("Show in Finder", this, [this, path = paths.first()] {
                if (!QProcess::startDetached("/usr/bin/open", {"-R", path}))
                    statusBar()->showMessage("Could not show the shader in Finder.", 5000);
            });
        }
#endif
        menu.addSeparator();
        menu.addAction("Move to Trash…", this, [this, paths] { actOnShaders(paths, FileAction::Trash); });
        menu.exec(tree->viewport()->mapToGlobal(position));
    });
    connect(search, &QLineEdit::textChanged, this, &MainWindow::refresh);
    controls({});
    const QSettings session;
    folder = session.value("mediaBin/lastFolder", folder).toString();
    const auto savedEntries = session.value("mediaBin/entries").toMap();
    for (auto it = savedEntries.cbegin(); it != savedEntries.cend(); ++it) {
        if (const auto entry = readShader(it.key())) {
            entries.insert(it.key(), *entry);
            entryGroups.insert(it.key(), it.value().toString());
        }
    }
    const auto disabledTypes = session.value("mediaBin/disabledTypes").toStringList();
    for (auto *tab : typeTabs) {
        const QSignalBlocker blocker(tab);
        tab->setChecked(!disabledTypes.contains(tab->objectName()));
    }
    const auto disabledCategories = session.value("mediaBin/disabledCategories").toStringList();
    uncheckedCategories = QSet<QString>(disabledCategories.cbegin(), disabledCategories.cend());
    {
        const QSignalBlocker blocker(search);
        search->setText(session.value("mediaBin/nameFilter").toString());
    }
    refresh();
    const auto selectedPath = session.value("mediaBin/active").toString();
    if (auto *row = rows.value(selectedPath); row && !row->isHidden()) tree->setCurrentItem(row);
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
    QSettings settings;
    settings.setValue("mainWindow/geometry", saveGeometry());
    QVariantMap savedEntries;
    for (auto it = entryGroups.cbegin(); it != entryGroups.cend(); ++it)
        savedEntries.insert(it.key(), it.value());
    settings.setValue("mediaBin/entries", savedEntries);
    settings.setValue("mediaBin/lastFolder", folder);
    settings.setValue("mediaBin/active", active);
    QStringList disabledTypes;
    for (const auto *tab : typeTabs)
        if (!tab->isChecked()) disabledTypes << tab->objectName();
    settings.setValue("mediaBin/disabledTypes", disabledTypes);
    settings.setValue("mediaBin/disabledCategories", QStringList(uncheckedCategories.values()));
    settings.setValue("mediaBin/nameFilter", search->text());
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
                parent->setFlags(parent->flags() & ~Qt::ItemIsSelectable);
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
        case ISFType::NonISF: item->setText(1, "NON-ISF"); break;
        case ISFType::MadMapper: item->setText(1, "MAD"); break;
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
        const auto caption = kind == ISFType::Generator ? "Gen" : kind == ISFType::FX ? "FX" :
            kind == ISFType::Transition ? "Trans" : kind == ISFType::Unknown ? "ERR" :
            kind == ISFType::MadMapper ? "MadMapper" : "Non-ISF";
        it.value()->setText(QString("●  %1 %2").arg(shown == total ? QString::number(total) :
                            QString("%1/%2").arg(shown).arg(total)).arg(caption));
        it.value()->setToolTip(QString("%1 visible · %2 total").arg(shown).arg(total));
    }
    if (inserted) tree->sortItems(0, Qt::AscendingOrder);
    if (!active.isEmpty() && rows.contains(active) && !rows.value(active)->isHidden())
        tree->setCurrentItem(rows.value(active), 0, QItemSelectionModel::NoUpdate);
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
    vertexPath = ISFScanner::companionVertexPath(path);
    const auto vertex = vertexPath.isEmpty() ? std::optional<ShaderEntry>{} : readShader(vertexPath);
    vertexEditor->setPlaceholderText(vertexPath.isEmpty() ? QString{} : "The vertex shader could not be read.");
    vertexBaseline = vertex ? vertex->source : QByteArray{};
    vertexEditor->setPlainText(QString::fromUtf8(vertexBaseline));
    vertexEditor->document()->setModified(false);
    vertexEditor->setReadOnly(!vertex);
    sourceTabs->setTabVisible(1, !vertexPath.isEmpty());
    sourceTabs->setCurrentIndex(0);
    editor->setReadOnly(false);
    editor->setPlainText(QString::fromUtf8(entries.value(active).source));
    editor->document()->setModified(false);
    applyPreview();
}

bool MainWindow::resolveChanges() {
    if (!editor->document()->isModified() && !vertexEditor->document()->isModified()) return true;
    QMessageBox dialog(QMessageBox::Warning, "Unsaved shader", "Save changes to " + QFileInfo(active).fileName() + "?",
                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    dialog.button(QMessageBox::Discard)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    const auto choice = dialog.exec();
    if (choice == QMessageBox::Save) return save();
    if (choice != QMessageBox::Discard) return false;
    select(active);
    return true;
}

bool MainWindow::save() {
    if (active.isEmpty()) return true;
    const bool saveFragment = editor->document()->isModified();
    const bool saveVertex = vertexEditor->document()->isModified();
    auto &entry = entries[active];
    const auto source = editor->toPlainText().toUtf8();
    const auto header = ISFScanner::header(source);
    if (saveFragment && !header &&
        !(ISFScanner::isMadMapperMaterial(source) && ISFScanner::materialHeader(source))) {
        QMessageBox::warning(this, "Save shader", "The fragment source must begin with a valid shader header.");
        return false;
    }
    const auto unchanged = [&](const QString &path, const QByteArray &baseline) {
        const auto disk = readShader(path);
        if (disk && disk->source == baseline) return true;
        QMessageBox::warning(this, "Save shader",
            QFileInfo(path).fileName() + " changed outside ISFLab or could not be read.");
        return false;
    };
    if ((saveFragment && !unchanged(active, entry.source)) ||
        (saveVertex && !unchanged(vertexPath, vertexBaseline))) return false;
    const auto write = [&](const QString &path, const QByteArray &bytes) {
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
            return true;
        QMessageBox::warning(this, "Save shader", QFileInfo(path).fileName() + " · " + file.errorString());
        return false;
    };
    if (saveFragment) {
        if (!write(active, source)) return false;
        entry.source = source;
        entry.header = header;
        entry.madMapper = ISFScanner::isMadMapperMaterial(source);
        editor->document()->setModified(false);
    }
    if (saveVertex) {
        const auto bytes = vertexEditor->toPlainText().toUtf8();
        if (!write(vertexPath, bytes)) return false;
        vertexBaseline = bytes;
        vertexEditor->document()->setModified(false);
    }
    if (saveFragment || saveVertex) previewErrors.remove(active);
    applyPreview();
    refresh();
    return true;
}

void MainWindow::writeHeader(const QJsonObject &header) {
    const auto source = editor->toPlainText();
    const auto bytes = source.toUtf8();
    const auto startByte = ISFScanner::headerStart(bytes);
    if (startByte < 0) return;
    const auto start = QString::fromUtf8(bytes.left(startByte)).size();
    const auto end = source.indexOf("*/", start + 2);
    if (end < 0) return;
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
        statusBar()->showMessage("Save the edited source before saving preview defaults.", 5000);
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
    const auto source = previewedSource.toUtf8();
    const bool material = ISFScanner::isMadMapperMaterial(source);
    const auto header = material ? ISFScanner::materialHeader(source) : ISFScanner::header(source);
    previewStatus->setText(header ? "Compiling preview…" :
        material ? "Invalid MadMapper Material header." :
        entries.value(active).type() == ISFType::NonISF ? "No ISF JSON header." : "Invalid ISF JSON header.");
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
    vertexPath.clear();
    vertexBaseline.clear();
    vertexEditor->clear();
    vertexEditor->document()->setModified(false);
    sourceTabs->setCurrentIndex(0);
    sourceTabs->setTabVisible(1, false);
    editor->clear();
    editor->document()->setModified(false);
    editor->setReadOnly(true);
    preview->load({}, {}, {});
    controls({});
    previewStatus->setText("Select a shader to preview.");
}

void MainWindow::actOnShaders(const QStringList &paths, FileAction action) {
    const bool trash = action == FileAction::Trash;
    const bool copy = action == FileAction::Copy;
    const QString title = trash ? "Move to Trash" : copy ? "Copy to Folder" : "Move to Folder";
    QString destination;
    if (trash) {
        if (QMessageBox::question(this, title, QString("Move %1 selected shader file(s) to the Trash?").arg(paths.size()))
            != QMessageBox::Yes) return;
    } else {
        const auto chosen = QFileDialog::getExistingDirectory(this, title, folder);
        if (chosen.isEmpty()) return;
        destination = QFileInfo(chosen).canonicalFilePath();
        if (destination.isEmpty() || !QFileInfo(destination).isDir()) {
            QMessageBox::warning(this, title, "The destination folder is no longer available.");
            return;
        }
    }
    if (paths.contains(active) && !resolveChanges()) return;

    QStringList failures, removed;
    int done = 0;
    const int total = int(paths.size());
    std::atomic<int> processed{0};
    std::atomic<bool> cancelled{false};
    const auto snapshot = entries;
    class FileProgressDialog final : public QDialog {
    public:
        FileProgressDialog(QWidget *parent, std::atomic<bool> &cancelled)
            : QDialog(parent), cancelled(cancelled) {}
        void reject() override { cancelled.store(true); }
    private:
        std::atomic<bool> &cancelled;
    } progress(this, cancelled);
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
                    const ShaderEntry entry{path, bytes, ISFScanner::header(bytes)};
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

    const QSignalBlocker blocker(tree);
    if (!removed.isEmpty() && removed.size() >= entries.size() / 2) {
        tree->clear();
        rows.clear();
        groups.clear();
    }
    for (const auto &path : std::as_const(removed)) {
        if (active == path) clearSelection();
        entries.remove(path);
        delete rows.take(path);
        entryGroups.remove(path);
        previewErrors.remove(path);
    }
    refresh();
    const QString verb = trash ? "sent to the Trash" : copy ? "copied" : "moved";
    QMessageBox result(failures.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
                       title, QString("%1 shader(s) %2. %3 error(s).").arg(done).arg(verb).arg(failures.size()) +
                           (cancelled.load() ? QString("\nOperation cancelled.") : QString()),
                       QMessageBox::Ok, this);
    result.setDetailedText(failures.join('\n'));
    result.exec();
}

void MainWindow::findDuplicates() {
    if (entries.isEmpty()) {
        QMessageBox::information(this, "Find Duplicates", "Import shaders to look for duplicates.");
        return;
    }
    const auto snapshot = entries;
    QMap<QByteArray, QStringList> groupsByContent;
    std::atomic<bool> cancelled{false};
    int uncertain = 0;

    QDialog scan(this);
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
            bySource[QCryptographicHash::hash(entry.source, QCryptographicHash::Sha256)].append(entry.path);
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

    QDialog review(this);
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
        if (paths.removeOne(active)) paths.prepend(active);
        auto *group = new QTreeWidgetItem(results, {QString("%1 identical shaders").arg(paths.size())});
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
        if (!deleteFiles && selected.contains(active) && !resolveChanges()) return;
        review.accept();
        if (deleteFiles) {
            actOnShaders(selected, FileAction::Trash);
            return;
        }
        const QSignalBlocker blocker(tree);
        for (const auto &path : selected) {
            if (active == path) clearSelection();
            entries.remove(path);
            delete rows.take(path);
            entryGroups.remove(path);
            previewErrors.remove(path);
        }
        refresh();
    };
    connect(remove, &QPushButton::clicked, &review, [&] { process(false); });
    connect(trash, &QPushButton::clicked, &review, [&] { process(true); });
    review.resize(820, 460);
    review.exec();
}

void MainWindow::repairErrors(const QStringList &paths) {
    const QString title = "Repair ERR";
    QMap<QString, QList<qsizetype>> repairs;
    int errorCount = 0;
    for (const auto &path : paths) {
        const auto entry = entries.constFind(path);
        if (entry == entries.cend() || binType(*entry) != ISFType::Unknown) continue;
        ++errorCount;
        if (path == active && (editor->document()->isModified() || vertexEditor->document()->isModified())) continue;
        const auto commas = ISFScanner::trailingCommaFix(entry->source);
        if (!commas.isEmpty()) repairs.insert(path, commas);
    }
    if (repairs.isEmpty()) {
        QMessageBox::information(this, title, QString(
            "No safe automatic repair found for %1 shader(s) in error. Files with unsaved edits are skipped.")
            .arg(errorCount));
        return;
    }
    if (QMessageBox::question(this, title, QString(
            "Remove trailing JSON commas and save %1 of %2 shader(s) in error?")
            .arg(repairs.size()).arg(errorCount)) != QMessageBox::Yes) return;

    QStringList failures;
    int repaired = 0;
    bool reloadActive = false;
    for (auto it = repairs.cbegin(); it != repairs.cend(); ++it) {
        const auto &path = it.key();
        auto &entry = entries[path];
        const auto disk = readShader(path);
        if (!disk || QFileInfo(path).isSymLink() || disk->source != entry.source) {
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
        entry.source = source;
        entry.header = header;
        previewErrors.remove(path);
        reloadActive |= active == path;
        ++repaired;
    }
    if (reloadActive) select(active);
    refresh();

    QMessageBox report(this);
    report.setWindowTitle(title);
    report.setText(QString("%1 repaired · %2 skipped · %3 failed")
        .arg(repaired).arg(errorCount - repairs.size()).arg(failures.size()));
    if (!failures.isEmpty()) report.setDetailedText(failures.join('\n'));
    report.exec();
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
                           "%3 unsupported or invalid file(s) skipped. %4 error(s).")
                   .arg(result.copied).arg(QDir::toNativeSeparators(destination)).arg(result.skipped)
                   .arg(result.errors.size()).arg(zip ? "archived" : "copied");
    if (result.moved) summary += QString("\n%1 original shader(s) moved to the Trash.").arg(result.moved);
    message.setText(summary);
    if (!result.errors.isEmpty()) message.setDetailedText(result.errors.join("\n"));
    message.exec();
}
