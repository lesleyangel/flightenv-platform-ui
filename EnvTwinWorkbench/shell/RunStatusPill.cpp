#include "RunStatusPill.h"

#include "../theme/Palette.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

namespace twin {

namespace {

struct PillColors {
    QColor fill;
    QColor text;
    QColor border;
    QColor dot;
};

PillColors colorsFor(RunStatusPill::State state) {
    using namespace palette;
    switch (state) {
    case RunStatusPill::State::Running:   return {accSoft(),  accInk(),                 accSoft2(),               acc()};
    case RunStatusPill::State::Degraded:  return {warnSoft(), QColor(0x7a,0x58,0x06),   QColor(0xec,0xd8,0xa0),   warn()};
    case RunStatusPill::State::Failed:    return {failSoft(), QColor(0x9c,0x2b,0x2b),   QColor(0xf0,0xc4,0xc4),   fail()};
    case RunStatusPill::State::Replaying: return {QColor(0xea,0xe6,0xf6), QColor(0x57,0x4a,0x86), QColor(0xd6,0xcd,0xef), QColor(0x6f,0x5f,0xb3)};
    case RunStatusPill::State::Idle:
    default:                              return {unkSoft(),  ink2(),                   line2(),                  unk()};
    }
}

} // namespace

RunStatusPill::RunStatusPill(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    pulseTimer_ = new QTimer(this);
    pulseTimer_->setInterval(40);
    connect(pulseTimer_, &QTimer::timeout, this, [this]() {
        pulsePhase_ += 0.06;
        if (pulsePhase_ > 1.0) pulsePhase_ -= 1.0;
        update();
    });
}

void RunStatusPill::setState(State state, const QString& text) {
    state_ = state;
    text_ = text;
    if (state_ == State::Running) {
        pulseTimer_->start();
    } else {
        pulseTimer_->stop();
    }
    updateGeometry();
    update();
}

QSize RunStatusPill::sizeHint() const {
    QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(text_) + 34, 28);
}

void RunStatusPill::paintEvent(QPaintEvent*) {
    const PillColors c = colorsFor(state_);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
    p.fillPath(path, c.fill);
    p.setPen(QPen(c.border, 1));
    p.drawPath(path);

    const double cy = r.center().y();
    const double cx = 14;
    if (state_ == State::Running) {
        // 脉冲光晕
        const double ring = 3.5 + 4.0 * pulsePhase_;
        QColor halo = c.dot;
        halo.setAlphaF(0.35 * (1.0 - pulsePhase_));
        p.setBrush(halo);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(cx, cy), ring, ring);
    }
    p.setBrush(c.dot);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(cx, cy), 3.5, 3.5);

    p.setPen(c.text);
    QFont f = font();
    f.setBold(true);
    f.setPixelSize(12);
    p.setFont(f);
    p.drawText(QRectF(24, 0, r.width() - 28, r.height()), Qt::AlignVCenter | Qt::AlignLeft, text_);
}

} // namespace twin
