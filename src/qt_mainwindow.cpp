#include "qt_mainwindow.h"

#include "app_settings.h"
#include "qt_options_dialog.h"
#include "qt_progress_dialog.h"
#include "qt_timeline_slider.h"
#include "qt_upload_dialog.h"
#include "utils.h"
#include "video_player.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QStyle>
#include <QDateTime>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

#include <functional>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace
{
class VideoHostWidget final : public QWidget
{
public:
    explicit VideoHostWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_DontCreateNativeAncestors);
        setAutoFillBackground(true);
    }

    std::function<void(int, int)> onResized;

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (onResized)
            onResized(width(), height());
    }
};

static int msFromSeconds(double s)
{
    if (s <= 0.0)
        return 0;
    return (int)(s * 1000.0);
}

static double secondsFromMs(int ms)
{
    if (ms <= 0)
        return 0.0;
    return ms / 1000.0;
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("Video Editor");
    setAcceptDrops(true);

    createUi();
    createPlayer();

    m_uiTimer = new QTimer(this);
    connect(m_uiTimer, &QTimer::timeout, this, &MainWindow::tickUi);
    m_uiTimer->start(50);

    refreshUiEnabledState();
}

MainWindow::~MainWindow()
{
    if (m_player)
        m_player->Pause();
}

