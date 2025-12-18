#pragma once

#include <QSlider>

#include <vector>

struct TimelineKeyframeMark
{
    double seconds = 0.0;
    bool enabled = true;
};

class TimelineSlider final : public QSlider
{
    Q_OBJECT

public:
    explicit TimelineSlider(Qt::Orientation orientation, QWidget* parent = nullptr);

    void setFullRangeMs(int minMs, int maxMs);
    void ensureValueVisibleMs(int ms);
    void setSelectionMs(int startMs, int endMs);
    void setCropKeyframes(std::vector<TimelineKeyframeMark> marks);

signals:
    void jumped(int value);
    void requestSeekExact(double seconds);
    void requestDeleteKeyframe(double seconds);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    int valueFromPosition(const QPoint& pos) const;
    int xForValueMs(int ms, const QRect& groove, const QStyleOptionSlider& opt) const;
    int findKeyframeIndexNearX(int x, const QRect& groove, const QStyleOptionSlider& opt) const;

    int m_startMs = -1;
    int m_endMs = -1;
    std::vector<TimelineKeyframeMark> m_keyframes;

    int m_fullMinMs = 0;
    int m_fullMaxMs = 0;
    double m_zoomLevel = 1.0;
};
