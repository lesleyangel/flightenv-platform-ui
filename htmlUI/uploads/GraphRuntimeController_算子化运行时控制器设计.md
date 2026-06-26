# GraphRuntimeController 算子化运行时控制器设计

日期：2026-06-08

## 0. 代码入口速查（头文件优先）

这一节用于回答“主框架在哪、算子模板在哪、编排器在哪、现有流程在哪”。后续开发先看这些头文件，再进入对应 `.cpp` 看实现细节。

### 0.1 主框架：滤波、观测、预测的顶层组织

| 你要找的问题 | 代码入口 | 说明 |
| --- | --- | --- |
| 旧 cfg 如何升级为统一 graph | `flightenv-runtime-private/EnvPredictorIO/CfgToEquationGraphAdapter.h` | `build_equation_graph_from_cfg(...)` 把 launcher/model cfg 转成 `EquationGraphTemplate + OperatorSpec catalog`。 |
| 在线滤波主链路在哪里 | `flightenv-runtime-private/EnvPredictorIO/OnlinePfGraphAdapters.h` | `OnlinePfGraphAdapter` 把 `RuntimeEngineV2 + SimplePF` 包装成 `state_transition`、`observation_equation`、`filter_algorithm` 三类算子。 |
| 预测侧多物理场在哪里 | `flightenv-runtime-private/EnvPredictorIO/FieldForecastGraphAdapters.h` | `FieldForecastGraphAdapter` 把 P/K/S/T 四场作为可选 field operator 注册到 Controller。 |
| 损伤累计、破坏判据、寿命评估在哪里 | `flightenv-runtime-private/EnvPredictorIO/StructuralHealthGraphAdapters.h` | `StructuralHealthGraphAdapter` 把结构健康链路拆成 `damage_accumulation`、`failure_criterion`、`life_assessment` 三类算子。 |
| 真实模型/真实资源从哪接入 | `flightenv-runtime-private/tools/equation_graph/GraphRuntimeRealModelAdapters.h` | 读取 platform catalog，按对象和阶段锁定 DB/POD/BPNN/mesh/trajectory/model 资产，并注册真实模型 handler。 |

### 0.2 算子通用模板

| 模板层 | 代码入口 | 应该看什么 |
| --- | --- | --- |
| 单个算子模板 | `flightenv-contracts/include/EnvContracts/dto/OperatorSpec.hpp` | `OperatorSpec`、`OperatorPortSpec`、`OperatorExecutionSpec`、`OperatorAssetRef`。它定义“一个算子有哪些端口、怎么执行、依赖哪些资产”。 |
| 一张图的模板 | `flightenv-contracts/include/EnvContracts/dto/EquationGraphTemplate.hpp` | `EquationGraphTemplate`、`GraphOperatorNodeSpec`、`OptionalOperatorGroupSpec`。它定义“哪些算子按什么端口关系组成一次在线滤波或未来预测”。 |
| 算子执行上下文 | `flightenv-runtime-private/EnvPredictorIO/EquationGraphRuntime.h` | `EquationOperatorContext`、`EquationOperatorHandler`、`EquationGraphRunRequest/Result`。它定义 handler 收什么、出什么。 |

新增物理模型时，不应先写新的专用链路，而应先落到 `OperatorSpec`：声明输入端口、输出端口、执行形态、资产引用，再让 graph 模板引用它。

### 0.3 算子编排器结构

| 结构 | 代码入口 | 基本功能 |
| --- | --- | --- |
| 端口缓存 | `flightenv-runtime-private/EnvPredictorIO/GraphRuntimeController.h` 中的 `GraphPortStore` | 保存当前运行时各逻辑端口的数据，例如 `state.current`、`trajectory.prediction`、`field.P.forecast`。 |
| 编排器配置 | `GraphRuntimeControllerConfig` | 固定本次运行的对象、阶段、输出目录、图模板、算子目录和运行上下文。 |
| 编排器主类 | `GraphRuntimeController` | `configure -> set_port -> register_handler/register_execution_adapter -> run_once -> port_snapshot/evidence`。 |
| 图执行内核 | `flightenv-runtime-private/EnvPredictorIO/EquationGraphRuntime.h` 中的 `EquationGraphRuntime` | 按 `EquationGraphTemplate` 找到 `OperatorSpec` 和 handler，执行算子并汇总输出/evidence。 |
| 外部执行适配 | `flightenv-runtime-private/EnvPredictorIO/GraphExecutionAdapters.h` | 将 `execution.kind=dll/cli_exe/ros2_exe` 转成统一 `EquationOperatorHandler`。 |

`GraphRuntimeController` 是长期会话控制器；`EquationGraphRuntime` 是一次图执行内核。前者管理端口、资源快照和 evidence，后者只负责按图跑算子。

### 0.4 现有示例编排流程

| 流程 | 代码入口 | 当前用途 |
| --- | --- | --- |
| 真实模型 runner | `flightenv-runtime-private/tools/equation_graph/GraphRuntimeControllerRunner.cpp` | 正式入口。读取 cfg/catalog，锁定真实资产，注册真实模型 handler，执行多帧 replay，并写 evidence。 |
| 真实模型 adapter | `flightenv-runtime-private/tools/equation_graph/GraphRuntimeRealModelAdapters.h/.cpp` | 证明每个算子实际使用了哪个 DB/POD/BPNN/mesh/trajectory/model 资产。 |
| 旧 smoke/demo | `GraphRuntimeControllerDemo` 工程 | 仅保留为旧 smoke，不再作为主入口命名。 |

