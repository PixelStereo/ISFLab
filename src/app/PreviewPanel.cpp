#include "PreviewPanel.h"
#include "SettingsKeys.h"
#include "ShaderPreview.h"
#include "SliderField.h"
#include "UiHelpers.h"
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

PreviewPanel::PreviewPanel(QWidget *parent) : QWidget(parent) {
    auto *viewerLayout = new QVBoxLayout(this);
    preview = new ShaderPreview;
    preview->setMinimumSize(320, 180);
    auto *animationBar = new QHBoxLayout;
    transitionButton = new QPushButton("Animate progress");
    transitionButton->setCheckable(true);
    transitionButton->setChecked(true);
    transitionButton->setToolTip("Loop from 0 to 1 and back in two seconds each way, with a one-second hold at each end.");
    connect(transitionButton, &QPushButton::toggled, this, [this](bool running) {
        if (!transitionButton->isEnabled()) return;
        preview->setTransitionAnimation(running);
    });
    animationBar->addWidget(transitionButton);
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
        QSettings().setValue(SettingsKeys::PreviewResolution, size);
        autoResolution->setEnabled(!size.isEmpty());
        showResolution(preview->renderSize());
    };
    const QSize defaultResolution(1280, 720);
    const auto storedResolution = QSettings().value(SettingsKeys::PreviewResolution, defaultResolution).toSize();
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
    auto *audioButton = new QToolButton;
    audioButton->setText("Audio");
    audioButton->setPopupMode(QToolButton::InstantPopup);
    audioButton->setToolTip("Audio source for every audio input: live, off, or a looped file.");
    auto *audioMenu = new QMenu(audioButton);
    auto *audioGroup = new QActionGroup(audioMenu);
    auto *audioLive = audioMenu->addAction("Live input");
    auto *audioOff = audioMenu->addAction("Off");
    auto *audioFileAction = audioMenu->addAction("Loop audio file…");
    for (auto *action : {audioLive, audioOff, audioFileAction}) { action->setCheckable(true); audioGroup->addAction(action); }
    audioButton->setMenu(audioMenu);
    const auto applyAudio = [this, audioButton, audioLive, audioOff, audioFileAction](ShaderPreview::AudioMode mode, const QString &file) {
        QSettings settings;
        settings.setValue(SettingsKeys::AudioMode, int(mode));
        settings.setValue(SettingsKeys::AudioFile, file);
        (mode == ShaderPreview::AudioMode::Live ? audioLive : mode == ShaderPreview::AudioMode::Off ? audioOff : audioFileAction)->setChecked(true);
        audioButton->setText(mode == ShaderPreview::AudioMode::Live ? "Audio: Live" :
                             mode == ShaderPreview::AudioMode::Off ? "Audio: Off" : "Audio: " + QFileInfo(file).fileName());
        preview->setAudioMode(mode, file);
    };
    connect(audioLive, &QAction::triggered, this, [=] { applyAudio(ShaderPreview::AudioMode::Live, {}); });
    connect(audioOff, &QAction::triggered, this, [=] { applyAudio(ShaderPreview::AudioMode::Off, {}); });
    connect(audioFileAction, &QAction::triggered, this, [=] {
        const auto previous = QSettings().value(SettingsKeys::AudioFile).toString();
        const auto file = QFileDialog::getOpenFileName(this, "Audio file to loop", previous,
                                                       "Audio (*.wav *.mp3 *.flac *.ogg *.m4a *.aac *.aif *.aiff);;All files (*)");
        if (file.isEmpty()) {
            const auto mode = QSettings().value(SettingsKeys::AudioMode, 0).toInt();
            (mode == 1 ? audioOff : mode == 2 ? audioFileAction : audioLive)->setChecked(true);
            return;
        }
        applyAudio(ShaderPreview::AudioMode::File, file);
    });
    {
        const QSettings settings;
        const auto mode = ShaderPreview::AudioMode(qBound(0, settings.value(SettingsKeys::AudioMode, 0).toInt(), 2));
        const auto file = settings.value(SettingsKeys::AudioFile).toString();
        applyAudio(mode == ShaderPreview::AudioMode::File && file.isEmpty() ? ShaderPreview::AudioMode::Live : mode, file);
    }
    transport->addWidget(playing); transport->addWidget(reset); transport->addWidget(audioButton); transport->addWidget(timeline, 1);
    viewerLayout->addLayout(transport);
    status = plainLabel("Select a shader to preview.");
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    viewerLayout->addWidget(status);
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
}

void PreviewPanel::setStatusText(const QString &text) { status->setText(text); }
