#pragma once
#include "Library.h"
#include <QMainWindow>
#include <QSet>

class QCheckBox;
class QPushButton;
class SliderField;
class QToolButton;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class ShaderPreview;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private:
    void importPaths(const QStringList &paths, bool autoPreview = true);
    void addFiles(const QStringList &paths, const QString &group = {});
    void refresh();
    void select(const QString &path);
    void applyPreview();
    void writeHeader(const QJsonObject &header);
    void savePreviewDefaults();
    bool save();
    bool resolveChanges();
    void controls(const QJsonObject &header);
    bool matches(const ShaderEntry &entry) const;
    void exportFiltered(bool zip = false);
    void organize();
    void report(const ExportReport &result, const QString &destination, bool zip = false);

    QMap<QString, ShaderEntry> entries;
    QString active, folder;
    QTreeWidget *tree;
    QMap<QString, QTreeWidgetItem *> rows, groups;
    QMap<QString, QString> entryGroups;
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
    QLabel *count, *previewStatus;
    QPlainTextEdit *editor;
    QPushButton *compilePreviewButton;
    QPushButton *transitionAnimationButton;
    SliderField *transitionSpeedField;
    QString previewedSource;
    QVBoxLayout *parameters;
    ShaderPreview *preview;
};