一次有效运行至少应能在 evidence 里看到：

| evidence 文件 | 作用 |
| --- | --- |
| `resource_lock.json` | 本次锁定的数据库、模型包、POD/BPNN、mesh、trajectory 等资产路径和 checksum。 |
| `model_snapshot.json` | 每类模型实际选中的 model_id、version、binding、artifact。 |
| `workflow_snapshot.json` / `graph_snapshot.json` | 本次运行的 graph 模板和节点关系。 |
| `operator_snapshot.json` | 每个算子的端口、执行形态、资产引用和运行元信息。 |
| `graph_outputs.json` | 本次 run 输出的端口数据。 |
| `workflow_timeline.json` | replay/在线运行中的帧级执行记录和吞吐记录。 |

## 1. 结论

`GraphRuntimeController` 是把现有 `EquationGraphRuntime` 从 smoke/MVP 提升到主程序运行入口的控制器。它不应该重新发明物理模型，也不应该替代 `RuntimeEngineV2`、弹道库、多场预测、损伤累计或寿命评估模型；它的职责是把这些能力按统一 graph 和 operator contract 组装起来。

推荐目标形态是：

```text
cfg/catalog
  -> GraphResolver
  -> OperatorResolver
  -> OperatorFactory/Adapter
  -> GraphRuntimeController
  -> EvidenceWriter + SDK/UI reader
```

原来的在线 PF 主流程作为第一个等价验证示例接入：

```text
state_transition(RuntimeEngineV2::transition)
  -> observation_equation(RuntimeEngineV2::observe)
  -> filter_algorithm(SimplePF)
  -> state.posterior
```

原来的未来预测和结构健康链路作为第一个可选物理子图示例接入：

```text
state.initial
  -> trajectory_projection
  -> field.P / field.K / field.S / field.T
  -> field_forecast_merge
  -> damage_accumulation
  -> failure_criterion
  -> life_assessment
```

其中四个物理场或学科 `P/K/S/T` 应该成为 graph 中可独立启停、可独立替换、可独立登记资产的 field operator。当前 legacy 模型里如果存在 `predST` 这类耦合输出，可以先用一个兼容 operator 同时输出 `S/T`，但 graph 端口仍应把 `field.S.forecast` 和 `field.T.forecast` 显式化，后续再拆成单独算子不会影响下游。

## 2. 当前基础

当前仓库已经具备一部分基础，不需要推倒重来。

| 能力 | 当前实现 | 结论 |
| --- | --- | --- |
| graph DTO | `EquationGraphTemplate`、`OperatorSpec`、`GraphRunEvidence` | 可继续使用 |
| graph MVP | `EnvPredictorIO/EquationGraphRuntime.*` | 可作为执行内核雏形 |
| cfg 映射 | `CfgToEquationGraphAdapter.*` | 可作为 GraphResolver 第一版 |
| 在线 PF 主流程 | `MSMFusionProcessor + RuntimeEngineV2 + SimplePF` | 需要封成 online graph adapters |
| 多场/损伤/寿命 forecast | `FieldForecastRuntime`、`DamageAccumulationRuntime`、`LifeForecastRuntime` | 需要封成 operator adapters |
| 固定 workflow | `run_runtime_workflow_orchestrator.ps1` | 保留作兼容验证，后续输出 graph snapshot |
| 数据契约 | `StateFrame`、`ObservationFrame`、`QoiFrame`、`FieldForecastFrame`、`DamageForecastFrame` 等 | 第一版足够，不建议另起“大一统 DTO” |

当前缺口是：`EquationGraphRuntime` 能跑 MVP handler，但主程序仍由 `MSMFusionProcessor` 和固定 workflow/node smoke 分别驱动；`execution.kind=dll/ros2_exe/cli_exe` 目前主要是快照元数据，尚未成为真实执行适配层。

## 3. 数据模板策略

### 3.1 不做新的大一统数据 DTO

不建议新建一个包罗万象的 `UniversalDataFrame`。原因：

1. 现有 contracts 已经能表达主要语义：状态、观测、QoI、轨迹、场、损伤、寿命。
2. 大一统 DTO 会让每个算子都拿到过宽的数据，反而模糊边界。
3. UI、SDK 和归档已经开始围绕现有 DTO 工作，重建数据层成本高。

推荐做法是：保留现有 DTO，并把运行时端口信封分成“JSON 视图”和“强类型载荷”两部分。JSON 视图用于配置、外部适配、证据落盘和 UI/SDK 读取；强类型载荷用于进程内算子之间的高频数据面。

```text
OperatorPortValue
  port_name
  contract_id
  value_json       # 证据/外部适配视图
  typed_payload    # 进程内 DTO / buffer handle / artifact handle
  frame_id
  source_ref
  timestamp
  quality/status
```

它不是新的物理数据模板，只是 graph runtime 内部传递端口值的 envelope。端口里的 `value_json` 必须能反序列化成 `OperatorSpec.inputs/outputs.contract` 声明的 DTO；`typed_payload` 必须与同一个 contract 对应。新 in-process 算子优先消费 `typed_payload`，只有外部 adapter、证据写出或旧 handler 才退回 JSON。

