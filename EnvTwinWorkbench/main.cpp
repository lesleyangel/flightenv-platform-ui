#define NOMINMAX
#include <Windows.h>
#undef byte

#include <QApplication>
#include <QDir>
#include <QFile>

#include <memory>

#include "EnvNodeTools/Utf8Console.h"

#include "datahub/LegacyRunCatalogSource.h"
#include "datahub/LiveDataHub.h"
#include "shell/TwinWorkbenchWindow.h"

namespace {

// 从 exe 所在目录上溯定位工作区根。evidence 输出目录与 platform catalog 都基于它解析。
QDir resolveWorkspaceRoot() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (dir.exists(QStringLiteral("flightenv-runtime-private")) ||
            dir.exists(QStringLiteral("_local_artifacts"))) {
            return dir;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QDir(QCoreApplication::applicationDirPath());
}

// 平台 Runtime Host 的 evidence 输出目录。
// 无参数启动时不跟随任何旧 evidence；由 UI 内启动/载入对象后再建立当前 run。
QString resolveEvidenceRoot(int argc, char** argv, const QDir& workspace) {
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith(QStringLiteral("--evidence="))) {
            return arg.mid(QStringLiteral("--evidence=").size());
        }
        if (arg.startsWith(QStringLiteral("--online-root="))) {
            return arg.mid(QStringLiteral("--online-root=").size());
        }
    }
    return {};
}

// platform catalog 路径。允许 --catalog= 显式覆盖。
QString resolveCatalogPath(int argc, char** argv, const QDir& workspace) {
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith(QStringLiteral("--catalog="))) {
            return arg.mid(QStringLiteral("--catalog=").size());
        }
    }
    return workspace.filePath(QStringLiteral(
        "_local_artifacts/flightenv-runtime-private/platform/platform-catalog.json"));
}

// 对象包是新平台 UI 的主真源；catalog 只作为 run/model 兼容视图。
// 无参数启动时保持未载入状态，避免 UI 天生绑定某个具体对象。
QString resolveObjectPackageRoot(int argc, char** argv, const QDir&) {
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith(QStringLiteral("--object-package="))) {
            return arg.mid(QStringLiteral("--object-package=").size());
        }
    }
    return {};
}

void applyTheme(QApplication& app) {
    QFile qss(QStringLiteral(":/theme/twin.qss"));
    if (qss.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    }
}

} // namespace

int main(int argc, char* argv[]) {
    configure_utf8_console();
    QApplication app(argc, argv);
    applyTheme(app);

    const QDir workspace = resolveWorkspaceRoot();
    const QString evidenceRoot = resolveEvidenceRoot(argc, argv, workspace);
    const QString catalogPath = resolveCatalogPath(argc, argv, workspace);
    const QString objectPackageRoot = resolveObjectPackageRoot(argc, argv, workspace);

    auto hub = std::make_unique<twin::LiveDataHub>(evidenceRoot);
    auto legacyRunCatalog = std::make_unique<twin::LegacyRunCatalogSource>(catalogPath);

    auto window = std::make_unique<twin::TwinWorkbenchWindow>(
        hub.get(), legacyRunCatalog.get(), workspace.absolutePath(), objectPackageRoot);
    // 平台工作台信息密度高，默认最大化，给布局充分的横向空间。
    window->showMaximized();
    // 窗口构造函数会把各页面连接到 LiveDataHub。必须在连接完成后再 start，
    // 否则 start() 里第一次 evidence 轮询发出的 timelineUpdated 会被页面错过。
    hub->start();

    const int ret = app.exec();

    window.reset();
    legacyRunCatalog.reset();
    hub.reset();
    return ret;
}
