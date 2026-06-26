# FlightEnv Web Platform Workbench

这是 Qt 平台工作台的 Web 版重构入口。它的目标不是另写一套算法界面，而是用浏览器复刻 Qt 平台 UI 的核心工作流：对象加载、在线运行、RuntimeHost 状态、分支时间线、数据平面、三维场云图、QoI/证据回放和诊断检查。

## 边界

- WebUI 属于 `flightenv-controller-ui`，只做平台工作台展示和本地控制。
- 对象是谁、有哪些组件、资源、算子、工作流，由对象包声明。
- 算子如何计算，由算子包和 RuntimeHost 后端实现。
- WebUI 只读取对象包、run package、runtime evidence、data-plane artifact 和公开接口，不复制 runtime-private 源码。
- 三维云图只渲染真实 `geometry + field artifact`，缺数据时明确报错，不生成伪造云图。

## 启动

```powershell
python .\flightenv-controller-ui\webui\server.py
```

默认地址：

```text
http://127.0.0.1:8787/
```

直接进入工作流编辑页：`http://127.0.0.1:8787/?page=workflow`。

如果端口被占用：

```powershell
$env:FLIGHTENV_WEBUI_PORT = "8788"
python .\flightenv-controller-ui\webui\server.py
```

## 前端依赖构建

工作流编辑页使用真实的 `@xyflow/react`（React Flow）包，并通过 Vite 打成离线 bundle：

```powershell
cd .\flightenv-controller-ui\webui
npm.cmd install
npm.cmd run build
```

构建产物位于 `flightenv-controller-ui/webui/dist/`，页面会直接加载 `dist/flightenv-reactflow-editor.js` 和 `dist/flightenv-reactflow-editor.css`。`node_modules` 不进入仓库；依赖版本由 `package-lock.json` 固化。

## 页面对应 Qt 工作台

WebUI 以替代 Qt 平台工作台为目标，但把运行语义拆成三种模式，避免“在线运行”和“历史文件查看”混在一起：

| 模式 | 用途 | 数据来源 |
|---|---|---|
| 实时在线 | 导入对象包、初始化/加载模型、启动 RuntimeHost job、持续刷新最新 run evidence | WebUI 启动的对象包脚本 + 最新 run package |
| 历史回放 | 选择已有 run package，只读回放分支、场、QoI 和 runtime 事件 | `_local_artifacts/platform-*/mainline-runs` |
| 编排编辑 | 读取对象包 workflow，编辑阶段/算子图，校验并保存草稿 | 对象包 `workflows/` + 本地草稿 |

| Web 页面 | 对应 Qt 平台逻辑 |
|---|---|
| 总览 | 当前对象、run、workflow、分支和数据平面摘要 |
| 在线运行 | 导入模型/对象包、运行控制、实时输入、分支树、实时时间轴、分支曲线、真实多场云图 |
| 运行时主机 | RuntimeHost、adapter session、事件和分支状态 |
| 数据平面 | field artifact、QoI、port、branch、step 索引 |
| 对象画像 | 对象包声明的 object、component、workflow 总览 |
| 资源树 | 单独展示 resource/model asset 层级树、type 标注、详情和预览 |
| 算子库 | 2x2 展示 operator spec、算子接口、端口列表、端口 typed DTO/domain schema |
| 工作流编排 | workflow nodes / edges / contract 关系 |
| 工作流编辑 | 算子节点拖拽、连线、参数面板、校验、草稿保存/读取 |
| 状态/QoI账本 | evidence 中的 QoI、decision、summary |
| 证据回放 | 选择历史 run package 后回放数据平面 |
| 配置与数据源 | WebUI 入口、对象包和 run 根目录说明 |
| 诊断报告 | 对对象包、timeline、runtime、data-plane 的轻量检查 |

## 对象包建模器

`对象包建模器` 是当前 WebUI 新增的独立工程化入口，目标是把对象包从“手写一组 JSON”提升为“类 Abaqus/CAE 的对象工程”：