当前代码已经落地第一版：

| 能力 | 代码入口 | 说明 |
| --- | --- | --- |
| typed 输入 | `flightenv-runtime-private/EnvPredictorIO/EquationGraphRuntime.h` 的 `EquationOperatorContext::input_as<T>()` | 优先读强类型载荷，缺失时从 JSON 视图反序列化。 |
| typed 输出 | `EquationOperatorResult::set_output<T>()` | 同时写 JSON 视图和 typed payload。 |
| typed handler | `EquationGraphRuntime::register_typed_handler/register_typed_type_handler` | 允许新算子不再只返回 `map<string,json>`。 |
| typed 端口缓存 | `GraphPortValue::typed`、`GraphRuntimeController::set_port_typed<T>()` | Controller 会把 typed external input 送入运行时，并把 typed output 合并回 PortStore。 |

阶段性约束：

1. JSON handler 继续保留，保证旧 smoke、外部 CLI/DLL/ROS2 adapter 和证据链兼容。
2. typed payload 只保证进程内传递；跨进程执行仍以 JSON request/response 或后续 DLL/binary ABI 为边界。
3. 大型场数据不应长期塞进 JSON 数组，后续应把 `typed_payload` 升级为 `FieldBuffer/ArtifactRef` 一类二进制或句柄载荷，JSON 只保存 layout、路径、checksum 和摘要。

### 3.2 现有 DTO 如何使用

| 场景 | 推荐 DTO |
| --- | --- |
| 在线状态 | `StateFrame` 或兼容 `StateEstimateFrame` |
| 观测 | `ObservationFrame`、`ObservationVectorDTO`、`SensorFrame` |
| 滤波诊断 | `QoiFrame` 或 `filter.diagnostics` JSON |
| 弹道/未来状态 | `TrajectoryPredictionFrame`、`StateFrame`、`QoiFrame(qoi_type=trajectory)` |
| 四个物理场 | `FieldForecastFrame.steps[].fields[]`，用 `subject=P/K/S/T` 区分 |
| 损伤基准 | `DamageStateDTO` |
| 损伤累计 | `DamageForecastFrame` |
| 寿命/首超 | `LifeAssessmentFrame` |
| 运行证据 | `GraphRunEvidence`、`operator_snapshot.json`、`resource_lock.json` |

只有当现有 DTO 无法表达新的长期语义时才新增 contracts。例如后续如果控制输入 `u`、材料状态、边界条件要跨仓共享，可以新增 `ControlFrame`、`MaterialStateFrame` 或 `BoundaryConditionFrame`，但不应把它们塞进一个万能 JSON。

## 4. Controller 框架

### 4.1 模块组成

建议在 `flightenv-runtime-private/EnvPredictorIO` 内新增以下内部模块。

| 模块 | 职责 |
| --- | --- |
| `GraphRuntimeController` | 主控制器；管理 graph 生命周期、单步执行、预测执行、证据写出 |
| `GraphResolver` | 从 launcher cfg、slim cfg、workflow template 或 catalog binding 生成 `EquationGraphTemplate` |
| `OperatorResolver` | 按 `object_id + operator_type + role + phase + priority + enabled` 解析当前 operator |
| `OperatorFactoryRegistry` | 根据 `execution.kind` 创建 in-process、DLL、ROS2 exe、CLI exe adapter |
| `OperatorInstance` | 单个算子实例；承载初始化状态、缓存、invoke、shutdown |
| `GraphPortStore` | graph 内部端口表；保存 typed JSON DTO、来源、状态和质量信息 |
| `GraphScheduler` | 在线单步、未来预测 horizon、事件终止、异步/同步策略 |
| `GraphEvidenceWriter` | 写 `graph_snapshot`、`operator_snapshot`、`resource_lock`、`model_snapshot`、event log |

控制器不直接 include UI，不直接编译 `flightenv-trajectory/legacy/**`，也不把 runtime-private 内部模型类型暴露给 node-sdk。

### 4.2 运行流程

```text
configure(request)
  1. 读取 cfg/catalog/run context
  2. 生成或读取 graph template
  3. 解析 operator catalog 和资产锁
  4. 创建 operator instances
  5. 调每个 operator.initialize(...)
  6. 写 graph/operator/resource/model 初始快照

run_online_step(inputs)
  1. 把 SensorFrame/StateFrame 放入 GraphPortStore
  2. 按 graph 拓扑执行 state_transition / observation_equation / filter_algorithm
  3. 输出 state.posterior、filter.diagnostics
  4. 追加 step evidence

run_forecast(inputs)
  1. 以当前 state.posterior 或外部 state.initial 为起点
  2. 按 horizon policy 执行 forecast graph
  3. 可选执行 trajectory、P/K/S/T field、damage、failure、life 子图
  4. 输出未来状态、QoI、场、损伤、寿命和证据

shutdown()
  1. flush evidence
  2. 调 operator.shutdown()
  3. 写最终 run summary
```

### 4.3 与现有 `EquationGraphRuntime` 的关系

`EquationGraphRuntime` 可以保留为纯执行内核，负责“给定 graph + catalog + external_inputs，跑一次 DAG”。`GraphRuntimeController` 是它上面的一层长期会话控制器，负责：

