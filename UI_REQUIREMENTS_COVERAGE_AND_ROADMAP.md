# flightenv-controller-ui 需求覆盖度与后续开发任务报告

状态：当前 UI 已经具备实时启动、暂停、继续、关闭在线 run 归档收口、实时 prediction/sensor 展示和基础配置生成能力；当前 SDK 已经补齐历史 run 读取、归档配置预检、ViewModel 转换、退出回调清理和在线 run finalize 接口。UI 的下一阶段重点不再是“能不能跑起来”，而是把 SDK 已提供的能力闭环到界面工作流里。

更新时间：2026-05-22

## 1. 本报告回答的问题

本文只站在 `flightenv-controller-ui` 子仓视角，回答：

1. 当前 UI 是否能够完整覆盖现有需求和功能。
2. 当前 SDK 已经提供哪些 UI 可直接使用的能力。
3. 哪些功能只需要 UI 子仓自行开发。
4. 哪些功能需要 node-sdk、runtime-private 或 contracts 联动。
5. 后续 UI 开发应按什么顺序推进，如何验收。

接口边界详见：

```text
INTERFACE_GUIDE.md
../flightenv-node-sdk/INTERFACE_GUIDE.md
```

## 2. 总体结论

当前 UI 是“实时演示主流程 + 部分配置生成 + SDK 接入完成”的阶段性版本，还不是完整工程产品。它已经可以 artifact-only 独立开发，不需要 runtime-private 源码；但是历史回放、归档配置、诊断报告和结构安全看板还没有在 UI 层闭环。

| 维度 | 当前覆盖度 | 当前结论 |
| --- | --- | --- |
| 独立构建与 artifact-only 开发 | 高 | UI 只消费 contracts、node-sdk public headers/lib/dll、runtime artifact 和第三方依赖 |
| 启动与实时流控 | 中高 | `LaunchSession`、`LaunchSessionHost`、`ControllerViewAdapter` 已接入 |
| 实时 prediction 展示 | 中高 | 已消费 `PredictionViewModel`，可驱动 VTK、曲线和参数表 |
| 实时 sensor 展示 | 中 | 已消费 `SensorViewModel`，但数据源配置和状态表达仍偏演示 |
| 实时 state 展示 | 低 | `StateFrame` 回调已到 UI，但尚未形成状态弹道看板 |
| JSON 配置表达 | 中 | 能生成 `project.json`，但缺打开/另存为、schema/preflight 面板和 archive 配置页 |
| 在线归档历史回放 | 低 | SDK `OnlineRunReader` 已准备，UI 未接入 |
| 诊断报告展示 | 低 | 诊断由 runtime-private 通过 JSON 配置决定，UI 暂无报告入口 |
| 结构安全展示 | 未覆盖 | SDK reader 有 `has_safety` 标志位预留，但正式 Safety/Damage DTO 和 ViewModel 尚未完成 |
| 退出稳定性 | 中高 | 已增加关闭前清空 SDK 回调，QTest 已固化 |

一句话定性：

**当前 UI 能支撑实时在线演示；下一阶段要围绕 SDK 现有接口补齐“历史 run 打开、回放、归档配置、诊断报告入口、状态看板和错误反馈”。**

## 3. 当前 SDK 已提供的 UI 能力

### 3.1 Launch / 配置预检能力

SDK 公开头：

```text
EnvNodeSupport/LaunchSessionFacade.h
EnvNodeSupport/LaunchSessionHost.h
EnvNodeSupport/NodeHostRunner.h
EnvNodeSupport/SharedLaunchSupport.h
EnvPredictorIO/ProjectConfig.hpp
```

UI 可直接使用的接口：

| 接口 | UI 可用能力 |
| --- | --- |
| `load_and_validate(request)` | 读取、解析、归一化并校验 `ProjectConfig` |
| `LaunchPreflightResult` | 获取 `cfg_path`、归一化后的 `ProjectConfig` 和 warnings |
| `LaunchSession<NodeT>` | 绑定配置和节点，执行 runtime 初始化、启动/停止 streaming |
| `LaunchSessionHost<NodeT>` | 把 node 放入后台 executor spin |
| `ProjectConfig` / `load_project` / `save_project` | 读写 UI 生成或用户选择的 JSON 配置 |

