#include "GraphWorkflowDisplayWidgets.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <limits>

namespace flightenv::ui::display {
namespace {

double finiteOr(const double value, const double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

double altitudeOf(const contracts::TrajectorySampleDTO& sample)
{
    if (sample.lla_rad_m.size() >= 3) {
        return finiteOr(sample.lla_rad_m[2], 0.0);
    }
    if (sample.position_ned_m.size() >= 3) {
        return finiteOr(-sample.position_ned_m[2], 0.0);
    }
    return 0.0;
}

QColor statusColor(const QString& status)
{
    const QString value = status.toLower();
    if (value == QStringLiteral("ok") || value == QStringLiteral("passed")) {
        return QColor(22, 101, 52);
    }
    if (value == QStringLiteral("running")) {
        return QColor(3, 105, 161);
    }
    if (value == QStringLiteral("pending")) {
        return QColor(100, 116, 139);
    }
    if (value == QStringLiteral("failed") || value == QStringLiteral("error")) {
        return QColor(185, 28, 28);
    }
    return QColor(75, 85, 99);
}

QString elide(const QFontMetrics& metrics, const QString& text, const int width)
{
    return metrics.elidedText(text, Qt::ElideRight, std::max(20, width));
}

void drawEmpty(QPainter& painter, const QRect& rect, const QString& text)
{
    painter.save();
    painter.setPen(QColor(100, 116, 139));
    painter.drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, text);
    painter.restore();
}

} // namespace

WorkflowPathWidget::WorkflowPathWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void WorkflowPathWidget::setSteps(std::vector<WorkflowStepView> steps)
{
    steps_ = std::move(steps);
    update();
}

void WorkflowPathWidget::clear()
{
    steps_.clear();
    update();
}

void WorkflowPathWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(248, 250, 252));

    const QRect area = rect().adjusted(12, 12, -12, -12);
    if (steps_.empty()) {
        drawEmpty(painter, area, QStringLiteral("等待 graph_snapshot/operator_live_status 生成编排路径"));
        return;
    }

    const int count = static_cast<int>(steps_.size());
    const int gap = 14;
    const int minBoxWidth = 132;
    const int columns = std::max(1, std::min(count, std::max(1, area.width() / (minBoxWidth + gap))));
    const int rows = (count + columns - 1) / columns;
    const int boxWidth = std::max(minBoxWidth, (area.width() - gap * (columns - 1)) / columns);
    const int boxHeight = std::max(82, (area.height() - gap * (rows - 1)) / std::max(1, rows));

    std::vector<QRect> boxes;
    boxes.reserve(steps_.size());
    for (int i = 0; i < count; ++i) {
        const int row = i / columns;
        const int col = i % columns;
        QRect box(area.left() + col * (boxWidth + gap),
                  area.top() + row * (boxHeight + gap),
                  boxWidth,
                  std::min(104, boxHeight));
        boxes.push_back(box);
    }

    painter.setPen(QPen(QColor(148, 163, 184), 2));
    for (int i = 0; i + 1 < count; ++i) {
        const QPoint start(boxes[static_cast<std::size_t>(i)].right(),
                           boxes[static_cast<std::size_t>(i)].center().y());
        const QPoint end(boxes[static_cast<std::size_t>(i + 1)].left(),
                         boxes[static_cast<std::size_t>(i + 1)].center().y());
        if (boxes[static_cast<std::size_t>(i)].top() == boxes[static_cast<std::size_t>(i + 1)].top()) {
            painter.drawLine(start, end);
            painter.drawLine(end, QPoint(end.x() - 6, end.y() - 4));
            painter.drawLine(end, QPoint(end.x() - 6, end.y() + 4));
        }
        else {
            const QPoint down(boxes[static_cast<std::size_t>(i)].center().x(),
                              boxes[static_cast<std::size_t>(i)].bottom() + gap / 2);
            const QPoint next(boxes[static_cast<std::size_t>(i + 1)].center().x(),
                              boxes[static_cast<std::size_t>(i + 1)].top() - gap / 2);
            painter.drawLine(QPoint(boxes[static_cast<std::size_t>(i)].center().x(),
                                    boxes[static_cast<std::size_t>(i)].bottom()), down);
            painter.drawLine(down, next);
            painter.drawLine(next, QPoint(boxes[static_cast<std::size_t>(i + 1)].center().x(),
                                          boxes[static_cast<std::size_t>(i + 1)].top()));
        }
    }

    const QFontMetrics metrics(font());
    for (int i = 0; i < count; ++i) {
        const auto& step = steps_[static_cast<std::size_t>(i)];
        const QRect box = boxes[static_cast<std::size_t>(i)];
        const QColor color = statusColor(step.status);
        painter.setPen(QPen(color, 1.6));
        painter.setBrush(QColor(255, 255, 255));
        painter.drawRoundedRect(box, 6, 6);

        QRect inner = box.adjusted(8, 7, -8, -7);
        painter.setPen(color);
        painter.setFont(QFont(font().family(), font().pointSize(), QFont::DemiBold));
        painter.drawText(inner.left(), inner.top() + metrics.ascent(),
                         elide(metrics, step.node_id, inner.width()));

        painter.setFont(font());
        painter.setPen(QColor(30, 41, 59));
        painter.drawText(inner.left(), inner.top() + 28,
                         elide(metrics, step.operator_type + QStringLiteral(" / ") + step.status, inner.width()));
        painter.setPen(QColor(71, 85, 105));
        painter.drawText(inner.left(), inner.top() + 48,
                         elide(metrics, QStringLiteral("%1 ms  %2 bytes")
                             .arg(step.duration_ms)
                             .arg(static_cast<qulonglong>(step.output_bytes)), inner.width()));
        painter.drawText(inner.left(), inner.top() + 68,
                         elide(metrics, step.outputs, inner.width()));
    }
}