- cfg/catalog/workflow 输入收口；
- operator 初始化和缓存；
- 在线 tick 与 forecast 两类运行模式；
- run package 和 evidence 目录管理；
- legacy 主链等价验证；
- 后续 ROS2/CLI/DLL 适配。

第一版可以让 `GraphRuntimeController` 内部复用 `EquationGraphRuntime::run(...)`，但不建议长期只用无状态 `run(...)`，因为真实模型会需要缓存 BPNN/POD、字段布局、ROS2 连接、DLL handle 和 checkpoint。

## 5. 统一算子生命周期

每个算子可以有自己特有的初始化参数和内部对象，但对 Controller 暴露统一生命周期。

```cpp
struct OperatorInitContext {
    std::string run_id;
    std::string object_id;
    std::string phase;
    contracts::OperatorSpec spec;
    std::vector<contracts::ResourceLockEntryDTO> resource_locks;
    nlohmann::json runtime_context;
};

struct OperatorInvokeRequest {
    contracts::GraphOperatorNodeSpec node;
    std::map<std::string, nlohmann::json> inputs;
    std::map<std::string, EquationTypedPayload> typed_inputs;
    nlohmann::json step_context;
};

struct OperatorInvokeResult {
    std::map<std::string, nlohmann::json> outputs;
    std::map<std::string, EquationTypedPayload> typed_outputs;
    contracts::OperatorSnapshotDTO snapshot;
    nlohmann::json diagnostics;
};

class IEquationOperator {
public:
    virtual void initialize(const OperatorInitContext& ctx) = 0;
    virtual OperatorInvokeResult invoke(const OperatorInvokeRequest& req) = 0;
    virtual void reset() = 0;
    virtual void shutdown() = 0;
};
```

类型化接口可以作为内部 adapter 存在：

| operator_type | 类型化函数 | 典型实现 |
| --- | --- | --- |
| `state_transition` | `propagate(x, u, dt)` | `RuntimeEngineV2::transition`、弹道库、外部动力学 DLL |
| `observation_equation` | `observe(x)` | `RuntimeEngineV2::observe` |
| `filter_algorithm` | `update(prior, y, y_hat)` | `SimpleParticleFilter::run_single_time` |
| `qoi_equation` | `evaluate(x)` | 弹道 QoI、热流、动压、攻角、控制指标 |
| `field_reconstruction` | `predict_field(qoi/state/trajectory)` | P/K/S/T POD/BPNN 场算子 |
| `damage_accumulation` | `accumulate(d0, field, dt)` | `DamageAccumulationRuntime` 或疲劳/蠕变模型 |
| `failure_criterion` | `evaluate_limit(d, field)` | 最大温度、最大应力、损伤阈值等 |
| `life_assessment` | `assess(damage_curve)` | `LifeForecastRuntime` |

也就是说，每个算子可以有自己的初始化函数和特有算法函数，但必须包在同一个 `IEquationOperator` 生命周期后面。Controller 不直接调用“某个模型的私有初始化函数”，只调用 `initialize/invoke/shutdown`。

当前第一版实现还没有完整引入 `IEquationOperator` 类生命周期，但已经把 handler 执行面拆成两类：

```text
EquationOperatorHandler       # 旧 JSON handler，兼容外部 adapter 和旧测试
EquationOperatorTypedHandler  # 新 typed handler，进程内算子优先使用
```

推荐迁移顺序是：先让在线滤波、弹道预测、多场预测、损伤累计、寿命评估这些高频 in-process 算子改成 typed handler；CLI/ROS2/exe 仍通过 JSON 或文件 evidence 过渡。等 typed 数据面稳定后，再把 DLL ABI 从 request/response 文件提升为结构体或 binary buffer。

## 6. 四个物理场的算子化

### 6.1 端口设计

四个物理场或学科不应再被写死为一个固定 Field 节点。建议 graph 中显式建模：

```text
field.P.predict -> field.P.forecast
field.K.predict -> field.K.forecast
field.S.predict -> field.S.forecast
field.T.predict -> field.T.forecast
field.merge     -> field.forecast
```

每个 field operator 的输出是一个局部 `FieldForecastFrame` 或 `FieldTensorDTO` 序列，`field.merge` 聚合成统一的 `FieldForecastFrame`：

```text
FieldForecastFrame.steps[i].fields = [
  { subject=P, field_kind=..., layout_ref=..., values=[...] },
  { subject=K, field_kind=..., layout_ref=..., values=[...] },
  { subject=S, field_kind=..., layout_ref=..., values=[...] },
  { subject=T, field_kind=..., layout_ref=..., values=[...] }
]
```

这样损伤和寿命模块只消费 `field.forecast`，不关心上游到底是四个单独模型、一个耦合模型，还是两个组合模型。

### 6.2 与现有 `pred.mappings` 的关系

当前 legacy 配置里存在 `predP/predK/predST` 等映射。第一版不要强行拆坏现有模型，可以这样迁移：

| 当前映射 | 第一版 operator | 目标 operator |
| --- | --- | --- |
| `predP` | `field.P.predict` | `field.P.predict` |
| `predK` | `field.K.predict` | `field.K.predict` |
| `predST` | `field.ST.legacy_predict`，输出 `field.S.forecast` 和 `field.T.forecast` | 拆成 `field.S.predict`、`field.T.predict` 或保留耦合模型 |

