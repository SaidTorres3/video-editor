#include "qt_timeline_slider.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

TimelineSlider::TimelineSlider(Qt::Orientation orientation, QWidget* parent) : QSlider(orientation, parent)
{
    setMouseTracking(true);
}

void TimelineSlider::setFullRangeMs(int minMs, int maxMs)
{
    m_fullMinMs = std::max(0, minMs);
    m_fullMaxMs = std::max(m_fullMinMs, maxMs);
    m_zoomLevel = 1.0;
    setRange(m_fullMinMs, m_fullMaxMs);
}

void TimelineSlider::ensureValueVisibleMs(int ms)
{
    if (m_fullMaxMs <= m_fullMinMs)
        return;
    if (ms >= minimum() && ms <= maximum())
        return;
    const int viewRange = std::max(1, maximum() - minimum());
    const int fullRange = std::max(1, m_fullMaxMs - m_fullMinMs);
    if (viewRange >= fullRange)
        return;

    int newMin = ms - viewRange / 2;
    newMin = std::clamp(newMin, m_fullMinMs, m_fullMaxMs - viewRange);
    int newMax = newMin + viewRange;
    setRange(newMin, newMax);
    setValue(std::clamp(value(), newMin, newMax));
}

void TimelineSlider::setSelectionMs(int startMs, int endMs)
{
    m_startMs = startMs;
    m_endMs = endMs;
    update();
}

void TimelineSlider::setCropKeyframes(std::vector<TimelineKeyframeMark> marks)
{
    m_keyframes = std::move(marks);
    update();
}

int TimelineSlider::valueFromPosition(const QPoint& pos) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);

    QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    if (!groove.isValid())
        return value();

    const int minV = minimum();
    const int maxV = maximum();
    if (minV >= maxV)
        return minV;

    if (orientation() == Qt::Horizontal)
    {
        const int x = std::clamp(pos.x(), groove.left(), groove.right());
        const int sliderPos = x - groove.left();
        return QStyle::sliderValueFromPosition(minV, maxV, sliderPos, groove.width(), opt.upsideDown);
    }

    const int y = std::clamp(pos.y(), groove.top(), groove.bottom());
    const int sliderPos = y - groove.top();
    return QStyle::sliderValueFromPosition(minV, maxV, sliderPos, groove.height(), opt.upsideDown);
}

int TimelineSlider::xForValueMs(int ms, const QRect& groove, const QStyleOptionSlider& opt) const
{
    const int minV = minimum();
    const int maxV = maximum();
    if (minV >= maxV)
        return groove.left();
    ms = std::clamp(ms, minV, maxV);
    int pos = QStyle::sliderPositionFromValue(minV, maxV, ms, groove.width(), opt.upsideDown);
    return groove.left() + pos;
}

int TimelineSlider::findKeyframeIndexNearX(int x, const QRect& groove, const QStyleOptionSlider& opt) const
{
    int bestIndex = -1;
    int bestDist = 999999;
    for (int i = 0; i < (int)m_keyframes.size(); ++i)
    {
        const int ms = (int)std::llround(m_keyframes[i].seconds * 1000.0);
        const int kx = xForValueMs(ms, groove, opt);
        const int dist = std::abs(kx - x);
        if (dist < bestDist)
        {
            bestDist = dist;
            bestIndex = i;
        }
    }
    // Pixel threshold similar to the old timeline control.
    if (bestDist <= 6)
        return bestIndex;
    return -1;
}

void TimelineSlider::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return QSlider::mousePressEvent(event);

    QStyleOptionSlider opt;
    initStyleOption(&opt);
    QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
    if (handle.contains(event->pos()))
        return QSlider::mousePressEvent(event);

    const int newValue = valueFromPosition(event->pos());
    setValue(newValue);
    emit jumped(newValue);
    event->accept();
}

void TimelineSlider::contextMenuEvent(QContextMenuEvent* event)
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    if (!groove.isValid() || minimum() >= maximum())
        return;

    const int x = std::clamp(event->pos().x(), groove.left(), groove.right());
    const int idx = findKeyframeIndexNearX(x, groove, opt);
    if (idx < 0)
        return;

    const double seconds = m_keyframes[idx].seconds;

    QMenu menu(this);
    QAction* actEdit = menu.addAction("Edit keyframe (seek)");
    QAction* actDelete = menu.addAction("Delete keyframe");

    QAction* chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;
    if (chosen == actEdit)
        emit requestSeekExact(seconds);
    else if (chosen == actDelete)
        emit requestDeleteKeyframe(seconds);
}