当前 SDK 已处理的关键规则：

- `archive.root_dir` 在 `archive.enabled=true` 时会按配置文件目录归一化为绝对路径。
- `archive.run_id` 为空时会生成 `run_yyyyMMdd_HHmmss`。
- `frame_storage` 合法值在预检阶段拦截。
- diagnostics、retrain、online evaluator 不在 SDK 写死挂载；它们通过同一份 `ProjectConfig.test_eval` 透传给 runtime-private 标准链路。

UI 当前接入情况：

- `main.cpp` 已使用 `load_and_validate` 和 `LaunchSession`。
- UI 还没有把 preflight warnings/errors 展示成用户可理解的面板。
- UI 还没有为 `archive` 字段提供可视化配置入口。

### 3.2 NodeControl 能力

SDK 公开头：

```text
EnvNodeSupport/NodeControlClient.h
```

UI 可直接使用的接口：

| 接口 | UI 可用能力 |
| --- | --- |
| `start_senser_stream()` / `stop_senser_stream()` | 控制传感器流 |
| `start_filter_stream()` / `stop_filter_stream()` | 控制滤波节点流 |
| `start_senser_initialization(cfg_json)` | 初始化传感器节点 |
| `start_filter_initialization(cfg_json)` | 初始化滤波节点 |
| `runtime_snapshot_copy()` | 获取 runtime 布局快照 |
| `runtime_meta_copy()` | 获取 runtime 元信息 |
| `drain_prediction_queue(timeout)` | 等待 prediction callback 队列清空 |

回调面：

| 回调 | 数据类型 | UI 当前状态 |
| --- | --- | --- |
| `onLog` | `std::string` | 已接入控制台/日志 |
| `onPredictionResult` | `contracts::PredictionResultDTO` | 经 adapter 转为 `PredictionViewModel` 后接入 |
| `onSensorFrame` | `contracts::SensorFrame` | 经 adapter 转为 `SensorViewModel` 后接入 |
| `onStateFrame` | `contracts::StateFrame` | 已接收，但展示不足 |
| `onRuntimeSnapshot` | `contracts::RuntimeSnapshotDTO` | 已用于 runtime view |
| `onRuntimeMeta` | `contracts::RuntimeMetaDTO` | 已用于 runtime view |

### 3.3 ControllerViewAdapter / ViewModel 能力

SDK 公开头：

```text
EnvNodeSupport/ControllerViewAdapter.h
EnvNodeSupport/ControllerViewModels.h
```

UI 可直接使用的 ViewModel：

| ViewModel | 作用 |
| --- | --- |
| `RuntimeViewModel` | runtime snapshot/meta、场布局、传感器布局、mesh、参数样本 |
| `PredictionViewModel` | prediction DTO、时间戳、按 subject/taskpoint/node 整形后的场值和参数 |
| `SensorViewModel` | sensor DTO、时间戳、key、按 subject/taskpoint/node 整形后的传感器通道 |
| `RuntimeFieldView` / `RuntimeMeshView` | UI 友好的场和网格描述 |
| `ParameterSampleView` | 参数显示名、文本值、数值值、单位、说明、硬上限 |

Adapter 能力：

| 接口 | UI 可用能力 |
| --- | --- |
| `initialize_runtime()` | 初始化 runtime 并缓存 snapshot/meta/view |
| `bind_callbacks(callbacks)` | 一次性绑定 UI 所需回调 |
| `set_on_prediction()` / `set_on_sensor()` / `set_on_state()` | 分项接实时数据 |
| `set_on_runtime()` / `set_on_runtime_meta()` | 接 runtime 布局和元信息 |
| `resume_streaming()` / `pause_streaming()` / `stop_online_run()` | 统一控制传感器和滤波流；暂停不结束 run，关闭在线 run 时 finalize 归档 |
| `clear_callbacks()` | UI 关闭前断开 SDK 回调，防止退出阶段崩溃 |

当前 UI 已接入：

- 实时 prediction/sensor/runtime。
- `clear_callbacks()` 已在 `EnvPredictorUI::prepareForShutdown()`、`closeEvent()` 和析构路径中调用。
- QTest 已新增 `closingWindowClearsSdkCallbacks` 固化退出稳定性。