规则是：graph 端口要显式，模型实现可以耦合。这样既满足“搭积木”，又不牺牲现有模型的物理耦合关系。

### 6.3 资产绑定

每个 field operator 应声明自己的资产：

```json
{
  "operator_id": "field.P.predict.bpnn.v1",
  "operator_type": "field_reconstruction",
  "roles": ["field:P"],
  "assets": [
    {"role": "pod_basis", "asset_ref": "catalog://asset.pod.P"},
    {"role": "bpnn_network", "asset_ref": "catalog://asset.bpnn.predP"},
    {"role": "field_layout", "asset_ref": "catalog://layout.P"}
  ]
}
```

如果四个场共用同一个模型包，也可以多个 operator 指向同一 `model_package`，但 `operator_snapshot` 中必须记录每个 operator 读取了哪些资源。

## 7. 多场初始化和训练放在哪里

### 7.1 训练不放在在线 GraphRuntimeController 中

多场的初始化训练应放在 runtime-private 的训练/模型包链路中，而不是放在在线 graph controller 中。

推荐归属：

| 工作 | 归属 | 输出 |
| --- | --- | --- |
| POD 基训练 | `EnvTrainer` / `TrainingFacade` / model family trainer | POD basis asset |
| BPNN 训练 | `EnvTrainer` / model family trainer | BPNN network asset |
| 训练诊断 | runtime-private trainer tools | diagnostics asset |
| 模型包打包 | runtime-private package tools | model package + manifest |
| catalog 登记 | platform catalog import/register | model_definitions/data_asset_refs/model_bindings |
| 在线加载 | GraphRuntimeController operator.initialize | model_snapshot/resource_lock |

Controller 只做“加载已经训练好的资产”和“记录本次运行锁定了什么”。如果在线自适应或增量训练将来要做，也应作为单独 `training_update` 或 `parameter_update` operator 明确接入，不要混进普通 `field_reconstruction` 初始化。

### 7.2 算子初始化做什么

每个算子的 `initialize` 可以做自己的准备：

- 校验 `OperatorSpec` 的端口和 contract；
- 解析 catalog asset/ref；
- 加载 POD 基、BPNN 权重、材料参数、mesh/layout；
- 建立 DLL/ROS2/CLI 适配器；
- 构造 `ModelSnapshotDTO`；
- 预热缓存，但不得改变训练资产；
- 失败时返回结构化 reason。

训练和初始化的边界是：训练产生资产，初始化消费资产。

## 8. 现有流程作为第一个验证示例

### 8.1 示例 A：在线 PF 等价 graph

目标：证明原来的 `MSMFusionProcessor` 在线一步，可以由 graph 表达并逐步切换。

Graph：

```text
sensor.observation
state.posterior_prev
  -> filter_transition(RuntimeEngineV2::transition)
  -> observation(RuntimeEngineV2::observe)
  -> filter_update(SimplePF)
  -> state.posterior
```

第一版 adapter：

| graph operator | adapter | 当前代码来源 |
| --- | --- | --- |
| `state_transition` role=`filter_transition` | `RuntimeEngineTransitionOperator` | `RuntimeEngineV2::transition` |
| `observation_equation` | `RuntimeEngineObservationOperator` | `RuntimeEngineV2::observe` |
| `filter_algorithm` | `SimplePfFilterOperator` | `SimpleParticleFilter::run_single_time` |

验收：

1. 同一个 cfg、同一个 taskpoint、同一个 seed。
2. 旧 `MSMFusionProcessor` 和新 graph controller 都输出 posterior mean、predicted observation、residual、ESS。
3. 关键数值在容差内一致。
4. 新 graph run 写出 `graph_snapshot/operator_snapshot/resource_lock/model_snapshot/graph_run_evidence`。
5. 旧入口仍可运行，用于回归对照。

### 8.2 示例 B：未来预测与结构健康 graph

目标：证明“现有四场 + 弹道 + 损伤 + 寿命”能按统一算子模板搭积木。

Graph：

```text
state.posterior
  -> trajectory_projection
  -> trajectory.qoi
  -> field.P.predict
  -> field.K.predict
  -> field.S.predict
  -> field.T.predict
  -> field.merge
  -> damage_accumulation
  -> failure_criterion
  -> life_assessment
```

第一版 adapter：

| graph operator | 当前实现来源 | 输出 |
| --- | --- | --- |
| `trajectory_projection` | `flightenv-trajectory` public API 或当前 baseline trajectory | `TrajectoryPredictionFrame` |
| `field.P/K/S/T.predict` | `RuntimeEngineV2` model package / POD-BPNN relation runtime / `FieldForecastRuntime` fallback | `field.<subject>.forecast` |
| `field.merge` | 新增轻量 adapter | `FieldForecastFrame` |
| `damage_accumulation` | `DamageAccumulationRuntime` | `DamageForecastFrame` |
| `failure_criterion` | 第一版可内置阈值 operator | `failure.assessment` |
| `life_assessment` | `LifeForecastRuntime` | `LifeAssessmentFrame` |

验收：

