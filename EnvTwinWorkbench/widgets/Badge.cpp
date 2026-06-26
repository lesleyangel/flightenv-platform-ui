#include "Badge.h"

#include "../theme/Palette.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

namespace twin {

namespace {

struct BadgeColors {
    QColor fill;
    QColor text;
    QColor border;
    QColor dot;
};

BadgeColors colorsFor(Badge::Kind kind) {
    using namespace palette;
    switch (kind) {
    case Badge::Kind::Ok:      return {okSoft(),   QColor(0x16, 0x72, 0x43), QColor(0xc3, 0xe6, 0xd1), ok()};
    case Badge::Kind::Warn:    return {warnSoft(), QColor(0x7a, 0x58, 0x06), QColor(0xec, 0xd8, 0xa0), warn()};
    case Badge::Kind::Fail:    return {failSoft(), QColor(0x9c, 0x2b, 0x2b), QColor(0xf0, 0xc4, 0xc4), fail()};
    case Badge::Kind::Accent:  return {accSoft(),  accInk(),                 accSoft2(),               acc()};
    case Badge::Kind::Running: return {accSoft(),  accInk(),                 accSoft2(),               acc()};
    case Badge::Kind::Unknown:
    default:                   return {unkSoft(),  QColor(0x5d, 0x63, 0x6e), line2(),                  unk()};
    }
}

} // namespace

Badge::Badge(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
}

void Badge::setText(const QString& text) {
    text_ = text;
    updateGeometry();
    update();
}

void Badge::setKind(Kind kind) {
    kind_ = kind;
    update();
}

QSize Badge::sizeHint() const {
    QFontMetrics fm(font());
    const int textW = fm.horizontalAdvance(text_);
    return QSize(textW + 28, 22);
}

void Badge::paintEvent(QPaintEvent*) {
    const BadgeColors c = colorsFor(kind_);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, 5, 5);
    p.fillPath(path, c.fill);
    p.setPen(QPen(c.border, 1));
    p.drawPath(path);

    const double cy = r.center().y();
    p.setBrush(c.dot);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(11, cy), 3, 3);

    p.setPen(c.text);
    QFont f = font();
    f.setBold(true);
    f.setPixelSize(11);
    p.setFont(f);
    p.drawText(QRectF(18, 0, r.width() - 22, r.height()), Qt::AlignVCenter | Qt::AlignLeft, text_);
}

} // namespace twin
