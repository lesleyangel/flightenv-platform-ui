# FlightEnv 数字孪生平台原型设计简报

日期：2026-06-10

用途：把本文直接交给 Claude Design / UI 原型工具，让它基于 FlightEnv 当前平台设计稿生成一套工程数字孪生运行平台原型。

## 1. 平台画像

FlightEnv 不是普通 IoT 大屏，也不是 CAD/CAE/PLM 替代品。它的准确定位是：

> 面向复杂工程对象的模型驱动数字孪生运行平台。

平台核心能力不是“把数据画得炫”，而是把工程对象、传感器/数据库输入、模型资产、状态估计、未来推演、物理场预测、损伤累计、剩余寿命评估、历史回放和证据链统一到一个可运行、可复现、可审计的工作台里。

第一版原型应该让用户一眼看懂：

```text
我当前看的是哪个工程对象
这次运行用了哪些数据和模型
当前状态是什么
未来轨迹和关键物理场会怎样发展
损伤和剩余寿命如何变化
结果保存在哪里，能不能回放
如果失败或无法评估，原因是什么
```

## 2. 原型目标

请设计一个桌面端工程工作台原型，目标用户是飞行器/热防护/结构健康/模型联调工程师。

原型应覆盖 7 个主场景：

1. 在线运行监控：看到传感器/滤波/预测/健康评估的实时链路。
2. 工程对象画像：看到飞行器对象树、部件、区域、测点、传感器、模型绑定。
3. 模型资产管理：看到弹道、多场、损伤、寿命等模型资产、版本、checksum、适用范围和校验状态。
4. 算子图编排：看到状态方程、观测方程、滤波算法、QoI、弹道、多场、损伤、寿命这些算子如何连接。
5. 历史回放：打开 run package，按帧查看状态、轨迹、场、损伤、寿命和证据。
6. 健康账本：查看对象/区域的累计损伤、剩余寿命、首超破坏时间、证据来源和维护修正。
7. 诊断报告：解释模型缺失、端口不匹配、输入过期、单位冲突、运行失败和降级原因。

## 3. 产品边界

必须做：

- 做成专业工程软件工作台，不做营销首页。
- 首页就是可操作的工作台，不要 landing page。
- 强调可追溯：run manifest、resource lock、model snapshot、operator snapshot、graph outputs。
- 强调模型资产：模型 ID、版本、类型、执行形态、适用包络、校验报告。
- 强调运行链路：在线滤波、未来预测、QoI、可选物理子图。
- 强调真实工程对象：飞行器三维模型/网格、P/K/S/T 物理场、损伤场、寿命场。

不要做：

- 不要做通用 IoT 大屏。
- 不要做单纯 3D 可视化展示页。
- 不要把 UI 设计成“所有 JSON 字段平铺编辑器”。
- 不要把弹道、多场、损伤、寿命写死成永远必选的固定流程。
- 不要在 UI 里暗示它直接访问内部算法、数据库私有结构或 legacy solver。
- 不要给未经验证的寿命结论做绝对承诺。需要显示 confidence、适用范围、unknown/reason。

## 4. 信息架构

推荐左侧主导航 + 顶部运行上下文栏 + 主工作区布局。

顶部上下文栏固定展示：

- 当前对象：`flightenv_object / Reentry Vehicle`
- 当前阶段：`reentry`
- 当前模式：`Online / Replay / Validation`
- 当前 run：`run_20260610_153000`
- 当前 graph：`state_prediction_qoi_graph.v1`
- Archive 状态：enabled / disabled
- 运行状态：idle / running / degraded / failed / replaying

左侧导航建议：

```text
1. 总览
2. 在线运行
3. 对象画像
4. 模型资产
5. 算子编排
6. 回放实验
7. 健康账本
8. 配置与数据源
9. 诊断报告
```

## 5. 页面设计要求

### 5.1 总览页

用途：回答“这套数字孪生当前处于什么状态”。

核心区域：

