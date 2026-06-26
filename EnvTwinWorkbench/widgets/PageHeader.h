#pragma once

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace twin {

// 页头：等价 htmlUI .page-head 的标题 + 副标题。inline helper，无需 moc。
inline QWidget* makePageHeader(const QString& title, const QString& subtitle, QWidget* parent = nullptr) {
    auto* head = new QWidget(parent);
    auto* col = new QVBoxLayout(head);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);
    auto* t = new QLabel(title, head);
    t->setObjectName(QStringLiteral("PageTitle"));
    auto* s = new QLabel(subtitle, head);
    s->setObjectName(QStringLiteral("PageSubtitle"));
    s->setWordWrap(true);
    col->addWidget(t);
    col->addWidget(s);
    return head;
}

} // namespace twin
