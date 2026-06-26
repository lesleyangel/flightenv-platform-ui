#include "Panel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace twin {

Panel::Panel(const QString& title, QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("Panel"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("PanelHeader"));
    headerLayout_ = new QHBoxLayout(header);
    headerLayout_->setContentsMargins(12, 9, 12, 9);
    headerLayout_->setSpacing(8);

    titleLabel_ = new QLabel(title, header);
    titleLabel_->setObjectName(QStringLiteral("PanelTitle"));
    headerLayout_->addWidget(titleLabel_);

    subLabel_ = new QLabel(header);
    subLabel_->setObjectName(QStringLiteral("PanelSub"));
    subLabel_->hide();
    headerLayout_->addWidget(subLabel_);

    headerLayout_->addStretch(1);

    body_ = new QWidget(this);
    bodyLayout_ = new QVBoxLayout(body_);
    bodyLayout_->setContentsMargins(12, 12, 12, 12);
    bodyLayout_->setSpacing(10);

    root->addWidget(header);
    root->addWidget(body_, 1);
}

void Panel::addHeaderWidget(QWidget* widget) {
    // 插在 stretch 之前，让追加控件靠右、标题靠左。
    headerLayout_->insertWidget(headerLayout_->count() - 1, widget);
}

void Panel::setSubtitle(const QString& text) {
    subLabel_->setText(text);
    subLabel_->setVisible(!text.isEmpty());
}

} // namespace twin