- 对象状态摘要：当前高度、马赫数、攻角、时间戳、状态来源。
- 运行链路状态：Sensor / Filter / State / Trajectory / Field / Damage / Life 每个环节一个状态块。
- 风险摘要：最大温度、最大应力、最大损伤、RUL、首超破坏时间。
- 当前模型快照：trajectory、field_prediction、damage_assessment、life_assessment 模型 ID/版本。
- 最近 run 列表：可点击进入回放。

视觉重点：

- 使用状态色：ok 绿色、degraded 黄色、failed 红色、unknown 灰色。
- 不要做巨幅炫光背景；要像专业控制台。

### 5.2 在线运行页

用途：实时看“滤波状态 -> 未来预测 -> 场/损伤/寿命”的吞吐。

布局建议：

- 左侧：实时输入和滤波状态。
  - 数据源帧率
  - 最新状态帧 ID
  - 状态新鲜度
  - PF 粒子数、ESS、残差、诊断状态
- 中间：飞行器模型/场显示。
  - 3D 飞船模型或网格占位
  - 可切换场：temperature、stress、damage、remaining life
  - colormap、min/max、当前选中节点值
- 右侧：预测结果。
  - 时间-高度曲线
  - 时间-马赫数/攻角曲线
  - 未来轨迹表格
  - 当前预测 run 编号和耗时
- 底部：workflow timeline。
  - 在线帧数
  - 预测触发次数
  - 每次预测的步数
  - 每个算子的耗时和输出字节

必须表达的逻辑：

```text
实时数据可能有 50/100 帧
在线融合每帧都推进
预测按自己的频率触发
每次预测从当前状态预测到未来 horizon/落点
多场、损伤、寿命沿未来预测步推进
```

### 5.3 对象画像页

用途：让平台知道“对象是谁、测点在哪里、模型绑在哪里”。

布局建议：

- 左侧对象树：
  - vehicle
  - shell
  - nose cap
  - thermal protection region
  - sensor mount / node set
- 中间 3D/网格视图：
  - 显示选中的部件/区域
  - 传感器 marker
  - 节点集/区域高亮
- 右侧详情面板：
  - object_id
  - object_type
  - geometry_ref
  - material_ref
  - sensor bindings
  - model bindings

### 5.4 模型资产页

用途：管理和审查模型资产，不是训练模型本身。

表格列：

- model_id
- model_type：trajectory / field_prediction / damage / life / mapper
- runtime_type：in_process / dll / cli_exe / ros2_exe
- version
- artifact_ref
- checksum
- applicable_envelope
- validation_status
- enabled
- priority

模型详情侧栏：

- 输入端口和输出端口
- 使用资产：DB、POD 基、BPNN 网络、mesh、trajectory package
- 校验报告链接
- 最近使用它的 run

### 5.5 算子编排页

用途：展示 graph，不要做成固定节点串联。

核心概念：

主干算子：

```text
state_transition
observation_equation
filter_algorithm
qoi_equation
```

可选物理子算子：

```text
trajectory_projection
field_reconstruction
damage_accumulation
failure_criterion
life_assessment
```

页面布局：

- 中间 graph canvas：节点 + 端口连接。
- 左侧模板列表：
  - online_bayes_filter_graph.v1
  - state_prediction_qoi_graph.v1
  - trajectory_only_forecast
  - structural_health_forecast
- 右侧节点详情：
  - operator_id
  - operator_type
  - role
  - execution.kind
  - inputs/outputs contract
  - assets
  - status
  - reason
- 底部 evidence 文件：
  - graph_snapshot.json
  - operator_snapshot.json
  - resource_lock.json
  - model_snapshot.json
  - graph_outputs.json
  - run_manifest.json

重要设计口径：

- `priority` 只用于模型选择，不代表 DAG 执行顺序。
- graph 执行顺序来自端口依赖。
- optional group 关闭时要显示 disabled/reason，不要静默消失。

### 5.6 回放实验页