当前 UI 待补：

- 将 `StateFrame` 做成专门状态弹道看板。
- 把实时/回放展示状态从 `EnvPredictorUI` 大成员中进一步拆出。

### 3.4 OnlineRunReader / 历史回放能力

SDK 公开头：

```text
EnvNodeSupport/OnlineRunReader.h
```

UI 可直接使用的类型：

| 类型 | 作用 |
| --- | --- |
| `OnlineRunManifestView` | run 目录、run_id、状态、格式、是否存在 binary/jsonl/index/dashboard |
| `OnlineFrameSummaryView` | 帧号、时间戳、任务点、文件位置、记录大小和内容 flags |
| `OnlineReplayFrameView` | 帧摘要、`PredictionResultDTO` 和可选 `StateFrame` |

UI 可直接使用的方法：

| 方法 | 作用 |
| --- | --- |
| `open(run_dir)` | 打开 run 目录并读取 manifest |
| `manifest()` | 获取当前 manifest view |
| `read_frame_summaries()` | 读取全部帧摘要 |
| `read_all_frames()` | 读取全部历史帧 |
| `read_frame_range(first_index, max_count)` | 范围读取历史帧 |
| `prediction_view_model_from_online_replay_frame(frame, runtime_snapshot)` | 将历史帧转为 `PredictionViewModel` |

当前 reader 支持：

```text
<run_dir>/manifest.json
<run_dir>/frames/pred_000000.bin
<run_dir>/frames/pred_000000.jsonl
<run_dir>/index.sqlite        存在性检测
<run_dir>/summary/dashboard_timeseries.csv  存在性检测
```

重要边界：

- UI 不应自己解析 `.bin` 或 `.jsonl`。
- UI 不应自己读 `index.sqlite`。
- 第一阶段 UI 历史回放应以 `OnlineRunReader` 的顺序读取能力为准。
- `has_safety` 只是帧摘要中的能力标志，正式结构安全展示仍等待上游 DTO/ViewModel。

## 4. 当前 UI 已覆盖能力

### 4.1 实时启动与流控

已覆盖：

- 创建 `LaunchSession` 和 `LaunchSessionHost`。
- 初始化 runtime。
- 开始按钮调用 `resume_streaming()`。
- 暂停按钮调用 `pause_streaming()`。
- 退出时调用 `stop_online_run()` 完成在线 run 归档收口，然后断开 UI 回调并释放 host/session/node。

限制：

- 启动失败、action 超时、snapshot 未发布等错误仍主要进入日志，缺少 UI 提示。
- 控制命令仍偏窄，没有 runtime 级 reset、重连、停止并保存等用户工作流。

### 4.2 实时 prediction / sensor / runtime 展示

已覆盖：

- `PredictionViewModel` 驱动 VTK 场更新、曲线和参数表。
- `SensorViewModel` 驱动传感器曲线和 marker。
- `RuntimeViewModel` 支撑 mesh、field、parameter 的初始化布局。

限制：

- `StateFrame` 没有专门页面。
- 实时缓存、历史最大值、表格内容、VTK 状态仍主要散落在 `EnvPredictorUI` 成员中。
- 还没有 Live/Replay 双模式隔离。

### 4.3 配置 JSON 生成

当前 UI 能生成 `project.json`，覆盖：

- `pipeline_mode`
- `sources`
- `sync`
- `runtime`
- `model_ref` / legacy `model`
- `test_eval`

限制：

- 保存路径仍偏固定。
- 打开已有配置、另存为、配置 diff、配置校验面板不足。
- `archive` 字段还没有 UI 表达。
- diagnostics 是否启用应由 `test_eval` JSON 控制，UI 尚未提供清晰开关组和报告目录选择。

### 4.4 测试固化

当前已有 QTest 覆盖：

- 控制按钮存在。
- 学科树驱动页面切换。
- 轻量模式下开始按钮不依赖运行中的 ROS。
- 可选进程级集成测试启动 Senser/Filter 并等待 runtime snapshot。
- 关闭窗口时清空 SDK 回调，防止退出阶段访问已关闭 UI。

