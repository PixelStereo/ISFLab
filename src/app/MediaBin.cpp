#include "MediaBin.h"
#include "AppStyle.h"
#include "SettingsKeys.h"
#include "ShaderTypes.h"
#include "UiHelpers.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#ifdef Q_OS_MACOS
#include <QProcess>
#endif

MediaBin::MediaBin(QWidget *parent) : QWidget(parent) {
    setObjectName("libraryPanel");
    auto *left = new QVBoxLayout(this);
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
    for (const auto &info : shaderTypes()) {
        auto *tab = new QPushButton(info.filterLabel);
        tab->setProperty("filterTab", true);
        tab->setObjectName(info.objectName);
        tab->setCheckable(true);
        tab->setChecked(true);
        tab->setToolTip(info.toolTip);
        typeTabs.insert(info.type, tab);
        (info.primary ? typeLayout : otherTypes)->addWidget(tab, 1);
        connect(tab, &QPushButton::toggled, this, &MediaBin::refresh);
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
    flatBin = QSettings().value(SettingsKeys::BinFlat, false).toBool();
    flatToggle->setChecked(flatBin);
    tree->setRootIsDecorated(!flatBin);
    connect(flatToggle, &QCheckBox::toggled, this, [this](bool enabled) {
        flatBin = enabled;
        QSettings().setValue(SettingsKeys::BinFlat, enabled);
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
    for (const auto &info : shaderTypes()) {
        auto *label = plainLabel();
        label->setStyleSheet("color:" + info.color.name() + "; font-size:11px; font-weight:600;");
        typeCounts.insert(info.type, label);
        (info.primary ? stats : diagnosticCounts)->addWidget(label, 1);
    }
    left->addWidget(footer);
    auto *clearBin = new QPushButton("Clear");
    clearBin->setToolTip("Remove all items from the Media Bin.");
    connect(clearBin, &QPushButton::clicked, this, [this] {
        if (!entries.isEmpty()) emit clearRequested();
    });
    auto *duplicates = new QPushButton("Find Duplicates…");
    connect(duplicates, &QPushButton::clicked, this, &MediaBin::duplicatesRequested);
    auto *binActions = new QHBoxLayout;
    binActions->addWidget(duplicates, 1);
    binActions->addWidget(clearBin);
    left->addLayout(binActions);

    const auto activateRow = [this](QTreeWidgetItem *item) {
        const auto modifiers = QApplication::keyboardModifiers();
        if (!item || (modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier)) ||
            QApplication::mouseButtons().testFlag(Qt::RightButton)) return;
        const auto path = item->data(0, AppStyle::PathRole).toString();
        if (path.isEmpty() || path == active) return;
        emit activationRequested(path);
    };
    connect(tree, &QTreeWidget::currentItemChanged, this, activateRow);
    connect(tree, &QTreeWidget::itemClicked, this, activateRow);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeWidget::customContextMenuRequested, this, &MediaBin::showContextMenu);
    connect(search, &QLineEdit::textChanged, this, &MediaBin::refresh);
}

void MediaBin::showContextMenu(const QPoint &position) {

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
        menu.addAction("Repair Selected ERR…", this, [this, paths] { emit repairRequested(paths); });
        menu.addSeparator();
    }
    menu.addAction("Copy to Folder…", this, [this, paths] { emit fileActionRequested(paths, FileAction::Copy); });
    menu.addAction("Move to Folder…", this, [this, paths] { emit fileActionRequested(paths, FileAction::Move); });
#ifdef Q_OS_MACOS
    if (paths.size() == 1) {
        menu.addAction("Show in Finder", this, [this, path = paths.first()] {
            if (!QProcess::startDetached("/usr/bin/open", {"-R", path}))
                emit message("Could not show the shader in Finder.");
        });
    }
#endif
    menu.addSeparator();
    menu.addAction("Move to Trash…", this, [this, paths] { emit fileActionRequested(paths, FileAction::Trash); });
    menu.exec(tree->viewport()->mapToGlobal(position));
}

void MediaBin::setSource(const QString &path, const QByteArray &source) {
    const auto entry = entries.find(path);
    if (entry != entries.end()) entry->setSource(source);
}

