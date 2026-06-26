#pragma once

#include "Badge.h"
#include "StageStrip.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QSizePolicy>
#include <QString>
#include <QStringList>
#include <QTableWidget>

namespace twin {

// 统一把契约里的状态字符串映射到徽章/阶段状态着色。
inline Badge::Kind badgeKindFromStatus(const QString& s) {
    if (s == QStringLiteral("ok") || s == QStringLiteral("enabled") || s == QStringLiteral("active")) return Badge::Kind::Ok;
    if (s == QStringLiteral("warn") || s == QStringLiteral("degraded") || s == QStringLiteral("warning")) return Badge::Kind::Warn;
    if (s == QStringLiteral("fail") || s == QStringLiteral("failed") || s == QStringLiteral("error")) return Badge::Kind::Fail;
    if (s == QStringLiteral("running") || s == QStringLiteral("replaying")) return Badge::Kind::Running;
    return Badge::Kind::Unknown;
}

inline StageStrip::Status stageStatusFromString(const QString& s) {
    if (s == QStringLiteral("ok")) return StageStrip::Status::Ok;
    if (s == QStringLiteral("warn") || s == QStringLiteral("degraded")) return StageStrip::Status::Warn;
    if (s == QStringLiteral("fail") || s == QStringLiteral("failed") || s == QStringLiteral("error")) return StageStrip::Status::Fail;
    if (s == QStringLiteral("running")) return StageStrip::Status::Running;
    return StageStrip::Status::Unknown;
}

// 建一个统一风格的只读表格（行选择、交替行、舒适行高、超长省略、随面板伸展）。
inline QTableWidget* makeTable(const QStringList& headers, QWidget* parent = nullptr) {
    auto* t = new QTableWidget(0, headers.size(), parent);
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(30);          // 统一舒适行高，避免疏密不均
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setShowGrid(false);
    t->setAlternatingRowColors(true);                        // 斑马行，长表更易读
    t->setWordWrap(false);
    t->setTextElideMode(Qt::ElideRight);                     // 超长内容省略而非撑宽
    t->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    t->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    t->horizontalHeader()->setStretchLastSection(true);
    t->horizontalHeader()->setHighlightSections(false);
    t->horizontalHeader()->setMinimumSectionSize(64);
    t->setFocusPolicy(Qt::NoFocus);
    return t;
}

// 在 (row,col) 放一个状态徽章单元。
inline void setBadgeCell(QTableWidget* t, int row, int col, const QString& status, const QString& text = QString()) {
    auto* badge = new twin::Badge();
    badge->setKind(badgeKindFromStatus(status));
    badge->setText(text.isEmpty() ? status : text);
    t->setCellWidget(row, col, badge);
}

} // namespace twin