缺口：

- 历史 run 打开/读取/渲染还没有测试。
- 配置生成和 preflight error 展示还没有测试。
- artifact 缺失、run 打不开、非法 JSON 等错误状态还没有测试。

## 5. UI 可独立完成的任务

这些任务原则上只改 `flightenv-controller-ui`，不需要 runtime-private 算法改动。

| 优先级 | 任务 | SDK 依据 | 验收 |
| --- | --- | --- | --- |
| P0 | 历史 run 打开入口 | `OnlineRunReader::open()` | 不启动 ROS，选择 run 目录后显示 manifest 摘要 |
| P0 | 历史帧摘要列表 | `read_frame_summaries()` | 显示 frame_id、time、task/taskpoint、flags、文件位置 |
| P0 | 单帧回放渲染 | `read_frame_range()` + `prediction_view_model_from_online_replay_frame()` | 点击某帧后复用现有 VTK/曲线/参数表刷新 |
| P0 | Live / Replay 模式隔离 | 现有 ViewModel | 回放帧不污染实时缓存，切回实时后状态正常 |
| P1 | archive 配置页 | `ProjectConfig::archive` | 可配置 enabled/root_dir/run_id/frame_storage/ring_buffer/summary |
| P1 | test_eval/diagnostics 配置页 | `ProjectConfig::test_eval` | 可配置诊断开关、report_dir、online compare、样本报告开关 |
| P1 | 配置打开/另存为 | `load_project` / `save_project` | 用户可选择路径读写 JSON |
| P1 | preflight 结果面板 | `load_and_validate` / `LaunchPreflightResult.warnings` | 非法 frame_storage、缺文件等错误能展示 |
| P1 | 状态弹道看板 | `contracts::StateFrame` | state 回调展示关键弹道参数和趋势 |
| P1 | 错误/空状态/加载状态 | Qt UI 自身 | QTest 覆盖 run 不存在、配置非法、artifact 缺失 |
| P2 | UI state store / presenter | 现有 ViewModel | `EnvPredictorUI` 继续瘦身，实时/回放状态可单测 |
| P2 | 诊断报告文件壳 | `test_eval.report_dir` | 先展示报告目录和 summary 文件，不解析私有对象 |

## 6. 需要联动开发的任务

这些任务不能只靠 UI 仓完成，必须等待或同步修改 SDK/runtime/contracts。

| 功能 | 需要联动模块 | UI 等待的接口/产物 | 原因 |
| --- | --- | --- | --- |
| 大 run 快速随机定位 | node-sdk + runtime-private | `OnlineRunReader` 基于 `index.sqlite` 的分页/cursor 接口 | UI 不应自己读 SQLite |
| dashboard 曲线读取 | node-sdk | dashboard CSV/JSONL reader 或 summary ViewModel | 当前 SDK 只检测 dashboard 是否存在 |
| 结构安全实时展示 | contracts + runtime-private + node-sdk | `DamageState` / `SafetyState` / `FailureJudgement` DTO 或 SDK ViewModel | UI 不定义安全判据 |
| 结构安全历史回放 | runtime-private + node-sdk | run package 写入 safety frame，reader 读取 safety payload | 当前只有 `has_safety` 标志 |
| 诊断报告结构化展示 | runtime-private + node-sdk/contracts | report index/schema 或 SDK report reader | UI 不解析 runtime-private 私有报告对象 |
| 训练任务控制台 | runtime-private + node-sdk | trainer CLI/facade 状态文件或 SDK reader | UI 不直接调用训练内部类 |
| 远程节点连接 | node-sdk | transport/client abstraction | 当前是本地 ROS2/SDK session 模式 |
| 权威配置 schema | runtime-private 或 node-sdk | 公开 validation result DTO | UI 本地校验不能替代 runtime 预检 |

## 7. 推荐开发顺序

### Round UI-1：历史回放最小闭环

目标：不启动 ROS，只打开一个 run package，显示摘要并渲染一帧。

范围：

- 新增“历史回放/打开 run”入口。
- 调用 `OnlineRunReader::open()`。
- 展示 `OnlineRunManifestView`。
- 调用 `read_frame_summaries()` 展示帧列表。
- 调用 `read_frame_range()` 读取选中帧。
- 调用 `prediction_view_model_from_online_replay_frame()` 复用现有展示组件。