用途：打开历史 run、回看帧、比较模型版本。

布局建议：

- 左侧 run catalog：
  - run_id
  - object_id
  - phase
  - status
  - started_at
  - model snapshot summary
- 中间时间轴：
  - frame summaries
  - 当前帧游标
  - 播放/暂停/上一帧/下一帧/倍速
- 右侧当前帧：
  - 状态
  - 轨迹曲线
  - 场视图
  - 损伤/寿命摘要
  - evidence links
- 底部实验对比：
  - input_run
  - model_package_A / B
  - 输出指标差异
  - 风险等级变化

### 5.7 健康账本页

用途：展示长期损伤和寿命，不只是单次 run 的一个数。

核心内容：

- 对象/区域列表：nose cap、thermal protection、structure region。
- 当前损伤：0-1 范围，超过阈值后显示 saturated。
- 损伤增量来源：run_id、frame range、model_id、evidence source。
- RUL：剩余寿命估计、置信度、适用范围。
- 首超破坏时间：first_exceedance_s / unknown。
- 维护修正：manual correction、原因、操作者、时间。
- 趋势图：damage over runs、RUL over runs。
- 寿命场：remaining life field，必须绑定飞船模型/网格，不要画随机热力图。

### 5.8 配置与数据源页

用途：以用户语言配置运行，不要平铺 JSON。

一级分区：

- 运行概览
- 数据源与同步
- 运行模式与诊断
- 模型来源
- 模型细节
- 在线归档

高频控件：

- 运行模式：标准在线流程 / 无观测只预测 / 训练诊断 / 回放验证。
- 数据源：database / replay / sensor。
- 是否先重训。
- 是否启用 archive。
- frame_storage：binary / jsonl / both。
- 输出目录预览。

### 5.9 诊断报告页

用途：解释“为什么可信/为什么失败”。

内容：

- Preflight 检查：
  - catalog 是否存在
  - model binding 是否完整
  - artifact 是否存在
  - checksum 是否匹配
  - 端口 contract 是否匹配
  - 单位是否匹配
- Runtime 诊断：
  - 输入过期
  - 算子失败
  - 模型不适用当前包络
  - 输出 missing/degraded/unknown
- 报告输出：
  - validation_report.json
  - graph_run_evidence.json
  - dashboard csv
  - run summary

## 6. 关键数据对象

原型中可以用 mock 数据，但命名要贴近真实契约。

建议包含：

```text
TwinObject
SensorChannel
ModelDefinition
ModelBinding
ModelPort
DataAssetRef
WorkflowDefinition / EquationGraphTemplate
OperatorSpec
GraphRunEvidence
RunManifest
ResourceLock
ModelSnapshot
StateFrame / StateEstimateFrame
TrajectoryPredictionFrame
FieldForecastFrame
DamageForecastFrame
LifeAssessmentFrame
HealthLedger
```

典型端口名：

```text
state.posterior
state.initial
state.future
observation.predicted
filter.diagnostics
qoi.series
trajectory.forecast
field.P.forecast
field.K.forecast
field.S.forecast
field.T.forecast
field.forecast
damage.current
damage.forecast
failure.assessment
life.assessment
life.field
```

## 7. 视觉风格

推荐风格：

- 工程软件 / 仿真工作台 / 运行控制台。
- 深浅均可，但优先清晰、可读、密集有序。
- 适合 1440x900 或 1600x1000 桌面屏。
- 表格、曲线、状态块、graph canvas 和 3D/网格视图并重。
- 色彩克制：中性色背景 + 蓝/青作为信息色 + 绿黄红灰作为状态色。
- 字体清晰，中文界面，术语专业但不要堆代码变量名。

不要：

- 不要大面积紫蓝渐变。
- 不要做成科幻大屏。
- 不要只放几个巨型 KPI。
- 不要用装饰性卡片堆满页面。
- 不要让文本挤出按钮/卡片。

## 8. 原型交互要求

至少实现这些交互：