1. graph snapshot 中能看到 P/K/S/T 四个 field operator 或一个带多输出的 legacy ST operator。
2. 关闭任意一个 field operator 时，`field.merge` 输出 degraded/unknown reason，而不是静默伪造结果。
3. `DamageForecastFrame.initial_state` 来自当前损伤基准，不默认从 0 开始。
4. 损伤累计裁剪到 `[0,1]` 或输出明确饱和状态。
5. 首超时刻和 RUL 来自未来损伤曲线，不是固定常数。
6. 不启动 `EnvNodeFieldPrediction/EnvNodeDamage/EnvNodeLife` 也能在同一 controller 内完成 forecast graph。
7. 旧三节点 smoke 继续作为兼容对照。

## 9. 执行形态

第一阶段推荐优先级：

1. `in_process` adapter：最快把现有 C++ runtime 包进去，适合 PF、POD/BPNN、损伤、寿命等同步计算。
2. `dll` adapter：用于算法子仓或第三方模型交付二进制，接口稳定后再做。
3. `ros2_exe` adapter：用于真实传感器、外部设备、慢模型、需要隔离的模型。
4. `cli_exe` adapter：用于离线批处理、训练验证、一次性模型评估。

不要把 P/K/S/T 四个场强制拆成四个常驻 ROS2 节点。它们是四个算子，不等于四个进程。默认应是 in-process 或 DLL；只有模型太慢、需隔离或本来就是外部服务时才走 ROS2/exe。

## 10. 建议实施阶段

### Phase G0：设计冻结

范围：

- 落本文档；
- 明确数据模板策略；
- 明确四场算子化方式；
- 明确训练与初始化边界。

验收：

- 文档进入 `doc/design/README.md`、`doc/总览_文档_索引.md`、`doc/总览_文档状态_当前口径.md`。

### Phase G1：Controller 内部骨架

范围：

- 新增 `GraphRuntimeController`、`GraphPortStore`、`GraphEvidenceWriter`；
- 复用现有 `EquationGraphRuntime` 的 validator、operator resolver 和 evidence DTO；
- 不改变现有主程序默认入口。

验收：

- 最小 `state_transition -> qoi_equation` 由 Controller 跑通；
- 产物和现有 `run_equation_graph_mvp` 一致；
- `tools/equation_graph/run_graph_runtime_controller_smoke.ps1` 通过。

### Phase G2：在线 PF adapter

范围：

- 封装 `RuntimeEngineV2::transition` 为 `state_transition` operator；
- 封装 `RuntimeEngineV2::observe` 为 `observation_equation` operator；
- 封装 `SimplePF` 为 `filter_algorithm` operator；
- 建立旧 `MSMFusionProcessor` 对照测试。

验收：

- 同 cfg/seed/taskpoint 下，新旧 posterior mean 和 diagnostics 在容差内一致；
- `pf_coupled` cfg 可由 Controller 执行，不只是生成 graph。

### Phase G3：四场 forecast adapter

范围：

- 根据 `pred.mappings` 生成 P/K/S/T field operator；
- 第一版允许 `predST` 作为多输出 legacy operator；
- 新增 `field.merge`；
- 输出标准 `FieldForecastFrame`。

验收：

- graph snapshot 明确列出四场输出端口；
- `FieldForecastFrame.steps[].fields[]` 包含 P/K/S/T 或明确缺失 reason；
- 可通过开关只跑部分场。

### Phase G4：结构健康 forecast graph

范围：

- 接入 `DamageAccumulationRuntime`、`failure_criterion`、`LifeForecastRuntime`；
- 从 `DamageStateDTO` 或健康账本读取当前损伤基准；
- 输出 `DamageForecastFrame` 和 `LifeAssessmentFrame`。

验收：

- 当前损伤不是默认 0；
- 损伤随未来场积分；
- 首超破坏时间和 RUL 可追溯；
- 损伤域裁剪或饱和状态明确。

### Phase G5：执行适配层

范围：

- 实现 DLL adapter 最小 ABI；
- 实现 ROS2/exe adapter 的 request/response wrapper；
- 同一 operator spec 可切换 execution kind。

验收：

- in-process 与 DLL adapter 对同一 smoke 输出一致；
- ROS2/exe adapter 独立 smoke 通过；
- graph ports 和 DTO 不因执行形态变化。

### Phase G6：主入口切换和 UI/SDK

范围：

- Launcher/Controller 增加 graph runtime mode；
- UI 通过 SDK 读取 graph evidence、operator 状态和结果 DTO；
- 固定节点链路退为兼容工具。

验收：

- VS 里可启动 GraphRuntimeController demo；
- 原主链路对照 case 通过；
- UI 不 include runtime-private 私有头；
- 根多仓边界验证通过。

### Phase G6R：真实模型适配器收口

2026-06-08 起，真实模型链路入口使用 `GraphRuntimeControllerRunner`，`GraphRuntimeControllerDemo` 只保留为旧 smoke/demo 工程。

范围：

- Runner 读取 `_local_artifacts/flightenv-runtime-private/platform/platform-catalog.json`；
- 按 `object_id + model_type + phase + priority + enabled` 解析并锁定模型资产；
- 通过 `FlightEnvTrajectoryCli.exe` 适配弹道算子，保持 `flightenv-trajectory` 子仓私有实现不穿透；
- 通过 legacy cfg + SQLite DB + POD 基 + BPNN 网络 + mesh 初始化多场算子；
- 结构健康算子记录 damage/life model snapshot，并消费 graph 中的 field/damage DTO；
- 写出 `graph_snapshot.json`、`operator_snapshot.json`、`resource_lock.json`、`graph_run_evidence.json`、`graph_outputs.json`。

