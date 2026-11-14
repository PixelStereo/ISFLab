#include "ParameterPanel.h"
#include "ColorPicker.h"
#include "Library.h"
#include "PointPad.h"
#include "SettingsKeys.h"
#include "ShaderPreview.h"
#include "SliderField.h"
#include "UiHelpers.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <algorithm>
#include <limits>

namespace {
// Asks for a Syphon or Spout sender (macOS, Windows) or a GStreamer shared memory socket (Linux).
std::unique_ptr<LiveInput> chooseLiveInput(QWidget *parent, const QString &liveName) {
    QString sender;
    QSize size;
    bool accepted = false;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    auto discovery = createLiveInput({});
    const auto discoveryError = discovery->error();
    if (!discoveryError.isEmpty()) { QMessageBox::information(parent, liveName, discoveryError); return nullptr; }
    const auto senders = discovery->senders();
    if (senders.isEmpty()) {
        QMessageBox::information(parent, liveName, "No sender was found. Start a sender and try again.");
        return nullptr;
    }
    QStringList labels;
    for (int i = 0; i < senders.size(); ++i)
        labels << QString("%1. %2").arg(i + 1).arg(senders.at(i).label);
    const auto selected = QInputDialog::getItem(parent, liveName, "Sender:", labels, 0, false, &accepted);
    if (!accepted) return nullptr;
    const auto index = labels.indexOf(selected);
    if (index < 0) return nullptr;
    sender = senders.at(index).id;
#else
    sender = QInputDialog::getText(parent, liveName, "Socket path (RGBA video):",
                                  QLineEdit::Normal, "/tmp/isflab-video", &accepted).trimmed();
    if (!accepted || sender.isEmpty()) return nullptr;
    QDialog dimensions(parent);
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
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dimensions, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dimensions, &QDialog::reject);
    if (dimensions.exec() != QDialog::Accepted) return nullptr;
    size = QSize(int(width->value()), int(height->value()));
#endif
    return createLiveInput(sender, size);
}
}

ParameterPanel::ParameterPanel(ShaderPreview *preview, QPushButton *transitionAnimation, QWidget *parent)
    : QWidget(parent), preview(preview), transitionAnimationButton(transitionAnimation),
      parameters(new QVBoxLayout(this)) {}

void ParameterPanel::rebuild(const QJsonObject &header, const QString &path) {
    shaderPath = path;
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
            const auto path = QFileInfo(shaderPath).dir().filePath(relative);
            auto *row = new QWidget;
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 2, 0, 2);
            QImage image;
            QSize dimensions;
            if (!relative.isEmpty() && QDir::isRelativePath(relative) && !shaderPath.isEmpty()) {
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
        } else if (kind == "floatRange") {
            const auto initial = value.toArray();
            const double minimum = input.value("MIN").toDouble(0.);
            const double maximum = qMax(minimum, input.value("MAX").toDouble(1.));
            auto *lower = new SliderField;
            auto *upper = new SliderField;
            for (auto *field : {lower, upper}) {
                field->setDecimals(4);
                field->setRange(minimum, maximum);
                field->setTypedRange(minimum, maximum);
            }
            lower->setValue(at(initial, 0).toDouble(minimum));
            upper->setValue(qMax(lower->value(), at(initial, 1).toDouble(maximum)));
            lower->setAccessibleName("Range start");
            upper->setAccessibleName("Range end");
            parameters->addWidget(lower);
            parameters->addWidget(upper);
            const auto updateRange = [=] {
                preview->setValue(name, QVariant::fromValue(QVector2D(lower->value(), upper->value())));
            };
            connect(lower, &SliderField::valueEdited, lower, [=](double v) {
                if (v > upper->value()) upper->setValue(v);
                updateRange();
            });
            connect(upper, &SliderField::valueEdited, upper, [=](double v) {
                if (v < lower->value()) lower->setValue(v);
                updateRange();
            });
            updateRange();
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
                trackMouse->setChecked(QSettings().value(SettingsKeys::PreviewTrackMouse, false).toBool());
                preview->setMouseTracking(trackMouse->isChecked());
                connect(trackMouse, &QCheckBox::toggled, row, [this](bool enabled) {
                    QSettings().setValue(SettingsKeys::PreviewTrackMouse, enabled);
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
            picker->setSettingsKey(SettingsKeys::PreviewColorPicker);
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
                const auto path = QFileDialog::getOpenFileName(this, "Choose image", browseFolder ? browseFolder() : QString());
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
                const auto path = QFileDialog::getOpenFileName(this, "Choose ISF generator", browseFolder ? browseFolder() : QString(), ISFScanner::shaderFileFilter());
                if (path.isEmpty()) return;
                const auto shader = ShaderEntry::read(path);
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
                    auto input = chooseLiveInput(this, liveName);
                    if (!input) return;
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
            auto *source = plainLabel(preview->audioStatus());
            source->setWordWrap(true);
            connect(preview, &ShaderPreview::audioStatusChanged, source, &QLabel::setText);
            parameters->addWidget(source);
        }
    }
    const bool hasDefaults = std::any_of(inputs.begin(), inputs.end(), [](const QJsonValue &entry) {
        const auto type = entry.toObject().value("TYPE").toString();
        return ISFScanner::defaultableInputTypes().contains(type);
    });
    auto *saveDefaults = new QPushButton("Save as Default");
    saveDefaults->setEnabled(hasDefaults);
    saveDefaults->setToolTip("Save the current parameter values as defaults in the shader file.");
    connect(saveDefaults, &QPushButton::clicked, this, &ParameterPanel::saveDefaultsRequested);
    parameters->addWidget(saveDefaults);
    parameters->addStretch();
}