- 左侧是对象工程树：对象清单、组件、数据结构、资源、算子、workflow、run profile、runtime profile、编译产物和运行包。
- 中间是定义编辑器：选中树节点后读取对应 JSON；修改后保存到对象草稿，不直接覆盖正式对象包。
- 右侧是执行链：对象校验、PDK 编译、运行前检查、RuntimeHost 启动，以及运行包/云图入口。
- 新建对象工程会写入 `_local_artifacts/webui-object-projects/<object_id>/package`，包含标准对象包目录和 `object_project.json`。
- 读取现有对象包时，如果没有 `object_project.json`，后端会从 `object/twin_object.json` 派生工程视图，不强制污染对象包。
- 删除实体前会扫描对象包 JSON 引用；被引用时默认阻止删除，需要明确强制删除。

相关 API：

| API | 说明 |
|---|---|
| `GET /api/object-project/tree` | 返回对象工程树、当前对象摘要、12 条交付状态 |
| `GET /api/object-project/entity?kind=&entity_id=&rel_path=` | 读取单个工程实体 JSON 或数组内实体 |
| `POST /api/object-project/create` | 新建本地对象工程并载入 |
| `POST /api/object-project/save` | 保存实体到对象草稿 |
| `POST /api/object-project/delete` | 带引用检查地删除实体 |
| `POST /api/object-project/validate` | 调用对象包校验 |
| `POST /api/object-project/compile` | 调用 PDK workflow 编译 |
| `POST /api/object-project/preflight` | 检查 Runtime 运行前条件 |
| `POST /api/object-project/run` | 启动 RuntimeHost 运行入口 |