void MainWindow::createUi()
{
    auto* tb = addToolBar("Main");
    tb->setMovable(false);

    auto* actOpen = tb->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "Open");
    connect(actOpen, &QAction::triggered, this, &MainWindow::openFileDialog);

    auto* actPlay = tb->addAction(style()->standardIcon(QStyle::SP_MediaPlay), "Play/Pause");
    connect(actPlay, &QAction::triggered, this, &MainWindow::togglePlayPause);

    auto* actStop = tb->addAction(style()->standardIcon(QStyle::SP_MediaStop), "Stop");
    connect(actStop, &QAction::triggered, this, &MainWindow::stopPlayback);

    tb->addSeparator();

    auto* actOptions = tb->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), "Options");
    connect(actOptions, &QAction::triggered, this, [this]() {
        OptionsDialog dlg(this);
        dlg.exec();
    });

    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(actOpen);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);

    auto* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction(actOptions);

    auto* splitter = new QSplitter(this);

    auto* left = new QWidget();
    auto* leftLayout = new QVBoxLayout();
    leftLayout->setContentsMargins(12, 12, 12, 12);
    leftLayout->setSpacing(10);

    auto* host = new VideoHostWidget();
    host->setMinimumSize(640, 360);
    host->setStyleSheet("background: #101214; border: 1px solid #2a2f36; border-radius: 10px;");
    m_videoHost = host;
    leftLayout->addWidget(host, 1);

    auto* timelineRow = new QHBoxLayout();
    m_timeline = new TimelineSlider(Qt::Horizontal);
    m_timeline->setRange(0, 0);
    m_timeline->setSingleStep(50);
    m_timeline->setPageStep(250);
    m_timeLabel = new QLabel("00:00 / 00:00");
    m_timeLabel->setMinimumWidth(140);
    m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    timelineRow->addWidget(m_timeline, 1);
    timelineRow->addWidget(m_timeLabel);
    leftLayout->addLayout(timelineRow);

    connect(m_timeline, &QSlider::sliderPressed, this, [this]() {
        if (!m_player)
            return;
        m_timelineWasPlaying = m_player->IsPlaying();
        if (m_timelineWasPlaying)
            m_player->Pause();
    });
    connect(m_timeline, &QSlider::sliderReleased, this, [this]() {
        seekToSeconds(secondsFromMs(m_timeline->value()), m_timelineWasPlaying);
        m_timelineWasPlaying = false;
    });
    connect(m_timeline, &QSlider::valueChanged, this, [this](int v) {
        if (!m_player || !m_player->IsLoaded())
            return;
        if (m_timeline->isSliderDown())
            m_timeLabel->setText(QString::fromWCharArray(FormatTime(secondsFromMs(v), true).c_str()) + " / " +
                                 QString::fromWCharArray(FormatTime(m_player->GetDuration(), true).c_str()));
    });
    connect(m_timeline, &TimelineSlider::jumped, this, [this](int v) {
        if (!m_player || !m_player->IsLoaded())
            return;
        bool wasPlaying = m_player->IsPlaying();
        if (wasPlaying)
            m_player->Pause();
        seekToSeconds(secondsFromMs(v), wasPlaying);
    });
    connect(m_timeline, &TimelineSlider::requestSeekExact, this, [this](double seconds) {
        if (!m_player || !m_player->IsLoaded())
            return;
        bool wasPlaying = m_player->IsPlaying();
        if (wasPlaying)
            m_player->Pause();
        m_player->SeekToTimeExact(seconds);
        if (wasPlaying)
            m_player->Play();
    });
    connect(m_timeline, &TimelineSlider::requestDeleteKeyframe, this, [this](double seconds) {
        if (!m_player || !m_player->IsLoaded())
            return;
        if (m_player->RemoveCropKeyframe(seconds))
        {
            m_player->UpdateCropForTime(m_player->GetCurrentTime());
            if (!m_player->IsPlaying())
                m_player->SeekToTimeExact(m_player->GetCurrentTime());
        }
    });

    left->setLayout(leftLayout);

    auto* right = new QTabWidget();
    right->setMinimumWidth(360);

    // Audio tab
    auto* audioTab = new QWidget();
    auto* audioLayout = new QVBoxLayout();
    audioLayout->setContentsMargins(12, 12, 12, 12);
    audioLayout->setSpacing(10);

    m_audioTracks = new QListWidget();
    m_audioTracks->setSelectionMode(QAbstractItemView::SingleSelection);
    audioLayout->addWidget(m_audioTracks, 1);

    m_trackMuted = new QCheckBox("Mute selected track");
    m_voiceIsolation = new QCheckBox("Voice isolation (RNNoise)");
    audioLayout->addWidget(m_trackMuted);
    audioLayout->addWidget(m_voiceIsolation);

    auto* trackVolBox = new QGroupBox("Track Volume");
    auto* trackVolLayout = new QVBoxLayout();
    m_trackVolume = new QSlider(Qt::Horizontal);
    m_trackVolume->setRange(0, 200);
    m_trackVolume->setValue(100);
    trackVolLayout->addWidget(m_trackVolume);
    trackVolBox->setLayout(trackVolLayout);
    audioLayout->addWidget(trackVolBox);

    auto* masterVolBox = new QGroupBox("Master Volume");
    auto* masterVolLayout = new QVBoxLayout();
    m_masterVolume = new QSlider(Qt::Horizontal);
    m_masterVolume->setRange(0, 200);
    m_masterVolume->setValue(100);
    masterVolLayout->addWidget(m_masterVolume);
    masterVolBox->setLayout(masterVolLayout);
    audioLayout->addWidget(masterVolBox);

    audioTab->setLayout(audioLayout);
    right->addTab(audioTab, "Audio");

    connect(m_audioTracks, &QListWidget::currentRowChanged, this, [this]() { refreshAudioSelection(); });
    connect(m_trackMuted, &QCheckBox::toggled, this, [this](bool muted) {
        if (!m_player)
            return;
        int idx = m_audioTracks->currentRow();
        if (idx >= 0)
            m_player->SetAudioTrackMuted(idx, muted);
    });
    connect(m_voiceIsolation, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!m_player)
            return;
        int idx = m_audioTracks->currentRow();
        if (idx >= 0)
            m_player->SetVoiceIsolationEnabled(idx, enabled);
    });
    connect(m_trackVolume, &QSlider::valueChanged, this, [this](int v) {
        if (!m_player)
            return;
        int idx = m_audioTracks->currentRow();
        if (idx < 0)
            return;
        m_player->SetAudioTrackVolume(idx, v / 100.0f);
    });
    connect(m_masterVolume, &QSlider::valueChanged, this, [this](int v) {
        if (!m_player)
            return;
        m_player->SetMasterVolume(v / 100.0f);
    });

    // Export tab
    auto* exportTab = new QWidget();
    auto* exportLayout = new QVBoxLayout();
    exportLayout->setContentsMargins(12, 12, 12, 12);
    exportLayout->setSpacing(10);

    auto* markersBox = new QGroupBox("Selection");
    auto* markersForm = new QFormLayout();
    m_startEdit = new QLineEdit();
    m_endEdit = new QLineEdit();
    m_startEdit->setPlaceholderText("mm:ss or hh:mm:ss");
    m_endEdit->setPlaceholderText("mm:ss or hh:mm:ss");
    markersForm->addRow("Start:", m_startEdit);
    markersForm->addRow("End:", m_endEdit);
    markersBox->setLayout(markersForm);
    exportLayout->addWidget(markersBox);

    auto* markerButtons = new QHBoxLayout();
    auto* btnSetStart = new QPushButton("Set Start");
    auto* btnSetEnd = new QPushButton("Set End");
    auto* btnPlaySel = new QPushButton("Play Selection");
    markerButtons->addWidget(btnSetStart);
    markerButtons->addWidget(btnSetEnd);
    markerButtons->addWidget(btnPlaySel);
    exportLayout->addLayout(markerButtons);

    connect(btnSetStart, &QPushButton::clicked, this, &MainWindow::setStartMarker);
    connect(btnSetEnd, &QPushButton::clicked, this, &MainWindow::setEndMarker);
    connect(btnPlaySel, &QPushButton::clicked, this, &MainWindow::playSelection);

    connect(m_startEdit, &QLineEdit::editingFinished, this, &MainWindow::syncStateFromTimeEdits);
    connect(m_endEdit, &QLineEdit::editingFinished, this, &MainWindow::syncStateFromTimeEdits);

    m_mergeAudio = new QCheckBox("Merge audio tracks");
    exportLayout->addWidget(m_mergeAudio);

    auto* codecBox = new QGroupBox("Video");
    auto* codecLayout = new QVBoxLayout();
    m_copyCodec = new QRadioButton("Copy codec (fast, no re-encode)");
    m_h264 = new QRadioButton("Convert to H.264 (supports crop/size target)");
    m_copyCodec->setChecked(true);
    codecLayout->addWidget(m_copyCodec);
    codecLayout->addWidget(m_h264);
    codecBox->setLayout(codecLayout);
    exportLayout->addWidget(codecBox);

    auto* rateBox = new QGroupBox("Bitrate / Size");
    auto* rateForm = new QFormLayout();
    m_useBitrate = new QRadioButton("Use bitrate (kbps)");
    m_useSize = new QRadioButton("Target size (MB)");
    m_useBitrate->setChecked(true);
    m_bitrateEdit = new QLineEdit("6000");
    m_targetSizeEdit = new QLineEdit("25");
    rateForm->addRow(m_useBitrate, m_bitrateEdit);
    rateForm->addRow(m_useSize, m_targetSizeEdit);
    rateBox->setLayout(rateForm);
    exportLayout->addWidget(rateBox);

    auto* btnExport = new QPushButton("Export…");
    btnExport->setDefault(true);
    connect(btnExport, &QPushButton::clicked, this, &MainWindow::exportCurrent);
    exportLayout->addWidget(btnExport);
    exportLayout->addStretch(1);

    exportTab->setLayout(exportLayout);
    right->addTab(exportTab, "Export");

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    setCentralWidget(splitter);
    statusBar()->showMessage("Ready");

    auto* actPlayPause = new QAction(this);
    actPlayPause->setShortcut(QKeySequence(Qt::Key_Space));
    actPlayPause->setShortcutContext(Qt::ApplicationShortcut);
    connect(actPlayPause, &QAction::triggered, this, &MainWindow::shortcutTogglePlayPause);
    addAction(actPlayPause);

    auto* actPlayPauseK = new QAction(this);
    actPlayPauseK->setShortcut(QKeySequence(Qt::Key_K));
    actPlayPauseK->setShortcutContext(Qt::ApplicationShortcut);
    connect(actPlayPauseK, &QAction::triggered, this, &MainWindow::shortcutTogglePlayPause);
    addAction(actPlayPauseK);

    auto* actLeft = new QAction(this);
    actLeft->setShortcut(QKeySequence(Qt::Key_Left));
    actLeft->setShortcutContext(Qt::ApplicationShortcut);
    connect(actLeft, &QAction::triggered, this, [this]() { shortcutSeekBy(-5.0); });
    addAction(actLeft);

    auto* actRight = new QAction(this);
    actRight->setShortcut(QKeySequence(Qt::Key_Right));
    actRight->setShortcutContext(Qt::ApplicationShortcut);
    connect(actRight, &QAction::triggered, this, [this]() { shortcutSeekBy(5.0); });
    addAction(actRight);

    auto* actJ = new QAction(this);
    actJ->setShortcut(QKeySequence(Qt::Key_J));
    actJ->setShortcutContext(Qt::ApplicationShortcut);
    connect(actJ, &QAction::triggered, this, [this]() { shortcutSeekBy(-10.0); });
    addAction(actJ);

    auto* actL = new QAction(this);
    actL->setShortcut(QKeySequence(Qt::Key_L));
    actL->setShortcutContext(Qt::ApplicationShortcut);
    connect(actL, &QAction::triggered, this, [this]() { shortcutSeekBy(10.0); });
    addAction(actL);

    auto* actComma = new QAction(this);
    actComma->setShortcut(QKeySequence(Qt::Key_Comma));
    actComma->setShortcutContext(Qt::ApplicationShortcut);
    connect(actComma, &QAction::triggered, this, [this]() { shortcutStepFrame(-1); });
    addAction(actComma);

    auto* actDot = new QAction(this);
    actDot->setShortcut(QKeySequence(Qt::Key_Period));
    actDot->setShortcutContext(Qt::ApplicationShortcut);
    connect(actDot, &QAction::triggered, this, [this]() { shortcutStepFrame(1); });
    addAction(actDot);
}

