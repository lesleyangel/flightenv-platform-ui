---
title: FlightEnv Web Platform Workbench — Experience / Information Architecture
status: draft
updated: 2026-06-22
owner: UX (bmad-ux)
sources:
  - file: flightenv-controller-ui/webui/README.md   # 现有页面与 API 真源
  - file: flightenv-controller-ui/webui/app.jsx      # 现有页面实现
relation: 本文件定义"如何工作"(IA/行为/状态/流程)。视觉令牌(颜色/字体/间距)由 DESIGN.md 拥有，本文用 {token} 交叉引用，未单独建 DESIGN.md 前沿用现 styles.css。
binding_decisions:
  - 把 在线监控 + 数据平面 + 运行时主机 + QoI账本 + 证据回放 合并为单一"运行检视器"(run-scoped, 内部 tab)。
  - 分页轴 = 生命周期角色(设计/运行/分析/系统)，不再按数据切片分页。
  - live 与 历史 不是独立模式，只是"运行检视器"里选哪个 run。
---

# FlightEnv Web Platform Workbench — EXPERIENCE

> 本契约在与任何 mock/wireframe/旧实现冲突时获胜。落地代码以本文 IA 为准。

## Foundation

- **形态**：桌面 Web 工作台（≥1280px 主用，自适应到 1920；非移动）。React(UMD)+babel-in-browser + 本地 vendored 依赖；工作流画布用 `@xyflow/react`(Vite bundle)，三维场用 three.js。
- **角色边界（子仓约束，不可越界）**：WebUI 只读对象包 / run package / runtime evidence / data-plane artifact / 公开 DTO；**不**复制算法私有源码，**不**在前端写对象专属规则，**不**绕过平台调度链路。三维只渲染真实 `geometry + field artifact`，缺数据明确报错、绝不画假图。
- **视觉系统**：沿用现 `styles.css`（浅色 + 青强调 + 卡片阴影）。本文只规定行为，不规定颜色。

## Information Architecture

### 顶层组织：按"用户在干什么"分四簇（不按数据切片分页）

```
设计态 ──────────→ 运行态 ──────────→ 分析态 ──────────→ 系统
对象/编排           配 + 启 + 进度        看这次跑出了什么      入口/配置/诊断
(不碰 run 数据)     (不画分支/场)        (只读证据, scoped)
```

每页只有**一个动词**，越界即冗余。

### 导航分组与页职责（边界 = "该做什么 / 不该做什么"）

| 簇 | 页 | 职责（做什么） | 边界（不该做什么） |
|---|---|---|---|
| **设计态** | 对象画像 | 组件 / 传感器布局 / 真实网格三维 | ❌ 不再列 workflow（挪到编排）；❌ 无 run 数据 |
| | 资源与模型 | resource/model 资产层级树 + 详情 | ❌ 无 run 数据 |
| | 算子库 | operator spec + 端口 + **typed DTO/schema** | ❌ 无 run 数据 |
| | 工作流编排 | **唯一**的 workflow 视图+编辑（宏观只读 + 阶段内编辑 + 校验 + 草稿） | ❌ 无运行控制、无实时数据；只读对象包/草稿 |
| **运行态** | **运行控制台** | 选对象包/草稿 → 选 workflow+profile → 帧数/触发间隔参数 → 准备/启动/停止 → 进度+状态+日志尾 | ❌ 不画分支树/场云图/QoI（那是检视器的活） |
| **分析态** | **运行检视器** | run-scoped 工作区：顶部选 run+分支+step；内部 tab=分支时间线 / 场云图 / 数据平面 / 运行时主机 / 健康 | ❌ 不启动/不改运行；只读证据 |
| **系统** | 总览 | 当前对象 + 激活 run + 健康红绿灯的**入口卡**，点击跳对应簇 | ❌ 不复述详情（瘦身） |
| | 配置与数据源 | WebUI 入口 / 对象包根 / run 根说明 | |
| | 诊断报告 | 对象包/timeline/runtime/data-plane 轻量自检 | |

### 核心结构决定：运行检视器（合并 5 页）

`在线监控 + 数据平面 + 运行时主机 + QoI账本 + 证据回放` → **一个 run-scoped 工作区**。

- **共享 scope（全局，顶栏）**：`run` × `branch` × `step`。这三者是检视器内**所有 tab 的统一输入**——切 run/分支/step，所有 tab 同步。
- **live 与 历史统一**：不是两个模式，只是 `run` 选择器里"正在跑的 run"还是"历史 run"。live = 自动跟随运行中 job + 轮询；历史 = 选定后只读。reader 完全同一套。
- **Tabs（每个只回答一个问题，scoped 到 run+branch+step）**：
  - `分支时间线`：分支树 + 统一时间轴 scrubber + 帧表 + 当前状态 KV。回答"现在/某帧是什么状态"。
  - `场云图`：选场端口 → 真实网格三维 + colorbar。回答"这帧的场长什么样"。**场云图全 UI 只在此出现一次**（消灭在线/数据平面重复）。
  - `数据平面`：artifact / port / contract / QoI 索引表 + 对账。回答"这帧产出了哪些 artifact"。
  - `运行时主机`：adapter session 生命周期 + runtime 事件。回答"执行层发生了什么"。
  - `健康`：损伤/烧蚀场三维 + RUL/失效 QoI + 区域累计。回答"对象还能撑多久"。

### Surface closure 自检

每个需求都落到唯一 surface：载对象→对象画像/控制台；改编排→编排；配+启→控制台；看场/分支/QoI/健康/执行→检视器对应 tab。无悬空需求，无重复 surface。

