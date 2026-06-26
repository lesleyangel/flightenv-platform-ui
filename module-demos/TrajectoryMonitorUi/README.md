# 弹道模块 Demo UI

这是独立 Qt Widgets demo，用于观察弹道模块的公开 DTO。它只位于 `flightenv-controller-ui/module-demos/TrajectoryMonitorUi`，不改动 `EnvNodeController`。

默认订阅：

- `/flightenv/state_estimate`
- `/flightenv/trajectory_prediction`

## 功能

- 展示状态 topic、弹道 topic、`model_id`、`rate_hz`、`horizon_s`、`step_s`、`max_samples`。
- 绘制当前状态点、预测弹道折线、预测首点和预测末点。
- 显示最新状态 readout：时间、X、高度、Mach、动压、攻角。
- 展示最近状态帧。
- 展示历史预测弹道，包括采样数、起止时间、预测航程、起点高度、首点连续性误差和状态。
- 展示最新预测弹道的采样点明细，包括时间、位置、高度、速度、Mach、动压和法向过载。

## 边界

本 demo 不 include 或链接：

- `flightenv-runtime-private`
- `flightenv-node-sdk/source-supported/**`
- `flightenv-trajectory` 算法内核源码
- `EnvPredictorCore` / `EnvDataBase` 私有实现

本地模拟源只用于无节点时占位；接入真实链路时，以 ROS topic 中的节点输出为准。

## 构建

```powershell
MSBuild .\module-demos\TrajectoryMonitorUi\TrajectoryMonitorUi.vcxproj /m /p:Configuration=Release;Platform=x64
```
