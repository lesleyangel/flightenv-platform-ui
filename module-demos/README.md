# FlightEnv 模块 Demo UI

本目录放置独立模块观察界面，用来查看各节点公开 DTO 的实时推流、算法展示量和历史帧。它们不是 `EnvNodeController` 主程序的一部分，也不承载 runtime-private 算法实现。

默认数据源是 ROS2 `std_msgs/String` topic 中的公开 JSON DTO；没有真实节点输入时，可以手动启用本地 fallback 数据源，方便独立检查 UI 布局和接口形态。

## 边界

- 不 include 或编译 `flightenv-runtime-private/**` 私有源码。
- 不 include 或编译 `flightenv-node-sdk/source-supported/**`。
- 不直接链接 `EnvPredictorCore.lib` 或 `EnvDataBase.lib`。
- 不把模块 demo 代码混入 `EnvNodeController`。
- 真实算法、数据库、训练包、预测和评估逻辑仍由对应 runtime/node/sdk 层提供。

## 当前模块

- `TrajectoryMonitorUi`：展示当前状态、未来预测弹道、预测首末点、采样明细和历史预测帧。
- `FieldPredictionMonitorUi`：展示场预测摘要、真实飞船模型 VTK 预测场和历史预测场。
- `DamageAssessmentMonitorUi`：展示损伤增量、累计曲线、真实飞船模型 VTK 损伤场、最大温度/最大应力/热流等准则和历史损伤帧。
- `LifeAssessmentMonitorUi`：展示 RUL、首超时间、RUL 趋势、真实飞船模型 VTK 剩余寿命场和历史寿命估计。

## 实时 Topic

- `TrajectoryMonitorUi` 订阅 `/flightenv/state_estimate` 和 `/flightenv/trajectory_prediction`。
- `FieldPredictionMonitorUi` 订阅 `/flightenv/field_prediction`、`/flightenv/runtime_snapshot` 和 `/flightenv/prediction_result`。
- `DamageAssessmentMonitorUi` 订阅 `/flightenv/damage_assessment`、`/flightenv/runtime_snapshot` 和 `/flightenv/damage_field`。
- `LifeAssessmentMonitorUi` 订阅 `/flightenv/life_assessment`、`/flightenv/runtime_snapshot` 和 `/flightenv/life_field`。

四个 demo 均使用 `flightenv-contracts` 中对应 Frame 的 `nlohmann::json` 反序列化能力，不直接 include 或链接各节点私有实现。

## 场云图口径

预测、损伤和寿命云图必须绑定真实飞船模型：UI 需要先收到 `RuntimeSnapshotDTO` 中的 `field_layouts`、`meshes` 和节点坐标，再收到 `PredictionResultDTO` 或 `FieldBundleDTO` 中的节点场值，随后由 VTK 按面片索引文件贴色渲染。`FieldPredictionFrame`、`DamageAssessmentFrame`、`LifeAssessmentFrame` 只作为摘要/指标/历史表输入，不能反推或生成云图。

当前真实面片索引来自共享资产目录，例如 `_deps/example/ele_aero_surf.txt`、`_deps/example/ele_surf_whole.txt`、`_deps/example/ele_in_whole.txt`。如果节点只发布全局标量而没有节点场，UI 会显示等待或缺场数据，不再绘制伪场。

## Visual Studio

打开以下任一 solution 后，可以分别将对应项目设为启动项目：

```text
flightenv-controller-ui.sln
..\FlightEnvMultiRepo.sln
```

构建输出统一放在共享工作区：

```text
..\_deps\workspace\module-demos\<ProjectName>\x64\Release\
```

VS 调试时先启动对应运行时节点链路。UI demo 启动后会显示订阅 topic，节点开始发布后界面会实时刷新；本地模拟源默认关闭，只作为无节点时的 fallback。