## Voice and Tone（微文案）

- 面板副标题 = 一句"这块看什么/数据来源"（如"data_plane_manifest"）。动词在标题，来源在副标题。
- 空态：说**为什么空 + 下一步**（"尚未载入对象包：去对象接入载入"），不只"暂无数据"。
- 错误：**显式拒绝 + 原因**（"节点数不一致，拒绝渲染：field=9006/mesh=24385"），绝不静默或画假图。
- 边界提示：编辑器越界连线时明确指路（"阶段输入只能连到算子输入"）。

## Component Patterns（行为，视觉在 DESIGN.md）

- **RunScopeBar（新，检视器顶栏）**：`run 选择器(live●/历史○ 标记) · 分支选择器 · step scrubber`。改任一项 → 广播给所有 tab。live 时 run 自动跟随运行中 job。
- **Panel**：标题+副标题+工具区+可滚动 body；`min-height` 保证可读，不被挤扁；超出 body 内滚动。
- **FieldViewer**：节点数严格对账，不匹配/缺数据 → 覆盖层报错；拖拽旋转+滚轮缩放+colorbar。
- **DataTable**：粘性表头、行可选驱动 scope、长表内滚动。
- **WorkflowCanvas**：宏观(只读 stage 列)↔阶段(可编辑算子图)双视图；端口 handle、连线、边界节点(stage I/O)、参数检视、校验。

## State Patterns

- **无对象**：所有页 → "去对象接入载入对象包"的引导空态（不报错）。
- **无 run**：检视器 → "选择一个 run 或先在控制台启动运行"。
- **live 跟随**：控制台启动后，检视器 run 自动指向该 job 的 run dir；轮询刷新；run 写盘前显示"等待首帧"。
- **可选数据失败**：单面板降级提示，**不**阻塞整页（修当前"对象包列表 404 拖垮全局遮罩"）。
- **缺场/缺网格**：FieldViewer 显式拒绝，列出 field/mesh node_count。

## Interaction Primitives

- **scope 级联**：run → 自动选主分支 → 自动选最新 step；任一上游变化重置下游为合理默认。
- **轮询/推送**：live 默认 ~1.8s 轮询；后续可升级 SSE。
- **连线即校验**：编辑器连线时用 `typed_io_contract`/`contract_id` 校验源出↔目标入，不匹配高亮（发挥结构体化价值）。

## Accessibility Floor

- 键盘可达：nav、run/分支/step 选择器、tab 切换、表行选择。
- 焦点可见；颜色不作唯一信息载体（live/失败除颜色外带文字/图标）。
- 三维区提供文字诊断（场名/节点数/统计），不依赖纯视觉。

## Key Flows

1. **配置并启动一次在线运行（工程师 老周）**
   老周在**运行控制台**选 reentry 对象包 → workflow=online_filtering_external_input、profile=online_filtering_full、在线帧=70、触发间隔=30 → 点"启动"。进度条走起，状态=运行中。**климакс**：他切到**运行检视器**，run 已自动指向这次 job，分支时间线随帧前进——他全程没离开"运行→分析"这条线，没在 5 个页面间找数据。

2. **检视一次运行的损伤演化（结构工程师 小林）**
   小林在**运行检视器**选历史 run → 分支选 main.realtime_prediction → 拖 step scrubber 到末帧 → 切到`健康` tab。**климакс**：损伤场三维随 step 累积变红，RUL 曲线下行——同一个 run+分支+step scope，场云图/QoI/健康三个 tab 讲同一个故事。

3. **给在线路径加损伤 stage（平台工程师 阿杰）**
   阿杰在**工作流编排**（设计态，无任何 run 数据干扰）打开 online_filtering_external_input → 进 posterior_field_reconstruction stage → 加 structure_damage 节点、连 strain/temperature → 连线即校验 contract 匹配 → 保存草稿。**климакс**：他不需要碰运行控制台就完成编排；下次在控制台选这个草稿启动即可。

## 迁移映射（现 12 页 → 4 簇 / 9 职责单一页）

| 现有页 | 去向 |
|---|---|
| 对象接入 / 系统设置 | 运行控制台(对象选择段) / 配置与数据源 |
| 在线运行 | **拆**：控制半边→运行控制台；监控半边→运行检视器(分支时间线 tab) |
| 运行时主机 | 运行检视器 · 运行时主机 tab |
| 数据平面 | 运行检视器 · 数据平面 tab |
| 状态/QoI账本 | 运行检视器 · 数据平面 tab(QoI 区) |
| 证据回放 | 运行检视器(run 选择器选历史 run，不再单独成页) |
| 工作流编排 | 工作流编排(设计态, 不变职责) |
| 对象画像 / 资源与模型 / 算子库 | 设计态(对象画像去掉 workflow 列表) |
| 总览 / 配置与数据源 / 诊断报告 | 系统(总览瘦身为入口卡) |

净效果：**12 扁平页 → 4 簇 / 约 9 个职责单一页**；消灭 3 处重复（场云图、QoI、运行配置）；模式退化为检视器的 run 来源。

## Open Questions（待 {user} 决策，落地前确认）

- 检视器 5 个 tab 的默认顺序与默认 tab（建议默认`分支时间线`）。
- "总览"是否保留为独立页，还是并入运行控制台首屏。
- 控制台的"对象/草稿选择"与现"对象接入/Phase8 发布闭环"如何合并（草稿发布是否单列）。
