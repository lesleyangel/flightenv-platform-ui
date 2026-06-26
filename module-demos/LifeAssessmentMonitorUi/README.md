# 寿命评估模块 Demo UI

这是独立 Qt Widgets demo，用于观察 `life_assessment` 模块的公开 DTO。它默认接入真实寿命评估节点输出，但不消费 runtime-private、node-sdk/source-supported 或算法内核源码。

默认订阅 `/flightenv/life_assessment`、`/flightenv/runtime_snapshot` 和 `/flightenv/life_field`，消息载荷为 `std_msgs/String` 中的公开 JSON DTO。

## 功能

- 展示 `model_type=life_assessment`、`model_id`、`asset_id`、`input_topic`、`output_topic`、`life_rule`、`threshold`。
- 展示 `current_damage`、`damage_rate`、`rul_s`、`first_exceed_time`、`status` 和 source frame。
- 绘制 RUL 时间趋势。
- 订阅 `FieldBundleDTO`，在真实飞船模型 VTK 网格上展示节点级剩余寿命场。
- 展示历史寿命估计，包括 damage、RUL、first exceed 和 status。
- 真实节点推流时，直接展示 `/flightenv/life_assessment` 中的 `health_state`、`rul_s` 和 `first_limit_exceedance_s`。

`LifeAssessmentFrame` 只有全局寿命标量时只更新 KPI、趋势和历史记录，不能绘制云图。剩余寿命云图必须来自 `/flightenv/life_field` 的节点级 RUL 场；界面会把场中的最小 RUL 和对应节点作为全局寿命瓶颈显示，某节点 RUL 到 0 即代表首超/失效已经发生。

## Fallback Demo 算法

以下算法只在手动启用本地演示时使用：

```text
remaining_damage = threshold - current_damage
rul_s = remaining_damage / damage_rate
first_exceed_time = source_frame.timestamp + rul_s
```

当 `current_damage >= threshold` 时，RUL 置为 `0 s`。该算法没有标定、不建模不确定性，也不代表真实结构寿命结论。

## 边界

- 不编辑 `EnvNodeController`。
- 不 include 或链接 `flightenv-runtime-private`、`flightenv-node-sdk/source-supported/**`、`EnvPredictorCore`、`EnvDataBase`。
- 真实寿命结果由 runtime 节点产出，再由 UI 消费公开 DTO。

## 构建

```powershell
MSBuild .\module-demos\LifeAssessmentMonitorUi\LifeAssessmentMonitorUi.vcxproj /m /p:Configuration=Release;Platform=x64
```
