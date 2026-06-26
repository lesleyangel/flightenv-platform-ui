#include "NavRail.h"

#include "../theme/Palette.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace twin {

NavRail::NavRail(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("NavRail"));
    setFixedWidth(216);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* brand = new QWidget(this);
    brand->setStyleSheet(QStringLiteral("border-bottom:1px solid #e2e6ec;"));
    auto* brandRow = new QHBoxLayout(brand);
    brandRow->setContentsMargins(16, 14, 16, 12);
    brandRow->setSpacing(10);

    auto* mark = new QLabel(brand);
    mark->setFixedSize(30, 30);
    mark->setStyleSheet(QStringLiteral(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #0e8a9c,stop:1 #0a6c7b);"
        "border-radius:7px;color:white;font-weight:700;"));
    mark->setAlignment(Qt::AlignCenter);
    mark->setText(QStringLiteral("FE"));
    brandRow->addWidget(mark);

    auto* brandText = new QWidget(brand);
    auto* brandCol = new QVBoxLayout(brandText);
    brandCol->setContentsMargins(0, 0, 0, 0);
    brandCol->setSpacing(0);
    auto* name = new QLabel(QStringLiteral("FlightEnv"), brandText);
    name->setObjectName(QStringLiteral("NavBrandName"));
    auto* sub = new QLabel(QStringLiteral("Twin Workbench"), brandText);
    sub->setObjectName(QStringLiteral("NavBrandSub"));
    brandCol->addWidget(name);
    brandCol->addWidget(sub);
    brandRow->addWidget(brandText);
    brandRow->addStretch(1);
    root->addWidget(brand);

    auto* navHost = new QWidget(this);
    auto* nav = new QVBoxLayout(navHost);
    nav->setContentsMargins(8, 8, 8, 12);
    nav->setSpacing(2);

    addSection(nav, QStringLiteral("平台运行"));
    addItem(nav, QStringLiteral("overview"), QStringLiteral("总览"), true);
    addItem(nav, QStringLiteral("online"), QStringLiteral("在线运行"), true, QStringLiteral("实时"));
    addItem(nav, QStringLiteral("runtime"), QStringLiteral("运行时主机"), true);
    addItem(nav, QStringLiteral("dataplane"), QStringLiteral("数据平面"), true);

    addSection(nav, QStringLiteral("对象与资产"));
    addItem(nav, QStringLiteral("object"), QStringLiteral("对象画像"), true);
    addItem(nav, QStringLiteral("models"), QStringLiteral("资源与模型"), true);
    addItem(nav, QStringLiteral("operators"), QStringLiteral("算子库"), true);

    addSection(nav, QStringLiteral("编排与健康"));
    addItem(nav, QStringLiteral("graph"), QStringLiteral("工作流编排"), true);
    addItem(nav, QStringLiteral("health"), QStringLiteral("健康账本"), true);
    addItem(nav, QStringLiteral("replay"), QStringLiteral("证据回放"), true);

    addSection(nav, QStringLiteral("配置与诊断"));
    addItem(nav, QStringLiteral("config"), QStringLiteral("配置与数据源"), true);
    addItem(nav, QStringLiteral("diagnostics"), QStringLiteral("诊断报告"), true);

    nav->addStretch(1);
    root->addWidget(navHost, 1);

    auto* foot = new QLabel(QStringLiteral("契约 v0.9      SDK 只读"), this);
    foot->setObjectName(QStringLiteral("NavFoot"));
    root->addWidget(foot);
}

void NavRail::addSection(QVBoxLayout* layout, const QString& title) {
    auto* label = new QLabel(title, this);
    label->setObjectName(QStringLiteral("NavSection"));
    layout->addWidget(label);
}

void NavRail::addItem(
    QVBoxLayout* layout,
    const QString& key,
    const QString& label,
    bool enabled,
    const QString& badge) {
    auto* btn = new QPushButton(label, this);
    btn->setProperty("navItem", true);
    btn->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    btn->setEnabled(enabled);
    if (!badge.isEmpty()) {
        btn->setText(QStringLiteral("%1    · %2").arg(label, badge));
    }
    if (!enabled) {
        btn->setToolTip(QStringLiteral("后续迭代开放"));
    }
    connect(btn, &QPushButton::clicked, this, [this, key]() { emit pageChanged(key); });
    items_.insert(key, btn);
    layout->addWidget(btn);
}

void NavRail::setActive(const QString& pageKey) {
    active_ = pageKey;
    for (auto it = items_.begin(); it != items_.end(); ++it) {
        const bool on = (it.key() == pageKey);
        it.value()->setProperty("active", on);
        it.value()->style()->unpolish(it.value());
        it.value()->style()->polish(it.value());
    }
}

} // namespace twin
