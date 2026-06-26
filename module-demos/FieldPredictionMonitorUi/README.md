# 场预测模块 Demo UI

这是独立 Qt Widgets demo，用于观察 `field_prediction` 模块的公开 DTO。它不混入 `EnvNodeController`，也不调用真实场预测内核。

默认订阅 `/flightenv/field_prediction`、`/flightenv/runtime_snapshot` 和 `/flightenv/prediction_result`，消息载荷为 `std_msgs/String` 中的公开 JSON DTO。

## 功能

- 展示 `model_type=field_prediction`、`model_id`、`asset_id`、`input_topic`、`output_topic`、`algorithm`、`grid_size`、`update_rate`。
- 订阅 `FieldPredictionFrame`，实时显示节点发布的 `FieldSummaryDTO` 摘要。
- 订阅 `RuntimeSnapshotDTO` 和 `PredictionResultDTO`，在真实飞船模型 VTK 网格上显示节点级预测场。
- 展示 `FieldSummaryDTO` 表格，包括 `min/mean/max/status`。
- 展示历史预测场帧，包括 source、status、图层数、峰值和模型 id。

`FieldPredictionFrame` 只有摘要值时只更新表格和历史记录，不能绘制云图。VTK 云图必须来自 `RuntimeSnapshotDTO.field_layouts/meshes` 与 `PredictionResultDTO.fields` 的节点场值；缺少任一项时界面显示等待或缺场数据。

## 边界

- 不编辑 `EnvNodeController`。
- 不 include 或链接 `flightenv-runtime-private/**` 私有源码。
- 不 include 或编译 `flightenv-node-sdk/source-supported/**`。
- 不直接链接 `EnvPredictorCore.lib` 或 `EnvDataBase.lib`。

真实结果由 runtime 节点产出，本 UI 只消费 ROS topic 中的公开 DTO。
