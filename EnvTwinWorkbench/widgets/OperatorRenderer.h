#pragma once

#include "../datahub/PdkUiReaders.h"

#include <QString>
#include <QWidget>

#include <functional>
#include <memory>

namespace twin {

// 算子展示上下文：renderer 渲染一个算子输出所需的全部输入。
// 设计文档 §6.4.1 / §8：算子只声明 display_descriptor，平台 RendererRegistry
// 根据 renderer_id + 运行证据选择并填充标准展示控件，算子本身不携带 UI 代码。
struct OperatorRenderContext {
    PdkOperatorView op;                          // 算子 spec + display descriptor
    QString evidenceRoot;                        // 当前 run evidence 根（可空 = 仅 spec 浏览）
    QString objectPackageRoot;                   // 对象包根（解析 mesh/资源）
    QVector<PdkDataPlaneEntryView> dataPlane;    // 本 run 的 DataPlane 条目（可空）
};

// 平台标准 renderer 接口。实现位于 flightenv-controller-ui（平台展示组件库），
// 不在对象包或算子实现仓里。
class IOperatorRenderer {
public:
    virtual ~IOperatorRenderer() = default;
    virtual QString rendererId() const = 0;
    // 为算子构建展示控件；约定 parent 持有所有权。
    virtual QWidget* createWidget(const OperatorRenderContext& ctx, QWidget* parent) const = 0;
};

// renderer 选择结果：供 UI 显示"匹配到哪个 renderer / 是否走了 fallback"。
struct RendererMatch {
    QString requestedRendererId;   // display_descriptor 声明的 renderer_id
    QString resolvedRendererId;    // 实际使用的 renderer_id
    bool usedFallback = false;     // 是否回退（声明的未注册）
    bool usedGeneric = false;      // 是否最终回退到通用端口视图
};

// 平台 RendererRegistry：renderer_id -> 工厂。
// 解析顺序：display.rendererId -> display.fallbackRenderer -> generic_operator_ports.v1。
class RendererRegistry {
public:
    static RendererRegistry& instance();

    void registerRenderer(std::unique_ptr<IOperatorRenderer> renderer);
    bool hasRenderer(const QString& rendererId) const;

    // 解析将为该算子使用的 renderer（不构建控件，仅判定匹配/fallback）。
    RendererMatch resolve(const PdkOperatorView& op) const;

    // 按 resolve 结果构建控件；任何情况下都返回非空控件（最差为通用视图）。
    QWidget* render(const OperatorRenderContext& ctx, QWidget* parent) const;

private:
    RendererRegistry();
    void ensureDefaults();

    QHash<QString, std::shared_ptr<IOperatorRenderer>> renderers_;
};

} // namespace twin