验收：

- `operator_snapshot.json` 能看到 trajectory CLI 路径、request/result 路径和 exit code；
- field operator 明确记录 `real_legacy_pod_bpnn_loaded`、DB/POD/BPNN/mesh 路径和 P/K/S/T layout 节点数；
- `resource_lock.json` 同时包含 trajectory package、DB、POD、BPNN、mesh 和模型引用；
- `model_snapshot` 至少包含 trajectory、field_prediction、damage_assessment、life_assessment；
- Runner 可在 VS `01 Runtime Private -> GraphRuntimeControllerRunner` 下直接启动。

## 11. 总体验收目标

完成后应满足：

1. 给定现有 launcher cfg，系统可以生成并执行等价 graph。
2. 在线 PF 主流程可以用 graph controller 执行，并与旧 `MSMFusionProcessor` 对照一致。
3. 未来预测可以只启用弹道，也可以启用 P/K/S/T 多场、损伤、失效、寿命子图。
4. P/K/S/T 四个场作为 graph 端口和 operator 可独立开关、替换和追溯。
5. 多场训练资产由 trainer/model package 产生并登记到 catalog；在线 controller 只加载和锁定资产。
6. 每个算子都有统一生命周期和特有初始化，输出 DTO 和 evidence 稳定。
7. 新增模型时只新增 operator adapter、asset/binding 和模板配置，不改 UI 主逻辑和 graph 引擎。

## 12. 风险和约束

| 风险 | 处理 |
| --- | --- |
| 四场模型存在物理耦合，不能简单拆开 | graph 端口显式，implementation 可多输出；先保留 `predST` 兼容 operator |
| DLL ABI 过早固化成本高 | 第一阶段用 in-process adapter，DLL 放到 G5 |
| 在线 PF 状态和 DTO 之间存在 legacy 转换 | 先写 adapter 和对照测试，避免一次性改 PF 内核 |
| 资产路径和初始化逻辑分散 | 所有 operator 初始化必须从 catalog/resource lock 读取资产 |
| UI 误读 graph 内部细节 | SDK 只暴露 graph evidence view 和 DTO，不暴露 runtime-private 类型 |
| 旧 workflow 与新 graph 并存导致混乱 | workflow snapshot 后续作为 graph snapshot 的兼容外壳，固定节点链只保留 smoke/对照 |

## 13. Graph 静态校验规则

`GraphRuntimeController::configure(...)` 和 `EquationGraphRuntime::run(...)` 之前必须先过 graph 静态校验。当前已有 `contracts::EquationGraphValidator` 作为第一版入口，后续增强时不应在各个 runner 里散落特殊 if，而应继续收敛到 validator。

最低校验规则：

1. 每个 `GraphOperatorNodeSpec.operator_ref` 必须能在 `operator_catalog` 中解析到一个启用的 `OperatorSpec`。
2. `OperatorSpec` 定义算子能力、默认 execution、输入输出 contract 和资产类型；`GraphOperatorNodeSpec` 定义图内实例、端口绑定、参数覆盖、启停状态。
3. `OperatorSpec.enabled` 表示这个实现是否可作为候选；`GraphOperatorNodeSpec.enabled` 表示本图实例是否执行；catalog binding 的 `enabled` 只参与真实模型选择。
4. 图执行顺序不得依赖 `priority`。`priority` 只用于 resolver 在多个候选实现中选择默认实现；真正执行顺序由端口依赖、DAG edge 和 input/output binding 拓扑排序决定。
5. 每个 input port 必须由上游 output 或 external input 提供；允许外部输入的命名空间要显式登记，例如 `state.*`、`sensor.*`、`control.*`、`assets.*`、`damage.*`。
6. 每个 output port 默认只能有一个生产者；确需多生产者时必须在 metadata 中声明 merge/reduce 规则。
7. `contract_id` 必须与 `OperatorSpec.inputs/outputs.contract` 匹配；不匹配时应在 configure 阶段失败，不允许运行时靠特殊分支猜格式。
8. optional group 关闭后，下游要么有 fallback，要么输出明确的 `disabled/missing_input` 状态。
9. horizon policy 必须和 forecast operator 支持的 `dt/steps/until` 能力一致。
10. 多场 layout 必须一致；不一致时必须有显式 projection operator，不能让 `field.merge` 偷做物理投影。

第一版已覆盖 template/node/operator 基础一致性；下一步增强重点是 contract 校验、DAG 环检测、optional group 条件输出和 field layout 检查。

## 14. Port 命名与状态语义

端口命名长期采用点分层，避免 `state_pred`、`field_all_pred`、`life_status` 这类混合风格。推荐稳定端口：

```text
state.prior
state.predicted
state.posterior
state.covariance
observation.predicted
observation.measured
filter.diagnostics
trajectory.forecast
field.P.forecast
field.K.forecast
field.S.forecast
field.T.forecast
field.forecast
damage.initial
damage.forecast
failure.assessment
life.assessment
qoi.snapshot
```

`GraphPortStore` 不应退化成万能 JSON 黑板。端口值除 `port_name/contract_id/value/frame_id/source_ref/status` 外，后续应补齐并校验：

```text
schema_version
producer_node_id
producer_operator_id
valid_time
sample_time
coordinate_frame
unit_system
quality_code
```

端口状态至少包括：

