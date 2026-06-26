#include "KvList.h"

#include <QGridLayout>
#include <QLabel>

namespace twin {

KvList::KvList(QWidget* parent) : QWidget(parent) {
    grid_ = new QGridLayout(this);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(12);
    grid_->setVerticalSpacing(6);
    grid_->setColumnStretch(0, 0);
    grid_->setColumnStretch(1, 1);
}

QLabel* KvList::addRow(const QString& key, const QString& value, bool mono) {
    auto* k = new QLabel(key, this);
    k->setObjectName(QStringLiteral("KvKey"));

    auto* v = new QLabel(value, this);
    v->setObjectName(QStringLiteral("KvVal"));
    v->setProperty("mono", mono);
    v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);

    grid_->addWidget(k, row_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid_->addWidget(v, row_, 1, Qt::AlignRight | Qt::AlignVCenter);
    ++row_;
    return v;
}

void KvList::clear() {
    while (QLayoutItem* item = grid_->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    row_ = 0;
}

} // namespace twin