bool MainWindow::isTextInputFocused() const
{
    QWidget* w = QApplication::focusWidget();
    return qobject_cast<QLineEdit*>(w) != nullptr;
}

void MainWindow::shortcutTogglePlayPause()
{
    if (isTextInputFocused())
        return;
    togglePlayPause();
}

void MainWindow::shortcutSeekBy(double deltaSeconds)
{
    if (isTextInputFocused())
        return;
    if (!m_player || !m_player->IsLoaded())
        return;
    const double dur = m_player->GetDuration();
    const double cur = m_player->GetCurrentTime();
    const double next = std::clamp(cur + deltaSeconds, 0.0, dur > 0.0 ? dur : cur + deltaSeconds);
    const bool wasPlaying = m_player->IsPlaying();
    if (wasPlaying)
        m_player->Pause();
    m_player->SeekToTime(next, 0);
    if (wasPlaying)
        m_player->Play();
}

void MainWindow::shortcutStepFrame(int deltaFrames)
{
    if (isTextInputFocused())
        return;
    if (!m_player || !m_player->IsLoaded())
        return;
    const bool wasPlaying = m_player->IsPlaying();
    if (wasPlaying)
        m_player->Pause();
    int64_t frame = m_player->GetCurrentFrame() + deltaFrames;
    if (frame < 0)
        frame = 0;
    const int64_t total = m_player->GetTotalFrames();
    if (total > 0 && frame >= total)
        frame = total - 1;
    m_player->SeekToFrame(frame);
    if (wasPlaying)
        m_player->Play();
}