TrajectoryPathWidget::TrajectoryPathWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TrajectoryPathWidget::setTrajectory(const contracts::TrajectoryPredictionFrame& trajectory)
{
    trajectory_ = trajectory;
    hasTrajectory_ = !trajectory_.samples.empty();
    update();
}

void TrajectoryPathWidget::clear()
{
    trajectory_ = contracts::TrajectoryPredictionFrame{};
    hasTrajectory_ = false;
    update();
}

void TrajectoryPathWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(255, 255, 255));

    const QRect plot = rect().adjusted(58, 22, -24, -42);
    if (!hasTrajectory_ || plot.width() <= 20 || plot.height() <= 20) {
        drawEmpty(painter, rect(), QStringLiteral("等待 state.future 轨迹 DTO"));
        return;
    }

    double minT = std::numeric_limits<double>::max();
    double maxT = std::numeric_limits<double>::lowest();
    double minH = std::numeric_limits<double>::max();
    double maxH = std::numeric_limits<double>::lowest();
    for (const auto& sample : trajectory_.samples) {
        minT = std::min(minT, sample.time_s);
        maxT = std::max(maxT, sample.time_s);
        const double h = altitudeOf(sample);
        minH = std::min(minH, h);
        maxH = std::max(maxH, h);
    }
    if (minT >= maxT) {
        maxT = minT + 1.0;
    }
    if (minH >= maxH) {
        minH -= 1.0;
        maxH += 1.0;
    }

    painter.setPen(QPen(QColor(203, 213, 225), 1));
    painter.drawRect(plot);
    painter.drawText(QRect(rect().left() + 8, rect().top() + 4, rect().width() - 16, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("弹道预测：时间-高度曲线，首点应贴合当前状态"));

    auto mapPoint = [&](const double t, const double h) {
        const double x = (t - minT) / (maxT - minT);
        const double y = (h - minH) / (maxH - minH);
        return QPointF(plot.left() + x * plot.width(),
                       plot.bottom() - y * plot.height());
    };

    QPainterPath path;
    bool first = true;
    for (const auto& sample : trajectory_.samples) {
        const QPointF point = mapPoint(sample.time_s, altitudeOf(sample));
        if (first) {
            path.moveTo(point);
            first = false;
        }
        else {
            path.lineTo(point);
        }
    }
    painter.setPen(QPen(QColor(2, 132, 199), 2.4));
    painter.drawPath(path);

    painter.setBrush(QColor(22, 163, 74));
    painter.setPen(Qt::NoPen);
    const auto firstPoint = mapPoint(trajectory_.samples.front().time_s, altitudeOf(trajectory_.samples.front()));
    painter.drawEllipse(firstPoint, 4, 4);
    painter.setBrush(QColor(220, 38, 38));
    const auto lastPoint = mapPoint(trajectory_.samples.back().time_s, altitudeOf(trajectory_.samples.back()));
    painter.drawEllipse(lastPoint, 4, 4);

    painter.setPen(QColor(71, 85, 105));
    painter.drawText(QRect(plot.left(), plot.bottom() + 8, plot.width(), 18),
                     Qt::AlignCenter,
                     QStringLiteral("time: %1 - %2 s").arg(minT, 0, 'g', 6).arg(maxT, 0, 'g', 6));
    painter.save();
    painter.translate(16, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRect(-plot.height() / 2, 0, plot.height(), 18),
                     Qt::AlignCenter,
                     QStringLiteral("altitude: %1 - %2 m").arg(minH, 0, 'g', 6).arg(maxH, 0, 'g', 6));
    painter.restore();
}