1. 左侧导航切换 9 个页面。
2. 在线运行页可以模拟开始/暂停/replay 模式。
3. 算子编排页点击节点，右侧显示节点详情。
4. 回放页拖动时间轴，曲线和当前帧摘要变化。
5. 模型资产页点击模型，显示模型端口和资源锁。
6. 健康账本页切换对象区域，损伤/RUL/寿命场随之更新。
7. 诊断页可在 ok/degraded/failed 三种状态之间切换示例。

## 9. 可直接给 Claude Design 的提示词

```text
你是一名资深工业软件产品设计师和前端原型设计师。请基于下面的设计简报，为 FlightEnv 设计一套“工程对象数字孪生运行平台”桌面端高保真交互原型。

请注意：这不是普通 IoT 大屏，不是 CAD/CAE/PLM 替代品，也不是营销首页。首页必须直接是可操作的工程工作台。目标用户是飞行器再入环境预测、热防护、结构健康和模型联调工程师。

平台定位：
面向复杂工程对象的模型驱动数字孪生运行平台。它统一管理工程对象、传感器/数据库输入、模型资产、状态估计、未来推演、物理场预测、损伤累计、剩余寿命评估、历史回放和证据链。

请设计一个桌面端应用，推荐 1440x900 画布。使用中文界面。整体风格应是专业工程工作台：清晰、克制、信息密度高、适合长期使用。不要做科幻大屏，不要用大面积渐变，不要做 landing page。

必须包含以下页面：
1. 总览
2. 在线运行
3. 对象画像
4. 模型资产
5. 算子编排
6. 回放实验
7. 健康账本
8. 配置与数据源
9. 诊断报告

全局布局：
- 左侧导航栏。
- 顶部上下文栏，显示 object_id、phase、mode、run_id、graph template、archive 状态、运行状态。
- 主工作区按页面变化。

核心业务链路：
实时/回放数据进入后，平台执行在线滤波，得到 state.posterior；预测侧按自身频率从当前状态出发执行未来预测 graph。graph 主干是 state_transition、observation_equation、filter_algorithm、qoi_equation；弹道、多场、损伤、失效、寿命是可选物理子算子，不要设计成永远固定必选的节点链。

在线运行页必须展示：
- 传感器/数据库输入帧率。
- PF/滤波状态，包括 ESS、残差、当前状态帧。
- 未来轨迹曲线，例如时间-高度、时间-马赫数、时间-攻角。
- 真实飞行器模型/网格上的场显示，占位也要看起来像工程网格，而不是随机热力图。
- 可切换 temperature、stress、damage、remaining life 四类场。
- workflow timeline，显示在线帧、预测触发次数、每个算子耗时、输出字节、状态。

对象画像页必须展示：
- 飞行器对象树：vehicle、shell、nose cap、thermal protection region、sensor mount。
- 3D/网格视图中高亮对象区域和传感器 marker。
- 右侧展示 object_id、geometry_ref、material_ref、sensor bindings、model bindings。

模型资产页必须展示：
- trajectory、field_prediction、damage_assessment、life_assessment 等模型资产表。
- 字段包括 model_id、model_type、runtime_type、version、artifact_ref、checksum、applicable_envelope、validation_status、enabled、priority。
- 点击模型后显示输入端口、输出端口、DB/POD/BPNN/mesh/trajectory package 等资源引用。

算子编排页必须展示：
- graph canvas，节点包括 state_transition、observation_equation、filter_algorithm、qoi_equation、trajectory_projection、field_reconstruction、damage_accumulation、failure_criterion、life_assessment。
- 右侧节点详情显示 operator_id、operator_type、role、execution.kind、inputs、outputs、assets、status、reason。
- 底部 evidence 文件列表：graph_snapshot.json、operator_snapshot.json、resource_lock.json、model_snapshot.json、graph_outputs.json、run_manifest.json。
- optional group 关闭时显示 disabled/reason。

回放实验页必须展示：
- run catalog 列表。
- 时间轴、播放/暂停、上一帧/下一帧、倍速。
- 当前帧的状态、轨迹、场、损伤、寿命。
- 两个 run 或两个模型包的对比摘要。

健康账本页必须展示：
- 对象或区域的累计损伤，范围 0-1。
- RUL、首超破坏时间、confidence、适用范围、unknown/reason。
- damage over runs 和 RUL over runs 趋势。
- remaining life field，绑定飞行器模型/网格显示。
- 维护修正记录。

配置与数据源页不要平铺 JSON，而要用用户语言组织：
- 运行概览
- 数据源与同步
- 运行模式与诊断
- 模型来源
- 模型细节
- 在线归档
高频控件包括运行模式、数据库路径、模型文件、是否先重训、是否启用 archive、frame_storage、输出目录预览。

诊断报告页必须解释：
- catalog 缺失、模型绑定缺失、artifact 缺失、checksum 不匹配、端口 contract 不匹配、单位冲突、输入过期、算子失败、模型不适用当前包络。
- 每条诊断要有 severity、source、reason、suggested action。

请使用 mock 数据，但命名要贴近真实平台：
StateFrame、StateEstimateFrame、TrajectoryPredictionFrame、FieldForecastFrame、DamageForecastFrame、LifeAssessmentFrame、HealthLedger、RunManifest、ResourceLock、ModelSnapshot、OperatorSpec、EquationGraphTemplate。

视觉要求：
- 专业、工程、控制台、可维护。
- 不要过度装饰，不要科幻大屏。
- 使用状态色：ok 绿色，degraded 黄色，failed 红色，unknown 灰色。
- 使用表格、时间轴、曲线图、算子图、3D/网格视图、资源锁列表、证据文件列表。
- 所有中文文本必须能放进容器，不要溢出。

最终输出：
- 给出完整可交互原型。
- 页面之间能通过左侧导航切换。
- 每个页面都要有真实感 mock 数据。
- 重点展示平台的“对象-模型-运行-证据-健康-回放”闭环。
```