void MainWindow::createPlayer()
{
    auto* host = static_cast<VideoHostWidget*>(m_videoHost);

    // Ensure a native HWND exists
    WId win = host->winId();
    m_player = std::make_unique<VideoPlayer>((HWND)win);

    host->onResized = [this](int w, int h) {
        if (m_player)
            m_player->SetPosition(0, 0, w, h);
    };

    // Initial size
    m_player->SetPosition(0, 0, host->width(), host->height());
}

void MainWindow::openFileDialog()
{
    const QString path = QFileDialog::getOpenFileName(this, "Open video", QString(), "Video files (*.mp4 *.mkv *.mov *.webm);;All files (*.*)");
    if (!path.isEmpty())
        loadVideoFile(path);
}

void MainWindow::loadVideoFile(const QString& path)
{
    if (!m_player)
        return;

    stopPlayback();

    const std::wstring wpath = path.toStdWString();
    if (!m_player->LoadVideo(wpath))
    {
        QMessageBox::critical(this, "Error", "Failed to load the video file. Check FFmpeg setup and file format.");
        refreshUiEnabledState();
        return;
    }

    m_cutStartTime = -1.0;
    m_cutEndTime = -1.0;
    syncTimeEditsFromState();

    m_timeline->setFullRangeMs(0, msFromSeconds(m_player->GetDuration()));
    m_timeline->setValue(0);
    m_timeline->setSelectionMs(-1, -1);
    m_timeline->setCropKeyframes({});

    refreshAudioTracks();
    refreshUiEnabledState();

    statusBar()->showMessage("Loaded: " + QFileInfo(path).fileName());
    if (g_autoPlay)
        m_player->Play();
}

void MainWindow::togglePlayPause()
{
    if (!m_player || !m_player->IsLoaded())
        return;
    if (m_player->IsPlaying())
        m_player->Pause();
    else
        m_player->Play();
}

void MainWindow::stopPlayback()
{
    if (!m_player)
        return;
    m_player->Stop();
}

void MainWindow::seekToSeconds(double seconds, bool resumeIfPlaying)
{
    if (!m_player || !m_player->IsLoaded())
        return;
    m_player->SeekToTime(seconds);
    if (resumeIfPlaying)
        m_player->Play();
}