void TimelineSlider::wheelEvent(QWheelEvent* event)
{
    if (m_fullMaxMs <= m_fullMinMs)
        return QSlider::wheelEvent(event);

    // Match the old behavior: wheel zooms, anchored at mouse position.
    const int delta = event->angleDelta().y();
    if (delta == 0)
        return;

    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    if (!groove.isValid() || groove.width() <= 0)
        return;

    int anchorX = std::clamp(event->position().toPoint().x(), groove.left(), groove.right());
    const double edgeThreshold = 0.1;
    if (anchorX < groove.left() + (int)(groove.width() * edgeThreshold))
        anchorX = groove.left();
    else if (anchorX > groove.right() - (int)(groove.width() * edgeThreshold))
        anchorX = groove.right();

    const double frac = (anchorX - groove.left()) / (double)groove.width();
    const int timeAtAnchor = valueFromPosition(QPoint(anchorX, groove.center().y()));

    const double fullRange = (double)(m_fullMaxMs - m_fullMinMs);
    double viewRange = (double)(maximum() - minimum());
    if (viewRange <= 0.0)
        viewRange = fullRange;

    // Shift+wheel pans, plain wheel zooms.
    if ((event->modifiers() & Qt::ShiftModifier) != 0)
    {
        const double panStep = std::max(50.0, viewRange * 0.1);
        double scrollOffset = (double)minimum() - m_fullMinMs;
        scrollOffset += (delta < 0 ? panStep : -panStep);
        const double maxOffset = std::max(0.0, fullRange - viewRange);
        scrollOffset = std::clamp(scrollOffset, 0.0, maxOffset);
        const int newMin = m_fullMinMs + (int)std::llround(scrollOffset);
        const int newMax = std::min(m_fullMaxMs, newMin + (int)std::llround(viewRange));
        setRange(newMin, newMax);
        setValue(std::clamp(value(), newMin, newMax));
        event->accept();
        return;
    }

    const double currentViewRange = std::max(50.0, viewRange);
    m_zoomLevel = std::clamp(fullRange / currentViewRange, 1.0, 500.0);

    const double oldZoom = m_zoomLevel;
    if (delta > 0)
        m_zoomLevel = std::min(500.0, m_zoomLevel * 1.2);
    else
        m_zoomLevel = std::max(1.0, m_zoomLevel / 1.2);

    if (m_zoomLevel == oldZoom)
        return;

    viewRange = std::max(50.0, fullRange / m_zoomLevel);

    double scrollOffset = timeAtAnchor - frac * viewRange;
    const double maxOffset = std::max(0.0, fullRange - viewRange);
    scrollOffset = std::clamp(scrollOffset, 0.0, maxOffset);

    const int newMin = m_fullMinMs + (int)std::llround(scrollOffset);
    const int newMax = std::min(m_fullMaxMs, newMin + (int)std::llround(viewRange));
    setRange(newMin, newMax);
    setValue(std::clamp(value(), newMin, newMax));
    event->accept();
}

void TimelineSlider::paintEvent(QPaintEvent* event)
{
    QSlider::paintEvent(event);

    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    if (!groove.isValid())
        return;

    const int minV = minimum();
    const int maxV = maximum();
    if (minV >= maxV)
        return;

    auto xForValue = [&](int v) { return xForValueMs(v, groove, opt); };

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Selection highlight (start/end)
    if (m_startMs >= 0 && m_endMs >= 0 && m_endMs > m_startMs)
    {
        const int x1 = xForValue(m_startMs);
        const int x2 = xForValue(m_endMs);
        QRect sel(QPoint(std::min(x1, x2), groove.top()), QPoint(std::max(x1, x2), groove.bottom()));
        sel.adjust(0, -6, 0, 6);
        QColor c(70, 130, 180, 80);
        p.fillRect(sel, c);
    }

    auto drawMarkerLine = [&](int ms, const QColor& color, int thickness) {
        if (ms < 0)
            return;
        const int x = xForValue(ms);
        QPen pen(color);
        pen.setWidth(thickness);
        p.setPen(pen);
        p.drawLine(QPoint(x, groove.top() - 10), QPoint(x, groove.bottom() + 10));
    };

    // Start/end markers
    drawMarkerLine(m_startMs, QColor(80, 200, 120), 2);
    drawMarkerLine(m_endMs, QColor(240, 90, 90), 2);

    // Crop keyframes
    for (const auto& k : m_keyframes)
    {
        const int ms = (int)std::llround(k.seconds * 1000.0);
        const int x = xForValue(ms);
        const QColor color = k.enabled ? QColor(180, 120, 255) : QColor(160, 160, 160);
        QPen pen(color);
        pen.setWidth(2);
        p.setPen(pen);
        p.drawLine(QPoint(x, groove.top() - 6), QPoint(x, groove.bottom() + 6));

        QBrush brush(color);
        p.setBrush(brush);
        p.setPen(Qt::NoPen);
        QPointF pts[3] = {
            QPointF(x, groove.top() - 10),
            QPointF(x - 5, groove.top() - 2),
            QPointF(x + 5, groove.top() - 2),
        };
        p.drawPolygon(pts, 3);
        p.setPen(pen);
    }
}