ScalarTrendWidget::ScalarTrendWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(170);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ScalarTrendWidget::setTitle(const QString& title, const QString& unit)
{
    title_ = title;
    unit_ = unit;
    update();
}

void ScalarTrendWidget::setSamples(const std::vector<double>& samples)
{
    samples_ = samples;
    update();
}

void ScalarTrendWidget::setFixedRange(const double minValue, const double maxValue)
{
    fixedRange_ = std::make_pair(minValue, maxValue);
    update();
}

void ScalarTrendWidget::clearFixedRange()
{
    fixedRange_.reset();
    update();
}

void ScalarTrendWidget::clear()
{
    samples_.clear();
    update();
}

void ScalarTrendWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(255, 255, 255));

    const QRect plot = rect().adjusted(46, 24, -18, -28);
    if (samples_.empty()) {
        drawEmpty(painter, rect(), QStringLiteral("等待趋势数据"));
        return;
    }

    double minV = 0.0;
    double maxV = 1.0;
    if (fixedRange_.has_value()) {
        minV = fixedRange_->first;
        maxV = fixedRange_->second;
    }
    else {
        minV = *std::min_element(samples_.begin(), samples_.end());
        maxV = *std::max_element(samples_.begin(), samples_.end());
        if (minV >= maxV) {
            minV -= 1.0;
            maxV += 1.0;
        }
    }
    if (minV >= maxV) {
        maxV = minV + 1.0;
    }

    painter.setPen(QPen(QColor(203, 213, 225), 1));
    painter.drawRect(plot);
    painter.drawText(QRect(rect().left() + 8, rect().top() + 4, rect().width() - 16, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     unit_.isEmpty() ? title_ : title_ + QStringLiteral(" / ") + unit_);

    auto mapPoint = [&](const int index, const double value) {
        const double x = samples_.size() <= 1
            ? 0.0
            : static_cast<double>(index) / static_cast<double>(samples_.size() - 1);
        const double y = (std::clamp(value, minV, maxV) - minV) / (maxV - minV);
        return QPointF(plot.left() + x * plot.width(),
                       plot.bottom() - y * plot.height());
    };

    QPainterPath path;
    for (int i = 0; i < static_cast<int>(samples_.size()); ++i) {
        const QPointF point = mapPoint(i, samples_[static_cast<std::size_t>(i)]);
        if (i == 0) {
            path.moveTo(point);
        }
        else {
            path.lineTo(point);
        }
    }
    painter.setPen(QPen(QColor(79, 70, 229), 2.2));
    painter.drawPath(path);

    painter.setPen(QColor(71, 85, 105));
    painter.drawText(QRect(plot.left(), plot.bottom() + 6, plot.width(), 18),
                     Qt::AlignCenter,
                     QStringLiteral("samples=%1").arg(static_cast<int>(samples_.size())));
    painter.drawText(QRect(4, plot.top(), 40, 16), Qt::AlignRight, QString::number(maxV, 'g', 4));
    painter.drawText(QRect(4, plot.bottom() - 16, 40, 16), Qt::AlignRight, QString::number(minV, 'g', 4));
}

} // namespace flightenv::ui::display