void MainWindow::refreshAudioTracks()
{
    m_audioTracks->clear();
    if (!m_player || !m_player->IsLoaded())
        return;

    for (int i = 0; i < m_player->GetAudioTrackCount(); ++i)
        m_audioTracks->addItem(QString::fromUtf8(m_player->GetAudioTrackName(i).c_str()));

    if (m_player->GetAudioTrackCount() > 0)
        m_audioTracks->setCurrentRow(0);
    refreshAudioSelection();
}

void MainWindow::refreshAudioSelection()
{
    if (!m_player || !m_player->IsLoaded())
        return;
    int idx = m_audioTracks->currentRow();
    const bool has = idx >= 0 && idx < m_player->GetAudioTrackCount();
    m_trackMuted->setEnabled(has);
    m_voiceIsolation->setEnabled(has);
    m_trackVolume->setEnabled(has);
    if (!has)
        return;

    m_trackMuted->setChecked(m_player->IsAudioTrackMuted(idx));
    m_voiceIsolation->setChecked(m_player->IsVoiceIsolationEnabled(idx));
    m_trackVolume->setValue((int)(m_player->GetAudioTrackVolume(idx) * 100.0f));
}

void MainWindow::refreshUiEnabledState()
{
    const bool loaded = m_player && m_player->IsLoaded();
    m_timeline->setEnabled(loaded);
}

void MainWindow::tickUi()
{
    if (!m_player || !m_player->IsLoaded() || m_timeline->isSliderDown())
    {
        if (!m_player || !m_player->IsLoaded())
            m_timeLabel->setText("00:00 / 00:00");
        return;
    }

    const double cur = m_player->GetCurrentTime();
    const double dur = m_player->GetDuration();
    const int ms = msFromSeconds(cur);

    // Keep the playhead visible while zoomed/panned.
    m_timeline->ensureValueVisibleMs(ms);

    m_timeline->blockSignals(true);
    m_timeline->setValue(ms);
    m_timeline->blockSignals(false);

    m_timeLabel->setText(QString::fromWCharArray(FormatTime(cur, true).c_str()) + " / " +
                         QString::fromWCharArray(FormatTime(dur, true).c_str()));

    // Update timeline marks (crop keyframes + selection).
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastKeyframeUiUpdateMs > 200)
    {
        std::vector<TimelineKeyframeMark> marks;
        for (const auto& k : m_player->GetCropKeyframes())
        {
            marks.push_back(TimelineKeyframeMark{k.time, k.enabled});
        }
        m_timeline->setCropKeyframes(std::move(marks));

        const int startMs = m_cutStartTime >= 0.0 ? msFromSeconds(m_cutStartTime) : -1;
        const int endMs = m_cutEndTime >= 0.0 ? msFromSeconds(m_cutEndTime) : -1;
        m_timeline->setSelectionMs(startMs, endMs);

        m_lastKeyframeUiUpdateMs = now;
    }
}

bool MainWindow::hasValidSelection() const
{
    return m_cutStartTime >= 0.0 && m_cutEndTime > m_cutStartTime;
}

void MainWindow::syncTimeEditsFromState()
{
    m_startEdit->setText(m_cutStartTime < 0 ? "" : QString::fromWCharArray(FormatTime(m_cutStartTime, true).c_str()));
    m_endEdit->setText(m_cutEndTime < 0 ? "" : QString::fromWCharArray(FormatTime(m_cutEndTime, true).c_str()));
}

void MainWindow::syncStateFromTimeEdits()
{
    const auto startW = m_startEdit->text().trimmed().toStdWString();
    const auto endW = m_endEdit->text().trimmed().toStdWString();
    const double start = startW.empty() ? -1.0 : ParseTimeString(startW);
    const double end = endW.empty() ? -1.0 : ParseTimeString(endW);

    if (!startW.empty() && start < 0)
    {
        QMessageBox::warning(this, "Invalid time", "Start time format should be mm:ss or hh:mm:ss(.ms).");
        return;
    }
    if (!endW.empty() && end < 0)
    {
        QMessageBox::warning(this, "Invalid time", "End time format should be mm:ss or hh:mm:ss(.ms).");
        return;
    }
    if (start >= 0 && end >= 0 && end <= start)
    {
        QMessageBox::warning(this, "Invalid range", "End must be greater than start.");
        return;
    }

    m_cutStartTime = start;
    m_cutEndTime = end;
}