验收：

- 使用小型 run fixture 可打开。
- 不新增 runtime-private 源码依赖。
- QTest 覆盖打开 run、读取摘要、渲染一帧。

### Round UI-2：Live / Replay 双模式整理

目标：实时流和历史回放互不污染。

范围：

- 增加 UI 模式状态。
- 分离实时缓存和回放缓存。
- 回放模式下禁用或隔离实时开始/暂停。
- 切回实时前清理回放临时状态。

验收：

- 实时 -> 回放 -> 实时切换稳定。
- 历史帧不会写入实时 sensor/prediction 缓存。
- 退出时仍通过 `closingWindowClearsSdkCallbacks`。

### Round UI-3：配置工程化

目标：让 UI 不再只是写固定 `project.json`。

范围：

- 打开/另存为配置。
- archive 配置字段 UI。
- test_eval / diagnostics 配置字段 UI。
- 配置预检结果面板。

验收：

- 生成 JSON 可被 SDK `load_and_validate()` 接受。
- `archive.root_dir` 相对路径预检后变为绝对路径。
- `archive.run_id` 为空时预检后生成 `run_` 前缀 ID。
- 非法 `frame_storage` 能在 UI 中显示错误。

### Round UI-4：状态、诊断与结构安全入口

目标：先补状态弹道和诊断文件入口；结构安全等上游 DTO 完成后再接入。

范围：

- `StateFrame` 看板和趋势图。
- 诊断报告目录浏览。
- 诊断 summary 文件入口。
- Safety/Damage ViewModel 准备好后叠加到实时和回放界面。

验收：

- UI 不解析 runtime-private 私有对象。
- 所有新增结构化字段来自 contracts 或 SDK ViewModel。

## 8. 当前不应由 UI 自己做的事

- 不在 UI 中解析 `.bin` 或 `.jsonl` 历史帧；应调用 SDK `OnlineRunReader`。
- 不在 UI 中读取 `index.sqlite`；等待 SDK 提供 cursor/page API。
- 不在 UI 中定义结构安全判据；判据属于 runtime/contracts。
- 不在 UI 中直接调用训练内部类；训练应走 CLI/facade/status reader。
- 不在 UI 中 include runtime-private `EnvPredictorCore`、`EnvDataBase`、`EnvPredictorIO` 私有头。
- 不把 `flightenv-node-sdk/source-supported/**` 加进 UI 工程。
- 不把 diagnostics 是否启用写死在 UI 程序逻辑里；应通过 `ProjectConfig.test_eval` JSON 控制。

## 9. UI 完整覆盖的判定标准

只有同时满足下面条件，才能说 UI 基本覆盖当前需求：

1. 实时启动、暂停、关闭、错误提示完整。
2. 实时 prediction、sensor、state、runtime 都有对应展示。
3. `project.json` 支持打开、编辑、校验、另存为。
4. `archive` 在线归档配置可在 UI 中表达。
5. 历史 run 可打开、浏览、步进、播放，并复用实时展示组件。
6. Live/Replay 模式状态隔离。
7. 诊断配置由 JSON 控制，报告至少能以稳定目录/index/summary 方式展示。
8. 结构安全结果在 contracts/SDK 准备后可展示。
9. UI QTest 覆盖轻量页面、配置生成、历史回放、错误状态和退出回调清理。
10. UI 仍保持 artifact-only 消费，不引入 runtime-private 源码依赖。

## 10. 当前优先结论

下一轮最值得先做：

```text
UI-1：历史回放最小闭环
```

原因：

- SDK 已经有 `OnlineRunReader`。
- runtime 已经能产出 run package。
- UI 已经有 `PredictionViewModel` 驱动的 VTK/曲线/参数表。
- 这轮可以只改 UI，不碰 Core/DB/算法。

同步建议：

- Round UI-1 完成后，立刻做 Round UI-2，避免实时流和回放流混在同一批 UI 成员变量里继续膨胀。
- archive/test_eval 配置页放在 UI-3 做，因为它依赖历史回放和诊断入口的用户工作流定型。
