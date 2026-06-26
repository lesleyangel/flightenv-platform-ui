# 平台原生 UI 数据接口

## 目标

`flightenv-platform-ui` 后续按平台和对象包的结构数据工作，不再把平台运行结果先映射回旧 `flightenv-contracts` DTO。

这件事的目的不是简单换名字，而是让 UI 直接呈现平台已经定义好的对象结构、算子端口、workflow、时间策略、分支策略、data-plane artifact 和 evidence。

## 对象包是 UI 的工程源

对象包相当于建模工程文件，UI 的对象树、资源树、算子库和 workflow 编辑器都应从对象包读取。

主要文件：

```text
object/twin_object.json                 对象身份、组件、引用关系、默认 profile
assets/resources.json                   资源分组、模型、网格、数据库、传感器、adapter 资源
domain_schemas/*.schema.json            端口数据结构和 typed DTO 来源
operators/*.atomic.json                 算子定义、输入输出端口、adapter、显示描述
workflows/*.workflow.json               算子图、阶段、连线、激活策略
run_profiles/*.json                     运行方案、帧数、分支、时间、输入流
runtime/platform_runtime_profile.json   RuntimeHost 默认入口和候选资源
```

UI 的“新建、保存、编辑、删除”应作用在这些对象包文件上。

## PDK 编译产物是执行源

RuntimeHost 不直接靠 UI 临时拼图运行，而是读取 PDK 编译后的产物。UI 可以触发 PDK 编译，也可以展示编译结果。

典型编译产物：

```text
execution_plan.json      节点、边、端口、执行后端
time_plan.json           时钟、节点周期、输入对齐、公开物化策略
scheduler_plan.json      ready queue、资源锁、并行组、deadline、失败策略
data_plane_plan.json     哪些端口走 artifact/tensor/buffer，如何落盘和索引
operator_snapshot.json   算子快照，便于 UI 展示和运行审计
workflow_snapshot.json   workflow 冻结快照，保证运行可复现
resource_lock.json       资源锁和路径解析结果
model_snapshot.json      模型与资源绑定快照
```

UI 的编译页和作业页应围绕这些文件解释“为什么能跑、会怎么跑、失败在哪”。

## Runtime evidence 是在线和回放源

运行时 UI 不应依赖旧 ROS2 DTO 来显示平台结果，而应读取 run package。

主要运行数据：

```text
run manifest             run_id、object_id、workflow_id、profile_id
timeline index           public frame、internal frame、held/carry-forward 状态
branch index             parent branch、cause event、trigger frame、checkpoint
data-plane manifest      每个端口输出的 artifact/tensor/ref
field artifact           节点场值、mesh/layout 引用、统计量、单位
qoi/diagnostics evidence 失效判断、寿命、诊断报告、告警
progress/evidence log    compile/preflight/runtime 状态
```

UI 的实时页和回放页应以 `timeline + branch + field artifact` 为主线。

## 平台原生 view model

新 UI 内部建议使用自己的平台 view model，而不是旧 contracts。

建议最小集合：

```text
PlatformObjectView       对象身份、组件、资源引用
PlatformResourceView     资源 ID、类型、URI、真实路径、校验状态
PlatformSchemaView       domain schema 字段、数组/张量/field/tensor_ref 描述
PlatformOperatorView     算子元信息、端口、typed_io_contract、adapter、显示描述
PlatformWorkflowView     stage、node、edge、端口连接、激活策略
PlatformRunProfileView   帧数、时钟、分支、输入流、运行参数
PlatformTimelineView     主线/分支时间线、public frame、internal frame
PlatformFieldArtifactView 场名、component、mesh/layout、values/statistics
PlatformMeshLayoutCatalog 对象包 mesh/resource/layout 解析后的三维网格布局集合
PlatformEvidenceView     QoI、诊断、告警、失败策略、报告链接
```

这些 view model 可由对象包 JSON、compiled workflow JSON 和 run evidence JSON 解析得到。

## 旧 contracts 的处理

短期内，旧代码中仍可能存在：

```text
RuntimeSnapshotDTO
PredictionResultDTO
FieldBundleDTO
PredictionViewModel
SubjectType
```

它们只允许作为迁移期兼容入口存在。新增页面、新增功能和新平台 SDK 接口不再围绕这些类型设计。

当前三维场渲染已经切到 `PlatformMeshLayoutReader + PlatformMeshLayoutCatalog + field artifact`：

- `PlatformMeshLayoutReader` 读取对象包 `object/twin_object.json`、`domain_schemas/*.json` 和对象包资产中的兼容布局源，生成 UI 可消费的 layout catalog。
- `VtkModelFieldWidget` 只消费 `PlatformMeshLayoutCatalog` 和 field artifact 的扁平场值，不再直接接收 `RuntimeSnapshotDTO`。
- 兼容 `runtime_snapshot.json` 目前只作为“节点坐标/面片索引来源”被 reader 读取，页面和 VTK 控件不再直接依赖它的 DTO 类型。

后续替换顺序建议：

1. 对象树、资源树、算子库、workflow 编辑器全部改为对象包 JSON 驱动。
2. 在线/回放时间线改为 run evidence 驱动。
3. 继续把对象包 mesh layout 从兼容 `runtime_snapshot.json` 迁移成正式 `assets/mesh_layouts/*.json` 或等价平台资源。
4. 删除旧 DTO demo 和旧 controller 兼容入口。
