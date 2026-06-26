#pragma once

#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

class QTimer;

namespace twin {

// 平台运行控制器：只负责从 UI 启动对象包的 Runtime Host 主链路脚本，并跟踪进度文件。
// 真实模型初始化、DLL/adapter 生命周期和 evidence 写入仍由平台 Runtime Host 完成。
class PlatformRunController : public QObject {
    Q_OBJECT
public:
    PlatformRunController(QString workspaceRoot, QString objectPackageRoot, QObject* parent = nullptr);

    bool isRunning() const;
    QString activeEvidenceRoot() const { return activeEvidenceRoot_; }
    void setObjectPackageRoot(const QString& objectPackageRoot);

public slots:
    void prepareDefaultMainline();
    void startDefaultMainline();
    void prepareWorkflow(const QString& workflowId, const QString& profileId);
    void startWorkflow(const QString& workflowId, const QString& profileId);
    void stopCurrentRun();

signals:
    void evidenceRootChanged(const QString& evidenceRoot);
    void progressUpdated(const QJsonObject& progress);
    void logLine(const QString& line);
    void statusChanged(const QString& status, const QString& message);
    void runFinished(int exitCode, const QString& status);

private slots:
    void readProcessOutput();
    void pollProgress();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QString makeRunId() const;
    QString defaultWorkflowId() const;
    QString defaultProfileId(const QString& workflowId) const;
    QString stableWorkflowDirName(const QString& workflowId) const;
    QJsonObject readProgressFile();
    void emitSyntheticProgress(const QString& stage, const QString& status, const QString& message, double percent);
    void startMainline(bool prepareOnly);
    void startWorkflowInternal(bool prepareOnly, const QString& workflowId, const QString& profileId);
    void enqueueStep(QString stage, QString message, QString program, QStringList arguments);
    void startNextStep();

    QString workspaceRoot_;
    QString objectPackageRoot_;
    QString activeRunId_;
    QString activeWorkflowId_;
    QString activeProfileId_;
    QString activeEvidenceRoot_;
    QString progressPath_;
    QString currentStepStage_;
    QProcess* process_ = nullptr;
    QTimer* progressTimer_ = nullptr;
    qint64 lastProgressMtime_ = -1;
    bool watchDetachedBranches_ = false;

    struct RunStep {
        QString stage;
        QString message;
        QString program;
        QStringList arguments;
    };
    QVector<RunStep> pendingSteps_;
};

} // namespace twin