void MediaBin::addFiles(const QStringList &files, const QString &group) {
    int added = 0, failed = 0;
    for (const auto &path : files) {
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (canonical.isEmpty()) { ++failed; continue; }
        if (entries.contains(canonical)) continue;
        const auto entry = ShaderEntry::read(canonical);
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
    emit message(QString("Imported %1 shader(s); %2 could not be read.").arg(added).arg(failed));
}

void MediaBin::forget(const QStringList &paths) {
    const QSignalBlocker blocker(tree);
    // Rebuilding is faster than removing rows one by one when most of the list goes.
    if (paths.size() >= entries.size() / 2) {
        tree->clear();
        rows.clear();
        groups.clear();
    }
    for (const auto &path : paths) {
        entries.remove(path);
        delete rows.take(path);
        entryGroups.remove(path);
        previewErrors.remove(path);
        if (active == path) active.clear();
    }
    refresh();
}

void MediaBin::clear() {
    const QSignalBlocker blocker(tree);
    tree->clear();
    rows.clear();
    groups.clear();
    entries.clear();
    entryGroups.clear();
    previewErrors.clear();
    active.clear();
    refresh();
}

QStringList MediaBin::filteredErrors() const {
    QStringList paths;
    for (const auto &entry : entries)
        if (binType(entry) == ISFType::Unknown && matches(entry)) paths << entry.path;
    return paths;
}

void MediaBin::setActive(const QString &path) {
    if (auto *previous = rows.value(active)) {
        previous->setData(0, AppStyle::ActiveRole, false);
        previous->setData(1, AppStyle::ActiveRole, false);
    }
    active = path;
    if (auto *item = rows.value(active)) {
        item->setData(0, AppStyle::ActiveRole, true);
        item->setData(1, AppStyle::ActiveRole, true);
    }
}

void MediaBin::revertCurrent() {
    const QSignalBlocker blocker(tree);
    tree->setCurrentItem(rows.value(active), 0, QItemSelectionModel::NoUpdate);
}

void MediaBin::selectFirstVisible() {
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        auto *item = *it;
        if (!item->isHidden() && !item->data(0, AppStyle::PathRole).toString().isEmpty()) {
            tree->setCurrentItem(item);
            return;
        }
    }
}

void MediaBin::requestSelection(const QString &path) {
    if (auto *row = rows.value(path); row && !row->isHidden()) tree->setCurrentItem(row);
}

void MediaBin::setPreviewError(const QString &path, const QString &status) {
    if (previewErrors.value(path) == status) return;
    // The errored shader leaves the current filter; park the keyboard cursor on the
    // row above it so Up/Down continues from this position instead of restarting.
    QTreeWidgetItem *anchor = nullptr;
    if (auto *row = rows.value(path); row && !row->isHidden() && tree->currentItem() == row)
        anchor = tree->itemAbove(row);
    previewErrors.insert(path, status);
    refresh();
    if (anchor && !anchor->isHidden() && rows.value(path) && rows.value(path)->isHidden()) {
        const QSignalBlocker blocker(tree);
        tree->setCurrentItem(anchor, 0, QItemSelectionModel::NoUpdate);
    }
}

bool MediaBin::clearPreviewError(const QString &path) {
    if (!previewErrors.remove(path)) return false;
    refresh();
    return true;
}

void MediaBin::restoreSession(const QSettings &settings) {
    const auto savedEntries = settings.value(SettingsKeys::BinEntries).toMap();
    for (auto it = savedEntries.cbegin(); it != savedEntries.cend(); ++it) {
        if (const auto entry = ShaderEntry::read(it.key())) {
            entries.insert(it.key(), *entry);
            entryGroups.insert(it.key(), it.value().toString());
        }
    }
    const auto disabledTypes = settings.value(SettingsKeys::BinDisabledTypes).toStringList();
    for (auto *tab : typeTabs) {
        const QSignalBlocker blocker(tab);
        tab->setChecked(!disabledTypes.contains(tab->objectName()));
    }
    const auto disabledCategories = settings.value(SettingsKeys::BinDisabledCategories).toStringList();
    uncheckedCategories = QSet<QString>(disabledCategories.cbegin(), disabledCategories.cend());
    {
        const QSignalBlocker blocker(search);
        search->setText(settings.value(SettingsKeys::BinNameFilter).toString());
    }
    refresh();
}

void MediaBin::saveSession(QSettings &settings) const {
    QVariantMap savedEntries;
    for (auto it = entryGroups.cbegin(); it != entryGroups.cend(); ++it) savedEntries.insert(it.key(), it.value());
    settings.setValue(SettingsKeys::BinEntries, savedEntries);
    QStringList disabledTypes;
    for (const auto *tab : typeTabs)
        if (!tab->isChecked()) disabledTypes << tab->objectName();
    settings.setValue(SettingsKeys::BinDisabledTypes, disabledTypes);
    settings.setValue(SettingsKeys::BinDisabledCategories, QStringList(uncheckedCategories.values()));
    settings.setValue(SettingsKeys::BinNameFilter, search->text());
}

ISFType MediaBin::binType(const ShaderEntry &entry) const {
    return previewErrors.contains(entry.path) ? ISFType::Unknown : entry.type();
}

bool MediaBin::matches(const ShaderEntry &entry) const {
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

void MediaBin::refresh() {
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
        item->setText(1, shaderTypeInfo(kind).badge);
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
        const auto &caption = shaderTypeInfo(kind).caption;
        it.value()->setText(QString("●  %1 %2").arg(shown == total ? QString::number(total) :
                            QString("%1/%2").arg(shown).arg(total)).arg(caption));
        it.value()->setToolTip(QString("%1 visible · %2 total").arg(shown).arg(total));
    }
    if (inserted) tree->sortItems(0, Qt::AscendingOrder);
    if (!active.isEmpty() && rows.contains(active) && !rows.value(active)->isHidden())
        tree->setCurrentItem(rows.value(active), 0, QItemSelectionModel::NoUpdate);
    count->setText(QString("%1 of %2 shaders").arg(visible).arg(entries.size()));
}
