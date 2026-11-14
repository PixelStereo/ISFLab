#include "HeaderInspector.h"
#include "ISFScanner.h"
#include "ShaderDocument.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

HeaderInspector::HeaderInspector(ShaderDocument *document, QWidget *parent)
    : QTabWidget(parent), document(document) {
    addTable("INPUTS", {"NAME", "TYPE", "LABEL", "DEFAULT", "IDENTITY", "MIN", "MAX", "VALUES", "LABELS"},
             createMetadata());
    addTable("PASSES", {"TARGET", "WIDTH", "HEIGHT", "PERSISTENT", "FLOAT"});
}

QWidget *HeaderInspector::createMetadata() {
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
            const auto &header = document->header();
            field->setEnabled(header.has_value() && document->isOpen());
            field->setText(header ? displayValue(*header) : QString());
        };
        syncField();
        connect(document, &ShaderDocument::changed, field, syncField);
        connect(field, &QLineEdit::editingFinished, this, [this, field, key, categories, displayValue] {
            if (!field->isModified()) return;
            auto header = document->header();
            if (!header || !document->isOpen() || field->text() == displayValue(*header)) return;
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
            document->writeHeader(*header);
        });
    }
    return metadata;
}

// Columns 0-2 hold text; for INPUTS, the remaining columns hold JSON values. For PASSES, columns 3 and
// later are checkboxes, and the last row is the output pass, which has no TARGET.
void HeaderInspector::addTable(const QString &key, const QStringList &columns, QWidget *basicInfo) {
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
    const auto readHeader = [this] { return document->header(); };
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
        document->writeHeader(header);
    };
    connect(document, &ShaderDocument::changed, page, syncTable);
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
            if (!passes && column == 1 && !ISFScanner::inputTypes().contains(input)) {
                syncTable();
                return;
            }
            object.insert(field, input);
        } else {
            const auto value = QJsonDocument::fromJson("[" + input.toUtf8() + "]").array();
            if (value.size() != 1) {
                emit message("Enter a valid JSON value.");
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
    addTab(page, passes ? "Passes" : "Inputs");
    syncTable();
}
