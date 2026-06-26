# 损伤累计模块 Demo UI

这是独立 Qt Widgets demo，用于观察 `damage_assessment` 模块的公开 DTO。它不混入 `EnvNodeController`，也不调用真实损伤模型。

默认订阅 `/flightenv/damage_assessment`、`/flightenv/runtime_snapshot` 和 `/flightenv/damage_field`，消息载荷为 `std_msgs/String` 中的公开 JSON DTO。

## 功能

- 展示 `model_type=damage_assessment`、`model_id`、`asset_id`、`input_topic`、`output_topic`、`damage_rule`、`threshold`。
- 订阅 `DamageAssessmentFrame`，实时显示节点发布的 `DamageIncrementDTO`。
- 绘制累计损伤曲线和阈值线。
- 订阅 `FieldBundleDTO`，在真实飞船模型 VTK 网格上展示节点级累计损伤场。
- 展示损伤准则表，包括 Miner 累计损伤、最大温度准则、最大应力准则、最大热流准则和单帧损伤增量。
- 展示历史损伤帧，包括 delta、cumulative、Tmax、stress 和 status。

最大温度、最大应力和热流优先读取 `diagnostic.evidence`；如果真实节点暂未发布这些 evidence，demo 使用 fallback 代理值展示 UI 形态。它们是诊断展示准则，不替代核心损伤模型。

`DamageAssessmentFrame` 只表达损伤摘要、增量和准则 evidence，不能生成云图。损伤云图必须来自 `/flightenv/damage_field` 的节点场值，并依赖 `/flightenv/runtime_snapshot` 中的真实飞船模型布局和面片索引。

## 边界

- 不编辑 `EnvNodeController`。
- 不 include 或链接 `flightenv-runtime-private/**` 私有源码。
- 不 include 或编译 `flightenv-node-sdk/source-supported/**`。
- 不直接链接 `EnvPredictorCore.lib` 或 `EnvDataBase.lib`。

真实结果由 runtime 节点产出，本地线性累计只作为无节点时的 fallback。