```text
ok
missing_input
disabled
degraded
invalid
stale
error
```

每次 `set_port` 和 operator 输出写入 PortStore 时，都应检查 `contract_id` 与对应 `OperatorSpec` 声明一致。否则 `field.S.forecast` 名义上是 `FieldForecastFrame`、实际却塞 legacy array，会让下游只能靠特殊兼容分支读取。

## 15. 时间语义：online step、forecast horizon、mission/life horizon

在线滤波和未来预测必须分清状态推进关系：

```text
run_online_step(t_k) -> state.posterior(t_k)
run_forecast(t_k, horizon=N) 从 state.posterior(t_k) 预测 t_k+1 ... t_k+N
```

规则：

1. forecast 默认只读 `state.posterior(t_k)`，不得反写在线主状态；除非后续显式增加 `assimilation/update` operator。
2. forecast 每一步都必须携带时间语义：

```text
step_index
t_rel
t_abs
dt
source_state_time
```

3. 损伤累计强依赖 `dt`，不得只依赖数组下标 `steps[i]`。
4. 短时预测不能直接等同总寿命评估。设计上必须区分：

```text
short_horizon_forecast：未来几秒，用于在线风险、QoI、近期超限预警。
mission_damage_projection：任务剖面级，用于本次任务损伤累计。
life_baseline_assessment：鉴定试验/历史数据级，用于寿命基准。
RUL_estimation：结合当前损伤、未来任务剖面和置信信息估计剩余寿命。
```

如果当前配置是 `80 steps * 0.05 s = 4 s`，输出只能解释为短时风险/短时损伤增量，不能声称给出结构总寿命。

## 16. Optional Group 与条件输出规则

optional group 关闭时，不能让输出端口静默消失，也不能输出空对象冒充成功。统一输出 envelope：

```json
{
  "port": "life.assessment",
  "status": "disabled",
  "reason": "optional group structural_health is disabled",
  "value": null
}
```

规则：

1. `enabled=false` 的节点不执行，但其声明的最终输出若被 graph.outputs 引用，必须产生 `disabled` 或 `missing_input` 状态。
2. 下游 operator 看到上游 `disabled` 时，应按自己的 fallback 策略输出 `disabled/degraded`，不得伪造 `ok`。
3. UI/SDK 只根据端口状态解释结果，不直接推断 optional group 内部开关。
4. 部分场关闭时，`field.merge` 输出 `degraded` 并列出缺失 subject；不得静默补零。

## 17. Evidence run_manifest 与可复现实验字段

evidence 文件很多，必须有统一主索引。正式 graph run 应写出：

```text
run_manifest.json
graph_snapshot.json
operator_snapshot.json
resource_lock.json
model_snapshot.json
graph_outputs.json
graph_run_evidence.json
workflow_timeline.json
```

`run_manifest.json` 是 SDK/UI 的第一读取入口，至少包含：

```text
run_id
object_id
phase
template_id
graph_version
start_time_ns
end_time_ns
status
reason
graph_snapshot_path
operator_snapshot_path
resource_lock_path
model_snapshot_path
graph_run_evidence_path
outputs_path
timeline_path
checksum_policy
```

为了支持 deterministic replay，PF 和未来预测还应记录：

```text
random_seed
particle_count
resample_threshold
model_version
input_frame_id
input_checksum
output_checksum
```

第一版 `EquationGraphRuntime` 已写出 `run_manifest.json` 作为主索引；checksum 可先记录策略，后续再补真实 checksum。

## 18. Failure/Degraded 运行语义

外部执行适配器和 in-process handler 的失败语义要统一，不允许“CLI 失败但 graph 继续输出假结果”。推荐错误码：

```text
timeout
nonzero_exit
invalid_output_contract
missing_artifact
resource_lock_failed
adapter_init_failed
runtime_exception
missing_input
disabled
degraded
stale
```

写入位置：

1. `operator_live_status.json` 记录实时状态、开始/结束时间、duration、reason。
2. `operator_snapshot.json` 记录最终 execution kind、资源引用、diagnostics 和失败语义。
3. `graph_run_evidence.json` 汇总 run 级状态。
4. `run_manifest.json` 作为 SDK/UI 入口记录最终状态和各 evidence 文件路径。

`field.merge` 第一版定义为容器聚合 + 时间/布局一致性检查：不改变 P/K/S/T 数值，不做物理耦合融合。未来热-结构耦合、压力到应力转换、mesh projection 等都必须作为单独 operator 接入。

结构健康链路边界固定为：

| 算子 | 输入 | 输出 | 不该做什么 |
| --- | --- | --- | --- |
| `damage_accumulation` | 初始损伤、未来载荷/场、`dt` | 损伤时间序列 | 不判断任务放行 |
| `failure_criterion` | 损伤、应力、温度、应变等 | 是否超限、首超时间、margin | 不估计完整寿命分布 |
| `life_assessment` | 损伤曲线、首超、任务剖面、置信信息 | RUL、寿命消耗、风险等级 | 不重新计算场 |

P/K/S/T 必须在 contract metadata 中给出正式物理定义：

```json
{
  "subject": "K",
  "subject_name": "...",
  "physical_meaning": "...",
  "unit": "...",
  "layout_ref": "..."
}
```

短端口名可以继续用 `field.K.forecast`，但长期 contract 不能只靠单字母让开发者猜。
