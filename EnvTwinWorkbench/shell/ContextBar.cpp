#include "ContextBar.h"

#include "RunStatusPill.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace twin {

ContextBar::ContextBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ContextBar"));
    setFixedHeight(62);

    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 14, 0);
    layout_->setSpacing(0);

    objectVal_ = addField(QStringLiteral("object"), QStringLiteral("-"), 160);
    runVal_ = addField(QStringLiteral("run"), QStringLiteral("-"), 110);
    workflowVal_ = addField(QStringLiteral("workflow"), QStringLiteral("-"), 150);
    profileVal_ = addField(QStringLiteral("profile"), QStringLiteral("-"), 120);
    phaseVal_ = addField(QStringLiteral("phase"), QStringLiteral("-"));
    clockVal_ = addField(QStringLiteral("clock"), QStringLiteral("-"), 100);
    runtimeVal_ = addField(QStringLiteral("runtime"), QStringLiteral("-"), 110);
    evidenceVal_ = addField(QStringLiteral("evidence"), QStringLiteral("-"), 170);
    modeVal_ = addField(QStringLiteral("mode"), QStringLiteral("-"));
    graphVal_ = addField(QStringLiteral("graph"), QStringLiteral("-"));
    layout_->addStretch(1);

    pill_ = new RunStatusPill(this);
    pill_->setState(RunStatusPill::State::Idle, QStringLiteral("空闲"));
    layout_->addWidget(pill_);
}

QLabel* ContextBar::addField(const QString& key, const QString& value, int minWidth) {
    auto* field = new QWidget(this);
    auto* col = new QVBoxLayout(field);
    col->setContentsMargins(14, 0, 14, 0);
    col->setSpacing(1);

    auto* k = new QLabel(key, field);
    k->setObjectName(QStringLiteral("CtxFieldKey"));
    auto* v = new QLabel(value, field);
    v->setObjectName(QStringLiteral("CtxFieldVal"));
    if (minWidth > 0) {
        v->setMinimumWidth(minWidth);
    }
    col->addStretch(1);
    col->addWidget(k);
    col->addWidget(v);
    col->addStretch(1);

    layout_->addWidget(field);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet(QStringLiteral("color:#e2e6ec;"));
    layout_->addWidget(sep);

    return v;
}

void ContextBar::setObjectField(const QString& title, const QString& id) {
    objectVal_->setText(id.isEmpty() ? title : QStringLiteral("%1 / %2").arg(title, id));
}
void ContextBar::setPhase(const QString& phase) { phaseVal_->setText(phase); }
void ContextBar::setMode(const QString& mode) { modeVal_->setText(mode); }
void ContextBar::setRun(const QString& runId) { runVal_->setText(runId); }
void ContextBar::setGraph(const QString& graphId) { graphVal_->setText(graphId); }
void ContextBar::setWorkflow(const QString& workflowId) {
    workflowVal_->setText(workflowId.isEmpty() ? QStringLiteral("-") : workflowId);
}
void ContextBar::setRunProfile(const QString& profileId) {
    profileVal_->setText(profileId.isEmpty() ? QStringLiteral("-") : profileId);
}
void ContextBar::setClock(const QString& clockText) {
    clockVal_->setText(clockText.isEmpty() ? QStringLiteral("-") : clockText);
}
void ContextBar::setRuntimeStatus(const QString& statusText) {
    runtimeVal_->setText(statusText.isEmpty() ? QStringLiteral("-") : statusText);
}
void ContextBar::setEvidenceRoot(const QString& evidenceRoot) {
    QString text = evidenceRoot;
    text.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const int slash = text.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) {
        text = text.mid(slash + 1);
    }
    evidenceVal_->setText(text.isEmpty() ? QStringLiteral("-") : text);
    evidenceVal_->setToolTip(evidenceRoot);
}

QPushButton* ContextBar::addActionButton(const QString& text) {
    auto* button = new QPushButton(text, this);
    button->setObjectName(QStringLiteral("ContextActionButton"));
    button->setMinimumHeight(28);
    const int insertIndex = std::max(0, layout_->count() - 1);
    layout_->insertWidget(insertIndex, button);
    return button;
}

} // namespace twin