void MainWindow::setStartMarker()
{
    if (!m_player || !m_player->IsLoaded())
        return;
    m_cutStartTime = m_player->GetCurrentTime();
    if (m_cutEndTime >= 0 && m_cutEndTime <= m_cutStartTime)
        m_cutEndTime = -1.0;
    syncTimeEditsFromState();
    m_timeline->setSelectionMs(msFromSeconds(m_cutStartTime), m_cutEndTime < 0 ? -1 : msFromSeconds(m_cutEndTime));
}

void MainWindow::setEndMarker()
{
    if (!m_player || !m_player->IsLoaded())
        return;
    const double t = m_player->GetCurrentTime();
    if (m_cutStartTime >= 0 && t <= m_cutStartTime)
    {
        QMessageBox::warning(this, "Invalid time", "End point must be after the start point.");
        return;
    }
    m_cutEndTime = t;
    syncTimeEditsFromState();
    m_timeline->setSelectionMs(m_cutStartTime < 0 ? -1 : msFromSeconds(m_cutStartTime), msFromSeconds(m_cutEndTime));
}

void MainWindow::playSelection()
{
    if (!m_player || !m_player->IsLoaded() || !hasValidSelection())
    {
        QMessageBox::warning(this, "Selection required", "Set a valid start and end first.");
        return;
    }
    m_player->PlayClip(m_cutStartTime, m_cutEndTime);
}

void MainWindow::exportCurrent()
{
    if (!m_player || !m_player->IsLoaded())
        return;

    syncStateFromTimeEdits();

    const QString outPath = QFileDialog::getSaveFileName(this, "Export video", QString(), "MP4 Video (*.mp4);;All files (*.*)");
    if (outPath.isEmpty())
        return;

    const bool mergeAudio = m_mergeAudio->isChecked();
    const bool convertH264 = m_h264->isChecked();
    const bool useSize = m_useSize->isChecked();
    int bitrateKbps = m_bitrateEdit->text().trimmed().toInt();
    int targetSizeMb = m_targetSizeEdit->text().trimmed().toInt();

    double startTime = 0.0;
    double endTime = m_player->GetDuration();
    if (hasValidSelection())
    {
        startTime = m_cutStartTime;
        endTime = m_cutEndTime;
    }

    if (convertH264 && useSize && targetSizeMb > 0)
    {
        const double duration = endTime - startTime;
        if (duration > 0.01)
        {
            // Total size budget in kilobits: MB * 8192
            const int totalKbps = (int)((targetSizeMb * 8192.0) / duration);
            const int audioKbps = mergeAudio ? 128 : 256; // coarse fallback estimate
            bitrateKbps = totalKbps > audioKbps ? (totalKbps - audioKbps) : (std::max)(200, totalKbps / 2);
        }
    }

    stopPlayback();

    std::atomic<bool> cancel(false);
    ProgressDialog dlg(&cancel, this);
    dlg.setStatusText("Exporting…");
    dlg.setProgressValue(0);

    const std::wstring outW = outPath.toStdWString();

    std::thread worker([this, &dlg, &cancel, outW, startTime, endTime, mergeAudio, convertH264, bitrateKbps]() {
        auto progress = [&dlg](int pct) {
            QMetaObject::invokeMethod(&dlg, [&dlg, pct]() { dlg.setProgressValue(pct); });
        };

        bool ok = m_player->CutVideo(outW, startTime, endTime, mergeAudio, convertH264, g_encoderSelection, bitrateKbps, nullptr,
                                     &cancel, progress);
        if (!ok || cancel.load())
        {
            QMetaObject::invokeMethod(&dlg, [&dlg]() { dlg.reject(); });
            return;
        }

        QMetaObject::invokeMethod(&dlg, [&dlg]() { dlg.accept(); });
    });

    worker.detach();

    const int result = dlg.exec();
    if (result != QDialog::Accepted)
    {
        statusBar()->showMessage("Export canceled/failed");
        return;
    }

    statusBar()->showMessage("Export complete");
    if ((g_useCatbox || g_useB2) && (g_autoUpload || QMessageBox::question(this, "Upload", "Upload the exported file now?") == QMessageBox::Yes))
    {
        UploadDialog uploadDlg(outW, g_useCatbox, g_useB2, g_autoUpload, this);
        uploadDlg.exec();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;
    const QString path = urls.first().toLocalFile();
    if (!path.isEmpty())
        loadVideoFile(path);
}