## 10. 给 Claude 的补充资料摘要

可以把下面这段作为背景附在提示词后：

```text
当前 FlightEnv 已经具备多仓结构：
- flightenv-contracts：DTO/API/配置契约
- flightenv-runtime-private：运行时、模型执行、数据库、训练、诊断、GraphRuntimeController
- flightenv-node-sdk：UI/外部工具只读 SDK、ViewModel、run/evidence reader
- flightenv-controller-ui：Qt UI，只消费 SDK 和 DTO
- flightenv-trajectory：弹道 core lib/CLI/node，legacy solver 私有隔离

当前动态编排主线已经从固定多 ROS2 节点链路升级为状态方程/QoI 算子图：
- 在线滤波：state_transition + observation_equation + filter_algorithm
- 未来预测：state_transition + qoi_equation
- 可选物理子图：trajectory_projection、field_reconstruction、damage_accumulation、failure_criterion、life_assessment

当前真实 evidence 包括：
- graph_snapshot.json
- operator_snapshot.json
- resource_lock.json
- model_snapshot.json
- graph_outputs.json
- run_manifest.json

UI 原型不要穿透 runtime-private 内部实现；所有展示都假设来自 SDK / DTO / evidence reader。
```

## 11. 原型验收标准

这套 Claude Design 原型如果做得好，应该满足：

1. 用户不看代码，也能理解 FlightEnv 是工程数字孪生运行平台。
2. 用户能看出平台不是大屏，而是对象、模型、运行、证据、健康和回放工作台。
3. 用户能看出弹道、多场、损伤、寿命是可选算子，不是 UI 写死流程。
4. 用户能找到一次 run 使用了哪些模型和资源。
5. 用户能看到当前状态、未来轨迹、场、损伤、寿命和历史回放如何联动。
6. 用户能看到结果可信度、失败原因和证据文件。
7. 页面信息密度足够工程化，但不混乱。