验收脚本：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\verify_webui_object_modeler.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\verify_webui_object_modeler.ps1 -RunCompile
```

第一条覆盖对象树、实体读取和新建对象工程；第二条额外覆盖 PDK 编译和 run preflight。

## Qt 等价能力状态

- 导入模型：WebUI 支持切换本地对象包目录，重新加载对象、资源、算子、workflow 和对象包运行脚本。
- 在线运行：WebUI 调用对象包工具脚本启动 RuntimeHost job，启动后立即切换到该 job 预计写入的 run package，并通过 `/api/run/status` 跟踪进程状态。它没有直接复用 Qt 的 ROS action/client 类；若要完全复刻 Qt 连接控制，需要在后端增加 SDK/ROS bridge。
- 多场云图：WebUI 从 data-plane field artifact 自动识别当前分支/步的所有场，用户选择任一场后渲染真实网格云图。
- 分支实时曲线：WebUI 从 timeline 动态识别连续数值列，按当前分支绘制曲线；没有数据时明确显示为空。
- 实时时间轴：WebUI 把在线帧、分支步和 runtime event 统一显示为进度条滑块，并和当前分支时间线放在同一操作区。
- 历史回放：同一套 field/timeline/dataplane reader 可用于已有 run package，只读不启动计算。

## 后端 API

| API | 说明 |
|---|---|
| `GET /api/object` | 读取对象包摘要：组件、资源、算子、workflow |
| `GET /api/object/packages` | 列出工作区内可导入的对象包 |
| `POST /api/object/select` | 切换当前 WebUI 对象包 |
| `GET /api/runs` | 扫描本地 run package |
| `GET /api/runtime?run=` | 读取 RuntimeHost evidence、adapter session、事件 |
| `GET /api/timeline?run=` | 读取在线帧、预测分支、分支 step、事件 |
| `GET /api/dataplane?run=` | 读取 field artifact 与 QoI 索引 |
| `GET /api/field?run=&port=&branch=&step=` | 读取单个场 artifact 的节点值 |
| `GET /api/geometry?run=&subject=` | 读取真实网格 positions / indices |
| `GET /api/workflow/raw?id=` | 读取对象包 workflow 原始 JSON |
| `GET /api/workflow/raw?draft=` | 读取已保存 workflow 草稿 |
| `GET /api/workflow/drafts` | 列出本地 workflow 草稿 |
| `POST /api/workflow/validate` | 校验 workflow 节点、端口、contract 和环 |
| `POST /api/workflow/draft` | 保存 workflow 草稿 |
| `POST /api/run` | 调用对象包工具脚本进入 RuntimeHost |
| `POST /api/run/stop` | 停止由 WebUI 启动的后台任务 |

## 工作流编辑

工作流编辑页支持：

- 从对象包读取正式 workflow JSON。
- 默认显示宏观 workflow 阶段图，点击阶段后进入阶段内部算子图。
- 宏观页展示 workflow 的 clock、solver、scheduler、branching、stop、checkpoint 策略，以及每个算子的 `time_policy` / `time_policy_override` 明细；这些信息来自对象包声明，不从前端硬编码。
- 阶段内部图会把 `stage_inputs` / `stage_outputs` 画成左右边界节点：左侧代表上一时刻状态、外部观测或上游阶段输入，右侧代表下一时刻状态、QoI 或 evidence 输出。边界节点不是算子，但可以选中后在右侧增删 `node_id.port_id` 引用；也可以从左边界拖到算子输入、从算子输出拖到右边界，自动更新当前 stage 的边界声明。虚线边可选中删除，内部使用完整 `node_id.port_id` 作为 handle id，避免不同节点端口短名重复导致拖线不稳定。
- 从 operator spec 添加算子节点。
- 在同一 stage 内新增节点连线；删除节点时会同步移除当前 stage 中指向该节点的边界引用。
- 编辑节点的 `node_id`、`operator_ref`、`activation_policy`。
- 编辑连线的 `source_port` 和 `target_port`。
- 调用后端校验端口方向、contract 匹配、未知算子、未知节点、stage 内环和必需输入。
- 保存为本地草稿，位置为 `_local_artifacts/webui-workflow-drafts`。
- 从草稿重新读取，也可导入/导出 JSON。

当前画布已经接入 `@xyflow/react`，用于阶段内部的节点拖拽、端口 handle、连线、缩放、小地图和背景网格。宏观流程图只负责展示 phase/stage 层级，不把候选算子直接摆到主流程上。后验场重建、损伤累计、烧蚀累计这类物化细节归入阶段内部，不作为宏观流程节点展示。WebUI 自己只保留 `workflow JSON ⇄ graph model ⇄ validator` 这层 adapter，避免把对象语义或算子实现塞进前端画布。

当前对象建议按四类大算子组织：

- 状态转移算子：承载弹道状态推进、多场系数推进、场重建、损伤累计和烧蚀累计；既可用于在线滤波的先验推进，也可用于基于后验状态的未来预测。
- 观测方程算子：把状态或场映射为弹道、热流、压力、温度、应变等观测。
- 滤波算法：消费先验状态、预测观测和实际观测，输出当前后验状态。
- QoI/决策算子：只对当前时刻场和状态做失效、寿命等摘要，不反馈下一时刻状态。

宏观流程图只显示主阶段。后验场重建、累计状态更新等细节 stage 会标记为“状态转移细节”，需要进入对应 stage 后编辑内部算子和边界端口。

草稿默认写入 `_local_artifacts/webui-workflow-drafts`；如当前启动环境没有该目录写权限，后端会退到用户临时目录并在 API 返回实际 `draft_root`。也可以通过 `FLIGHTENV_WEBUI_DRAFT_ROOT` 指定草稿目录。

## 数据来源

WebUI 后端会从工作区自动发现：

- 对象包：`flightenv-object-reentry-vehicle`
- 运行包：`_local_artifacts/platform-*/mainline-runs`
- RuntimeHost evidence：`runtime_host_evidence.json`、`runtime_events.json`
- 分支索引：`branch_registry.json`、`run_timeline_index.json`
- 数据平面：field artifact、QoI artifact、runtime snapshot

## 维护约束

- 新页面优先消费通用 API，不在前端写对象专属规则。
- 新三维渲染能力通过 renderer/field artifact 扩展，不把算法逻辑塞进 UI。
- 启动运行仍通过对象包工具脚本和 RuntimeHost，不绕过平台调度链路。
- `htmlUI` 仅保留为历史原型；正式 Web Workbench 以本目录为准。
