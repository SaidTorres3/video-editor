#pragma once

#include <QMainWindow>

#include <memory>

class QListWidget;
class QLineEdit;
class QSlider;
class QLabel;
class QCheckBox;
class QRadioButton;
class QTimer;
class QWidget;

class VideoPlayer;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void loadVideoFile(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void createUi();
    void createPlayer();
    void refreshUiEnabledState();
    void refreshAudioTracks();
    void refreshAudioSelection();
    void tickUi();

    void openFileDialog();
    void togglePlayPause();
    void stopPlayback();
    void seekToSeconds(double seconds, bool resumeIfPlaying);

    void setStartMarker();
    void setEndMarker();
    void playSelection();
    void exportCurrent();

    bool hasValidSelection() const;
    void syncTimeEditsFromState();
    void syncStateFromTimeEdits();

private:
    QWidget* m_videoHost = nullptr;
    QSlider* m_timeline = nullptr;
    QLabel* m_timeLabel = nullptr;

    QListWidget* m_audioTracks = nullptr;
    QCheckBox* m_trackMuted = nullptr;
    QCheckBox* m_voiceIsolation = nullptr;
    QSlider* m_trackVolume = nullptr;
    QSlider* m_masterVolume = nullptr;

    QLineEdit* m_startEdit = nullptr;
    QLineEdit* m_endEdit = nullptr;
    QCheckBox* m_mergeAudio = nullptr;

    QRadioButton* m_copyCodec = nullptr;
    QRadioButton* m_h264 = nullptr;
    QRadioButton* m_useBitrate = nullptr;
    QRadioButton* m_useSize = nullptr;
    QLineEdit* m_bitrateEdit = nullptr;
    QLineEdit* m_targetSizeEdit = nullptr;

    QTimer* m_uiTimer = nullptr;

    std::unique_ptr<VideoPlayer> m_player;
    double m_cutStartTime = -1.0;
    double m_cutEndTime = -1.0;
    bool m_timelineWasPlaying = false;
};

