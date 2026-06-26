#include "Meter.h"

#include "../theme/Palette.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace twin {

Meter::Meter(QWidget* parent) : QWidget(parent) {
    color_ = palette::acc();
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Meter::setValue(double value01) {
    value_ = std::clamp(value01, 0.0, 1.0);
    update();
}

void Meter::setColor(const QColor& color) {
    color_ = color;
    update();
}

QSize Meter::sizeHint() const {
    return QSize(120, 7);
}

void Meter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double h = height();
    QRectF track(0.5, 0.5, width() - 1.0, h - 1.0);
    QPainterPath bg;
    bg.addRoundedRect(track, h / 2.0, h / 2.0);
    p.fillPath(bg, palette::panel3());
    p.setPen(QPen(palette::line(), 1));
    p.drawPath(bg);

    if (value_ > 0.0) {
        QRectF fill(0.5, 0.5, std::max(h, (width() - 1.0) * value_), h - 1.0);
        QPainterPath fg;
        fg.addRoundedRect(fill, h / 2.0, h / 2.0);
        p.fillPath(fg, color_);
    }
}

} // namespace twin
