#include "StageStrip.h"

#include "../theme/Palette.h"

#include <QPainter>
#include <QPainterPath>

namespace twin {

namespace {
QColor topColor(StageStrip::Status s) {
    using namespace palette;
    switch (s) {
    case StageStrip::Status::Ok: return ok();
    case StageStrip::Status::Warn: return warn();
    case StageStrip::Status::Fail: return fail();
    case StageStrip::Status::Running: return acc();
    default: return unk();
    }
}
} // namespace

StageStrip::StageStrip(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void StageStrip::setStages(std::vector<Stage> stages) {
    stages_ = std::move(stages);
    update();
}

QSize StageStrip::sizeHint() const {
    return QSize(640, 78);
}

void StageStrip::paintEvent(QPaintEvent*) {
    if (stages_.empty()) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int n = static_cast<int>(stages_.size());
    const int arrowW = 18;
    const double cardW = (width() - arrowW * (n - 1)) / static_cast<double>(n);
    const double h = height() - 2;
    double x = 0.5;

    for (int i = 0; i < n; ++i) {
        const Stage& s = stages_[static_cast<std::size_t>(i)];
        QRectF card(x, 1.0, cardW - 1.0, h);
        QPainterPath bg;
        bg.addRoundedRect(card, palette::radiusM, palette::radiusM);
        p.fillPath(bg, palette::panel());
        p.setPen(QPen(palette::line(), 1));
        p.drawPath(bg);
        // 顶部状态色条
        QPainterPath topBar;
        topBar.addRoundedRect(QRectF(card.left(), card.top(), card.width(), 3), 1.5, 1.5);
        p.fillPath(topBar, topColor(s.status));

        const double tx = card.left() + 10;
        // name + type
        QFont nf = font(); nf.setBold(true); nf.setPixelSize(11); p.setFont(nf);
        p.setPen(palette::ink());
        p.drawText(QRectF(tx, card.top() + 9, card.width() - 20, 14), Qt::AlignLeft | Qt::AlignVCenter, s.name);
        QFont tyf = font(); tyf.setPixelSize(9); p.setFont(tyf);
        p.setPen(palette::ink4());
        p.drawText(QRectF(tx, card.top() + 9, card.width() - 16, 14), Qt::AlignRight | Qt::AlignVCenter, s.type);
        // value
        QFont vf("JetBrains Mono"); vf.setBold(true); vf.setPixelSize(16); p.setFont(vf);
        p.setPen(palette::ink());
        const QString val = s.unit.isEmpty() ? s.value : (s.value + " " + s.unit);
        p.drawText(QRectF(tx, card.top() + 26, card.width() - 20, 20), Qt::AlignLeft | Qt::AlignVCenter, val);
        // sub
        QFont sf = font(); sf.setPixelSize(10); p.setFont(sf);
        p.setPen(palette::ink3());
        p.drawText(QRectF(tx, card.top() + 48, card.width() - 20, 14), Qt::AlignLeft | Qt::AlignVCenter, s.sub);

        x += cardW;
        if (i < n - 1) {
            // 箭头
            p.setPen(QPen(palette::line3(), 1.5));
            const double ay = card.center().y();
            const double a0 = x + 3, a1 = x + arrowW - 3;
            p.drawLine(QPointF(a0, ay), QPointF(a1, ay));
            p.drawLine(QPointF(a1 - 4, ay - 3), QPointF(a1, ay));
            p.drawLine(QPointF(a1 - 4, ay + 3), QPointF(a1, ay));
            x += arrowW;
        }
    }
}

} // namespace twin
