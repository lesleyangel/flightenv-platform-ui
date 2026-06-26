const { useCallback, useEffect, useMemo, useRef, useState } = React;

const MODULE_CARDS = [
  ["workspace", "主页", "工程"],
  ["modeler", "对象定义", "源定义"],
  ["resources", "资源与模型", "资产"],
  ["operators", "算子库", "能力"],
  ["workflow", "工作流编排", "图"],
  ["runtime", "作业", "编译/运行"],
  ["inspector", "结果回放", "Evidence"],
  ["diagnostics", "诊断", "Gate"],
  ["config", "配置", "环境"],
];

const GENERIC_FRAME_CONTRACT_OPTIONS = ["ScalarValue.v1", "VectorValue.v1", "TensorValue.v1", "TensorRef.v1", "ArtifactRef.v1", "EventSnapshot.v1"];
const GENERIC_VALUE_KIND_OPTIONS = ["scalar", "vector", "tensor", "field", "decision", "event"];

const LEGACY_PAGE_ALIASES = {
  online: "inspector",
  dataplane: "inspector",
  ledger: "inspector",
  replay: "inspector",
  runconfig: "runtime",
};

const STATUS_LABELS = {
  running: "运行中",
  completed: "已完成",
  ready: "就绪",
  failed: "失败",
  error: "错误",
  stopped: "已停止",
  unknown: "未知",
};

const RUN_MODES = {
  live: {
    title: "实时在线",
    hint: "连接 WebUI 启动的 RuntimeHost job，并轮询当前运行包。",
  },
  replay: {
    title: "历史回放",
    hint: "只读本地 run package / evidence / data-plane artifact。",
  },
  edit: {
    title: "编排编辑",
    hint: "读取对象包 workflow，编辑并保存草稿，不启动计算。",
  },
};

const PAGE_IDS = new Set([
  ...MODULE_CARDS.map(([id]) => id),
  "overview",
  "object",
  ...Object.keys(LEGACY_PAGE_ALIASES),
]);

function initialPageFromLocation() {
  const params = new URLSearchParams(window.location.search || "");
  const fromQuery = params.get("page");
  const fromHash = String(window.location.hash || "").replace(/^#\/?/, "");
  if (fromQuery === "models" || fromHash === "models") return "resources";
  if (LEGACY_PAGE_ALIASES[fromQuery]) return LEGACY_PAGE_ALIASES[fromQuery];
  if (LEGACY_PAGE_ALIASES[fromHash]) return LEGACY_PAGE_ALIASES[fromHash];
  if (PAGE_IDS.has(fromQuery)) return fromQuery;
  if (PAGE_IDS.has(fromHash)) return fromHash;
  return "workspace";
}

function initialRunFromLocation() {
  const params = new URLSearchParams(window.location.search || "");
  return params.get("run") || "";
}

function modeForPage(page) {
  if (page === "workflow" || page === "modeler" || page === "runconfig") return "edit";
  if (page === "inspector") return "live";
  return "live";
}

function cls(...parts) {
  return parts.filter(Boolean).join(" ");
}

function asArray(value) {
  return Array.isArray(value) ? value : [];
}

function firstDefined(...values) {
  for (const value of values) {
    if (value !== undefined && value !== null && value !== "") return value;
  }
  return "";
}

function fmtNumber(value, digits = 3) {
  const n = Number(value);
  if (!Number.isFinite(n)) return value === 0 ? "0" : "-";
  if (Math.abs(n) >= 1000 || (Math.abs(n) > 0 && Math.abs(n) < 0.01)) {
    return n.toExponential(2);
  }
  return n.toLocaleString("zh-CN", { maximumFractionDigits: digits });
}

function fmtTime(value) {
  const n = Number(value);
  if (!Number.isFinite(n)) return "-";
  return `${fmtNumber(n, 2)} s`;
}

function displayStatus(status) {
  const key = String(status || "unknown").toLowerCase();
  return STATUS_LABELS[key] || status || "未知";
}

function branchKindLabel(kind) {
  return humanizeObjectKey(kind) || "分支";
}

const ACTIVE_RUN_STATUSES = new Set(["running", "preparing", "starting", "ready", "paused", "resuming"]);

function runRecordFor(runs, run) {
  return asArray(runs).find((item) => item && item.run === run) || {};
}

function runStatusValue(run, runs, runtime, status) {
  const row = runRecordFor(runs, run);
  return String(firstDefined(
    status && status.running ? "running" : "",
    runtime && runtime.status,
    status && status.status,
    status && status.state,
    row.status,
    row.summary_status,
    run ? "completed" : "unknown",
  )).toLowerCase();
}

function isActiveRun(run, runs, runtime, status) {
  if (!run) return false;
  if (status && status.running) return true;
  return ACTIVE_RUN_STATUSES.has(runStatusValue(run, runs, runtime, status));
}

function runScopeLabel(run, runs, runtime, status, followLive) {
  if (!run) return "未选择 run";
  if (isActiveRun(run, runs, runtime, status)) return followLive ? "实时跟随" : "实时 run / 手动定位";
  return "历史回放";
}

function portDisplay(port) {
  if (!port) return "-";
  return String(port).replace(/^field\./, "field / ");
}

function fieldPort(field) {
  return firstDefined(field && field.port_id, field && field.port, field && field.contract_id);
}

function fieldStats(field) {
  const stats = (field && field.stats) || {};
  return {
    min: firstDefined(stats.min, field && field.min),
    max: firstDefined(stats.max, field && field.max),
    mean: firstDefined(stats.mean, field && field.mean),
  };
}

function fieldStep(field) {
  return Number(firstDefined(field && field.step, field && field.step_index, 0));
}

function fieldDisplayName(field) {
  return firstDefined(
    field && field.display_name,
    field && field.field_name,
    field && field.role,
    field && field.component_id,
    portDisplay(fieldPort(field)),
  );
}

function modeLabel(mode) {
  return (RUN_MODES[mode] && RUN_MODES[mode].title) || mode || "-";
}

function shortPath(value) {
  if (!value) return "-";
  const text = String(value).replaceAll("\\", "/");
  const parts = text.split("/");
  return parts.length <= 4 ? text : `.../${parts.slice(-4).join("/")}`;
}

function isObjectLoaded(workspace, object) {
  return Boolean((workspace && workspace.object_loaded) || (object && object.object_loaded));
}

function objectPackageRoot(workspace, object) {
  return firstDefined(
    object && object.package_dir,
    object && object.object_package_root,
    workspace && workspace.object_package_root,
  );
}

function diagnosticsOf(validation) {
  return asArray(validation && validation.diagnostics);
}

function joinList(value, limit = 4) {
  const items = asArray(value).filter(Boolean);
  if (!items.length) return "-";
  const visible = items.slice(0, limit).join("、");
  return items.length > limit ? `${visible} 等 ${items.length} 项` : visible;
}

function timePolicyText(policy) {
  if (!policy || typeof policy !== "object") return "-";
  return [
    policy.kind,
    firstDefined(policy.fixed_dt_s, policy.sample_period_s) ? `dt=${firstDefined(policy.fixed_dt_s, policy.sample_period_s)}s` : "",
    policy.output_time_role ? `输出=${policy.output_time_role}` : "",
  ].filter(Boolean).join(" / ") || "-";
}

function lifecycleText(value) {
  return joinList(value, 8);
}

function rendererResolutionText(resolution) {
  if (!resolution || typeof resolution !== "object") return "-";
  return `${firstDefined(resolution.resolved_renderer, "-")} (${firstDefined(resolution.resolution, "unknown")})`;
}

const UI_RENDERER_REGISTRY = {
  "field.vtk.scalar.v1": {
    renderer_id: "field.vtk.scalar.v1",
    value_kind: "field_tensor",
    component: "Field3D",
    display_name: "三维标量场",
  },
  "coefficient_series.v1": {
    renderer_id: "coefficient_series.v1",
    value_kind: "series",
    component: "ScalarTrendWidget",
    display_name: "序列曲线",
  },
  "branch.timeline.v1": {
    renderer_id: "branch.timeline.v1",
    value_kind: "timeline",
    component: "BranchTimelineWidget",
    display_name: "分支时间线",
  },
  "evidence.table.v1": {
    renderer_id: "evidence.table.v1",
    value_kind: "evidence",
    component: "DataTable",
    display_name: "证据表",
  },
  "qoi.decision.v1": {
    renderer_id: "qoi.decision.v1",
    value_kind: "qoi",
    component: "DataTable",
    display_name: "QoI 摘要",
  },
  "generic.operator_ports.v1": {
    renderer_id: "generic.operator_ports.v1",
    value_kind: "generic",
    component: "DataTable",
    display_name: "通用端口表",
  },
};

function resolveUiRenderer(descriptor, valueKind) {
  const requested = firstDefined(
    descriptor && descriptor.renderer_id,
    descriptor && descriptor.rendererId,
    valueKind === "field_tensor" ? "field.vtk.scalar.v1" : "",
  );
  const fallback = firstDefined(
    descriptor && descriptor.fallback_renderer,
    descriptor && descriptor.fallbackRenderer,
    "generic.operator_ports.v1",
  );
  const resolved = UI_RENDERER_REGISTRY[requested] ? requested : fallback;
  const item = UI_RENDERER_REGISTRY[resolved] || UI_RENDERER_REGISTRY["generic.operator_ports.v1"];
  return {
    requested_renderer: requested || "-",
    resolved_renderer: item.renderer_id,
    resolution: requested && UI_RENDERER_REGISTRY[requested] ? "direct" : "fallback",
    value_kind: valueKind || item.value_kind,
    display_name: item.display_name,
    component: item.component,
  };
}

function artifactValueKind(row) {
  return firstDefined(row && row.value_kind, row && row.kind, row && row.representation, "generic");
}

function artifactRendererDescriptor(row) {
  const valueKind = artifactValueKind(row);
  if (valueKind === "field_tensor" || Number(firstDefined(row && row.node_count, 0)) > 0) {
    return { renderer_id: "field.vtk.scalar.v1", fallback_renderer: "generic.operator_ports.v1" };
  }
  if (String(valueKind).toLowerCase().includes("qoi")) {
    return { renderer_id: "qoi.decision.v1", fallback_renderer: "generic.operator_ports.v1" };
  }
  return { renderer_id: "generic.operator_ports.v1", fallback_renderer: "generic.operator_ports.v1" };
}

function artifactRows(dataplane) {
  const fields = asArray(dataplane && dataplane.fields).map((row, index) => ({
    ...row,
    artifact_row_id: firstDefined(row.artifact_id, row.id, row.uri, `field-${index}`),
    artifact_id: firstDefined(row.artifact_id, row.id, row.uri, `field-${index}`),
    kind: firstDefined(row.kind, row.value_kind, "field_tensor"),
    value_kind: firstDefined(row.value_kind, "field_tensor"),
    port_id: fieldPort(row),
    producer: firstDefined(row.producer, row.operator_id, row.node_id, "-"),
    artifact_uri: firstDefined(row.artifact_uri, row.uri, row.ref, ""),
  }));
  const qois = asArray(dataplane && dataplane.qois).map((row, index) => ({
    ...row,
    artifact_row_id: firstDefined(row.artifact_id, row.qoi_id, row.id, row.port, `qoi-${index}`),
    artifact_id: firstDefined(row.artifact_id, row.qoi_id, row.id, row.port, `qoi-${index}`),
    kind: firstDefined(row.kind, row.value_kind, "qoi"),
    value_kind: firstDefined(row.value_kind, "qoi"),
    port_id: firstDefined(row.port_id, row.port, row.qoi_id, row.id),
    producer: firstDefined(row.producer, row.operator_id, row.node_id, "-"),
    artifact_uri: firstDefined(row.artifact_uri, row.uri, row.ref, ""),
  }));
  return [...fields, ...qois];
}

function groupBy(items, keyFn) {
  return asArray(items).reduce((acc, item) => {
    const key = keyFn(item) || "未分组";
    if (!acc[key]) acc[key] = [];
    acc[key].push(item);
    return acc;
  }, {});
}

function resourceId(resource) {
  return firstDefined(resource && resource.resource_id, resource && resource.id, resource && resource.uri);
}

function resourceTitle(resource) {
  return firstDefined(resource && resource.display_name, resource && resource.name, resourceId(resource));
}

function resourceType(resource) {
  return firstDefined(resource && resource.resource_type, resource && resource.type, "resource");
}

function operatorDisplayName(operator) {
  const id = firstDefined(operator && operator.operator_id, operator && operator.id);
  return firstDefined(operator && operator.display_name, operator && operator.title, operator && operator.name, id);
}

function formatJsonValue(value) {
  if (Array.isArray(value)) return value.join("、");
  if (value && typeof value === "object") return JSON.stringify(value);
  return firstDefined(value, "-");
}

function portId(port) {
  return firstDefined(port && port.port_id, port && port.id, port && port.name);
}

function portContractId(port) {
  return firstDefined(
    port && port.contract_id,
    port && port.schema_id,
    port && port.dto_id,
    port && port.type,
    port && port.typed_io_contract && port.typed_io_contract.schema_id,
  );
}

function portTimeRole(port) {
  return firstDefined(
    port && port.time_role,
    port && port.output_time_role,
    port && port.temporal_role,
    port && port.time_policy && port.time_policy.output_time_role,
  );
}

function portTypedDtoRef(port) {
  const typed = (port && port.typed_io_contract) || {};
  return firstDefined(typed.dto_name, typed.type_name, typed.schema_id, port && port.typed_dto_ref);
}

function operatorPorts(operator) {
  const inputs = asArray(operator && (operator.inputs || operator.input_ports))
    .map((port) => ({ ...port, direction: "input" }));
  const outputs = asArray(operator && (operator.outputs || operator.output_ports))
    .map((port) => ({ ...port, direction: "output" }));
  return [...inputs, ...outputs];
}

function profileId(profile) {
  return firstDefined(profile && profile.profile_id, profile && profile.id);
}

function workflowId(workflow) {
  return firstDefined(workflow && workflow.workflow_id, workflow && workflow.id);
}

function workflowPhase(workflow) {
  return firstDefined(workflow && workflow.phase, workflow && workflow.phase_id);
}

function profileAllowsWorkflow(profile, workflow) {
  const phases = asArray(profile && profile.workflow_phases).map(String);
  return !phases.length || phases.includes(String(workflowPhase(workflow)));
}

function planRefRows(result) {
  return asArray(result && result.plan && result.plan.plan_refs);
}

function compileResultRows(result) {
  return asArray(result && result.results);
}

function diagnosticRows(result) {
  return [
    ...asArray(result && result.diagnostics),
    ...asArray(result && result.errors).map((message) => ({ severity: "blocking", message })),
    ...asArray(result && result.warnings).map((message) => ({ severity: "warning", message })),
  ];
}

function planSummaryRows(result) {
  const summary = (result && result.plan && result.plan.summary) || {};
  return [
    ["execution nodes", fmtNumber(summary.node_count, 0)],
    ["time nodes", fmtNumber(summary.time_node_count, 0)],
    ["scheduler nodes", fmtNumber(summary.scheduler_node_count, 0)],
    ["data-plane nodes", fmtNumber(summary.data_plane_node_count, 0)],
    ["data-plane ports", fmtNumber(summary.data_plane_port_count, 0)],
    ["resource locks", fmtNumber(summary.resource_lock_count, 0)],
    ["model snapshots", fmtNumber(summary.model_snapshot_count, 0)],
    ["standard artifacts", `${fmtNumber(summary.existing_standard_artifact_count, 0)} / ${fmtNumber(summary.standard_artifact_count, 0)}`],
    ["stale", summary.stale ? "yes" : "no"],
  ];
}

function useJson(url, deps, options = {}) {
  const { pollMs = 0, enabled = true } = options;
  const [state, setState] = useState({ data: null, loading: true, error: "" });

  const load = useCallback(async () => {
    if (!enabled || !url) {
      setState({ data: null, loading: false, error: "" });
      return;
    }
    try {
      const res = await fetch(url, { cache: "no-store" });
      if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
      const data = await res.json();
      setState({ data, loading: false, error: "" });
    } catch (err) {
      setState((old) => ({ ...old, loading: false, error: err.message || String(err) }));
    }
  }, [url, enabled]);

  useEffect(() => {
    let cancelled = false;
    const run = async () => {
      if (!cancelled) await load();
    };
    run();
    if (!pollMs || !enabled) return () => { cancelled = true; };
    const timer = setInterval(run, pollMs);
    return () => {
      cancelled = true;
      clearInterval(timer);
    };
  }, [load, pollMs, enabled, ...(deps || [])]);

  return { ...state, reload: load };
}

async function apiPost(url, body = {}) {
  const res = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body || {}),
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok || data.ok === false) throw new Error(data.message || data.error || `${res.status} ${res.statusText}`);
  return data;
}

function Panel({ title, subtitle, toolbar, children, className, bodyClassName }) {
  return (
    <section className={cls("panel", className)}>
      {(title || subtitle || toolbar) && (
        <header className="panel-head">
          <div>
            {title && <h2>{title}</h2>}
            {subtitle && <p>{subtitle}</p>}
          </div>
          {toolbar && <div className="panel-tools">{toolbar}</div>}
        </header>
      )}
      <div className={cls("panel-body", bodyClassName)}>{children}</div>
    </section>
  );
}

function InfoModal({ title, subtitle, onClose, children }) {
  if (!title && !children) return null;
  return (
    <div className="modal-backdrop" role="presentation" onMouseDown={onClose}>
      <section className="modal-panel" role="dialog" aria-modal="true" aria-label={title} onMouseDown={(event) => event.stopPropagation()}>
        <header className="modal-head">
          <div>
            <h2>{title}</h2>
            {subtitle && <p>{subtitle}</p>}
          </div>
          <button className="modal-close" type="button" onClick={onClose} aria-label="关闭">×</button>
        </header>
        <div className="modal-body">{children}</div>
      </section>
    </div>
  );
}

function Badge({ children, tone = "neutral" }) {
  return <span className={cls("badge", `badge-${tone}`)}>{children}</span>;
}

function StatusPill({ status }) {
  const key = String(status || "unknown").toLowerCase();
  const tone = key.includes("fail") || key.includes("error") ? "bad" : key.includes("run") ? "live" : "good";
  return <Badge tone={tone}>{displayStatus(status)}</Badge>;
}

function Button({ children, tone = "default", busy = false, ...props }) {
  return (
    <button className={cls("btn", `btn-${tone}`)} disabled={busy || props.disabled} {...props}>
      {busy ? "处理中..." : children}
    </button>
  );
}

function EditModeToolbar({ editing, setEditing, canEdit = true, dirty = false, label = "编辑" }) {
  function toggleEdit() {
    if (editing && dirty && !window.confirm("当前有未保存修改，退出编辑会保留页面内容但不能保存。确认退出编辑？")) return;
    setEditing(!editing);
  }
  return (
    <div className="edit-mode-toolbar">
      <Badge tone={editing ? "live" : "neutral"}>{editing ? "编辑模式" : "查看模式"}</Badge>
      <Button disabled={!canEdit} tone={editing ? "neutral" : "primary"} onClick={toggleEdit}>
        {editing ? "退出编辑" : label}
      </Button>
      {dirty && <span className="edit-mode-dirty">有未保存修改</span>}
    </div>
  );
}

function EditingGate({ editing, children, className = "" }) {
  return (
    <fieldset className={cls("editing-gate", !editing && "readonly", className)} disabled={!editing}>
      {children}
    </fieldset>
  );
}

function ReadonlyHint({ editing, text = "当前为查看模式。点击“编辑”后才能修改字段、创建、删除或保存。" }) {
  if (editing) return null;
  return <div className="readonly-hint">{text}</div>;
}

function Empty({ title = "暂无数据", hint = "等待运行产物或对象包声明。" }) {
  return (
    <div className="empty">
      <strong>{title}</strong>
      <span>{hint}</span>
    </div>
  );
}

function MetricCard({ label, value, hint, tone }) {
  return (
    <div className={cls("metric-card", tone && `metric-${tone}`)}>
      <span>{label}</span>
      <strong>{value}</strong>
      {hint && <small>{hint}</small>}
    </div>
  );
}

function KeyValue({ rows }) {
  return (
    <dl className="kv">
      {rows.map(([key, value]) => (
        <React.Fragment key={key}>
          <dt>{key}</dt>
          <dd>{value || value === 0 ? value : "-"}</dd>
        </React.Fragment>
      ))}
    </dl>
  );
}

function DataTable({ columns, rows, emptyHint, rowKey }) {
  const data = asArray(rows);
  if (!data.length) return <Empty hint={emptyHint || "当前没有可展示的记录。"} />;
  return (
    <div className="table-wrap">
      <table className="data-table">
        <thead>
          <tr>
            {columns.map((col) => <th key={col.key}>{col.title}</th>)}
          </tr>
        </thead>
        <tbody>
          {data.map((row, index) => (
            <tr key={rowKey ? rowKey(row, index) : index}>
              {columns.map((col) => (
                <td key={col.key}>{col.render ? col.render(row, index) : row[col.key]}</td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function ProgressBar({ value, total }) {
  const pct = total ? Math.max(0, Math.min(100, (Number(value) / Number(total)) * 100)) : 0;
  return (
    <div className="progress">
      <div style={{ width: `${pct}%` }} />
      <span>{fmtNumber(value, 0)} / {fmtNumber(total, 0)}</span>
    </div>
  );
}

function RunPicker({ runs, value, onChange }) {
  return (
    <label className="select-label">
      <span>当前运行包</span>
      <select value={value || ""} onChange={(e) => onChange(e.target.value)}>
        {asArray(runs).map((run) => (
          <option key={run.run} value={run.run}>
            {run.run}
          </option>
        ))}
      </select>
    </label>
  );
}

function ModeSwitcher({ mode, onChange }) {
  return (
    <div className="mode-switcher">
      {Object.entries(RUN_MODES).map(([id, item]) => (
        <button key={id} className={mode === id ? "active" : ""} onClick={() => onChange(id)}>
          <strong>{item.title}</strong>
          <span>{item.hint}</span>
        </button>
      ))}
    </div>
  );
}

function ModeBanner({ mode, status, run }) {
  const item = RUN_MODES[mode] || {};
  const liveState = status && status.running ? "已连接 WebUI job" : "未检测到正在运行的 WebUI job";
  return (
    <div className={cls("mode-banner", `mode-${mode}`)}>
      <div>
        <strong>{item.title || mode}</strong>
        <span>{item.hint || "-"}</span>
      </div>
      <Badge tone={mode === "live" && status && status.running ? "live" : "neutral"}>
        {mode === "live" ? liveState : (run || "未选择 run")}
      </Badge>
    </div>
  );
}

function collectBranches(timeline, dataplane) {
  const map = new Map();
  asArray(timeline && timeline.branches).forEach((branch) => {
    const id = firstDefined(branch.branch_id, branch.id, branch.branch);
    if (!id) return;
    const summary = branch.summary || {};
    const control = branch.control_state || {};
    map.set(id, {
      branch_id: id,
      parent_branch_id: firstDefined(branch.parent_branch_id, branch.parent_id, branch.parent, ""),
      branch_kind: firstDefined(branch.branch_kind, branch.kind, branch.type),
      display_name: firstDefined(branch.display_name, branch.name, id),
      kind_label: firstDefined(branch.kind_label, branch.branch_kind, branch.kind, ""),
      status: firstDefined(branch.status, "unknown"),
      frame_count: Number(firstDefined(branch.frame_count, branch.frames, summary.frame_count, 0)),
      step_count: Number(firstDefined(branch.step_count, branch.steps, summary.step_count, 0)),
      trigger: firstDefined(branch.trigger, branch.trigger_kind, branch.trigger_frame, branch.seed_runtime_outputs_ref, "-"),
      start_time_s: firstDefined(branch.start_time_s, branch.public_start_time_s, branch.created_at_utc, ""),
      progress: firstDefined(branch.progress_percent, branch.progress, summary.progress_percent, ""),
      stop_reason: firstDefined(branch.stop_reason, branch.reason, branch.termination_reason, ""),
      last_action: firstDefined(control.last_action, ""),
    });
  });
  const fieldStatsByBranch = new Map();
  asArray(dataplane && dataplane.fields).forEach((field) => {
    const id = firstDefined(field.branch_id, field.branch, "main");
    if (!fieldStatsByBranch.has(id)) {
      fieldStatsByBranch.set(id, { steps: new Set(), frames: new Set(), count: 0 });
    }
    const stats = fieldStatsByBranch.get(id);
    stats.count += 1;
    const step = fieldStep(field);
    if (Number.isFinite(step)) stats.steps.add(step);
    const frame = Number(firstDefined(field.mainline_frame_index, field.frame, field.frame_index, field.step, field.step_index));
    if (Number.isFinite(frame)) stats.frames.add(frame);
    if (!map.has(id)) {
      map.set(id, {
        branch_id: id,
        parent_branch_id: firstDefined(field.parent_branch_id, field.parent_id, ""),
        branch_kind: firstDefined(field.branch_kind, field.family, "field"),
        display_name: id,
        kind_label: firstDefined(field.family, "field"),
        status: "completed",
        frame_count: 0,
        step_count: 0,
        trigger: "-",
        start_time_s: "",
        progress: "",
        stop_reason: "",
        last_action: "",
      });
    }
  });
  fieldStatsByBranch.forEach((stats, id) => {
    const branch = map.get(id);
    if (!branch) return;
    branch.step_count = Math.max(Number(branch.step_count) || 0, stats.steps.size);
    branch.frame_count = Math.max(Number(branch.frame_count) || 0, stats.frames.size || stats.steps.size);
  });
  return Array.from(map.values()).sort((a, b) => String(a.branch_id).localeCompare(String(b.branch_id)));
}

function collectBranchSteps(timeline, branchId) {
  const online = asArray(timeline && timeline.online_frames).map((frame, index) => {
    const row = { ...frame };
    row.kind = "online";
    row.branch_id = firstDefined(frame.branch_id, frame.branch, "main.online");
    row.step = Number(firstDefined(frame.step, frame.step_index, frame.frame, frame.frame_index, index));
    row.frame = Number(firstDefined(frame.frame, frame.frame_index, row.step, index));
    row.public_time_s = firstDefined(frame.public_time_s, frame.t, frame.time_s, frame.sample_time_s, index);
    row.h_m = firstDefined(frame.h_m, frame.h, frame.height_m);
    row.mach = firstDefined(frame.Ma, frame.ma, frame.mach);
    row.qoi_count = Number(firstDefined(frame.qoi_count, frame.qois, 0));
    row.input_status = firstDefined(frame.freshness, frame.input_status, "");
    return row;
  });
  const branch = asArray(timeline && timeline.branch_steps).map((step, index) => {
    const row = { ...step };
    row.kind = "branch";
    row.branch_id = firstDefined(step.branch_id, step.branch, "");
    row.step = Number(firstDefined(step.step, step.step_index, step.frame_index, index));
    row.frame = Number(firstDefined(step.frame, step.frame_index, step.step, index));
    row.public_time_s = firstDefined(step.public_time_s, step.t_s, step.time_s, index);
    row.h_m = firstDefined(step.h_m, step.height_m);
    row.mach = firstDefined(step.Ma, step.ma, step.mach);
    row.qoi_count = Number(firstDefined(step.qoi_count, step.qois, 0));
    return row;
  });
  return [...online, ...branch]
    .filter((step) => !branchId || step.branch_id === branchId)
    .sort((a, b) => {
      const at = Number(firstDefined(a.public_time_s, a.time_s, a.t, a.step));
      const bt = Number(firstDefined(b.public_time_s, b.time_s, b.t, b.step));
      if (Number.isFinite(at) && Number.isFinite(bt) && at !== bt) return at - bt;
      return Number(a.step) - Number(b.step);
    });
}

function collectTimelineRows(timeline, runtime) {
  const online = asArray(timeline && timeline.online_frames).map((frame, index) => ({
    kind: "online",
    branch_id: firstDefined(frame.branch_id, "main.online"),
    step: Number(firstDefined(frame.step, frame.step_index, frame.frame_index, index)),
    t: Number(firstDefined(frame.public_time_s, frame.t, frame.time_s, index)),
    label: `在线帧 ${firstDefined(frame.step, frame.frame_index, index)}`,
  }));
  const branch = asArray(timeline && timeline.branch_steps).map((step, index) => ({
    kind: "branch",
    branch_id: firstDefined(step.branch_id, ""),
    step: Number(firstDefined(step.step, step.step_index, index)),
    t: Number(firstDefined(step.public_time_s, step.t_s, step.time_s, index)),
    label: `${firstDefined(step.branch_id, "branch")} · step ${firstDefined(step.step, step.step_index, index)}`,
  }));
  const events = asArray(runtime && runtime.runtime_events).slice(-80).map((event, index) => ({
    kind: "event",
    branch_id: firstDefined(event.branch_id, ""),
    step: Number(firstDefined(event.step, event.step_index, index)),
    t: Number(firstDefined(event.public_time_s, event.time_s, index)),
    label: firstDefined(event.kind, event.event_kind, "runtime event"),
  }));
  return [
    ...collectBranchSteps(timeline, "").map((row, index) => ({
      kind: row.kind || "branch",
      branch_id: firstDefined(row.branch_id, ""),
      step: Number(firstDefined(row.step, row.step_index, index)),
      t: Number(firstDefined(row.public_time_s, row.t_s, row.time_s, row.t, index)),
      label: `${firstDefined(row.branch_id, "branch")} / step ${firstDefined(row.step, row.step_index, index)}`,
    })),
    ...events,
  ]
    .filter((row) => Number.isFinite(row.t))
    .sort((a, b) => a.t - b.t);
}

function collectFields(dataplane, branchId) {
  return asArray(dataplane && dataplane.fields)
    .filter((field) => !branchId || field.branch_id === branchId || !field.branch_id)
    .sort((a, b) => {
      const aStep = Number(firstDefined(a.step, a.step_index, 0));
      const bStep = Number(firstDefined(b.step, b.step_index, 0));
      if (aStep !== bStep) return aStep - bStep;
      return String(fieldPort(a)).localeCompare(String(fieldPort(b)));
    });
}

function defaultSelection(timeline, dataplane, old) {
  const branches = collectBranches(timeline, dataplane);
  const branchIds = branches.map((b) => b.branch_id);
  const firstBranchWithFields = branches.find((b) => collectFields(dataplane, b.branch_id).length > 0);
  const branch = branchIds.includes(old.branch) ? old.branch : ((firstBranchWithFields && firstBranchWithFields.branch_id) || branchIds[0] || "");
  const fields = collectFields(dataplane, branch);
  const ports = Array.from(new Set(fields.map((f) => fieldPort(f)).filter(Boolean)));
  const port = ports.includes(old.port) ? old.port : (ports[0] || "");
  const steps = fields
    .filter((f) => !port || fieldPort(f) === port)
    .map((f) => Number(firstDefined(f.step, f.step_index, 0)))
    .filter((v) => Number.isFinite(v));
  const lastStep = steps.length ? Math.max(...steps) : "";
  const step = old.step !== "" && steps.includes(Number(old.step)) ? old.step : lastStep;
  return { branch, port, step };
}

function latestOnlineSelection(timeline, dataplane, old) {
  const frames = asArray(timeline && timeline.online_frames);
  if (!frames.length) return defaultSelection(timeline, dataplane, old);
  const latest = frames[frames.length - 1] || {};
  const branch = firstDefined(latest.branch_id, latest.branch, "main.online");
  const step = Number(firstDefined(latest.step, latest.step_index, latest.frame, latest.frame_index, frames.length - 1));
  const fields = collectFields(dataplane, branch);
  const ports = Array.from(new Set(fields.map((f) => fieldPort(f)).filter(Boolean)));
  const port = ports.includes(old.port) ? old.port : (ports[0] || old.port || "");
  return { branch, port, step };
}

function intSetting(value, fallback) {
  const n = Number(value);
  return Number.isFinite(n) ? Math.max(0, Math.trunc(n)) : fallback;
}

function boolSetting(value, fallback = false) {
  if (value === true || value === false) return value;
  if (String(value).toLowerCase() === "true") return true;
  if (String(value).toLowerCase() === "false") return false;
  return fallback;
}

function workflowBranchPolicy(workflow) {
  return (workflow && (workflow.branching_policy || workflow.branch_policy)) || {};
}

function nonEmptyObject(value) {
  return value && typeof value === "object" && !Array.isArray(value) && Object.keys(value).length > 0 ? value : null;
}

function runtimeLaunchConfigFromProfile(profile, workflow) {
  const launch = (profile && (profile.runtime_launch || profile.runtime || profile.launch)) || {};
  const branch = nonEmptyObject(profile && (profile.branching_policy || profile.branch_policy)) || workflowBranchPolicy(workflow);
  const every = intSetting(firstDefined(launch.prediction_every_frames, branch.every_n_frames), 30);
  return {
    online_frames: Math.max(1, intSetting(firstDefined(launch.online_frames, launch.requested_frames, workflow && workflow.solver_policy && workflow.solver_policy.max_steps), 50)),
    prediction_every_frames: every > 0 ? every : 30,
    future_max_iterations: intSetting(firstDefined(launch.future_max_iterations, launch.future_max_steps), 0),
    branch_chunk_iterations: Math.max(1, intSetting(firstDefined(launch.branch_chunk_iterations, launch.branch_chunk), 1)),
    replay_by_platform_clock: boolSetting(firstDefined(launch.replay_by_platform_clock, launch.replay_clock), true),
    external_observation_stream: firstDefined(launch.external_observation_stream, launch.external_input_stream, ""),
    branch_enabled: boolSetting(firstDefined(branch.enabled, launch.branch_enabled), false),
    branch_target_workflow_id: firstDefined(branch.target_workflow_id, launch.branch_target_workflow_id, ""),
    branch_trigger_kind: firstDefined(branch.trigger_kind, "every_n_frames"),
    branch_every_n_frames: Math.max(1, intSetting(firstDefined(branch.every_n_frames, every), every || 30)),
    branch_max_concurrent: Math.max(1, intSetting(firstDefined(branch.max_concurrent_branches, launch.branch_max_concurrent), 1)),
    branch_seed_policy: firstDefined(branch.seed_policy, "latest_checkpoint"),
    branch_cancel_policy: firstDefined(branch.cancel_policy, "never"),
  };
}

function runtimeLaunchPayload(config) {
  const cfg = config || {};
  return {
    online_frames: Math.max(1, intSetting(cfg.online_frames, 50)),
    prediction_every_frames: Math.max(1, intSetting(cfg.prediction_every_frames, cfg.branch_every_n_frames || 30)),
    future_max_iterations: intSetting(cfg.future_max_iterations, 0),
    branch_chunk_iterations: Math.max(1, intSetting(cfg.branch_chunk_iterations, 1)),
    replay_by_platform_clock: boolSetting(cfg.replay_by_platform_clock, true),
    external_observation_stream: firstDefined(cfg.external_observation_stream, ""),
    branch_enabled: boolSetting(cfg.branch_enabled, false),
    branch_target_workflow_id: firstDefined(cfg.branch_target_workflow_id, ""),
    branch_trigger_kind: firstDefined(cfg.branch_trigger_kind, "every_n_frames"),
    branch_every_n_frames: Math.max(1, intSetting(cfg.branch_every_n_frames, cfg.prediction_every_frames || 30)),
    branch_max_concurrent: Math.max(1, intSetting(cfg.branch_max_concurrent, 1)),
    branch_seed_policy: firstDefined(cfg.branch_seed_policy, "latest_checkpoint"),
    branch_cancel_policy: firstDefined(cfg.branch_cancel_policy, "never"),
  };
}

function applyRuntimeLaunchToProfile(profile, config) {
  const next = { ...(profile || {}) };
  const payload = runtimeLaunchPayload(config);
  next.runtime_launch = {
    online_frames: payload.online_frames,
    prediction_every_frames: payload.prediction_every_frames,
    future_max_iterations: payload.future_max_iterations,
    branch_chunk_iterations: payload.branch_chunk_iterations,
    replay_by_platform_clock: payload.replay_by_platform_clock,
    external_observation_stream: payload.external_observation_stream,
  };
  next.branching_policy = {
    enabled: payload.branch_enabled,
    target_workflow_id: payload.branch_target_workflow_id,
    trigger_kind: payload.branch_trigger_kind,
    every_n_frames: payload.branch_every_n_frames,
    seed_policy: payload.branch_seed_policy,
    max_concurrent_branches: payload.branch_max_concurrent,
    cancel_policy: payload.branch_cancel_policy,
  };
  return next;
}

function RuntimeBranchSettingsEditor({ workflows, config, setConfig, compact = false }) {
  const update = (patch) => setConfig((old) => ({ ...old, ...patch }));
  const gridClass = compact ? "editor-grid compact" : "editor-grid";
  const payload = runtimeLaunchPayload(config);
  return (
    <div className="runtime-branch-settings">
      <h3>时间与分支运行设置</h3>
      <p>这些设置保存到 run profile 草稿中，运行时由 RuntimeHost 按当前 workflow/profile 消费。</p>
      <div className={gridClass}>
        <label><span>在线帧数</span><input type="number" min="1" value={config.online_frames} onChange={(event) => update({ online_frames: event.target.value })} /></label>
        <label><span>每 N 帧触发预测</span><input type="number" min="1" value={config.prediction_every_frames} onChange={(event) => update({ prediction_every_frames: event.target.value, branch_every_n_frames: event.target.value })} /></label>
        <label><span>未来预测最大步数</span><input type="number" min="0" value={config.future_max_iterations} onChange={(event) => update({ future_max_iterations: event.target.value })} /></label>
        <label><span>分支 chunk 步数</span><input type="number" min="1" value={config.branch_chunk_iterations} onChange={(event) => update({ branch_chunk_iterations: event.target.value })} /></label>
        <label><span>平台时钟回放</span><select value={String(boolSetting(config.replay_by_platform_clock, true))} onChange={(event) => update({ replay_by_platform_clock: event.target.value === "true" })}><option value="true">启用</option><option value="false">关闭</option></select></label>
        <label><span>外部输入流</span><input value={config.external_observation_stream || ""} onChange={(event) => update({ external_observation_stream: event.target.value })} placeholder="可为空；外部驱动文件/ROS2记录路径" /></label>
        <label><span>预测分支</span><select value={String(boolSetting(config.branch_enabled, false))} onChange={(event) => update({ branch_enabled: event.target.value === "true" })}><option value="true">启用</option><option value="false">关闭</option></select></label>
        <label><span>目标预测 workflow</span><select value={config.branch_target_workflow_id || ""} onChange={(event) => update({ branch_target_workflow_id: event.target.value })}><option value="">未指定</option>{asArray(workflows).map((wf) => <option key={workflowId(wf)} value={workflowId(wf)}>{workflowId(wf)}</option>)}</select></label>
        <label><span>分支触发方式</span><select value={config.branch_trigger_kind || "every_n_frames"} onChange={(event) => update({ branch_trigger_kind: event.target.value })}><option value="every_n_frames">每 N 帧</option><option value="manual">手动</option><option value="event">事件</option></select></label>
        <label><span>最大并发分支</span><input type="number" min="1" value={config.branch_max_concurrent} onChange={(event) => update({ branch_max_concurrent: event.target.value })} /></label>
        <label><span>分支种子</span><select value={config.branch_seed_policy || "latest_checkpoint"} onChange={(event) => update({ branch_seed_policy: event.target.value })}><option value="latest_checkpoint">最新检查点</option><option value="latest_posterior">最新后验</option><option value="current_state">当前状态</option></select></label>
        <label><span>取消策略</span><select value={config.branch_cancel_policy || "never"} onChange={(event) => update({ branch_cancel_policy: event.target.value })}><option value="never">不自动取消</option><option value="replace_oldest">替换最旧分支</option><option value="cancel_stale">取消过期分支</option></select></label>
      </div>
      <div className="runtime-settings-summary">
        <span>在线帧={payload.online_frames}</span>
        <span>预测触发={payload.prediction_every_frames}</span>
        <span>未来步数={payload.future_max_iterations}</span>
        <span>分支 chunk={payload.branch_chunk_iterations}</span>
        <span>分支={payload.branch_enabled ? "启用" : "关闭"}</span>
      </div>
    </div>
  );
}

function RunControl({
  run,
  status,
  objectLoaded,
  objectValidation,
  selectedWorkflowId,
  selectedRunProfileId,
  compileResult,
  preflightResult,
  runtimeLaunchConfig,
  onReload,
  onRunStarted,
}) {
  const [busy, setBusy] = useState("");
  const [lastMsg, setLastMsg] = useState("");
  const validationOk = objectLoaded && (!objectValidation || objectValidation.ok !== false);
  const compiledDir = firstDefined(compileResult && compileResult.compiled_dir, compileResult && compileResult.plan && compileResult.plan.compiled_dir);
  const preflightOk = preflightResult && preflightResult.ok;
  const missing = [
    !validationOk ? "对象包未载入或校验未通过" : "",
    !selectedWorkflowId ? "未选择 workflow" : "",
    !selectedRunProfileId ? "未选择 run profile" : "",
    !compiledDir ? "workflow 尚未编译" : "",
    !preflightOk ? "preflight 未通过" : "",
  ].filter(Boolean);
  const runDisabled = missing.length > 0;

  async function post(url, body) {
    setBusy(url);
    setLastMsg("");
    try {
      const res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body || {}),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || data.ok === false) throw new Error(data.error || data.message || `${res.status} ${res.statusText}`);
      setLastMsg(data.message || data.job_id || "命令已提交");
      if (data.run && (url.endsWith("/start") || (body && body.action === "run")) && onRunStarted) onRunStarted(data.run);
      if (onReload) onReload();
    } catch (err) {
      setLastMsg(err.message || String(err));
    } finally {
      setBusy("");
    }
  }

  return (
    <Panel title="平台运行控制" subtitle="运行入口必须消费当前 workflow、run profile、编译计划和 preflight 结果">
      <div className="control-stack">
        <Button disabled={runDisabled} busy={busy === "/api/runtime/prepare"} tone="primary" onClick={() => post("/api/runtime/prepare", {
          workflow_id: selectedWorkflowId,
          run_profile_id: selectedRunProfileId,
          compiled_dir: compiledDir,
          ...runtimeLaunchPayload(runtimeLaunchConfig),
        })}>
          初始化对象 / 加载模型
        </Button>
        <Button disabled={runDisabled} busy={busy === "/api/runtime/start"} onClick={() => post("/api/runtime/start", {
          workflow_id: selectedWorkflowId,
          run_profile_id: selectedRunProfileId,
          compiled_dir: compiledDir,
          run: true,
          ...runtimeLaunchPayload(runtimeLaunchConfig),
        })}>
          启动运行
        </Button>
        <Button busy={busy === "/api/runtime/stop"} tone="danger" onClick={() => post("/api/runtime/stop", { job_id: status && status.job_id, run })}>
          停止当前运行
        </Button>
      </div>
      <KeyValue rows={[
        ["当前运行包", run || "-"],
        ["workflow", selectedWorkflowId || "-"],
        ["run profile", selectedRunProfileId || "-"],
        ["compiled plan", compiledDir ? shortPath(compiledDir) : "-"],
        ["在线帧 / 预测触发", `${runtimeLaunchPayload(runtimeLaunchConfig).online_frames} / ${runtimeLaunchPayload(runtimeLaunchConfig).prediction_every_frames}`],
        ["未来步数 / 分支 chunk", `${runtimeLaunchPayload(runtimeLaunchConfig).future_max_iterations} / ${runtimeLaunchPayload(runtimeLaunchConfig).branch_chunk_iterations}`],
        ["预测分支", runtimeLaunchPayload(runtimeLaunchConfig).branch_enabled ? `启用 -> ${runtimeLaunchPayload(runtimeLaunchConfig).branch_target_workflow_id || "-"}` : "关闭"],
        ["preflight", preflightOk ? "通过" : (preflightResult ? "未通过" : "未执行")],
        ["运行入口", runDisabled ? missing.join("；") : "可执行"],
        ["WebUI job", status && status.running ? "运行中" : "未运行"],
        ["运行状态", status ? <StatusPill status={status.status || status.state || (status.running ? "running" : "idle")} /> : "-"],
        ["job run", firstDefined(status && status.run_id, "-")],
        ["最近消息", lastMsg || "-"],
      ]} />
    </Panel>
  );
}

function ModelImportPanel({ object, packages, workspace, onReload, setPage }) {
  const [packageDir, setPackageDir] = useState("");
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState("");
  const discovered = asArray(packages && packages.packages);
  const validation = firstDefined(workspace && workspace.object_validation, packages && packages.validation, object && object.validation);
  const diagnostics = diagnosticsOf(validation);

  async function postObject(url, body, okMessage, options = {}) {
    setBusy(true);
    setMessage("");
    try {
      const res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body || {}),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || (data.ok === false && !options.allowFalse)) throw new Error(data.error || data.message || "对象包操作失败");
      setMessage(okMessage(data));
      if (onReload) onReload();
      return data;
    } catch (err) {
      setMessage(err.message || String(err));
      return null;
    } finally {
      setBusy(false);
    }
  }

  function selectPackage(path) {
    if (!path) return;
    postObject("/api/object/select", { package_dir: path }, (data) => {
      if (setPage) setPage("modeler");
      return `已打开对象包：${shortPath(data.object_package)}`;
    });
  }

  function validatePackage() {
    const target = packageDir || objectPackageRoot(workspace, object);
    postObject("/api/object/validate", { package_dir: target }, (data) => {
      const validation = data.validation || {};
      return data.ok ? `对象包校验通过：${shortPath(validation.package_dir)}` : "对象包校验失败，请查看诊断信息";
    }, { allowFalse: true });
  }

  function unloadPackage() {
    postObject("/api/object/unload", {}, () => "对象包已卸载，平台回到空工作空间");
  }

  function rescanPackages() {
    postObject("/api/object/rescan", {}, (data) => `重新扫描完成：发现 ${asArray(data.packages).length} 个对象包`);
  }

  return (
    <Panel title="对象工程打开 / 新建" subtitle="打开已有对象包后进入对象树；新建对象工程会创建标准对象包目录。">
      <KeyValue rows={[
        ["工作空间", shortPath(workspace && workspace.workspace_root)],
        ["当前对象包", shortPath(objectPackageRoot(workspace, object))],
        ["对象", firstDefined(object && object.object_id, object && object.name, "-")],
        ["载入状态", isObjectLoaded(workspace, object) ? "已载入" : "未载入"],
        ["校验状态", validation && validation.ok === false ? "未通过" : (isObjectLoaded(workspace, object) ? "已通过" : "待校验")],
      ]} />
      <div className="import-stack">
        <label className="stack-field">
          <span>已发现对象包</span>
          <select value="" onChange={(e) => selectPackage(e.target.value)}>
            <option value="">选择已有对象包并打开</option>
            {discovered.map((pkg) => (
              <option key={pkg.package_dir} value={pkg.package_dir}>
                {pkg.active ? "当前 · " : ""}{pkg.object_id} · {shortPath(pkg.package_dir)}
              </option>
            ))}
          </select>
        </label>
        <label className="stack-field">
          <span>本地对象包目录</span>
          <input value={packageDir} onChange={(e) => setPackageDir(e.target.value)} placeholder="F:\\code\\...\\flightenv-object-xxx" />
        </label>
        <div className="button-row">
          <Button busy={busy} tone="primary" onClick={() => selectPackage(packageDir)}>打开对象包</Button>
          <Button busy={busy} onClick={() => setPage && setPage("modeler")}>新建对象工程</Button>
          <Button busy={busy} onClick={validatePackage}>校验对象包</Button>
          <Button busy={busy} onClick={rescanPackages}>重新扫描</Button>
          <Button busy={busy} tone="danger" onClick={unloadPackage}>卸载对象</Button>
        </div>
        {message && <small className="inline-message">{message}</small>}
        {diagnostics.length > 0 && (
          <div className="diagnostic-list compact">
            {diagnostics.slice(0, 8).map((item, idx) => (
              <div key={idx} className={cls("diagnostic-item", ["error", "blocking"].includes(item.level || item.severity) && "bad")}>
                <Badge tone={["error", "blocking"].includes(item.level || item.severity) ? "bad" : "neutral"}>{item.level || item.severity || "info"}</Badge>
                <span>{item.message || JSON.stringify(item)}</span>
              </div>
            ))}
          </div>
        )}
      </div>
    </Panel>
  );
}

function DraftLifecyclePanel({ draft, object, reloadAll }) {
  const status = draft || {};
  const drafts = asArray(status.drafts);
  const activeId = firstDefined(status.active_draft_id, object && object.active_draft_id, "");
  const [selectedDraft, setSelectedDraft] = useState(activeId);
  const [editing, setEditing] = useState(false);
  const [busy, setBusy] = useState("");
  const [message, setMessage] = useState("");
  const [publishResult, setPublishResult] = useState(null);

  useEffect(() => setSelectedDraft(activeId), [activeId]);

  async function run(action, body = {}) {
    if (["/api/draft/create", "/api/draft/select", "/api/draft/publish", "/api/draft/discard"].includes(action) && !editing) {
      setMessage("请先点击编辑，再变更对象草稿状态。");
      return;
    }
    setBusy(action);
    setMessage("");
    setPublishResult(null);
    try {
      const data = await apiPost(action, body);
      setMessage(data.message || "操作完成");
      if (action === "/api/draft/publish") setPublishResult(data);
      if (reloadAll) reloadAll();
    } catch (err) {
      setMessage(err.message || String(err));
    } finally {
      setBusy("");
    }
  }

  return (
    <Panel
      title="Phase 8 草稿与发布闭环"
      subtitle="普通编辑只写对象包草稿；发布动作生成版本包、diff 和校验报告，不自动覆盖正式对象包"
      toolbar={<EditModeToolbar editing={editing} setEditing={setEditing} />}
    >
      <div className="metric-grid tight">
        <MetricCard label="当前域" value={firstDefined(status.active_domain, object && object.package_domain, "-")} hint="object_package / object_draft / evidence" tone={activeId ? "amber" : "green"} />
        <MetricCard label="激活草稿" value={activeId || "正式对象包"} hint={shortPath(status.active_package || object && object.package_dir)} />
        <MetricCard label="草稿数" value={fmtNumber(drafts.length, 0)} hint={shortPath(status.draft_root)} tone="cyan" />
        <MetricCard label="发布区" value="release" hint={shortPath(status.release_root)} tone="green" />
      </div>
      <div className="control-stack">
        <label className="stack-field">
          <span>对象草稿</span>
          <select value={selectedDraft || ""} onChange={(event) => setSelectedDraft(event.target.value)}>
            <option value="">正式对象包</option>
            {drafts.map((item) => <option key={item.draft_id} value={item.draft_id}>{item.draft_id}</option>)}
          </select>
        </label>
        {editing && (
          <div className="button-row">
            <Button busy={busy === "/api/draft/create"} tone="primary" onClick={() => run("/api/draft/create", {})}>创建草稿</Button>
            <Button busy={busy === "/api/draft/select"} onClick={() => run("/api/draft/select", { draft_id: selectedDraft })}>激活/切回</Button>
            <Button disabled={!selectedDraft && !activeId} busy={busy === "/api/draft/publish"} onClick={() => run("/api/draft/publish", { draft_id: selectedDraft || activeId })}>发布版本</Button>
            <Button disabled={!selectedDraft && !activeId} busy={busy === "/api/draft/discard"} tone="danger" onClick={() => run("/api/draft/discard", { draft_id: selectedDraft || activeId })}>删除草稿</Button>
          </div>
        )}
        {message && <div className="inline-message">{message}</div>}
      </div>
      <DataTable
        rows={drafts}
        emptyHint="暂无对象包草稿。点击“创建草稿”后，资源、算子、run profile 和 workflow 的编辑都会写入草稿。"
        columns={[
          { key: "draft", title: "draft_id", render: (row) => row.draft_id },
          { key: "status", title: "状态", render: (row) => <Badge tone={row.active ? "live" : "neutral"}>{row.active ? "激活" : row.status}</Badge> },
          { key: "changes", title: "变更", render: (row) => fmtNumber(row.change_count, 0) },
          { key: "updated", title: "更新时间", render: (row) => firstDefined(row.updated_at_utc, row.created_at_utc, "-") },
          { key: "path", title: "路径", render: (row) => shortPath(row.package_dir) },
        ]}
      />
      {publishResult && (
        <div className="phase8-publish-result">
          <KeyValue rows={[
            ["version_id", publishResult.version_id],
            ["release_package", shortPath(publishResult.release_package)],
            ["bundle", shortPath(publishResult.bundle)],
            ["diff_count", fmtNumber(publishResult.diff && publishResult.diff.diff_count, 0)],
          ]} />
        </div>
      )}
    </Panel>
  );
}

function WorkspacePage({ workspace, object, packages, runs, settings, draft, reloadAll, setPage }) {
  const loaded = isObjectLoaded(workspace, object);
  const validation = workspace && workspace.object_validation;
  const diagnostics = diagnosticsOf(validation);
  const discovered = asArray(packages && packages.packages);
  return (
    <div className="page-grid workspace-layout">
      <ModelImportPanel object={object} packages={packages} workspace={workspace} onReload={reloadAll} setPage={setPage} />
      <DraftLifecyclePanel draft={draft} object={object} reloadAll={reloadAll} />
      <Panel title="平台入口状态" subtitle="无对象启动时平台保持空态；载入对象包后才开放对象、算子、workflow 与运行入口">
        <div className="metric-grid">
          <MetricCard label="对象包" value={loaded ? "已载入" : "未载入"} hint={shortPath(objectPackageRoot(workspace, object))} tone={loaded ? "green" : "amber"} />
          <MetricCard label="发现对象包" value={fmtNumber(discovered.length, 0)} hint="workspace scan" tone="cyan" />
          <MetricCard label="历史 run" value={fmtNumber(asArray(runs).length, 0)} hint="兼容 run 索引" />
          <MetricCard label="运行入口" value={loaded && (!validation || validation.ok !== false) ? "可用" : "禁用"} hint="受对象校验控制" tone={loaded ? "green" : "amber"} />
        </div>
        <div className="button-row">
          <Button disabled={!loaded} tone="primary" onClick={() => setPage("object")}>查看对象画像</Button>
          <Button disabled={!loaded} onClick={() => setPage("resources")}>查看资源与模型</Button>
          <Button disabled={!loaded} onClick={() => setPage("operators")}>查看算子库</Button>
          <Button disabled={!loaded} onClick={() => setPage("workflow")}>查看工作流</Button>
        </div>
      </Panel>
      <Panel title="对象包校验" subtitle="校验失败时运行入口保持禁用，错误原因由对象包读取器返回">
        {diagnostics.length ? (
          <DataTable
            rows={diagnostics}
            columns={[
              { key: "level", title: "级别", render: (row) => <Badge tone={["error", "blocking"].includes(row.level || row.severity) ? "bad" : "neutral"}>{row.level || row.severity || "info"}</Badge> },
              { key: "message", title: "说明", render: (row) => row.message || "-" },
            ]}
          />
        ) : <Empty title={loaded ? "校验通过" : "尚未载入对象包"} hint={loaded ? "对象包基础结构满足 Phase 1 入口要求。" : "请选择对象包后再执行校验。"} />}
      </Panel>
      <Panel title="设置来源预览" subtitle="设置按 workspace / object package / UI session 分源显示，后续阶段再接入持久化编辑">
        <SettingsSourceTable settings={settings} compact />
      </Panel>
    </div>
  );
}

function ObjectRequiredPage({ setPage, title = "尚未载入对象包" }) {
  return (
    <Panel title={title} subtitle="平台当前处于空工作空间。请先显式载入并校验对象包。">
      <Empty title="没有对象上下文" hint="对象画像、资源、算子、workflow 与运行入口都依赖对象包声明。" />
      <div className="button-row">
        <Button tone="primary" onClick={() => setPage("workspace")}>前往对象载入</Button>
      </div>
    </Panel>
  );
}

function BranchExplorer({ timeline, runtime, dataplane, selected, onSelect }) {
  const branches = collectBranches(timeline, dataplane);
  const steps = collectBranchSteps(timeline, selected.branch);
  const timelineRows = collectTimelineRows(timeline, runtime);
  const selectedTimelineIndex = timelineRows.findIndex((row) => (
    row.branch_id === selected.branch && Number(row.step) === Number(selected.step)
  ));
  const activeTimelineIndex = selectedTimelineIndex >= 0
    ? selectedTimelineIndex
    : Math.max(0, timelineRows.length - 1);
  const activeTimePoint = timelineRows[activeTimelineIndex] || null;
  const branchById = new Map(branches.map((branch) => [branch.branch_id, branch]));
  const depthOf = (branch) => {
    let depth = 0;
    let parentId = branch.parent_branch_id;
    const seen = new Set([branch.branch_id]);
    while (parentId && branchById.has(parentId) && !seen.has(parentId) && depth < 4) {
      seen.add(parentId);
      parentId = branchById.get(parentId).parent_branch_id;
      depth += 1;
    }
    return depth;
  };
  const selectTimelineIndex = (value) => {
    const row = timelineRows[Number(value)];
    if (!row) return;
    const branch = row.branch_id || selected.branch;
    if (!branch) return;
    onSelect({ ...selected, branch, step: row.step, port: selected.port });
  };

  return (
    <Panel title="分支与时间轴" subtitle="分支树、全局时间滑块和当前分支时间线放在同一操作区">
      <div className="branch-timeline-layout">
        <div className="branch-tree">
          <div className="section-caption">分支树（{branches.length}）</div>
          {branches.map((branch) => (
            <button
              key={branch.branch_id}
              className={cls("branch-tree-row", selected.branch === branch.branch_id && "active")}
              style={{ paddingLeft: `${10 + depthOf(branch) * 18}px` }}
              onClick={() => onSelect({ ...selected, branch: branch.branch_id, port: "", step: "" })}
            >
              <span className="tree-joint" />
              <strong>{branch.branch_id}</strong>
              <span>{branchKindLabel(branch.branch_kind)}</span>
              <small>{displayStatus(branch.status)} · 帧 {fmtNumber(branch.frame_count, 0)} · 步 {fmtNumber(branch.step_count, 0)}</small>
            </button>
          ))}
        </div>

        <div className="timeline-slider-card">
          <div className="section-caption">统一时间轴</div>
          {!timelineRows.length && <Empty hint="当前 run package 暂未写入时间点。" />}
          {!!timelineRows.length && (
            <>
              <input
                className="timeline-range"
                type="range"
                min="0"
                max={timelineRows.length - 1}
                value={activeTimelineIndex}
                onChange={(event) => selectTimelineIndex(event.target.value)}
              />
              <div className="timeline-range-meta">
                <span><strong>{activeTimePoint ? activeTimePoint.kind : "-"}</strong><small>类型</small></span>
                <span><strong>{activeTimePoint ? activeTimePoint.branch_id || "-" : "-"}</strong><small>分支</small></span>
                <span><strong>{activeTimePoint ? activeTimePoint.step : "-"}</strong><small>step</small></span>
                <span><strong>{activeTimePoint ? fmtNumber(activeTimePoint.t, 2) : "-"}</strong><small>t(s)</small></span>
              </div>
              <div className="timeline-ruler">
                <span>{fmtNumber(timelineRows[0].t, 2)}s</span>
                <span>{fmtNumber(timelineRows[timelineRows.length - 1].t, 2)}s</span>
              </div>
            </>
          )}
          <DataTable
            rows={steps}
            emptyHint="当前分支暂未写入时间线记录。"
            rowKey={(row) => `${row.branch_id}-${row.step}`}
            columns={[
              { key: "kind", title: "分支", render: (row) => row.branch_id },
              { key: "step", title: "step", render: (row) => (
                <button className={cls("link-btn", Number(selected.step) === row.step && "active")} onClick={() => onSelect({ ...selected, step: row.step })}>
                  {row.step}
                </button>
              ) },
              { key: "time", title: "t(s)", render: (row) => fmtNumber(row.public_time_s, 2) },
              { key: "h", title: "h(m)", render: (row) => fmtNumber(row.h_m, 1) },
              { key: "qoi", title: "QoI", render: (row) => fmtNumber(row.qoi_count, 0) },
            ]}
          />
        </div>
      </div>
    </Panel>
  );
}

function BranchLedgerPanel({ dataplane, selected }) {
  const step = Number(selected && selected.step);
  const fields = currentStepFields(dataplane, selected || {});
  const qois = asArray(dataplane && dataplane.qois).filter((row) => {
    const sameBranch = !selected.branch || row.branch_id === selected.branch || !row.branch_id;
    const rowStep = Number(firstDefined(row.step, row.step_index, 0));
    const sameStep = !Number.isFinite(step) || rowStep === step;
    return sameBranch && sameStep;
  });
  return (
    <Panel title="当前时刻状态 / QoI 账本" subtitle="按当前 branch/time point 从 data-plane 摘要中读取，不把结果写死到页面">
      <div className="metric-grid tight">
        <MetricCard label="field artifacts" value={fmtNumber(fields.length, 0)} />
        <MetricCard label="QoI records" value={fmtNumber(qois.length, 0)} />
        <MetricCard label="branch" value={selected.branch || "-"} />
        <MetricCard label="step" value={selected.step === "" ? "-" : selected.step} />
      </div>
      <DataTable
        rows={qois.slice(0, 40)}
        emptyHint="当前 branch/time point 暂无 QoI 记录。"
        columns={[
          { key: "port", title: "port", render: (row) => firstDefined(row.port_id, row.contract_id, "-") },
          { key: "summary", title: "summary", render: (row) => firstDefined(row.summary, row.decision, "-") },
          { key: "value", title: "value", render: (row) => fmtNumber(firstDefined(row.mean, row.max, row.min, row.value)) },
          { key: "ref", title: "ref", render: (row) => shortPath(firstDefined(row.ref, row.uri, "")) },
        ]}
      />
    </Panel>
  );
}

function BranchTimelineView({ run, timeline, runtime, dataplane, selected, onSelect, mode, setMode, followLive, setFollowLive, status, reloadAll }) {
  const branches = collectBranches(timeline, dataplane);
  const selectedBranch = branches.find((branch) => branch.branch_id === selected.branch) || branches[0] || {};
  const progress = (runtime && runtime.progress) || (timeline && timeline.progress) || {};
  const onlineProgress = progress.online || {};
  const branchSteps = collectBranchSteps(timeline, selected.branch);
  const onlineFrames = asArray(timeline && timeline.online_frames);
  const [busy, setBusy] = useState("");
  const [message, setMessage] = useState("");

  function selectTimelinePoint(next) {
    if (setFollowLive) setFollowLive(false);
    onSelect(next);
  }

  function backToLive() {
    const next = latestOnlineSelection(timeline, dataplane, selected);
    if (setFollowLive) setFollowLive(true);
    if (setMode) setMode("live");
    onSelect(next);
  }

  async function branchAction(action) {
    if (!selected.branch || !run) return;
    setBusy(action);
    setMessage("");
    try {
      const res = await fetch(`/api/branch/${action}`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          run,
          branch_id: selected.branch,
          job_id: status && status.job_id,
          reason: action === "stop" ? "user requested from BranchTimelineView" : "user requested resume from BranchTimelineView",
        }),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || data.ok === false) throw new Error(data.message || data.error || `${res.status} ${res.statusText}`);
      setMessage(data.message || `${action} recorded`);
      if (reloadAll) reloadAll();
    } catch (err) {
      setMessage(err.message || String(err));
    } finally {
      setBusy("");
    }
  }

  return (
    <div className="branch-view-stack">
      <Panel title="在线主线 / 分支浏览" subtitle="主线、派生分支、live/replay 和分支命令在同一视图里表达">
        <div className="runtime-command-bar">
          <Button tone={mode === "live" ? "primary" : "neutral"} onClick={backToLive}>回到 live</Button>
          <Button tone={mode === "replay" ? "primary" : "neutral"} onClick={() => setMode && setMode("replay")}>replay mode</Button>
          <Badge tone={followLive ? "live" : "neutral"}>{followLive ? "跟随 live" : "浏览选中时间点"}</Badge>
          <Button disabled={!selected.branch} busy={busy === "stop"} tone="danger" onClick={() => branchAction("stop")}>停止分支</Button>
          <Button disabled={!selected.branch} busy={busy === "resume"} onClick={() => branchAction("resume")}>继续分支</Button>
        </div>
        {message && <div className="inline-message">{message}</div>}
        <div className="metric-grid tight">
          <MetricCard label="external frames" value={fmtNumber(firstDefined(onlineProgress.completed_frames, onlineFrames.length), 0)} />
          <MetricCard label="posterior frames" value={fmtNumber(onlineFrames.length, 0)} />
          <MetricCard label="branches" value={fmtNumber(branches.length, 0)} />
          <MetricCard label="selected points" value={fmtNumber(branchSteps.length, 0)} />
        </div>
        <KeyValue rows={[
          ["view mode", modeLabel(mode)],
          ["selected branch", firstDefined(selectedBranch.branch_id, "-")],
          ["parent", firstDefined(selectedBranch.parent_branch_id, "-")],
          ["trigger", shortPath(firstDefined(selectedBranch.trigger, "-"))],
          ["start", firstDefined(selectedBranch.start_time_s, "-")],
          ["status", selectedBranch.status ? <StatusPill status={selectedBranch.status} /> : "-"],
          ["progress", selectedBranch.progress === "" ? "-" : `${fmtNumber(selectedBranch.progress, 1)}%`],
          ["stop reason", firstDefined(selectedBranch.stop_reason, selectedBranch.last_action, "-")],
        ]} />
      </Panel>
      <BranchExplorer timeline={timeline} runtime={runtime} dataplane={dataplane} selected={selected} onSelect={selectTimelinePoint} />
      <BranchLedgerPanel dataplane={dataplane} selected={selected} />
    </div>
  );
}

function seriesKeys(rows) {
  const skip = new Set(["branch_id", "step", "frame", "qoi_count"]);
  const counts = new Map();
  asArray(rows).forEach((row) => {
    Object.entries(row || {}).forEach(([key, value]) => {
      if (skip.has(key)) return;
      const n = Number(value);
      if (Number.isFinite(n)) counts.set(key, (counts.get(key) || 0) + 1);
    });
  });
  return Array.from(counts.entries())
    .filter(([, count]) => count >= 2)
    .sort((a, b) => b[1] - a[1])
    .slice(0, 4)
    .map(([key]) => key);
}

function MiniCurve({ rows, valueKey }) {
  const points = rows
    .map((row) => ({ x: Number(row.step), y: Number(row[valueKey]) }))
    .filter((p) => Number.isFinite(p.x) && Number.isFinite(p.y));
  if (points.length < 2) return <div className="curve-empty">暂无足够数据</div>;
  const minX = Math.min(...points.map((p) => p.x));
  const maxX = Math.max(...points.map((p) => p.x));
  const minY = Math.min(...points.map((p) => p.y));
  const maxY = Math.max(...points.map((p) => p.y));
  const sx = (x) => maxX === minX ? 8 : 8 + ((x - minX) / (maxX - minX)) * 224;
  const sy = (y) => maxY === minY ? 44 : 82 - ((y - minY) / (maxY - minY)) * 72;
  const d = points.map((p, i) => `${i ? "L" : "M"}${sx(p.x).toFixed(1)},${sy(p.y).toFixed(1)}`).join(" ");
  return (
    <svg className="mini-curve" viewBox="0 0 240 92" role="img" aria-label={valueKey}>
      <path className="curve-grid" d="M8 10H232M8 46H232M8 82H232" />
      <path className="curve-line" d={d} />
      <text x="8" y="16">{fmtNumber(maxY, 2)}</text>
      <text x="8" y="88">{fmtNumber(minY, 2)}</text>
    </svg>
  );
}

function BranchCurvePanel({ timeline, selected }) {
  const steps = collectBranchSteps(timeline, selected.branch);
  const keys = seriesKeys(steps);
  return (
    <Panel title="分支实时曲线" subtitle="按当前分支 timeline 动态绘制，不写死对象指标">
      {!steps.length && <Empty hint="当前分支暂未写入曲线数据。" />}
      {!!steps.length && !keys.length && <Empty hint="当前 timeline 没有可连续绘制的数值列。" />}
      <div className="curve-grid-panel">
        {keys.map((key) => (
          <div className="curve-card" key={key}>
            <strong>{key}</strong>
            <MiniCurve rows={steps} valueKey={key} />
          </div>
        ))}
      </div>
    </Panel>
  );
}

function RuntimeTimelineStrip({ timeline, runtime, selected, onSelect }) {
  const online = asArray(timeline && timeline.online_frames).map((frame, index) => ({
    kind: "online",
    branch_id: firstDefined(frame.branch_id, "online"),
    step: Number(firstDefined(frame.step, frame.step_index, frame.frame_index, index)),
    t: Number(firstDefined(frame.public_time_s, frame.t, frame.time_s, index)),
    label: `在线 ${firstDefined(frame.step, frame.frame_index, index)}`,
  }));
  const branch = asArray(timeline && timeline.branch_steps).map((step, index) => ({
    kind: "branch",
    branch_id: firstDefined(step.branch_id, ""),
    step: Number(firstDefined(step.step, step.step_index, index)),
    t: Number(firstDefined(step.public_time_s, step.t_s, step.time_s, index)),
    label: `${firstDefined(step.branch_id, "branch")} · ${firstDefined(step.step, step.step_index, index)}`,
  }));
  const events = asArray(runtime && runtime.runtime_events).slice(-80).map((event, index) => ({
    kind: "event",
    branch_id: firstDefined(event.branch_id, ""),
    step: Number(firstDefined(event.step, event.step_index, index)),
    t: Number(firstDefined(event.public_time_s, event.time_s, index)),
    label: firstDefined(event.kind, event.event_kind, "event"),
  }));
  const rows = [...online, ...branch, ...events].filter((row) => Number.isFinite(row.t)).sort((a, b) => a.t - b.t);
  const minT = rows.length ? Math.min(...rows.map((r) => r.t)) : 0;
  const maxT = rows.length ? Math.max(...rows.map((r) => r.t)) : 1;

  return (
    <Panel title="实时时间轴" subtitle="在线帧、分支步与 runtime 事件统一呈现">
      {!rows.length && <Empty hint="当前 run 尚未写入 timeline 或 runtime event。" />}
      {!!rows.length && (
        <div className="timeline-strip">
          <div className="timeline-axis" />
          {rows.map((row, index) => {
            const left = maxT === minT ? 0 : ((row.t - minT) / (maxT - minT)) * 100;
            return (
              <button
                key={`${row.kind}-${row.branch_id}-${row.step}-${index}`}
                className={cls("timeline-dot", `timeline-${row.kind}`, selected.branch === row.branch_id && Number(selected.step) === row.step && "active")}
                style={{ left: `${left}%` }}
                title={`${row.label} / t=${fmtNumber(row.t, 2)}s`}
                onClick={() => row.branch_id && onSelect({ ...selected, branch: row.branch_id, step: row.step })}
              />
            );
          })}
        </div>
      )}
    </Panel>
  );
}

function currentStepFields(dataplane, selected) {
  return resolveFieldBinding(dataplane, selected || {}).fieldsAtStep;
}

function FieldViewer({ run, selected, title = "三维场云图" }) {
  return (
    <Panel title={title} subtitle="读取 field artifact + runtime snapshot / VTK 布局">
      <Field3D run={run} branch={selected.branch} port={selected.port} step={selected.step} height={520} />
    </Panel>
  );
}

function FieldGallery({ run, dataplane, selected, onSelect, title = "真实场云图" }) {
  const fields = currentStepFields(dataplane, selected);
  const activeField = fields.find((field) => fieldPort(field) === selected.port) || fields[0];
  const activeSelection = activeField
    ? { ...selected, port: fieldPort(activeField), step: fieldStep(activeField) }
    : selected;
  return (
    <Panel title={title} subtitle="输出场表选择当前 artifact，三维区只渲染表中选中的场">
      <div className="field-gallery">
        {!!fields.length && (
          <div className="field-output-summary">
            <DataTable
              rows={fields}
              emptyHint="当前选择没有可视化场 artifact。"
              columns={[
                { key: "port", title: "输出场", render: (row) => (
                  <button className={cls("link-btn", fieldPort(row) === activeSelection.port && "active")} onClick={() => onSelect({ ...selected, port: fieldPort(row), step: fieldStep(row) })}>
                    {fieldDisplayName(row)}
                  </button>
                ) },
                { key: "step", title: "step", render: (row) => firstDefined(row.step, row.step_index, 0) },
                { key: "stats", title: "统计", render: (row) => {
                  const stats = fieldStats(row);
                  return `min ${fmtNumber(stats.min)} / max ${fmtNumber(stats.max)} / mean ${fmtNumber(stats.mean)}`;
                } },
              ]}
            />
          </div>
        )}
        {!fields.length && <Empty hint="当前分支/步没有可渲染场 artifact。" />}
        {!!fields.length && <Field3D run={run} branch={activeSelection.branch} port={activeSelection.port} step={activeSelection.step} height={500} />}
      </div>
    </Panel>
  );
}

function resolveFieldBinding(dataplane, selected) {
  let branch = firstDefined(selected && selected.branch, "");
  let fields = collectFields(dataplane, branch);
  // branch 为空（如刚切到历史 run，selected 尚未带分支）时，collectFields("") 会跨所有分支取场，
  // 导致 branch 一直留空、最终向 /api/field 发出无 branch 的歧义请求并被 abort —— 历史回放云图卡在“读取中”的根因。
  // 这里强制回退到第一个带场的具体分支。
  if (!branch || !fields.length) {
    const firstField = collectFields(dataplane, "")[0];
    const fallbackBranch = firstDefined(firstField && firstField.branch_id, firstField && firstField.branch, branch);
    if (fallbackBranch && fallbackBranch !== branch) {
      branch = fallbackBranch;
      fields = collectFields(dataplane, branch);
    }
  }

  const ports = Array.from(new Set(fields.map((field) => fieldPort(field)).filter(Boolean)));
  const selectedPort = ports.includes(selected && selected.port) ? selected.port : (ports[0] || "");
  const portFields = fields.filter((field) => !selectedPort || fieldPort(field) === selectedPort);
  const availableSteps = Array.from(new Set(portFields.map(fieldStep).filter(Number.isFinite))).sort((a, b) => a - b);
  const requestedStep = selected && selected.step !== "" ? Number(selected.step) : NaN;
  let resolvedStep = Number.isFinite(requestedStep) ? requestedStep : (availableSteps.length ? availableSteps[availableSteps.length - 1] : NaN);
  let activeField = portFields.find((field) => fieldStep(field) === resolvedStep);

  if (!activeField && portFields.length) {
    const previous = portFields
      .filter((field) => Number.isFinite(requestedStep) && fieldStep(field) <= requestedStep)
      .sort((a, b) => fieldStep(b) - fieldStep(a))[0];
    activeField = previous || portFields
      .slice()
      .sort((a, b) => {
        if (!Number.isFinite(requestedStep)) return fieldStep(b) - fieldStep(a);
        return Math.abs(fieldStep(a) - requestedStep) - Math.abs(fieldStep(b) - requestedStep);
      })[0];
    resolvedStep = activeField ? fieldStep(activeField) : resolvedStep;
  }

  // 渲染分支永远取自被选中的 field 自身，绝不留空 —— 保证 Field3D 拿到具体 branch。
  branch = firstDefined(activeField && (activeField.branch_id || activeField.branch), branch, "");

  const fieldsAtStep = Number.isFinite(resolvedStep)
    ? fields.filter((field) => fieldStep(field) === resolvedStep)
    : [];

  return {
    branch,
    fields,
    fieldsAtStep,
    ports,
    selectedPort,
    availableSteps,
    requestedStep,
    resolvedStep,
    activeField,
    activeSelection: {
      ...(selected || {}),
      branch,
      port: activeField ? fieldPort(activeField) : selectedPort,
      step: Number.isFinite(resolvedStep) ? resolvedStep : "",
    },
    exact: Number.isFinite(requestedStep) && Number.isFinite(resolvedStep) && requestedStep === resolvedStep,
  };
}

function FieldGalleryV2({ run, dataplane, selected, onSelect, title = "真实场云图" }) {
  const binding = resolveFieldBinding(dataplane, selected || {});
  const fields = binding.fieldsAtStep;
  const activeField = binding.activeField;
  const activeSelection = binding.activeSelection;
  const requestedLabel = Number.isFinite(binding.requestedStep) ? binding.requestedStep : "-";
  const resolvedLabel = Number.isFinite(binding.resolvedStep) ? binding.resolvedStep : "-";
  return (
    <Panel className="field-gallery-panel" bodyClassName="field-gallery-body" title={title} subtitle="三维场跟随顶部「运行范围」的分支 / 端口 / step；点击下方输出场表可切换当前场">
      <div className="field-gallery field-gallery-v2">
        {!fields.length && <Empty hint="该分支没有可渲染的场 artifact；请在顶部「运行范围」切换到带场的分支（实时预测 / 预测分支），或先运行 workflow。" />}
        {!!fields.length && (
          <Field3D
            key={run}
            run={run}
            branch={activeSelection.branch}
            port={activeSelection.port}
            step={activeSelection.step}
            artifactId={activeField && (activeField.artifact_id || activeField.artifact_uri)}
            height="100%"
          />
        )}
        <div className="field-binding-summary">
          <span>显示分支：{binding.branch || "-"}</span>
          <span>端口：{activeSelection.port || "-"}</span>
          <span>查看步：{requestedLabel}</span>
          <span>渲染步：{resolvedLabel}</span>
          <span>匹配：{binding.exact ? "精确" : "最近可用"}</span>
          <span>节点：{activeField ? fmtNumber(activeField.node_count, 0) : "-"}</span>
        </div>
        {!!fields.length && (
          <div className="field-output-summary">
            <DataTable
              rows={fields}
              emptyHint="当前选择没有可视化场 artifact。"
              columns={[
                { key: "port", title: "输出场", render: (row) => (
                  <button className={cls("link-btn", fieldPort(row) === activeSelection.port && "active")} onClick={() => onSelect({ ...(selected || {}), branch: binding.branch, port: fieldPort(row), step: fieldStep(row) })}>
                    {fieldDisplayName(row)}
                  </button>
                ) },
                { key: "step", title: "step", render: (row) => firstDefined(row.step, row.step_index, 0) },
                { key: "nodes", title: "节点", render: (row) => fmtNumber(row.node_count, 0) },
                { key: "stats", title: "统计", render: (row) => {
                  const stats = fieldStats(row);
                  return `min ${fmtNumber(stats.min)} / max ${fmtNumber(stats.max)} / mean ${fmtNumber(stats.mean)}`;
                } },
              ]}
            />
          </div>
        )}
      </div>
    </Panel>
  );
}

function SchedulerMiniPanel({ runtime, timeline, selected }) {
  const clock = (runtime && runtime.clock) || {};
  const scheduler = (runtime && runtime.scheduler) || {};
  const blocking = (runtime && runtime.blocking) || {};
  const progress = (runtime && runtime.progress) || (timeline && timeline.progress) || {};
  const workers = asArray(runtime && runtime.workers);
  const events = asArray(runtime && runtime.runtime_events).slice(-8).reverse();
  const branches = collectBranches(timeline, {});
  const lanes = [
    ["统一时钟", `tick ${fmtNumber(firstDefined(clock.tick_index, progress.tick_index), 0)} / ${fmtTime(firstDefined(clock.run_time_s, clock.source_time_s, progress.public_time_s))}`],
    ["在线主线", `${fmtNumber(firstDefined(progress.online && progress.online.completed_frames, progress.online_frame_count, asArray(timeline && timeline.online_frames).length), 0)} 帧`],
    ["预测分支", `${fmtNumber(branches.length, 0)} 分支 / 当前 ${selected && selected.branch ? selected.branch : "-"}`],
    ["调度阶段", firstDefined(scheduler.stage, progress.stage, blocking.current_stage, "-")],
  ];
  return (
    <Panel title="统一时钟与调度逻辑" subtitle="展示平台时钟、到期任务、分支 worker 和最近调度事件，判断当前是否卡在输入、初始化或后台分支">
      <div className="scheduler-mini">
        <div className="scheduler-stage-strip">
          {lanes.map(([label, value]) => (
            <div key={label}>
              <span>{label}</span>
              <strong>{value}</strong>
            </div>
          ))}
        </div>
        <div className="scheduler-split">
          <DataTable
            rows={workers.slice(0, 8)}
            emptyHint="当前 run 没有 worker 记录；若正在运行，应检查 RuntimeHost 是否写入 runtime snapshot。"
            columns={[
              { key: "branch", title: "worker / branch", render: (row) => firstDefined(row.branch_id, row.worker_id, "-") },
              { key: "status", title: "状态", render: (row) => <StatusPill status={firstDefined(row.status, "unknown")} /> },
              { key: "step", title: "step", render: (row) => firstDefined(row.step, row.step_index, "-") },
              { key: "progress", title: "进度", render: (row) => firstDefined(row.progress, row.progress_percent, "-") },
            ]}
          />
          <div className="scheduler-event-list">
            {!events.length && <Empty hint="暂无 runtime event。" />}
            {events.map((event, index) => (
              <div key={`${firstDefined(event.event_id, event.kind, index)}-${index}`}>
                <strong>{firstDefined(event.kind, event.event_kind, "event")}</strong>
                <span>{firstDefined(event.branch_id, "-")} / step {firstDefined(event.step, event.step_index, "-")} / {fmtTime(firstDefined(event.public_time_s, event.time_s))}</span>
              </div>
            ))}
          </div>
        </div>
      </div>
    </Panel>
  );
}

function OverviewPage({ object, runs, timeline, dataplane, runtime }) {
  const summary = object || {};
  const latest = asArray(runs).slice(0, 6);
  const branches = collectBranches(timeline, dataplane);
  return (
    <div className="page-grid overview-layout">
      <Panel title="平台总览" subtitle="对象包是真源，平台只负责加载、编排、运行与证据记录">
        <div className="metric-grid">
          <MetricCard label="对象" value={firstDefined(summary.object_id, summary.name, "-")} hint={summary.object_type || "object package"} />
          <MetricCard label="算子" value={fmtNumber(asArray(summary.operators).length, 0)} hint="operator specs" tone="cyan" />
          <MetricCard label="资源" value={fmtNumber(asArray(summary.resources).length, 0)} hint="resources" tone="amber" />
          <MetricCard label="工作流" value={fmtNumber(asArray(summary.workflows).length, 0)} hint="workflow profiles" tone="green" />
        </div>
        <KeyValue rows={[
          ["对象包", shortPath(summary.package_dir)],
          ["当前运行", firstDefined(timeline && timeline.run, "-")],
          ["运行状态", runtime && runtime.status ? <StatusPill status={runtime.status} /> : "-"],
          ["数据平面", `${fmtNumber(dataplane && dataplane.field_count, 0)} fields / ${fmtNumber(dataplane && dataplane.qoi_count, 0)} QoI`],
        ]} />
      </Panel>

      <Panel title="运行态摘要" subtitle="对齐 Qt 平台运行页的主状态区">
        <div className="metric-grid tight">
          <MetricCard label="分支数" value={fmtNumber(branches.length, 0)} />
          <MetricCard label="在线帧" value={fmtNumber((timeline && timeline.summary && timeline.summary.online_frame_count) || asArray(timeline && timeline.online_frames).length, 0)} />
          <MetricCard label="预测步" value={fmtNumber(asArray(timeline && timeline.branch_steps).length, 0)} />
          <MetricCard label="事件" value={fmtNumber(asArray(runtime && runtime.runtime_events).length || asArray(timeline && timeline.runtime_events).length, 0)} />
        </div>
        <DataTable
          rows={branches}
          columns={[
            { key: "branch", title: "分支", render: (row) => row.branch_id },
            { key: "kind", title: "类型", render: (row) => branchKindLabel(row.branch_kind) },
            { key: "status", title: "状态", render: (row) => <StatusPill status={row.status} /> },
            { key: "steps", title: "步数", render: (row) => fmtNumber(row.step_count, 0) },
          ]}
        />
      </Panel>

      <Panel title="最近运行包" subtitle="从本地 run package 自动发现">
        <DataTable
          rows={latest}
          columns={[
            { key: "run", title: "run", render: (row) => row.run },
            { key: "status", title: "状态", render: (row) => <StatusPill status={row.status || row.summary_status} /> },
            { key: "workflow", title: "workflow", render: (row) => firstDefined(row.workflow_id, row.workflow, "-") },
            { key: "dir", title: "目录", render: (row) => shortPath(row.dir || row.path) },
          ]}
        />
      </Panel>
    </div>
  );
}

function OnlinePage(props) {
  const { run, runs, timeline, dataplane, runtime, status, selected, setSelected, setRun, reloadAll, objectLoaded, objectValidation, mode, setMode, followLive, setFollowLive } = props;
  const progress = (runtime && runtime.progress) || (timeline && timeline.progress) || {};
  return (
    <div className="workbench-layout online-workbench-layout">
      <div className="left-rail-panels online-left-rail">
        <ModeBanner mode={mode} status={status} run={run} />
        <RunPicker runs={runs} value={run} onChange={setRun} />
        <Panel title="观察模式" subtitle="初始化和启动在 Runtime 控制中心完成；本页只读取当前运行包与实时进度">
          <KeyValue rows={[
            ["对象状态", objectLoaded && (!objectValidation || objectValidation.ok !== false) ? "已载入" : "未就绪"],
            ["当前 run", run || "-"],
            ["页面模式", modeLabel(mode)],
            ["切换说明", "拖动时间轴进入 replay，点击回到 live 后自动跟随最新在线帧"],
          ]} />
        </Panel>
        <Panel title="实时输入" subtitle="外部传感器 / 数据库回放 / 文件队列">
          <KeyValue rows={[
            ["数据源", firstDefined(progress.source, progress.input_source, "-")],
            ["最新状态", firstDefined(progress.latest_state, runtime && runtime.status, "-")],
            ["当前时刻", fmtTime(firstDefined(progress.public_time_s, progress.time_s))],
            ["在线帧数", fmtNumber(firstDefined(progress.online_frame_count, asArray(timeline && timeline.online_frames).length), 0)],
          ]} />
        </Panel>
      </div>

      <div className="center-panels online-center-panels">
        <BranchTimelineView
          run={run}
          timeline={timeline}
          runtime={runtime}
          dataplane={dataplane}
          selected={selected}
          onSelect={setSelected}
          mode={mode}
          setMode={setMode}
          followLive={followLive}
          setFollowLive={setFollowLive}
          status={status}
          reloadAll={reloadAll}
        />
        <SchedulerMiniPanel runtime={runtime} timeline={timeline} selected={selected} />
        <BranchCurvePanel timeline={timeline} selected={selected} />
      </div>

      <div className="viewer-pane online-viewer-pane">
        <FieldGalleryV2 run={run} dataplane={dataplane} selected={selected} onSelect={setSelected} title="真实场云图" />
      </div>
    </div>
  );
}

function RunScopeBar({ run, runs, setRun, timeline, dataplane, runtime, selected, setSelected, mode, setMode, followLive, setFollowLive, status }) {
  const branches = collectBranches(timeline, dataplane);
  const runActive = isActiveRun(run, runs, runtime, status);
  const branchId = selected.branch || (branches[0] && branches[0].branch_id) || "";
  const stepValues = branchStepValues(timeline, dataplane, branchId);
  const minStep = stepValues.length ? Math.min(...stepValues) : 0;
  const maxStep = stepValues.length ? Math.max(...stepValues) : 0;
  const currentStep = Number.isFinite(Number(selected.step)) ? Number(selected.step) : maxStep;
  // 场端口选项来自“解析后的场分支”，与场云图渲染同一套逻辑，保证顶部 scope 与云图永远一致。
  const fieldBinding = resolveFieldBinding(dataplane, selected);
  const portOptions = fieldBinding.ports;
  const activePort = fieldBinding.selectedPort || "";
  const fieldBranchDiffers = Boolean(fieldBinding.branch && branchId && fieldBinding.branch !== branchId);

  function selectPort(nextPort) {
    setSelected({ ...selected, branch: branchId, port: nextPort });
  }

  function selectBranch(nextBranch) {
    const branchSteps = branchStepValues(timeline, dataplane, nextBranch);
    const last = branchSteps.length ? branchSteps[branchSteps.length - 1] : selected.step;
    setFollowLive(false);
    setSelected({
      ...selected,
      branch: nextBranch,
      step: Number(firstDefined(last, selected.step || 0)),
    });
  }

  function selectStep(nextStep) {
    setFollowLive(false);
    setSelected({ ...selected, branch: branchId, step: Number(nextStep) });
  }

  function backToLive() {
    if (!runActive) return;
    setFollowLive(true);
    setMode("live");
    const next = latestOnlineSelection(timeline, dataplane, selected);
    setSelected(next);
  }

  return (
    <Panel className="run-scope-panel" title="运行范围" subtitle="run × 分支 × step 统一上下文，检视器各 tab 共用">
      <div className="run-scope-grid">
        <label>
          <span>run</span>
          <select value={run || ""} onChange={(event) => setRun(event.target.value)}>
            {!asArray(runs).length && <option value="">无可用运行包</option>}
            {asArray(runs).map((item) => (
              <option key={item.run} value={item.run}>{item.run}</option>
            ))}
          </select>
        </label>
        <label>
          <span>分支</span>
          <select value={branchId} onChange={(event) => selectBranch(event.target.value)}>
            <option value="">未选择</option>
            {branches.map((branch) => (
              <option key={branch.branch_id} value={branch.branch_id}>
                {branch.branch_id} · {branchKindLabel(branch.branch_kind)}
              </option>
            ))}
          </select>
        </label>
        <label>
          <span>场端口</span>
          <select value={activePort} onChange={(event) => selectPort(event.target.value)}>
            {!portOptions.length && <option value="">无可用场</option>}
            {portOptions.map((port) => (
              <option key={port} value={port}>{portDisplay(port)}</option>
            ))}
          </select>
        </label>
        <label>
          <span>step · {stepValues.length ? currentStep : "-"}{stepValues.length ? ` / ${maxStep}` : ""}</span>
          <input
            type="range"
            min={minStep}
            max={maxStep}
            value={Math.min(Math.max(currentStep, minStep), maxStep)}
            disabled={!stepValues.length}
            onChange={(event) => selectStep(event.target.value)}
          />
        </label>
        <div className="run-scope-actions">
          <Badge tone={runActive && followLive ? "live" : "neutral"}>
            {runScopeLabel(run, runs, runtime, status, followLive)}
          </Badge>
          <Badge tone="neutral">{fmtNumber(dataplane && dataplane.field_count, 0)} 场 / {fmtNumber(dataplane && dataplane.qoi_count, 0)} QoI</Badge>
          <Button disabled={!runActive} onClick={backToLive}>跟随最新帧</Button>
        </div>
      </div>
      {fieldBranchDiffers && (
        <p className="run-scope-note">
          分支「{branchId}」无独立场 artifact，场云图显示其后验场分支「{fieldBinding.branch}」。
        </p>
      )}
    </Panel>
  );
}

function RuntimeInspectorTab({ runtime, timeline }) {
  const events = asArray(runtime && (runtime.events || runtime.runtime_events)).slice(-120).reverse();
  const sessions = asArray(runtime && runtime.adapter_sessions);
  const clock = (runtime && runtime.clock) || {};
  const scheduler = (runtime && runtime.scheduler) || {};
  const workers = asArray(runtime && runtime.workers);
  return (
    <div className="inspector-tab-grid">
      <Panel title="Runtime Host 状态" subtitle="只读检视：adapter 生命周期、统一时钟、scheduler、worker，不提供运行控制">
        <div className="metric-grid tight">
          <MetricCard label="tick" value={fmtNumber(clock.tick_index, 0)} />
          <MetricCard label="time" value={fmtTime(firstDefined(clock.source_time_s, clock.run_time_s, runtime && runtime.public_time_s))} />
          <MetricCard label="events" value={fmtNumber(events.length, 0)} />
          <MetricCard label="workers" value={fmtNumber(workers.length, 0)} />
        </div>
        <KeyValue rows={[
          ["runtime", runtime && runtime.status ? <StatusPill status={runtime.status} /> : "-"],
          ["backend", firstDefined(runtime && runtime.backend, runtime && runtime.host && runtime.host.execution_backend, "-")],
          ["scheduler", firstDefined(scheduler.stage, scheduler.status, "-")],
          ["timeline", `${fmtNumber(asArray(timeline && timeline.online_frames).length, 0)} online / ${fmtNumber(asArray(timeline && timeline.branch_steps).length, 0)} branch steps`],
        ]} />
      </Panel>
      <Panel title="Adapter sessions" subtitle="每个算子后端实例的生命周期快照">
        <DataTable
          rows={sessions}
          emptyHint="当前 run 没有 adapter session 记录。"
          columns={[
            { key: "operator", title: "算子", render: (row) => firstDefined(row.operator_id, row.node_id, "-") },
            { key: "backend", title: "后端", render: (row) => firstDefined(row.backend, row.backend_kind, "-") },
            { key: "status", title: "状态", render: (row) => <StatusPill status={row.status} /> },
            { key: "duration", title: "耗时", render: (row) => fmtNumber(firstDefined(row.duration_ms, row.elapsed_ms), 1) + " ms" },
          ]}
        />
      </Panel>
      <Panel title="Runtime events" subtitle="调度、触发、失败与证据记录">
        <DataTable
          rows={events}
          emptyHint="当前 run 没有 runtime event。"
          columns={[
            { key: "time", title: "time", render: (row) => fmtTime(firstDefined(row.time_s, row.public_time_s, row.source_time_s)) },
            { key: "kind", title: "kind", render: (row) => firstDefined(row.kind, row.event_kind, row.type, "-") },
            { key: "target", title: "target", render: (row) => shortPath(firstDefined(row.node_id, row.branch_id, row.port_id, "-")) },
            { key: "message", title: "message", render: (row) => firstDefined(row.message, row.summary, row.status, "-") },
          ]}
        />
      </Panel>
    </div>
  );
}

function EvidenceInspectorTab({ run, evidence, timeline, runtime, dataplane }) {
  const snapshotRows = asArray(evidence && evidence.snapshot_files);
  const traceTargets = asArray(evidence && evidence.trace_targets);
  const eventRows = asArray(evidence && evidence.runtime_events);
  return (
    <div className="inspector-tab-grid">
      <Panel title="Evidence 摘要" subtitle="历史和 live run 都按 evidence 解释，不在这里启动运行">
        <KeyValue rows={[
          ["run", run || "-"],
          ["workflow", firstDefined(evidence && evidence.runtime && evidence.runtime.workflow_id, timeline && timeline.workflow_id, "-")],
          ["runtime", firstDefined(runtime && runtime.status, "-")],
          ["backend", firstDefined(runtime && runtime.backend, "-")],
          ["events", fmtNumber(eventRows.length, 0)],
          ["artifacts", `${fmtNumber(dataplane && dataplane.field_count, 0)} fields / ${fmtNumber(dataplane && dataplane.qoi_count, 0)} QoI`],
        ]} />
      </Panel>
      <Panel title="Snapshot 清单" subtitle="workflow / resource / model / operator / runtime evidence 文件">
        <DataTable
          rows={snapshotRows}
          emptyHint="当前 run 没有可读 snapshot 文件。"
          columns={[
            { key: "name", title: "file", render: (row) => row.name },
            { key: "exists", title: "status", render: (row) => <Badge tone={row.exists ? "good" : "bad"}>{row.exists ? "存在" : "缺失"}</Badge> },
            { key: "records", title: "records", render: (row) => fmtNumber(row.record_count, 0) },
            { key: "path", title: "path", render: (row) => shortPath(row.path) },
          ]}
        />
      </Panel>
      <Panel title="Trace 追溯" subtitle="artifact -> producer operator -> workflow node -> resource lock -> runtime event">
        <DataTable
          rows={traceTargets.slice(0, 180)}
          emptyHint="当前 run 没有 trace target。"
          columns={[
            { key: "artifact", title: "artifact", render: (row) => shortPath(firstDefined(row.artifact_id, row.artifact_uri, "-")) },
            { key: "branch", title: "branch", render: (row) => firstDefined(row.branch_id, "-") },
            { key: "step", title: "step", render: (row) => firstDefined(row.step, "-") },
            { key: "producer", title: "producer", render: (row) => shortPath(firstDefined(row.producer_operator, row.operator_id, "-")) },
          ]}
        />
      </Panel>
    </div>
  );
}

function BranchTree({ branches, selected, onSelect }) {
  const branchById = new Map(asArray(branches).map((b) => [b.branch_id, b]));
  const depthOf = (branch) => {
    let depth = 0;
    let parentId = branch.parent_branch_id;
    const seen = new Set([branch.branch_id]);
    while (parentId && branchById.has(parentId) && !seen.has(parentId) && depth < 4) {
      seen.add(parentId);
      parentId = branchById.get(parentId).parent_branch_id;
      depth += 1;
    }
    return depth;
  };
  return (
    <div className="branch-tree">
      {!asArray(branches).length && <Empty hint="当前 run 暂无分支。" />}
      {asArray(branches).map((branch) => (
        <button
          key={branch.branch_id}
          className={cls("branch-tree-row", selected.branch === branch.branch_id && "active")}
          style={{ paddingLeft: `${10 + depthOf(branch) * 18}px` }}
          onClick={() => onSelect({ ...selected, branch: branch.branch_id, port: "", step: "" })}
        >
          <span className="tree-joint" />
          <strong>{branch.branch_id}</strong>
          <span>{branchKindLabel(branch.branch_kind)}</span>
          <small>{displayStatus(branch.status)} · 帧 {fmtNumber(branch.frame_count, 0)} · 步 {fmtNumber(branch.step_count, 0)}</small>
        </button>
      ))}
    </div>
  );
}

// 单分支的步序列（去重升序）—— 播放/进度条只在当前分支内推进，不同分支互不串。
function branchStepValues(timeline, dataplane, branchId) {
  return Array.from(new Set(
    collectBranchSteps(timeline, branchId)
      .map((row) => Number(firstDefined(row.step, row.step_index, row.frame, 0)))
      .concat(collectFields(dataplane, branchId).map((field) => fieldStep(field)))
      .filter(Number.isFinite),
  )).sort((a, b) => a - b);
}

function PlaybackBar({ timeline, dataplane, selected, onSeek, playing, onTogglePlay, speed, onSpeed }) {
  const steps = branchStepValues(timeline, dataplane, selected.branch);
  const n = steps.length;
  const rawIdx = steps.indexOf(Number(selected.step));
  const curIdx = rawIdx >= 0 ? rawIdx : Math.max(0, n - 1);
  const curStep = n ? steps[Math.min(curIdx, n - 1)] : null;
  const seekIdx = (idx) => { if (!n) return; onSeek(steps[Math.max(0, Math.min(n - 1, idx))]); };
  const speeds = [0.5, 1, 2, 4];
  return (
    <div className="playback-bar">
      <button className="play-btn" disabled={n < 2} onClick={onTogglePlay}>
        {playing ? "⏸ 暂停" : "▶ 播放"}
      </button>
      <button className="pb-step" disabled={!n} onClick={() => seekIdx(curIdx - 1)} title="上一帧">◀</button>
      <button className="pb-step" disabled={!n} onClick={() => seekIdx(curIdx + 1)} title="下一帧">▶</button>
      <div className="pb-track">
        <input
          type="range"
          min="0"
          max={Math.max(0, n - 1)}
          value={Math.min(curIdx, Math.max(0, n - 1))}
          disabled={n < 2}
          onChange={(event) => seekIdx(Number(event.target.value))}
        />
      </div>
      <div className="pb-readout">
        <strong>{curStep === null ? "无帧" : `step ${curStep}`}</strong>
        <small>{n ? `${curIdx + 1} / ${n}` : "-"} · {selected.branch || "-"}</small>
      </div>
      <div className="pb-speed">
        {speeds.map((s) => (
          <button key={s} className={cls("pb-speed-btn", speed === s && "active")} onClick={() => onSpeed(s)}>{s}×</button>
        ))}
      </div>
    </div>
  );
}

function VizTab({ run, timeline, runtime, dataplane, selected, setSelected, setFollowLive }) {
  const branches = collectBranches(timeline, dataplane);
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(1);

  // 用 ref 持有最新步序列/当前步，让定时器闭包稳定，不必每次轮询重建。
  const stepsRef = useRef([]);
  stepsRef.current = branchStepValues(timeline, dataplane, selected.branch);
  const stepRef = useRef(Number(selected.step));
  stepRef.current = Number(selected.step);

  useEffect(() => {
    if (!playing) return undefined;
    const intervalMs = Math.max(120, Math.round(700 / (speed || 1)));
    const id = setInterval(() => {
      const steps = stepsRef.current;
      if (steps.length < 2) return;
      let idx = steps.indexOf(stepRef.current);
      if (idx < 0) idx = steps.length - 1;
      const nextStep = steps[(idx + 1) % steps.length]; // 末帧回到首帧循环播放
      stepRef.current = nextStep;
      setSelected((prev) => ({ ...prev, step: nextStep }));
    }, intervalMs);
    return () => clearInterval(id);
  }, [playing, speed, setSelected]);

  const togglePlay = () => {
    setPlaying((p) => {
      const next = !p;
      if (next && setFollowLive) setFollowLive(false);
      return next;
    });
  };
  const seek = (stepValue) => {
    setPlaying(false);
    if (setFollowLive) setFollowLive(false);
    setSelected((prev) => ({ ...prev, step: Number(stepValue) }));
  };

  return (
    <div className="viz-tab-layout">
      <div className="viz-side">
        <Panel title="分支" subtitle="选择分支查看其时间线与场；各分支进度条独立">
          <BranchTree branches={branches} selected={selected} onSelect={(next) => { setPlaying(false); setSelected(next); }} />
        </Panel>
        <BranchCurvePanel timeline={timeline} selected={selected} />
      </div>
      <div className="viz-main">
        <FieldGalleryV2 run={run} dataplane={dataplane} selected={selected} onSelect={setSelected} title="真实场云图" />
        <PlaybackBar
          timeline={timeline}
          dataplane={dataplane}
          selected={selected}
          onSeek={seek}
          playing={playing}
          onTogglePlay={togglePlay}
          speed={speed}
          onSpeed={setSpeed}
        />
      </div>
    </div>
  );
}

function RunInspectorPage(props) {
  const { run, runs, setRun, timeline, dataplane, runtime, evidence, status, selected, setSelected, mode, setMode, followLive, setFollowLive } = props;
  const [tab, setTab] = useState("viz");
  const tabs = [
    ["viz", "时间线 · 场云图"],
    ["dataplane", "数据平面"],
    ["runtime", "运行时主机"],
    ["health", "健康/QoI"],
    ["evidence", "证据审计"],
  ];
  return (
    <div className="run-inspector-layout">
      <RunScopeBar
        run={run}
        runs={runs}
        setRun={setRun}
        timeline={timeline}
        dataplane={dataplane}
        runtime={runtime}
        selected={selected}
        setSelected={setSelected}
        mode={mode}
        setMode={setMode}
        followLive={followLive}
        setFollowLive={setFollowLive}
        status={status}
      />
      <div className="inspector-tabs">
        {tabs.map(([id, label]) => (
          <button key={id} className={tab === id ? "active" : ""} onClick={() => setTab(id)}>
            {label}
          </button>
        ))}
      </div>
      {tab === "viz" && (
        <VizTab
          run={run}
          timeline={timeline}
          runtime={runtime}
          dataplane={dataplane}
          selected={selected}
          setSelected={setSelected}
          setFollowLive={setFollowLive}
        />
      )}
      {tab === "dataplane" && (
        <DataPlanePagePhase6 run={run} dataplane={dataplane} selected={selected} setSelected={setSelected} />
      )}
      {tab === "runtime" && (
        <RuntimeInspectorTab runtime={runtime} timeline={timeline} />
      )}
      {tab === "health" && (
        <div className="inspector-tab-grid">
          <LedgerPage dataplane={dataplane} />
          <FieldGalleryV2 run={run} dataplane={dataplane} selected={selected} onSelect={setSelected} title="健康相关场" />
        </div>
      )}
      {tab === "evidence" && (
        <EvidenceInspectorTab run={run} evidence={evidence} timeline={timeline} runtime={runtime} dataplane={dataplane} />
      )}
    </div>
  );
}

function RuntimeHostPage(props) {
  const {
    runtime,
    timeline,
    status,
    run,
    selectedWorkflowId,
    selectedRunProfileId,
    compileResult,
    preflightResult,
    reloadAll,
    setRun,
  } = props;
  const live = status && status.process ? status : {};
  const view = runtime && runtime.process ? runtime : live;
  const process = (view && view.process) || {};
  const init = (view && view.operator_initialization) || {};
  const clock = (view && view.clock) || {};
  const scheduler = (view && view.scheduler) || {};
  const blocking = (view && view.blocking) || {};
  const sessions = asArray(view && view.adapter_sessions);
  const events = asArray((view && (view.events || view.runtime_events))).slice(-80).reverse();
  const commands = asArray(view && view.command_records).slice(-80).reverse();
  const workers = asArray(view && view.workers);
  const compiledDir = firstDefined(compileResult && compileResult.compiled_dir, compileResult && compileResult.plan && compileResult.plan.compiled_dir);
  const [busy, setBusy] = useState("");
  const [message, setMessage] = useState("");

  async function postCommand(url, extra = {}) {
    setBusy(url);
    setMessage("");
    try {
      const res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          workflow_id: selectedWorkflowId,
          run_profile_id: selectedRunProfileId,
          compiled_dir: compiledDir,
          job_id: process.job_id || (status && status.job_id),
          run,
          ...extra,
        }),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || data.ok === false) throw new Error(data.message || data.error || `${res.status} ${res.statusText}`);
      setMessage(data.message || data.job_id || "命令已提交");
      if (data.run && url.endsWith("/start") && setRun) setRun(data.run);
      if (reloadAll) reloadAll();
    } catch (err) {
      setMessage(err.message || String(err));
    } finally {
      setBusy("");
    }
  }

  const canPrepare = Boolean(selectedWorkflowId && selectedRunProfileId && compiledDir && preflightResult && preflightResult.ok);
  const canStart = canPrepare;
  const running = Boolean(process.running || (status && status.running));

  return (
    <div className="runtime-control-layout">
      <Panel title="Runtime 控制中心" subtitle="prepare/start/stop/checkpoint 命令均经过 RuntimeHost 桥接并写入命令记录">
        <div className="runtime-command-bar">
          <Button disabled={!canPrepare} busy={busy === "/api/runtime/prepare"} tone="primary" onClick={() => postCommand("/api/runtime/prepare")}>prepare 初始化</Button>
          <Button disabled={!canStart} busy={busy === "/api/runtime/start"} onClick={() => postCommand("/api/runtime/start")}>start 启动</Button>
          <Button disabled={!running} busy={busy === "/api/runtime/pause"} onClick={() => postCommand("/api/runtime/pause")}>pause</Button>
          <Button busy={busy === "/api/runtime/resume"} onClick={() => postCommand("/api/runtime/resume")}>resume</Button>
          <Button busy={busy === "/api/runtime/checkpoint"} onClick={() => postCommand("/api/runtime/checkpoint")}>checkpoint</Button>
          <Button disabled={!running && !process.job_id} busy={busy === "/api/runtime/stop"} tone="danger" onClick={() => postCommand("/api/runtime/stop")}>stop</Button>
        </div>
        {message && <div className="inline-message">{message}</div>}
        <KeyValue rows={[
          ["status", view && view.status ? <StatusPill status={view.status} /> : "-"],
          ["pid", firstDefined(process.pid, "-")],
          ["job_id", firstDefined(process.job_id, "-")],
          ["backend", firstDefined(view && view.backend, view && view.host && view.host.execution_backend, "-")],
          ["external input", firstDefined(view && view.inputs && view.inputs.external_observation_stream, "-")],
          ["workflow", firstDefined(selectedWorkflowId, view && view.workflow_id, timeline && timeline.workflow_id, "-")],
          ["profile", firstDefined(selectedRunProfileId, view && view.run_profile_id, "-")],
          ["run", firstDefined(run, view && view.run, timeline && timeline.run, "-")],
          ["elapsed", process.elapsed_s || process.elapsed_s === 0 ? `${process.elapsed_s}s` : "-"],
          ["blocked", blocking.stalled ? blocking.message : "未检测到卡住"],
        ]} />
      </Panel>

      <Panel title="初始化进度" subtitle="resource lock、model snapshot、operator initialization evidence">
        <div className="metric-grid tight">
          <MetricCard label="resource locks" value={fmtNumber(init.resource_lock_count, 0)} />
          <MetricCard label="model snapshots" value={fmtNumber(init.model_snapshot_count, 0)} />
          <MetricCard label="operator specs" value={fmtNumber(init.operator_snapshot_count, 0)} />
          <MetricCard label="preflight runs" value={fmtNumber(init.preflight_run_count, 0)} />
        </div>
        <KeyValue rows={[
          ["status", firstDefined(init.status, view && view.initialization && view.initialization.status, "-")],
          ["workflow_count", fmtNumber(init.workflow_count, 0)],
          ["snapshot", shortPath(view && view.initialization && view.initialization.snapshot_path)],
          ["message", firstDefined(view && view.initialization && view.initialization.message, "-")],
        ]} />
        <DataTable
          rows={asArray(init.workflows)}
          emptyHint="prepare 之前没有初始化 workflow 记录。"
          columns={[
            { key: "role", title: "role", render: (row) => firstDefined(row.workflow_role, "-") },
            { key: "workflow", title: "workflow", render: (row) => firstDefined(row.workflow_id, "-") },
            { key: "resource", title: "resource_lock", render: (row) => shortPath(row.resource_lock) },
            { key: "model", title: "model_snapshot", render: (row) => shortPath(row.model_snapshot) },
          ]}
        />
      </Panel>

      <Panel title="Clock / Scheduler / Worker" subtitle="统一时钟、调度阶段、worker/branch 状态">
        <div className="metric-grid tight">
          <MetricCard label="tick" value={fmtNumber(clock.tick_index, 0)} />
          <MetricCard label="time" value={fmtTime(firstDefined(clock.source_time_s, clock.run_time_s))} />
          <MetricCard label="total" value={scheduler.total_progress_percent || scheduler.total_progress_percent === 0 ? `${fmtNumber(scheduler.total_progress_percent, 1)}%` : "-"} />
          <MetricCard label="workers" value={fmtNumber(workers.length, 0)} />
        </div>
        <KeyValue rows={[
          ["clock source", firstDefined(clock.source, "-")],
          ["delta_t", clock.delta_t_s || clock.delta_t_s === 0 ? `${clock.delta_t_s}s` : "-"],
          ["stage", firstDefined(scheduler.stage, blocking.current_stage, "-")],
          ["artifact age", blocking.artifact_age_s || blocking.artifact_age_s === 0 ? `${blocking.artifact_age_s}s` : "-"],
          ["recent event", firstDefined(blocking.recent_event && (blocking.recent_event.event_kind || blocking.recent_event.kind), "-")],
        ]} />
        <DataTable
          rows={workers}
          emptyHint="尚无 worker/branch 状态。"
          columns={[
            { key: "id", title: "worker", render: (row) => firstDefined(row.worker_id, "-") },
            { key: "kind", title: "kind", render: (row) => firstDefined(row.kind, "-") },
            { key: "status", title: "status", render: (row) => <StatusPill status={row.status} /> },
            { key: "progress", title: "progress", render: (row) => firstDefined(row.progress, "-") },
          ]}
        />
      </Panel>

      <Panel title="Adapter sessions" subtitle="算子后端实例与生命周期">
        <DataTable
          rows={sessions}
          columns={[
            { key: "operator", title: "算子", render: (row) => firstDefined(row.operator_id, row.node_id) },
            { key: "backend", title: "后端", render: (row) => firstDefined(row.backend, row.backend_kind, "-") },
            { key: "status", title: "状态", render: (row) => <StatusPill status={row.status} /> },
            { key: "duration", title: "耗时", render: (row) => fmtNumber(firstDefined(row.duration_ms, row.elapsed_ms), 1) + " ms" },
          ]}
        />
      </Panel>

      <Panel title="Runtime command records" subtitle="每个控制命令都要写入 evidence 或命令记录">
        <DataTable
          rows={commands}
          emptyHint="尚无 Runtime 控制命令记录。"
          columns={[
            { key: "time", title: "time", render: (row) => firstDefined(row.generated_at_utc, "-") },
            { key: "action", title: "action", render: (row) => firstDefined(row.action, "-") },
            { key: "status", title: "status", render: (row) => firstDefined(row.status, "-") },
            { key: "job", title: "job", render: (row) => firstDefined(row.job_id, "-") },
            { key: "msg", title: "message", render: (row) => firstDefined(row.message, "-") },
          ]}
        />
      </Panel>

      <Panel title="Runtime events" subtitle="事件队列、触发、失败与证据记录">
        <DataTable
          rows={events}
          columns={[
            { key: "time", title: "time", render: (row) => fmtTime(firstDefined(row.time_s, row.public_time_s)) },
            { key: "kind", title: "kind", render: (row) => firstDefined(row.kind, row.event_kind, "-") },
            { key: "target", title: "target", render: (row) => firstDefined(row.node_id, row.branch_id, row.port_id, "-") },
            { key: "msg", title: "message", render: (row) => firstDefined(row.message, row.summary, "-") },
          ]}
        />
      </Panel>
    </div>
  );
}

function DataPlanePage({ run, dataplane, selected, setSelected }) {
  const fields = collectFields(dataplane, selected.branch);
  const qois = asArray(dataplane && dataplane.qois);
  return (
    <div className="dataplane-layout">
      <Panel title="数据平面索引" subtitle="artifact / tensor / qoi / evidence reference">
        <div className="metric-grid tight">
          <MetricCard label="fields" value={fmtNumber(dataplane && dataplane.field_count, 0)} />
          <MetricCard label="QoI" value={fmtNumber(dataplane && dataplane.qoi_count, 0)} />
          <MetricCard label="ports" value={fmtNumber(asArray(dataplane && dataplane.ports).length, 0)} />
        </div>
        <DataTable
          rows={fields.slice(0, 120)}
          columns={[
            { key: "port", title: "端口", render: (row) => (
              <button className="link-btn" onClick={() => setSelected({ ...selected, branch: firstDefined(row.branch_id, selected.branch), port: fieldPort(row), step: Number(firstDefined(row.step, row.step_index, 0)) })}>
                {portDisplay(fieldPort(row))}
              </button>
            ) },
            { key: "branch", title: "分支", render: (row) => firstDefined(row.branch_id, "-") },
            { key: "step", title: "step", render: (row) => firstDefined(row.step, row.step_index, 0) },
            { key: "stats", title: "统计", render: (row) => {
              const s = fieldStats(row);
              return `${fmtNumber(s.min)} / ${fmtNumber(s.max)} / ${fmtNumber(s.mean)}`;
            } },
          ]}
        />
      </Panel>
      <FieldViewer run={run} selected={selected} title="artifact 可视化" />
      <Panel title="QoI / Decision" subtitle="摘要值不进入三维场，按证据表呈现">
        <DataTable
          rows={qois.slice(0, 120)}
          columns={[
            { key: "id", title: "id", render: (row) => firstDefined(row.qoi_id, row.id, row.port) },
            { key: "branch", title: "分支", render: (row) => firstDefined(row.branch_id, "-") },
            { key: "step", title: "step", render: (row) => firstDefined(row.step, row.step_index, "-") },
            { key: "value", title: "value", render: (row) => fmtNumber(firstDefined(row.value, row.mean, row.score)) },
          ]}
        />
      </Panel>
    </div>
  );
}

function DataPlanePagePhase6({ run, dataplane, selected, setSelected }) {
  const allArtifacts = artifactRows(dataplane);
  const branchOptions = Array.from(new Set(allArtifacts.map((row) => firstDefined(row.branch_id, row.branch, "")).filter(Boolean)));
  const operatorOptions = Array.from(new Set(allArtifacts.map((row) => firstDefined(row.operator_id, row.producer, row.node_id, "")).filter(Boolean)));
  const [branchFilter, setBranchFilter] = useState("");
  const [stepFilter, setStepFilter] = useState("");
  const [operatorFilter, setOperatorFilter] = useState("");

  const filteredArtifacts = allArtifacts.filter((row) => {
    const rowBranch = firstDefined(row.branch_id, row.branch, "");
    const rowStep = String(firstDefined(row.step, row.step_index, ""));
    const rowOperator = firstDefined(row.operator_id, row.producer, row.node_id, "");
    if (branchFilter && rowBranch !== branchFilter) return false;
    if (stepFilter !== "" && rowStep !== stepFilter) return false;
    if (operatorFilter && rowOperator !== operatorFilter) return false;
    return true;
  });
  const selectedArtifact = filteredArtifacts.find((row) => (
    firstDefined(row.branch_id, row.branch, "") === selected.branch
    && fieldPort(row) === selected.port
    && Number(firstDefined(row.step, row.step_index, 0)) === Number(selected.step || 0)
  )) || filteredArtifacts[0] || null;
  const selectedKind = artifactValueKind(selectedArtifact);
  const renderer = resolveUiRenderer(artifactRendererDescriptor(selectedArtifact), selectedKind);
  const artifactUri = selectedArtifact ? firstDefined(selectedArtifact.artifact_uri, selectedArtifact.uri, selectedArtifact.ref) : "";
  const canRenderField = selectedArtifact
    && renderer.resolved_renderer === "field.vtk.scalar.v1"
    && artifactUri
    && Number(firstDefined(selectedArtifact.node_count, 0)) > 0;
  const activeSelection = selectedArtifact
    ? {
      ...selected,
      branch: firstDefined(selectedArtifact.branch_id, selectedArtifact.branch, selected.branch),
      port: fieldPort(selectedArtifact),
      step: Number(firstDefined(selectedArtifact.step, selectedArtifact.step_index, selected.step || 0)),
    }
    : selected;

  function selectArtifact(row) {
    setSelected({
      ...selected,
      branch: firstDefined(row.branch_id, row.branch, selected.branch),
      port: fieldPort(row),
      step: Number(firstDefined(row.step, row.step_index, 0)),
    });
  }

  return (
    <div className="dataplane-layout dataplane-phase6-layout">
      <Panel title="数据平面索引" subtitle="artifact / tensor / QoI / evidence reference">
        <div className="metric-grid tight">
          <MetricCard label="field artifacts" value={fmtNumber(dataplane && dataplane.field_count, 0)} />
          <MetricCard label="QoI artifacts" value={fmtNumber(dataplane && dataplane.qoi_count, 0)} />
          <MetricCard label="ports" value={fmtNumber(asArray(dataplane && dataplane.ports).length, 0)} />
        </div>
        <div className="dataplane-filter-grid">
          <label>
            <span>branch</span>
            <select value={branchFilter} onChange={(event) => setBranchFilter(event.target.value)}>
              <option value="">全部</option>
              {branchOptions.map((item) => <option key={item} value={item}>{item}</option>)}
            </select>
          </label>
          <label>
            <span>step</span>
            <input value={stepFilter} onChange={(event) => setStepFilter(event.target.value)} placeholder="全部" />
          </label>
          <label>
            <span>operator</span>
            <select value={operatorFilter} onChange={(event) => setOperatorFilter(event.target.value)}>
              <option value="">全部</option>
              {operatorOptions.map((item) => <option key={item} value={item}>{item}</option>)}
            </select>
          </label>
        </div>
        <DataTable
          rows={filteredArtifacts.slice(0, 180)}
          emptyHint="当前筛选条件下没有 artifact。"
          rowKey={(row) => `${row.artifact_row_id}-${row.branch_id}-${row.step}-${row.port_id}`}
          columns={[
            { key: "artifact_id", title: "artifact_id", render: (row) => (
              <button className={cls("link-btn", selectedArtifact && row.artifact_row_id === selectedArtifact.artifact_row_id && "active")} onClick={() => selectArtifact(row)}>
                {shortPath(firstDefined(row.artifact_id, row.artifact_uri, "-"))}
              </button>
            ) },
            { key: "kind", title: "kind", render: (row) => firstDefined(row.kind, row.value_kind, "-") },
            { key: "contract", title: "contract", render: (row) => shortPath(firstDefined(row.contract_id, row.contract, "-")) },
            { key: "component", title: "component", render: (row) => firstDefined(row.component_id, row.component, "-") },
            { key: "mesh", title: "mesh", render: (row) => firstDefined(row.mesh_ref, row.layout_ref, "-") },
            { key: "node_count", title: "node_count", render: (row) => fmtNumber(firstDefined(row.node_count, "-"), 0) },
            { key: "producer", title: "producer", render: (row) => shortPath(firstDefined(row.producer, row.operator_id, row.node_id, "-")) },
          ]}
        />
      </Panel>

      <Panel title="Artifact 对账" subtitle="渲染前必须完成 artifact / mesh / layout / renderer 对账">
        {!selectedArtifact && <Empty title="没有可显示 artifact" hint="缺少 artifact 时不会绘制默认色标或伪场。" />}
        {selectedArtifact && (
          <div className="artifact-detail-grid">
            <div className="renderer-status-card">
              <h3>renderer</h3>
              <KeyValue rows={[
                ["requested", renderer.requested_renderer],
                ["resolved", renderer.resolved_renderer],
                ["resolution", renderer.resolution],
                ["component", renderer.component],
              ]} />
            </div>
            <div className="renderer-status-card">
              <h3>artifact</h3>
              <KeyValue rows={[
                ["artifact_id", shortPath(firstDefined(selectedArtifact.artifact_id, "-"))],
                ["kind", firstDefined(selectedArtifact.kind, selectedArtifact.value_kind, "-")],
                ["contract", shortPath(firstDefined(selectedArtifact.contract_id, selectedArtifact.contract, "-"))],
                ["port", portDisplay(fieldPort(selectedArtifact))],
                ["uri", shortPath(firstDefined(selectedArtifact.artifact_uri, selectedArtifact.uri, selectedArtifact.ref, "-"))],
              ]} />
            </div>
            <div className="renderer-status-card">
              <h3>mesh / layout</h3>
              <KeyValue rows={[
                ["mesh_ref", firstDefined(selectedArtifact.mesh_ref, "-")],
                ["layout_ref", firstDefined(selectedArtifact.layout_ref, "-")],
                ["node_count", fmtNumber(firstDefined(selectedArtifact.node_count, "-"), 0)],
                ["alignment", canRenderField ? "等待 Field3D 强制校验" : "非三维场或缺少 artifact，拒绝绘制伪图"],
              ]} />
            </div>
          </div>
        )}
      </Panel>

      <Panel title="Renderer 预览" subtitle="display_descriptor / value_kind 通过 RendererRegistry 解析">
        {!selectedArtifact && <Empty title="缺少 artifact" hint="TC-024：无 artifact 时显示缺失状态，不绘制默认假图。" />}
        {selectedArtifact && !canRenderField && (
          <div className="standard-warning">
            当前 artifact 解析为 {renderer.resolved_renderer}，没有完整 field artifact + mesh/layout 元数据，因此使用通用端口/证据展示。
          </div>
        )}
        {selectedArtifact && canRenderField && (
          <Field3D run={run} branch={activeSelection.branch} port={activeSelection.port} step={activeSelection.step} height={520} />
        )}
      </Panel>

      <Panel title="摘要与 QoI" subtitle="摘要值不进入三维场渲染，按证据表呈现">
        <DataTable
          rows={asArray(dataplane && dataplane.qois).slice(0, 120)}
          columns={[
            { key: "id", title: "id", render: (row) => firstDefined(row.qoi_id, row.id, row.port) },
            { key: "branch", title: "branch", render: (row) => firstDefined(row.branch_id, "-") },
            { key: "step", title: "step", render: (row) => firstDefined(row.step, row.step_index, "-") },
            { key: "value", title: "value", render: (row) => fmtNumber(firstDefined(row.value, row.mean, row.score)) },
          ]}
        />
      </Panel>
    </div>
  );
}

function resourcePreviewKind(resource) {
  const type = resourceType(resource).toLowerCase();
  if (type.includes("mesh") || type.includes("geometry")) return "mesh";
  if (type.includes("database") || type.includes("dataset") || type.includes("sample")) return "data";
  if (type.includes("sensor") || type.includes("noise") || type.includes("calibration")) return "table";
  if (type.includes("model") || type.includes("basis") || type.includes("criterion") || type.includes("state")) return "model";
  return "table";
}

function resourcePreviewRows(resource) {
  if (!resource) return [];
  const preferred = [
    "uri",
    "component_id",
    "layout_role",
    "model_kind",
    "source_database_ref",
    "output_contract_id",
    "observed_state_contract_id",
    "observed_field_contract_id",
    "sampling_rate_hz",
    "valid_range",
    "provides",
    "covers_contract_ids",
    "model_package_root",
    "pod_text_dump_dir",
    "pred_train_model_dir",
    "pod_model_dir",
  ];
  const rows = preferred
    .filter((key) => resource[key] !== undefined && resource[key] !== null && resource[key] !== "")
    .map((key) => ({ key, value: resource[key] }));
  if (rows.length) return rows;
  return Object.entries(resource)
    .filter(([key, value]) => !["resource_id", "resource_type", "group_id"].includes(key) && value !== undefined && value !== null && value !== "")
    .slice(0, 12)
    .map(([key, value]) => ({ key, value }));
}

function ResourcePreview({ resource, packageDir = "" }) {
  if (!resource) return null;
  const kind = resourcePreviewKind(resource);
  const rows = resourcePreviewRows(resource);
  return (
    <div className={cls("resource-preview-card", `resource-preview-${kind}`)}>
      <div className="resource-preview-stage">
        {kind === "mesh" && (
          <ObjectGeometry3D
            resource={resource}
            componentId={firstDefined(resource.component_id, "")}
            packageDir={packageDir}
            height={320}
            title="真实网格 / 几何"
          />
        )}
        {kind === "model" && (
          <div className="model-preview">
            <div className="model-blocks">
              <span />
              <span />
              <span />
            </div>
            <strong>模型资产</strong>
            <small>{firstDefined(resource.model_kind, resource.output_contract_id, resource.uri, "-")}</small>
          </div>
        )}
        {kind === "data" && (
          <div className="data-preview">
            <div className="data-grid-icon">
              {Array.from({ length: 12 }).map((_, index) => <span key={index} />)}
            </div>
            <strong>数据 / 样本资源</strong>
            <small>{firstDefined(asArray(resource.provides).join("、"), resource.source_database_ref, resource.uri, "-")}</small>
          </div>
        )}
        {kind === "table" && (
          <div className="table-preview">
            <strong>声明字段预览</strong>
            <small>{firstDefined(resource.model_kind, resource.output_contract_id, resource.uri, "-")}</small>
          </div>
        )}
      </div>
      <DataTable
        rows={rows}
        columns={[
          { key: "key", title: "字段", render: (row) => row.key },
          { key: "value", title: "值", render: (row) => formatJsonValue(row.value) },
        ]}
      />
    </div>
  );
}

function ResourceDraftEditor({ resource, reloadAll, editing = false }) {
  const rid = resourceId(resource);
  const isNew = !!(resource && resource.__new);
  const [editableId, setEditableId] = useState(rid || "");
  const [resourceTypeValue, setResourceTypeValue] = useState(resourceType(resource));
  const [componentId, setComponentId] = useState(firstDefined(resource && resource.component_id, ""));
  const [displayName, setDisplayName] = useState(firstDefined(resource && resource.display_name, resource && resource.name, ""));
  const [uri, setUri] = useState(firstDefined(resource && resource.uri, resource && resource.path, ""));
  const [version, setVersion] = useState(firstDefined(resource && resource.version, ""));
  const [enabled, setEnabled] = useState(resource && resource.enabled === false ? "false" : "true");
  const [groupId, setGroupId] = useState(firstDefined(resource && resource.group_id, ""));
  const [description, setDescription] = useState(firstDefined(resource && resource.description, ""));
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState("");

  useEffect(() => {
    setEditableId(rid || "");
    setResourceTypeValue(resourceType(resource));
    setComponentId(firstDefined(resource && resource.component_id, ""));
    setDisplayName(firstDefined(resource && resource.display_name, resource && resource.name, ""));
    setUri(firstDefined(resource && resource.uri, resource && resource.path, ""));
    setVersion(firstDefined(resource && resource.version, ""));
    setEnabled(resource && resource.enabled === false ? "false" : "true");
    setGroupId(firstDefined(resource && resource.group_id, ""));
    setDescription(firstDefined(resource && resource.description, ""));
    setMessage("");
  }, [rid, isNew]);

  async function save() {
    if (!editing) {
      setMessage("请先点击编辑，再保存资源草稿。");
      return;
    }
    const targetId = String(editableId || rid || "").trim();
    if (!targetId) {
      setMessage("请先填写 resource_id");
      return;
    }
    setBusy(true);
    setMessage("");
    try {
      const data = await apiPost("/api/resource/draft", {
        resource_id: targetId,
        create: isNew,
        patch: {
          resource_id: targetId,
          display_name: displayName,
          resource_type: resourceTypeValue,
          component_id: componentId,
          uri,
          version,
          enabled: enabled === "true",
          group_id: groupId,
          description,
        },
      });
      setMessage(data.message || "资源草稿已保存");
      if (reloadAll) reloadAll();
    } catch (err) {
      setMessage(err.message || String(err));
    } finally {
      setBusy(false);
    }
  }

  async function remove() {
    if (!editing) {
      setMessage("请先点击编辑，再删除资源草稿。");
      return;
    }
    const targetId = String(editableId || rid || "").trim();
    if (!targetId || isNew) return;
    if (!window.confirm(`确认从对象草稿删除资源：${targetId}？`)) return;
    setBusy(true);
    setMessage("");
    try {
      const data = await apiPost("/api/resource/delete", { resource_id: targetId });
      setMessage(data.message || "资源已删除");
      if (reloadAll) reloadAll();
    } catch (err) {
      setMessage(err.message || String(err));
    } finally {
      setBusy(false);
    }
  }

  if (!resource) return null;
  return (
    <div className="phase8-editor">
      <h3>资源草稿编辑</h3>
      <ReadonlyHint editing={editing} />
      <EditingGate editing={editing}>
        <div className="editor-grid compact">
          <label><span>resource_id</span><input value={editableId} readOnly={!isNew} onChange={(event) => setEditableId(event.target.value)} /></label>
          <label><span>中文名</span><input value={displayName} onChange={(event) => setDisplayName(event.target.value)} /></label>
          <label><span>resource_type</span><input value={resourceTypeValue} onChange={(event) => setResourceTypeValue(event.target.value)} /></label>
          <label><span>component_id</span><input value={componentId} onChange={(event) => setComponentId(event.target.value)} /></label>
          <label><span>uri / path</span><input value={uri} onChange={(event) => setUri(event.target.value)} /></label>
          <label><span>version</span><input value={version} onChange={(event) => setVersion(event.target.value)} /></label>
          <label><span>enabled</span><select value={enabled} onChange={(event) => setEnabled(event.target.value)}><option value="true">启用</option><option value="false">停用</option></select></label>
          <label><span>asset group</span><input value={groupId} onChange={(event) => setGroupId(event.target.value)} /></label>
          <label><span>description</span><input value={description} onChange={(event) => setDescription(event.target.value)} /></label>
        </div>
        <div className="button-row">
          <Button busy={busy} tone="primary" onClick={save}>保存到对象草稿</Button>
          <Button busy={busy} disabled={isNew || !rid} tone="danger" onClick={remove}>从对象草稿删除</Button>
          {message && <span className="inline-message">{message}</span>}
        </div>
      </EditingGate>
    </div>
  );
}

function ResourceBrowser({ resources, assetGroups, packageDir = "", selectedResourceId = "", standalone = false, reloadAll }) {
  const resourceList = asArray(resources);
  const groupList = asArray(assetGroups);
  const [editing, setEditing] = useState(false);
  const [validation, setValidation] = useState(null);
  const [validating, setValidating] = useState(false);
  const [validationMessage, setValidationMessage] = useState("");
  const [creatingResource, setCreatingResource] = useState(false);
  const resourceGroups = useMemo(() => {
    const byId = new Map(resourceList.map((item) => [resourceId(item), item]));
    const seen = new Set();
    const grouped = groupList
      .map((group) => {
        const groupId = firstDefined(group.group_id, group.id, "未命名资源组");
        const items = asArray(group.resources).map((id) => {
          seen.add(id);
          return byId.get(id) || { resource_id: id, group_id: groupId, missing: true };
        });
        return { groupId, label: firstDefined(group.display_name, group.name, groupId), items };
      })
      .filter((group) => group.items.length);
    const ungrouped = resourceList.filter((item) => !seen.has(resourceId(item)));
    if (ungrouped.length) grouped.push({ groupId: "未分组资源", label: "未分组资源", items: ungrouped });
    return grouped;
  }, [resources, assetGroups]);

  const allResources = useMemo(() => resourceGroups.flatMap((group) => group.items), [resourceGroups]);
  const current = selectedResourceId
    ? allResources.find((item) => resourceId(item) === selectedResourceId) || null
    : null;
  const selectedResourceGroup = current
    ? resourceGroups.find((group) => group.items.some((item) => resourceId(item) === resourceId(current)))
    : null;

  useEffect(() => {
    if (selectedResourceId && resourceGroups.some((group) => group.items.some((item) => resourceId(item) === selectedResourceId))) {
      setCreatingResource(false);
      setEditing(false);
    }
  }, [selectedResourceId, resourceGroups]);

  async function validateAllResources() {
    setValidating(true);
    setValidationMessage("");
    try {
      const res = await fetch("/api/resources/validate", { method: "POST", headers: { "Content-Type": "application/json" }, body: "{}" });
      const data = await res.json().catch(() => ({}));
      setValidation(data);
      setValidationMessage(data.ok ? "资源校验通过" : "资源校验存在阻断问题");
    } catch (err) {
      setValidationMessage(err.message || String(err));
    } finally {
      setValidating(false);
    }
  }

  const arrayRows = current
    ? Object.entries(current)
      .filter(([, value]) => Array.isArray(value))
      .map(([key, value]) => ({ key, value }))
    : [];
  const objectRows = current
    ? Object.entries(current)
      .filter(([, value]) => value && typeof value === "object" && !Array.isArray(value))
      .map(([key, value]) => ({ key, value }))
    : [];

  return (
    <Panel
      className={cls("object-resource-panel", standalone && "resource-standalone-panel")}
      bodyClassName={cls("resource-browser-panel-body", standalone && "resource-standalone-body")}
      title="资源与模型资产"
      subtitle="按对象包 asset_groups/resource kind/component 分组，展示资源声明、引用关系和校验结果"
      toolbar={<EditModeToolbar editing={editing} setEditing={setEditing} canEdit={!!reloadAll} />}
    >
      <div className="asset-toolbar">
        <Button busy={validating} onClick={validateAllResources}>校验全部资源</Button>
        <span>{validationMessage || (validation ? `最近校验：${validation.ok ? "通过" : "未通过"}` : "尚未执行资源校验")}</span>
      </div>
      {editing && (
        <div className="button-row">
          <Button onClick={() => setCreatingResource(true)}>新建对象资源</Button>
        </div>
      )}
      <div className="resource-browser resource-browser-detail-only">
        <div className="resource-detail">
          {editing && creatingResource ? (
            <>
              <div className="resource-detail-title">
                <div>
                  <strong>新建对象资源</strong>
                  <span>object draft resource</span>
                </div>
                <Badge tone="neutral">draft</Badge>
              </div>
              <ResourceDraftEditor
                resource={{ __new: true, resource_id: "", resource_type: "resource", enabled: true }}
                reloadAll={reloadAll}
                editing={editing}
              />
              <p className="muted-note">保存后会自动创建或选择对象草稿，并写入 object/twin_object.json 与 assets/resources.json。</p>
            </>
          ) : current ? (
            <>
              <div className="resource-detail-title">
                <div>
                  <strong>{resourceTitle(current)}</strong>
                  <span>{selectedResourceGroup ? `${selectedResourceGroup.label} / ${resourceType(current)}` : resourceType(current)}</span>
                </div>
                {current.missing && <Badge tone="bad">asset group 引用了未声明资源</Badge>}
              </div>
              <ResourcePreview resource={current} packageDir={packageDir} />
              {editing && <ResourceDraftEditor resource={current} reloadAll={reloadAll} editing={editing} />}
              <KeyValue rows={[
                ["resource_id", resourceId(current)],
                ["resource_type", resourceType(current)],
                ["group_id", firstDefined(current.group_id, "-")],
                ["uri", firstDefined(current.uri, current.path, "-")],
                ["checksum", firstDefined(current.checksum, "-")],
                ["component_id", firstDefined(current.component_id, "-")],
                ["layout_role", firstDefined(current.layout_role, current.role, "-")],
                ["model_kind", firstDefined(current.model_kind, "-")],
                ["source_database_ref", firstDefined(current.source_database_ref, "-")],
                ["output_contract_id", firstDefined(current.output_contract_id, "-")],
                ["used_by_operators", joinList(current.used_by_operators)],
                ["used_by_workflows", joinList(current.used_by_workflows)],
                ["status", firstDefined(current.resource_status, "-")],
                ["description", firstDefined(current.description, "-")],
              ]} />
              {!!asArray(current.path_checks).length && (
                <div className="resource-detail-section">
                  <h3>路径 / 引用校验</h3>
                  <DataTable
                    rows={asArray(current.path_checks)}
                    columns={[
                      { key: "key", title: "字段", render: (row) => row.key },
                      { key: "status", title: "状态", render: (row) => <Badge tone={row.status === "missing" ? "bad" : "good"}>{row.status}</Badge> },
                      { key: "value", title: "声明值", render: (row) => shortPath(row.value) },
                      { key: "resolved", title: "解析路径", render: (row) => shortPath(row.resolved_path) },
                    ]}
                  />
                </div>
              )}
              {validation && diagnosticsOf(validation).length > 0 && (
                <div className="resource-detail-section">
                  <h3>资源校验诊断</h3>
                  <DataTable
                    rows={diagnosticsOf(validation).filter((row) => !row.resource_id || row.resource_id === resourceId(current))}
                    columns={[
                      { key: "level", title: "级别", render: (row) => <Badge tone={row.severity === "blocking" ? "bad" : "neutral"}>{row.severity || "info"}</Badge> },
                      { key: "resource", title: "resource_id", render: (row) => firstDefined(row.resource_id, "-") },
                      { key: "refs", title: "引用者", render: (row) => joinList([...(row.operator_refs || []), ...(row.workflow_refs || [])], 5) },
                      { key: "msg", title: "说明", render: (row) => firstDefined(row.message, "-") },
                    ]}
                  />
                </div>
              )}
              {!!arrayRows.length && (
                <div className="resource-detail-section">
                  <h3>数组字段</h3>
                  <DataTable
                    rows={arrayRows}
                    columns={[
                      { key: "key", title: "字段", render: (row) => row.key },
                      { key: "value", title: "值", render: (row) => formatJsonValue(row.value) },
                    ]}
                  />
                </div>
              )}
              {!!objectRows.length && (
                <div className="resource-detail-section">
                  <h3>对象字段</h3>
                  <DataTable
                    rows={objectRows}
                    columns={[
                      { key: "key", title: "字段", render: (row) => row.key },
                      { key: "value", title: "值", render: (row) => formatJsonValue(row.value) },
                    ]}
                  />
                </div>
              )}
              <div className="resource-detail-section">
                <h3>原始声明</h3>
                <pre className="json-pre">{JSON.stringify(current, null, 2)}</pre>
              </div>
            </>
          ) : (
            <Empty
              title="请从左侧对象树选择资源"
              hint={`当前对象包共有 ${allResources.length} 个资源、${resourceGroups.length} 个资源分组；本页不再重复显示资源列表，只展示左侧树选中资源的详情。`}
            />
          )}
        </div>
      </div>
    </Panel>
  );
}

function ResourceTreePage({ object, selectedObjectTreeNode, reloadAll }) {
  const selectedResourceId = selectedObjectTreeNode && (selectedObjectTreeNode.entity_kind || selectedObjectTreeNode.kind) === "resource"
    ? selectedObjectTreeNode.entity_id
    : "";
  return (
    <ResourceBrowser
      resources={asArray(object && object.resources)}
      assetGroups={asArray(object && object.asset_groups)}
      packageDir={objectPackageRoot(null, object)}
      selectedResourceId={selectedResourceId}
      reloadAll={reloadAll}
      standalone
    />
  );
}

function ObjectPage({ object }) {
  const components = asArray(object && object.components);
  const resources = asArray(object && object.resources);
  const workflows = asArray(object && object.workflows);
  const operators = asArray(object && object.operators);
  const assetGroups = asArray(object && object.asset_groups);
  const operatorFamilies = groupBy(operators, (item) => firstDefined(item.family, item.operator_family, "operator"));
  return (
    <div className="page-grid object-layout">
      <Panel className="object-summary-panel" title="对象画像" subtitle="对象包声明对象身份、组件、资源、模型资产、workflow 和算子入口；平台只读取声明">
        <div className="metric-grid">
          <MetricCard label="组件" value={fmtNumber(components.length, 0)} hint="object components" tone="cyan" />
          <MetricCard label="资源/模型" value={fmtNumber(resources.length, 0)} hint={`${assetGroups.length} asset groups`} tone="amber" />
          <MetricCard label="算子" value={fmtNumber(operators.length, 0)} hint={Object.keys(operatorFamilies).join(" / ")} tone="green" />
          <MetricCard label="工作流" value={fmtNumber(workflows.length, 0)} hint="workflow profiles" />
        </div>
        <KeyValue rows={[
          ["object_id", firstDefined(object && object.object_id, object && object.id)],
          ["name", firstDefined(object && object.name, object && object.display_name)],
          ["type", firstDefined(object && object.object_type, object && object.type)],
          ["schema", firstDefined(object && object.schema_version, "-")],
          ["package", shortPath(firstDefined(object && object.object_package_root, object && object.package_dir))],
        ]} />
      </Panel>
      <Panel title="组件-资源-算子-workflow 关系" subtitle="从对象包声明推导 component 能力边界；平台只展示关系，不写死对象语义">
        <DataTable
          rows={components}
          columns={[
            { key: "id", title: "component_id", render: (row) => firstDefined(row.component_id, row.id) },
            { key: "role", title: "role", render: (row) => firstDefined(row.role, row.kind, "-") },
            { key: "resources", title: "资源", render: (row) => joinList(row.resource_ids, 5) },
            { key: "operators", title: "算子", render: (row) => joinList(row.operator_ids, 4) },
            { key: "workflows", title: "workflow", render: (row) => joinList(row.workflow_ids, 4) },
          ]}
        />
      </Panel>
      <Panel title="对象能力图" subtitle="对象能力由资源、算子和 workflow 共同声明；换对象包后此图随声明变化">
        <div className="relationship-lanes">
          {components.map((comp) => (
            <div key={firstDefined(comp.component_id, comp.id)} className="relationship-card">
              <strong>{firstDefined(comp.display_name, comp.component_id, comp.id)}</strong>
              <div>
                <span>资源</span>
                <small>{joinList(comp.resource_ids, 6)}</small>
              </div>
              <div>
                <span>算子</span>
                <small>{joinList(comp.operator_ids, 6)}</small>
              </div>
              <div>
                <span>workflow</span>
                <small>{joinList(comp.workflow_ids, 6)}</small>
              </div>
            </div>
          ))}
        </div>
      </Panel>
      <Panel title="工作流画像" subtitle="对象可运行的 workflow/profile，在线运行和回放按这些声明进入">
        <DataTable
          rows={workflows}
          columns={[
            { key: "id", title: "workflow_id", render: (row) => firstDefined(row.workflow_id, row.id) },
            { key: "phase", title: "phase", render: (row) => firstDefined(row.phase, "-") },
            { key: "scale", title: "规模", render: (row) => `${fmtNumber(row.phase_count, 0)} phase / ${fmtNumber(row.stage_count, 0)} stage / ${fmtNumber(row.node_count, 0)} node` },
            { key: "file", title: "文件", render: (row) => firstDefined(row.spec_file, "-") },
            { key: "desc", title: "说明", render: (row) => firstDefined(row.description, "-") },
          ]}
        />
      </Panel>
    </div>
  );
}

function PortListPanel({ ports, activeKey, onSelect }) {
  return (
    <Panel title="端口" subtitle="选中端口后，在右侧查看 contract 与 typed DTO 数据结构">
      <div className="port-list">
        {ports.length ? ports.map((port) => {
          const id = portId(port);
          const key = `${port.direction}:${id}`;
          return (
            <button
              key={key}
              className={cls("port-item", activeKey === key && "active")}
              onClick={() => onSelect(key)}
              type="button"
            >
              <div>
                <Badge tone={port.direction === "input" ? "neutral" : "live"}>
                  {port.direction === "input" ? "输入" : "输出"}
                </Badge>
                <strong>{id}</strong>
              </div>
              <span>{portContractId(port)}</span>
              <small>{[
                firstDefined(port.value_kind, port.frame_contract, "-"),
                port.required === false ? "optional" : "required",
                portTimeRole(port) ? `time=${portTimeRole(port)}` : "",
              ].filter(Boolean).join(" / ")}</small>
            </button>
          );
        }) : <Empty hint="当前算子没有声明输入或输出端口。" />}
      </div>
    </Panel>
  );
}

function PortSchemaPanel({ port, schema }) {
  const typed = (port && port.typed_io_contract) || {};
  const schemaId = firstDefined(typed.schema_id, portContractId(port));
  const fields = asArray(schema && schema.fields);
  const uncertainty = schema && schema.uncertainty;
  return (
    <Panel title="端口数据结构" subtitle="端口级 typed_io_contract 与 domain schema 字段">
      {port ? (
        <div className="port-schema">
          <KeyValue rows={[
            ["port", portId(port)],
            ["direction", port.direction === "input" ? "输入" : "输出"],
            ["contract_id", portContractId(port)],
            ["frame_contract", firstDefined(port.frame_contract, "-")],
            ["value_kind", firstDefined(port.value_kind, schema && schema.value_kind, "-")],
            ["required", port.required === false ? "false" : "true"],
            ["time_role", firstDefined(portTimeRole(port), "-")],
            ["schema_id", firstDefined(schemaId, "-")],
            ["dto_name", firstDefined(typed.dto_name, "-")],
            ["type_name", firstDefined(typed.type_name, "-")],
            ["typed_dto_ref", firstDefined(portTypedDtoRef(port), "-")],
            ["buffer_layout_id", firstDefined(typed.buffer_layout_id, schema && schema.layout_ref, "-")],
            ["json_io", typed.json_operator_io_forbidden ? "forbidden" : "-"],
            ["artifact_policy", firstDefined(schema && schema.artifact_policy, "-")],
          ]} />
          {fields.length ? (
            <div className="port-schema-section">
              <h3>字段</h3>
              <DataTable
                rows={fields}
                columns={[
                  { key: "name", title: "name", render: (row) => row.name },
                  { key: "type", title: "type", render: (row) => firstDefined(row.type, row.dtype, "-") },
                  { key: "unit", title: "unit", render: (row) => firstDefined(row.unit, "-") },
                  { key: "required", title: "required", render: (row) => row.required === false ? "false" : "true" },
                ]}
              />
            </div>
          ) : (
            <div className="port-schema-section">
              <h3>布局 / 张量</h3>
              <KeyValue rows={[
                ["quantity", firstDefined(schema && schema.quantity, "-")],
                ["component_id", firstDefined(schema && schema.component_id, "-")],
                ["dtype", firstDefined(schema && schema.dtype, "-")],
                ["rank", firstDefined(schema && schema.rank, "-")],
                ["shape_ref", firstDefined(schema && schema.shape_ref, "-")],
                ["layout_ref", firstDefined(schema && schema.layout_ref, "-")],
                ["schema_role", firstDefined(schema && schema.schema_role, "-")],
              ]} />
            </div>
          )}
          {uncertainty && (
            <div className="port-schema-section">
              <h3>不确定性</h3>
              <pre className="json-pre compact">{JSON.stringify(uncertainty, null, 2)}</pre>
            </div>
          )}
          <div className="port-schema-section">
            <h3>端口原始声明</h3>
            <pre className="json-pre compact">{JSON.stringify(port, null, 2)}</pre>
          </div>
        </div>
      ) : (
        <Empty title="未选择端口" hint="从左侧端口列表选择输入或输出端口。" />
      )}
    </Panel>
  );
}

function OperatorDraftEditor({ operator, reloadAll, editing = false }) {
  const oid = firstDefined(operator && operator.operator_id, operator && operator.id);
  const display = (operator && operator.display_descriptor) || {};
  const [displayName, setDisplayName] = useState(firstDefined(operator && operator.display_name, ""));
  const [backendKind, setBackendKind] = useState(firstDefined(operator && operator.backend, operator && operator.kind, ""));
  const [adapterId, setAdapterId] = useState(firstDefined(operator && operator.adapter_id, ""));
  const [lifecycle, setLifecycle] = useState(lifecycleText(operator && operator.lifecycle));
  const [rendererId, setRendererId] = useState(firstDefined(display.renderer_id, operator && operator.renderer_id, ""));
  const [fallbackRenderer, setFallbackRenderer] = useState(firstDefined(display.fallback_renderer, ""));
  const [primaryOutputs, setPrimaryOutputs] = useState(joinList(display.primary_outputs, 20));
  const [enabled, setEnabled] = useState(operator && operator.enabled === false ? "false" : "true");
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState("");

  useEffect(() => {
    setDisplayName(firstDefined(operator && operator.display_name, ""));
    setBackendKind(firstDefined(operator && operator.backend, operator && operator.kind, ""));
    setAdapterId(firstDefined(operator && operator.adapter_id, ""));
    setLifecycle(lifecycleText(operator && operator.lifecycle));
    setRendererId(firstDefined(display.renderer_id, operator && operator.renderer_id, ""));
    setFallbackRenderer(firstDefined(display.fallback_renderer, ""));
    setPrimaryOutputs(joinList(display.primary_outputs, 20));
    setEnabled(operator && operator.enabled === false ? "false" : "true");
    setMessage("");
  }, [oid, operator && operator.display_name]);

  async function save() {
    if (!editing) {
      setMessage("请先点击编辑，再保存算子声明草稿。");
      return;
    }
    if (!oid) return;
    setBusy(true);
    setMessage("");
    try {
      const data = await apiPost("/api/operator/draft", {
        operator_id: oid,
        patch: {
          display_name: displayName,
          backend_kind: backendKind,
          adapter_id: adapterId,
          lifecycle,
          renderer_id: rendererId,
          fallback_renderer: fallbackRenderer,
          primary_outputs: primaryOutputs,
          enabled: enabled === "true",
        },
      });
      setMessage(data.message || "算子草稿已保存");
      if (reloadAll) reloadAll();
    } catch (err) {
      setMessage(err.message || String(err));
    } finally {
      setBusy(false);
    }
  }

  if (!operator) return null;
  return (
    <div className="phase8-editor">
      <h3>算子声明草稿编辑</h3>
      <ReadonlyHint editing={editing} />
      <EditingGate editing={editing}>
        <div className="editor-grid compact">
          <label className="span-2"><span>中文显示名</span><input value={displayName} onChange={(event) => setDisplayName(event.target.value)} /></label>
          <label><span>backend kind</span><input value={backendKind} onChange={(event) => setBackendKind(event.target.value)} /></label>
          <label><span>adapter_id</span><input value={adapterId} onChange={(event) => setAdapterId(event.target.value)} /></label>
          <label><span>lifecycle</span><input value={lifecycle} onChange={(event) => setLifecycle(event.target.value)} /></label>
          <label><span>enabled</span><select value={enabled} onChange={(event) => setEnabled(event.target.value)}><option value="true">启用</option><option value="false">停用</option></select></label>
          <label><span>renderer_id</span><input value={rendererId} onChange={(event) => setRendererId(event.target.value)} /></label>
          <label><span>fallback_renderer</span><input value={fallbackRenderer} onChange={(event) => setFallbackRenderer(event.target.value)} /></label>
          <label className="span-2"><span>primary_outputs</span><input value={primaryOutputs} onChange={(event) => setPrimaryOutputs(event.target.value)} /></label>
        </div>
        <div className="button-row">
          <Button busy={busy} tone="primary" onClick={save}>保存到对象草稿</Button>
          {message && <span className="inline-message">{message}</span>}
        </div>
      </EditingGate>
    </div>
  );
}

function OperatorsPage({ object, reloadAll, selectedObjectTreeNode }) {
  const [activePort, setActivePort] = useState("");
  const [editing, setEditing] = useState(false);
  const [preflight, setPreflight] = useState(null);
  const [preflightBusy, setPreflightBusy] = useState(false);
  const operators = asArray(object && object.operators);
  const selectedOperatorId = selectedObjectTreeNode && (selectedObjectTreeNode.entity_kind || selectedObjectTreeNode.kind) === "operator"
    ? selectedObjectTreeNode.entity_id
    : "";
  const schemaById = useMemo(() => {
    return asArray(object && object.domain_schemas).reduce((acc, schema) => {
      if (schema && schema.schema_id) acc[schema.schema_id] = schema;
      return acc;
    }, {});
  }, [object && object.domain_schemas]);
  const current = selectedOperatorId
    ? operators.find((op) => firstDefined(op.operator_id, op.id) === selectedOperatorId) || null
    : null;
  const ports = current ? operatorPorts(current) : [];
  const currentPort = ports.find((port) => `${port.direction}:${portId(port)}` === activePort) || ports[0] || null;
  const currentSchemaId = firstDefined(
    currentPort && currentPort.typed_io_contract && currentPort.typed_io_contract.schema_id,
    currentPort && portContractId(currentPort),
  );
  useEffect(() => {
    setEditing(false);
    setPreflight(null);
  }, [selectedOperatorId]);
  useEffect(() => {
    const firstPortKey = ports[0] ? `${ports[0].direction}:${portId(ports[0])}` : "";
    if (!ports.some((port) => `${port.direction}:${portId(port)}` === activePort)) {
      setActivePort(firstPortKey);
    }
  }, [selectedOperatorId, ports.length]);

  async function runPreflight(action = "preflight") {
    const operatorId = firstDefined(current.operator_id, current.id);
    if (!operatorId) return;
    setPreflightBusy(true);
    try {
      const res = await fetch("/api/operators/preflight", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ operator_id: operatorId, action }),
      });
      const data = await res.json().catch(() => ({}));
      setPreflight(data);
    } catch (err) {
      setPreflight({ ok: false, diagnostics: [{ severity: "blocking", message: err.message || String(err) }] });
    } finally {
      setPreflightBusy(false);
    }
  }

  const trialResult = (asArray(preflight && preflight.sections).find((section) => section.section_id === "trial") || {}).trial_command_result;

  return (
    <div className="operators-layout operators-detail-only">
      <Panel
        title={`算子接口：${current ? operatorDisplayName(current) : "未选择算子"}`}
        subtitle="端口契约、资源绑定、adapter 生命周期和试算入口来自对象包算子声明"
        toolbar={<EditModeToolbar editing={editing} setEditing={setEditing} canEdit={!!current && !!firstDefined(current.operator_id, current.id)} />}
      >
        {current ? (
          <>
        <KeyValue rows={[
          ["中文显示名", operatorDisplayName(current)],
          ["operator_id", firstDefined(current.operator_id, current.id)],
          ["operator_type", firstDefined(current.operator_type, current.kind, "-")],
          ["backend", firstDefined(current.backend, current.backend_kind, current.kind, current.adapter, current.adapter_id, "-")],
          ["adapter_id", firstDefined(current.adapter_id, "-")],
          ["lifecycle", lifecycleText(current.lifecycle)],
          ["family", firstDefined(current.family, current.operator_family, "-")],
          ["failure_policy", firstDefined(current.failure_policy, current.on_failure, "-")],
          ["time_policy", timePolicyText(current.time_policy)],
          ["renderer", rendererResolutionText(current.renderer_resolution)],
          ["inputs", fmtNumber(asArray(current.inputs || current.input_ports).length, 0)],
          ["outputs", fmtNumber(asArray(current.outputs || current.output_ports).length, 0)],
          ["used_by_workflows", joinList(current.used_by_workflows)],
        ]} />
        {editing && <OperatorDraftEditor operator={current} reloadAll={reloadAll} editing={editing} />}
        <Panel
          title="连接 / 初始化 / 试算验证"
          subtitle="验证范围限定在当前算子：声明门禁、adapter registry/DLL 路径、resource_refs 初始化前置条件，以及测试向量或试算命令。"
          className="inner-panel operator-check-panel"
        >
          <div className="button-row">
            <Button busy={preflightBusy} onClick={() => runPreflight("preflight")}>声明预检</Button>
            <Button busy={preflightBusy} onClick={() => runPreflight("connect")}>连接后端/DLL</Button>
            <Button busy={preflightBusy} onClick={() => runPreflight("initialize")}>初始化资源</Button>
            <Button busy={preflightBusy} tone="primary" onClick={() => runPreflight("trial")}>试算/测试向量</Button>
            {preflight && <Badge tone={preflight.ok ? "good" : "bad"}>{preflight.ok ? "验证通过" : "验证未通过"}</Badge>}
          </div>
          <div className="muted-note">
            算子初始化发生在 RuntimeHost prepare 阶段：平台按 execution.lifecycle 连接 adapter，并把 resource_refs 解析成初始化上下文。本页只做单算子的连接、资源和试算门禁，不启动整条 workflow。
          </div>
          {preflight && (
            <>
              <KeyValue rows={[
                ["动作", firstDefined(preflight.action, "-")],
                ["adapter_id", firstDefined(preflight.adapter_id, "-")],
                ["后端", firstDefined(preflight.backend, "-")],
                ["registry", shortPath(preflight.adapter && preflight.adapter.registry_path)],
                ["library/path", shortPath(firstDefined(preflight.adapter && preflight.adapter.library, preflight.adapter && preflight.adapter.path, preflight.adapter && preflight.adapter.executable))],
              ]} />
              <DataTable
                rows={asArray(preflight.sections)}
                columns={[
                  { key: "section", title: "验证段", render: (row) => firstDefined(row.title, row.section_id) },
                  { key: "status", title: "状态", render: (row) => <Badge tone={row.ok ? "good" : "bad"}>{row.ok ? "通过" : "未通过"}</Badge> },
                  { key: "issues", title: "诊断数", render: (row) => asArray(row.diagnostics).length },
                  { key: "summary", title: "摘要", render: (row) => joinList(asArray(row.diagnostics).map((item) => item.message), 2) },
                ]}
                emptyHint="还没有执行当前算子的验证。"
              />
              {trialResult && (
                <pre className="json-pre compact">{trialResult.log_tail || "试算命令没有输出。"}</pre>
              )}
            </>
          )}
        </Panel>
        <div className="two-cols">
          <Panel title="输入端口" className="inner-panel">
            <DataTable
              rows={asArray(current.inputs || current.input_ports)}
              columns={[
                { key: "id", title: "port", render: (row) => firstDefined(row.port_id, row.id, row.name) },
                { key: "contract", title: "contract", render: (row) => portContractId(row) },
                { key: "kind", title: "value_kind", render: (row) => firstDefined(row.value_kind, "-") },
                { key: "time", title: "time_role", render: (row) => firstDefined(portTimeRole(row), "-") },
              ]}
            />
          </Panel>
          <Panel title="输出端口" className="inner-panel">
            <DataTable
              rows={asArray(current.outputs || current.output_ports)}
              columns={[
                { key: "id", title: "port", render: (row) => firstDefined(row.port_id, row.id, row.name) },
                { key: "contract", title: "contract", render: (row) => portContractId(row) },
                { key: "kind", title: "value_kind", render: (row) => firstDefined(row.value_kind, "-") },
                { key: "time", title: "time_role", render: (row) => firstDefined(portTimeRole(row), "-") },
              ]}
            />
          </Panel>
        </div>
        <div className="two-cols">
          <Panel title="资源依赖" className="inner-panel">
            <DataTable
              rows={asArray(current.resource_refs).map((id) => ({ id }))}
              columns={[
                { key: "id", title: "resource_id", render: (row) => row.id },
              ]}
            />
          </Panel>
          <Panel title="显示声明 / Renderer 解析" className="inner-panel">
            <KeyValue rows={[
              ["requested_renderer", firstDefined(current.renderer_resolution && current.renderer_resolution.requested_renderer, current.renderer_id, "-")],
              ["resolved_renderer", firstDefined(current.renderer_resolution && current.renderer_resolution.resolved_renderer, "-")],
              ["resolution", firstDefined(current.renderer_resolution && current.renderer_resolution.resolution, "-")],
              ["primary_outputs", joinList(current.display_descriptor && current.display_descriptor.primary_outputs)],
              ["fallback_renderer", firstDefined(current.display_descriptor && current.display_descriptor.fallback_renderer, "-")],
            ]} />
          </Panel>
        </div>
        {preflight && diagnosticsOf(preflight).length > 0 && (
          <Panel title="preflight 诊断" className="inner-panel">
            <DataTable
              rows={diagnosticsOf(preflight)}
              columns={[
                { key: "severity", title: "级别", render: (row) => <Badge tone={row.severity === "blocking" ? "bad" : "neutral"}>{row.severity || "info"}</Badge> },
                { key: "port", title: "port/resource", render: (row) => firstDefined(row.port_id, row.resource_id, "-") },
                { key: "msg", title: "说明", render: (row) => firstDefined(row.message, "-") },
              ]}
            />
          </Panel>
        )}
          </>
        ) : (
          <Empty
            title="请从左侧对象树选择算子"
            hint={`当前对象包共有 ${operators.length} 个算子；本页不再重复显示算子列表，只展示左侧树选中算子的端口、资源、初始化和验证信息。`}
          />
        )}
      </Panel>
      {current && (
        <>
          <PortListPanel ports={ports} activeKey={activePort} onSelect={setActivePort} />
          <PortSchemaPanel port={currentPort} schema={schemaById[currentSchemaId]} />
        </>
      )}
    </div>
  );
}

function WorkflowPage({
  object,
  run,
  status,
  objectLoaded,
  objectValidation,
  selectedWorkflowId,
  setSelectedWorkflowId,
  selectedRunProfileId,
  setSelectedRunProfileId,
  compileResult,
  setCompileResult,
  preflightResult,
  setPreflightResult,
  reloadAll,
  setRun,
}) {
  const workflows = asArray(object && object.workflows);
  const profiles = asArray(object && object.run_profiles);
  const workflowOptions = useMemo(() => {
    if (!selectedWorkflowId || workflows.some((wf) => workflowId(wf) === selectedWorkflowId)) return workflows;
    return [
      {
        workflow_id: selectedWorkflowId,
        id: selectedWorkflowId,
        description: "当前 WebUI 对象草稿中的 workflow。保存草稿后可直接校验、编译、预检和运行。",
        phase: "draft",
        source: "active_object_draft",
      },
      ...workflows,
    ];
  }, [selectedWorkflowId, workflows]);
  const selectedWorkflow = workflowOptions.find((wf) => workflowId(wf) === selectedWorkflowId) || null;
  const selectedProfile = profiles.find((profile) => profileId(profile) === selectedRunProfileId) || null;
  const studioReq = useJson(selectedWorkflowId ? `/api/workflow/studio?id=${encodeURIComponent(selectedWorkflowId)}` : "", [selectedWorkflowId], { enabled: !!selectedWorkflowId });
  const studio = studioReq.data || {};
  const [validation, setValidation] = useState(null);
  const [busy, setBusy] = useState("");
  const [message, setMessage] = useState("");
  const [editing, setEditing] = useState(false);
  const [detailModal, setDetailModal] = useState("");

  async function postJson(url, body) {
    setBusy(url);
    setMessage("");
    try {
      const res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body || {}),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || data.ok === false) throw new Error(data.message || data.error || `${res.status} ${res.statusText}`);
      return data;
    } finally {
      setBusy("");
    }
  }

  async function validateSelected() {
    if (!selectedWorkflowId) {
      setValidation({ ok: false, errors: ["请先选择 workflow"], warnings: [] });
      return;
    }
    try {
      const data = await postJson("/api/workflow/validate", { workflow_id: selectedWorkflowId });
      setValidation(data);
      setMessage(data.ok ? "workflow validate 通过" : "workflow validate 未通过");
    } catch (err) {
      setValidation({ ok: false, errors: [err.message || String(err)], warnings: [] });
    }
  }

  const WorkflowEditorComponent = window.WorkflowEditor;
  const editorWorkflowId = selectedWorkflowId || (workflows[0] && workflowId(workflows[0])) || "";
  const handleWorkflowSaved = useCallback((saved) => {
    const nextWorkflowId = saved && saved.workflow_id;
    if (nextWorkflowId) setSelectedWorkflowId(nextWorkflowId);
    setValidation(saved && saved.validation ? saved.validation : null);
    setCompileResult(null);
    setPreflightResult(null);
    setMessage(`workflow 草稿已保存到对象草稿：${nextWorkflowId || editorWorkflowId}；本页只做结构校验，compile / preflight / 启动请到“运行控制台”。`);
    if (reloadAll) reloadAll();
  }, [editorWorkflowId, reloadAll, setCompileResult, setPreflightResult, setSelectedWorkflowId]);

  const handleWorkflowSelected = useCallback((nextWorkflowId) => {
    setSelectedWorkflowId(nextWorkflowId || "");
    setValidation(null);
    setCompileResult(null);
    setPreflightResult(null);
  }, [setCompileResult, setPreflightResult, setSelectedWorkflowId]);

  const handleWorkflowDeleted = useCallback((deleted) => {
    const nextWorkflowId = deleted && deleted.next_workflow_id;
    setSelectedWorkflowId(nextWorkflowId || "");
    setValidation(null);
    setCompileResult(null);
    setPreflightResult(null);
    setMessage(`workflow 已从对象草稿删除：${(deleted && deleted.workflow_id) || ""}`);
    if (reloadAll) reloadAll();
  }, [reloadAll, setCompileResult, setPreflightResult, setSelectedWorkflowId]);

  const activeValidation = validation || studio.validation || null;
  const validationRows = diagnosticRows(activeValidation);
  const selectedWorkflowPhase = firstDefined(studio.phase, workflowPhase(selectedWorkflow), "-");
  const selectedWorkflowPath = firstDefined(studio.path, selectedWorkflow && selectedWorkflow.spec_file);
  const stageCount = asArray(studio.stages).length || (selectedWorkflow && selectedWorkflow.stage_count) || 0;
  const nodeCount = asArray(studio.nodes).length || (selectedWorkflow && selectedWorkflow.node_count) || 0;

  return (
    <div className="workflow-layout workflow-studio-layout workflow-edit-layout">
      <Panel
        title="图编排工作区"
        subtitle="拖拽算子、连接端口、编辑 stage 输入输出；对象包 workflow 是真源，草稿保存在本地工作区"
        className="workflow-editor-panel"
        bodyClassName="workflow-editor-body"
        toolbar={<EditModeToolbar editing={editing} setEditing={setEditing} canEdit={!!object} />}
      >
        {!object ? (
          <Empty title="未载入对象包" hint="请先在对象载入页选择对象包，随后才能编辑该对象的 workflow。" />
        ) : !WorkflowEditorComponent ? (
          <Empty title="图编辑器未加载" hint="React Flow bundle 未加载，请在 webui 目录运行 npm.cmd run build 后刷新页面。" />
        ) : (
          React.createElement(WorkflowEditorComponent, {
            key: `${object.object_id || "object"}:${editorWorkflowId}`,
            object,
            workflowId: editorWorkflowId,
            onWorkflowSaved: handleWorkflowSaved,
            onWorkflowSelected: handleWorkflowSelected,
            onWorkflowDeleted: handleWorkflowDeleted,
            readOnly: !editing,
          })
        )}
      </Panel>

      <Panel title="工作流主线" subtitle="先选 workflow，在左侧图中编辑；本页只做结构校验，运行相关操作放到运行配置。">
        <div className="control-stack">
          <label className="stack-field">
            <span>workflow</span>
            <select value={selectedWorkflowId || ""} onChange={(e) => {
              setSelectedWorkflowId(e.target.value);
              setValidation(null);
              setCompileResult(null);
              setPreflightResult(null);
            }}>
              <option value="">请选择 workflow</option>
              {workflowOptions.map((wf) => {
                const id = workflowId(wf);
                return <option key={id} value={id}>{id}</option>;
              })}
            </select>
          </label>
          <div className="workflow-main-status">
            <div>
              <span>阶段</span>
              <strong>{selectedWorkflowPhase}</strong>
            </div>
            <div>
              <span>规模</span>
              <strong>{fmtNumber(stageCount, 0)} stage / {fmtNumber(nodeCount, 0)} node</strong>
            </div>
            <div>
              <span>校验</span>
              <strong>{activeValidation ? (activeValidation.ok ? "通过" : "未通过") : "未校验"}</strong>
            </div>
          </div>
          <div className="button-row">
            <Button busy={busy === "/api/workflow/validate"} disabled={!selectedWorkflowId} tone="primary" onClick={validateSelected}>校验 workflow</Button>
            <Button onClick={() => setMessage("请切换到左侧“运行配置”页，选择 workflow + run profile 后再编译和 preflight。")}>去运行配置</Button>
          </div>
          <div className="workflow-secondary-actions">
            <Button disabled={!selectedWorkflowId} onClick={() => setDetailModal("workflow")}>查看 workflow 详情</Button>
            <Button disabled={!activeValidation} onClick={() => setDetailModal("validation")}>查看校验诊断</Button>
            <Button onClick={() => setDetailModal("boundary")}>查看页面边界</Button>
          </div>
          {message && <div className="inline-message">{message}</div>}
        </div>
        <div className="workflow-compact-note">
          <strong>主线</strong>
          <span>对象包 workflow 负责图结构、stage 边界和端口连接；时间、分支、外部输入、编译和启动统一转到运行配置页处理。</span>
        </div>
      </Panel>
      {detailModal === "workflow" && (
        <InfoModal title="当前 workflow 详情" subtitle="只读查看对象包 workflow 摘要；修改请回到左侧图编辑器。" onClose={() => setDetailModal("")}>
          <KeyValue rows={[
            ["workflow_id", selectedWorkflowId || "-"],
            ["phase", selectedWorkflowPhase],
            ["description", firstDefined(studio.description, selectedWorkflow && selectedWorkflow.description, "-")],
            ["source", shortPath(selectedWorkflowPath)],
            ["stage_count", fmtNumber(stageCount, 0)],
            ["node_count", fmtNumber(nodeCount, 0)],
          ]} />
        </InfoModal>
      )}
      {detailModal === "validation" && (
        <InfoModal title="workflow 校验诊断" subtitle="端口、contract、edge、stage 边界等结构问题集中在这里看。" onClose={() => setDetailModal("")}>
          <ValidationSummary validation={activeValidation} />
          {validationRows.length > 0 && (
            <DataTable
              rows={validationRows}
              columns={[
                { key: "severity", title: "级别", render: (row) => <Badge tone={row.severity === "blocking" ? "bad" : "neutral"}>{row.severity || "info"}</Badge> },
                { key: "target", title: "对象", render: (row) => firstDefined(row.node_id, row.stage_id, row.port_id, row.target, "-") },
                { key: "message", title: "说明", render: (row) => firstDefined(row.message, "-") },
              ]}
            />
          )}
        </InfoModal>
      )}
      {detailModal === "boundary" && (
        <InfoModal title="工作流页边界" subtitle="避免把运行控制、编译产物和 workflow 图编辑混到一个页面。" onClose={() => setDetailModal("")}>
          <KeyValue rows={[
            ["本页负责", "选择 workflow、图编辑、stage 输入输出、端口连接、结构校验"],
            ["运行配置负责", "run profile、时间策略、分支策略、外部输入、编译、preflight、启动"],
            ["当前 workflow", selectedWorkflowId || "-"],
            ["当前 run profile", selectedRunProfileId || "-"],
            ["profile source", shortPath(selectedProfile && selectedProfile.path)],
          ]} />
        </InfoModal>
      )}
    </div>
  );

}

function ValidationSummary({ validation }) {
  const rows = diagnosticRows(validation);
  if (!validation) return <Empty title="尚未校验" hint="点击 validate 后显示 workflow contract / edge / port 检查结果。" />;
  return (
    <div className={cls("validation-box", validation.ok ? "ok" : "bad")}>
      <strong>{validation.ok ? "校验通过" : "校验未通过"}</strong>
      <span>{fmtNumber(validation.summary && validation.summary.node_count, 0)} nodes / {fmtNumber(validation.summary && validation.summary.edge_count, 0)} edges</span>
      {rows.length > 0 && (
        <div className="diagnostic-list compact">
          {rows.slice(0, 18).map((row, index) => (
            <div key={index} className={cls("diagnostic-item", row.severity === "blocking" && "bad")}>
              <Badge tone={row.severity === "blocking" ? "bad" : "neutral"}>{row.severity || "info"}</Badge>
              <span>{row.message}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

function RunConfigPage(props) {
  const {
    object,
    objectTreeData,
    objectValidation,
    selectedWorkflowId,
    setSelectedWorkflowId,
    selectedRunProfileId,
    setSelectedRunProfileId,
    compileResult,
    setCompileResult,
    preflightResult,
    setPreflightResult,
  } = props;
  const workflows = asArray(object && object.workflows);
  const profiles = asArray(object && object.run_profiles);
  const validation = (objectTreeData && objectTreeData.validation) || objectValidation || null;
  const selectedWorkflow = workflows.find((wf) => workflowId(wf) === selectedWorkflowId) || null;
  const selectedProfile = profiles.find((profile) => profileId(profile) === selectedRunProfileId) || null;
  const [busy, setBusy] = useState("");
  const [message, setMessage] = useState("");
  const [profileText, setProfileText] = useState("");
  const [saveAsProfileId, setSaveAsProfileId] = useState("");
  const [editing, setEditing] = useState(false);
  const [runtimeConfig, setRuntimeConfig] = useState(() => runtimeLaunchConfigFromProfile(selectedProfile, selectedWorkflow));

  useEffect(() => {
    const nextRuntimeConfig = runtimeLaunchConfigFromProfile(selectedProfile, selectedWorkflow);
    setRuntimeConfig(nextRuntimeConfig);
    setProfileText(selectedProfile ? JSON.stringify(applyRuntimeLaunchToProfile(selectedProfile, nextRuntimeConfig), null, 2) : "");
    setSaveAsProfileId(selectedRunProfileId || "");
  }, [selectedWorkflowId, selectedRunProfileId, selectedProfile && selectedProfile.path, selectedWorkflow && selectedWorkflow.spec_file]);

  async function postJson(url, body) {
    setBusy(url);
    setMessage("");
    try {
      const res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body || {}),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || data.ok === false) throw new Error(data.message || data.error || `${res.status} ${res.statusText}`);
      return data;
    } finally {
      setBusy("");
    }
  }

  async function compileSelected() {
    try {
      const data = await postJson("/api/workflow/compile", {
        workflow_id: selectedWorkflowId,
        run_profile_id: selectedRunProfileId,
      });
      setCompileResult(data);
      setPreflightResult(null);
      setMessage(data.message || "workflow compile 完成");
    } catch (err) {
      const data = { ok: false, message: err.message || String(err) };
      setCompileResult(data);
      setMessage(data.message);
    }
  }

  async function validateObjectPackage() {
    try {
      const data = await postJson("/api/object-project/validate", {});
      setMessage(data.ok ? "对象包校验通过" : "对象包校验未通过");
      if (props.reloadAll) props.reloadAll();
    } catch (err) {
      setMessage(err.message || String(err));
    }
  }

  async function preflightSelected() {
    const compiledDir = firstDefined(compileResult && compileResult.compiled_dir, compileResult && compileResult.plan && compileResult.plan.compiled_dir);
    try {
      const data = await postJson("/api/run/preflight", {
        workflow_id: selectedWorkflowId,
        run_profile_id: selectedRunProfileId,
        compiled_dir: compiledDir,
      });
      setPreflightResult(data);
      setMessage(data.ok ? "preflight 通过" : "preflight 未通过");
    } catch (err) {
      setPreflightResult({ ok: false, diagnostics: [{ severity: "blocking", message: err.message || String(err) }] });
    }
  }

  function createProfileDraftTemplate() {
    if (!editing) {
      setMessage("请先点击编辑，再新建 run profile 草稿。");
      return;
    }
    if (!selectedWorkflowId) {
      setMessage("请先选择 workflow，再新建 run profile");
      return;
    }
    const phase = firstDefined(workflowPhase(selectedWorkflow), "phase");
    const baseId = `${String(selectedWorkflowId).replace(/[^A-Za-z0-9_.-]+/g, "_")}.profile.v1`;
    const nextId = saveAsProfileId || baseId;
    const profile = applyRuntimeLaunchToProfile({
      profile_id: nextId,
      title: `${selectedWorkflowId} 运行配置`,
      workflow_id: selectedWorkflowId,
      workflow_ids: [selectedWorkflowId],
      workflow_phases: [phase],
      description: "由 WebUI 创建的 run profile 草稿；用于声明同一 workflow 的运行时钟、分支、外部输入和启动参数。",
      metadata: {
        source: "webui",
        status: "draft",
      },
    }, runtimeConfig);
    setSaveAsProfileId(nextId);
    setProfileText(JSON.stringify(profile, null, 2));
    setMessage("已生成 run profile 草稿模板；确认参数后点击保存。");
  }

  async function saveProfileDraft() {
    if (!editing) {
      setMessage("请先点击编辑，再保存 run profile 草稿。");
      return;
    }
    if (!selectedRunProfileId && !saveAsProfileId) {
      setMessage("请先选择或填写 run profile id");
      return;
    }
    try {
      let profile = profileText ? JSON.parse(profileText) : {};
      if (selectedWorkflowId) {
        profile.workflow_id = selectedWorkflowId;
        profile.workflow_ids = Array.from(new Set([...(asArray(profile.workflow_ids)), selectedWorkflowId]));
      }
      profile.profile_id = saveAsProfileId || selectedRunProfileId;
      profile = applyRuntimeLaunchToProfile(profile, runtimeConfig);
      const data = await postJson("/api/run-profile/draft", {
        profile_id: selectedRunProfileId || profile.profile_id,
        save_as: profile.profile_id,
        profile,
      });
      setMessage(data.message || "run profile 草稿已保存");
      setSelectedRunProfileId(profile.profile_id);
      if (props.reloadAll) props.reloadAll();
    } catch (err) {
      setMessage(err.message || String(err));
    }
  }

  return (
    <div className="run-config-layout">
      <Panel
        title="运行配置"
        subtitle="作业页统一承接对象校验、PDK 编译、preflight 和 Runtime 启动前配置"
        toolbar={<EditModeToolbar editing={editing} setEditing={setEditing} />}
      >
        <p>run profile 是 workflow 的运行方案：它绑定要运行的 workflow，并保存时钟步长、在线帧数、预测分支、外部输入、checkpoint 和启动参数。同一张 workflow 可以有多个 profile，用于不同运行方式。</p>
        <ReadonlyHint editing={editing} text="当前为查看模式。可以选择 workflow/run profile 并执行校验、编译和预检；点击“编辑”后才能修改运行参数或保存 run profile 草稿。" />
        <div className="control-stack">
          <label className="stack-field">
            <span>workflow</span>
            <select value={selectedWorkflowId || ""} onChange={(e) => {
              setSelectedWorkflowId(e.target.value);
              setCompileResult(null);
              setPreflightResult(null);
            }}>
              <option value="">请选择 workflow</option>
              {workflows.map((wf) => <option key={workflowId(wf)} value={workflowId(wf)}>{workflowId(wf)}</option>)}
            </select>
          </label>
          <label className="stack-field">
            <span>run profile</span>
            <select value={selectedRunProfileId || ""} onChange={(e) => {
              setSelectedRunProfileId(e.target.value);
              setCompileResult(null);
              setPreflightResult(null);
            }}>
              <option value="">请选择 run profile</option>
              {profiles.map((profile) => {
                const id = profileId(profile);
                const allowed = selectedWorkflow ? profileAllowsWorkflow(profile, selectedWorkflow) : true;
                return <option key={id} value={id} disabled={!allowed}>{id}{allowed ? "" : "（phase 不匹配）"}</option>;
              })}
            </select>
          </label>
          <div className="button-row">
            <Button busy={busy === "/api/object-project/validate"} onClick={validateObjectPackage}>对象包校验</Button>
            <Button disabled={!selectedWorkflowId || !selectedRunProfileId} busy={busy === "/api/workflow/compile"} tone="primary" onClick={compileSelected}>编译 workflow</Button>
            <Button disabled={!selectedWorkflowId || !selectedRunProfileId || !(compileResult && compileResult.ok)} busy={busy === "/api/run/preflight"} onClick={preflightSelected}>运行前检查</Button>
          </div>
          {message && <div className="inline-message">{message}</div>}
        </div>
        <KeyValue rows={[
          ["workflow", selectedWorkflowId || "-"],
          ["run profile", selectedRunProfileId || "-"],
          ["profile source", shortPath(selectedProfile && selectedProfile.path)],
          ["对象校验", validation ? (validation.ok ? "已通过" : "未通过") : "未执行"],
          ["compile", compileResult ? (compileResult.ok ? "已通过" : "未通过") : "未执行"],
          ["preflight", preflightResult ? (preflightResult.ok ? "已通过" : "未通过") : "未执行"],
        ]} />
        <ValidationSummary validation={validation} />
      </Panel>

      <Panel title="策略与参数来源" subtitle="clock、backend、外部输入、分支与 checkpoint 策略均来自对象包 workflow/profile">
        <KeyValue rows={[
          ["clock", selectedWorkflow ? "见 workflow 编译结果 / workflow spec" : "-"],
          ["backend", "由 operator spec execution.kind / adapter_id 解析"],
          ["external input", "由 workflow/profile 声明或 RuntimeHost 启动参数提供"],
          ["branch policy", selectedWorkflow ? "由 workflow spec / compiled scheduler plan 提供" : "-"],
          ["checkpoint policy", selectedWorkflow ? "由 workflow spec / state store plan 提供" : "-"],
          ["profile features", joinList(Object.keys((selectedProfile && selectedProfile.features) || {}), 12)],
        ]} />
        {editing && (
          <>
            <RuntimeBranchSettingsEditor workflows={workflows} config={runtimeConfig} setConfig={setRuntimeConfig} compact />
            <div className="phase8-editor">
              <h3>Run Profile 草稿编辑</h3>
            <div className="editor-grid compact">
              <label><span>save as profile_id</span><input value={saveAsProfileId} onChange={(event) => setSaveAsProfileId(event.target.value)} /></label>
              <label><span>绑定 workflow</span><input value={selectedWorkflowId || ""} readOnly /></label>
            </div>
            <textarea className="json-editor" value={profileText} onChange={(event) => setProfileText(event.target.value)} />
            <div className="button-row">
              <Button disabled={!selectedWorkflowId} onClick={createProfileDraftTemplate}>新建 run profile 草稿</Button>
              <Button disabled={!selectedWorkflowId || (!profileText && !saveAsProfileId)} onClick={saveProfileDraft}>保存 profile 到对象草稿</Button>
            </div>
            </div>
          </>
        )}
        <SettingsSourceTable settings={{ settings: [
          { key: "workflow.selected", value: selectedWorkflowId || "-", source: "ui_session", editable: true },
          { key: "run_profile.selected", value: selectedRunProfileId || "-", source: "ui_session", editable: true },
          { key: "compile.output", value: firstDefined(compileResult && compileResult.compiled_dir, "-"), source: "runtime_launch", editable: false, requires_recompile: true },
          { key: "preflight.status", value: preflightResult ? (preflightResult.ok ? "ok" : "failed") : "pending", source: "runtime_launch", editable: false },
        ] }} compact />
      </Panel>

      {!props.embedded && (
        <RunControl
          {...props}
          selectedWorkflowId={selectedWorkflowId}
          selectedRunProfileId={selectedRunProfileId}
          compileResult={compileResult}
          preflightResult={preflightResult}
          runtimeLaunchConfig={runtimeConfig}
          onReload={props.reloadAll}
          onRunStarted={props.setRun}
        />
      )}

      <Panel title="编译计划" subtitle="execution / time / scheduler / data-plane plan 引用">
        <KeyValue rows={planSummaryRows(compileResult)} />
        <DataTable
          rows={planRefRows(compileResult)}
          emptyHint="尚未编译。"
          columns={[
            { key: "id", title: "plan", render: (row) => row.artifact_id },
            { key: "exists", title: "状态", render: (row) => <Badge tone={row.exists ? "good" : "bad"}>{row.exists ? "exists" : "missing"}</Badge> },
            { key: "path", title: "path", render: (row) => shortPath(row.path) },
          ]}
        />
      </Panel>
    </div>
  );
}

function LedgerPage({ dataplane }) {
  const qois = asArray(dataplane && dataplane.qois);
  return (
    <Panel title="状态 / QoI 账本" subtitle="所有摘要值按 evidence 记录，便于回放和审查">
      <DataTable
        rows={qois}
        columns={[
          { key: "id", title: "id", render: (row) => firstDefined(row.qoi_id, row.id, row.port) },
          { key: "branch", title: "branch", render: (row) => firstDefined(row.branch_id, "-") },
          { key: "step", title: "step", render: (row) => firstDefined(row.step, row.step_index, "-") },
          { key: "value", title: "value", render: (row) => fmtNumber(firstDefined(row.value, row.mean, row.max, row.min)) },
          { key: "artifact", title: "artifact", render: (row) => shortPath(firstDefined(row.path, row.artifact_path, row.uri)) },
        ]}
      />
    </Panel>
  );
}

function ReplayPage(props) {
  return (
    <div className="replay-layout">
      <Panel title="历史 run" subtitle="选择 run package 后，UI 只读 evidence 与数据平面">
        <ModeBanner mode="replay" status={props.status} run={props.run} />
        <RunPicker runs={props.runs} value={props.run} onChange={props.setRun} />
        <KeyValue rows={[
          ["run", props.run],
          ["workflow", firstDefined(props.timeline && props.timeline.workflow_id, "-")],
          ["fields", fmtNumber(props.dataplane && props.dataplane.field_count, 0)],
          ["qoi", fmtNumber(props.dataplane && props.dataplane.qoi_count, 0)],
        ]} />
      </Panel>
      <BranchExplorer timeline={props.timeline} dataplane={props.dataplane} selected={props.selected} onSelect={props.setSelected} />
      <FieldGalleryV2 run={props.run} dataplane={props.dataplane} selected={props.selected} onSelect={props.setSelected} title="历史场回放" />
    </div>
  );
}

function SettingsSourceTable({ settings, compact = false }) {
  const rows = asArray(settings && settings.settings);
  if (!rows.length) {
    return <Empty title="尚无设置源" hint="等待 WebUI 后端返回 workspace / object package / UI session 设置视图。" />;
  }
  return (
    <DataTable
      rows={rows}
      columns={[
        { key: "key", title: "设置项", render: (row) => row.key },
        { key: "value", title: "当前值", render: (row) => shortPath(formatJsonValue(row.value)) },
        { key: "source", title: "来源", render: (row) => <Badge tone={row.source === "object_package" ? "live" : "neutral"}>{row.source || "-"}</Badge> },
        { key: "editable", title: "可编辑", render: (row) => row.editable ? "是" : "否" },
        ...(compact ? [] : [
          { key: "scope", title: "作用域", render: (row) => row.scope || "-" },
          { key: "restart", title: "变更影响", render: (row) => [
            row.requires_recompile ? "需重新编译" : "",
            row.requires_reinitialize ? "需重新初始化" : "",
          ].filter(Boolean).join(" / ") || "即时视图设置" },
        ]),
      ]}
    />
  );
}

function ConfigPage({ object, runs, workspace, settings }) {
  return (
    <div className="page-grid">
      <Panel title="系统设置底座" subtitle="设置来源分为 workspace、object package、UI session；Phase 1 先固化来源、权限和影响范围">
        <KeyValue rows={[
          ["工作空间", shortPath(workspace && workspace.workspace_root)],
          ["对象包", shortPath(objectPackageRoot(workspace, object))],
          ["对象载入", isObjectLoaded(workspace, object) ? "已载入" : "未载入"],
          ["run 根目录", shortPath(workspace && workspace.artifacts_root)],
          ["发现 run", fmtNumber(asArray(runs).length, 0)],
          ["后端", "flightenv-controller-ui/webui/server.py"],
        ]} />
      </Panel>
      <Panel title="设置来源">
        <SettingsSourceTable settings={settings} />
      </Panel>
      <Panel title="设计边界">
        <ul className="note-list">
          <li>平台 UI 只读取对象包、workflow、runtime snapshot、data-plane artifact。</li>
          <li>对象语义在对象包和算子包声明，WebUI 不内置对象专属规则。</li>
          <li>三维视图只消费 geometry 与 field values，不生成伪造数据。</li>
          <li>启动运行通过对象包工具脚本进入 RuntimeHost，不绕过平台内核。</li>
        </ul>
      </Panel>
    </div>
  );
}

function DiagnosticsPage({ runtime, timeline, dataplane, object }) {
  const checks = [
    ["对象包", !!(object && (object.object_id || object.name)), shortPath(object && object.package_dir)],
    ["runtime", !!(runtime && Object.keys(runtime).length), firstDefined(runtime && runtime.status, "-")],
    ["timeline", asArray(timeline && timeline.branch_steps).length > 0 || asArray(timeline && timeline.online_frames).length > 0, `${asArray(timeline && timeline.branch_steps).length} steps`],
    ["data-plane", Number(dataplane && dataplane.field_count) > 0, `${fmtNumber(dataplane && dataplane.field_count, 0)} fields`],
    ["QoI", Number(dataplane && dataplane.qoi_count) >= 0, `${fmtNumber(dataplane && dataplane.qoi_count, 0)} qoi`],
  ];
  return (
    <Panel title="诊断报告" subtitle="前端按真实 API 可见性做轻量检查">
      <DataTable
        rows={checks.map(([name, ok, detail]) => ({ name, ok, detail }))}
        columns={[
          { key: "name", title: "项", render: (row) => row.name },
          { key: "ok", title: "状态", render: (row) => <Badge tone={row.ok ? "good" : "bad"}>{row.ok ? "通过" : "待检查"}</Badge> },
          { key: "detail", title: "说明", render: (row) => row.detail },
        ]}
      />
    </Panel>
  );
}

function ReplayPagePhase7(props) {
  const { run, runs, setRun, status, evidence, timeline, runtime, dataplane, selected, setSelected } = props;
  const traceTargets = asArray(evidence && evidence.trace_targets);
  const [activeTraceId, setActiveTraceId] = useState("");
  const [exporting, setExporting] = useState(false);
  const [exportResult, setExportResult] = useState(null);
  const activeTrace = traceTargets.find((row) => row.artifact_id === activeTraceId) || traceTargets[0] || null;
  const snapshotRows = asArray(evidence && evidence.snapshot_files);
  const eventRows = asArray(evidence && evidence.runtime_events);

  async function exportEvidence() {
    if (!run) return;
    setExporting(true);
    setExportResult(null);
    try {
      const res = await fetch("/api/evidence/export", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ run }),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || data.ok === false) throw new Error(data.message || data.error || `${res.status} ${res.statusText}`);
      setExportResult(data);
    } catch (err) {
      setExportResult({ ok: false, message: err.message || String(err) });
    } finally {
      setExporting(false);
    }
  }

  return (
    <div className="evidence-layout">
      <Panel title="Evidence 回放与审计" subtitle="历史 run context、snapshot、事件、artifact 索引和导出">
        <ModeBanner mode="replay" status={status} run={run} />
        <RunPicker runs={runs} value={run} onChange={setRun} />
        <div className="button-row">
          <Button disabled={!run} busy={exporting} tone="primary" onClick={exportEvidence}>导出 evidence bundle</Button>
          {exportResult && <Badge tone={exportResult.ok ? "good" : "bad"}>{exportResult.ok ? "导出完成" : "导出失败"}</Badge>}
        </div>
        {exportResult && (
          <KeyValue rows={[
            ["bundle", shortPath(exportResult.bundle || exportResult.bundle_path)],
            ["message", exportResult.message || "-"],
          ]} />
        )}
        <KeyValue rows={[
          ["run", run || "-"],
          ["workflow", firstDefined(evidence && evidence.runtime && evidence.runtime.workflow_id, timeline && timeline.workflow_id, "-")],
          ["runtime", firstDefined(runtime && runtime.status, "-")],
          ["backend", firstDefined(runtime && runtime.backend, "-")],
          ["events", fmtNumber(eventRows.length, 0)],
          ["artifacts", `${fmtNumber(dataplane && dataplane.field_count, 0)} fields / ${fmtNumber(dataplane && dataplane.qoi_count, 0)} QoI`],
        ]} />
      </Panel>

      <Panel title="Snapshot 清单" subtitle="workflow / resource / model / operator / runtime evidence 文件">
        <DataTable
          rows={snapshotRows}
          emptyHint="当前 run 没有可读 snapshot 文件。"
          columns={[
            { key: "name", title: "file", render: (row) => row.name },
            { key: "exists", title: "status", render: (row) => <Badge tone={row.exists ? "good" : "bad"}>{row.exists ? "存在" : "缺失"}</Badge> },
            { key: "records", title: "records", render: (row) => fmtNumber(row.record_count, 0) },
            { key: "size", title: "size", render: (row) => fmtNumber(row.size_bytes, 0) },
            { key: "path", title: "path", render: (row) => shortPath(row.path) },
          ]}
        />
      </Panel>

      <Panel title="Trace 追溯" subtitle="artifact -> producer operator -> workflow node -> resource lock -> runtime event">
        <div className="trace-layout">
          <DataTable
            rows={traceTargets.slice(0, 160)}
            emptyHint="当前 run 没有可追溯输出。"
            rowKey={(row) => `${row.artifact_id}-${row.branch_id}-${row.step}`}
            columns={[
              { key: "artifact", title: "artifact", render: (row) => (
                <button className={cls("link-btn", activeTrace && activeTrace.artifact_id === row.artifact_id && "active")} onClick={() => setActiveTraceId(row.artifact_id)}>
                  {shortPath(row.artifact_id)}
                </button>
              ) },
              { key: "branch", title: "branch", render: (row) => firstDefined(row.branch_id, "-") },
              { key: "step", title: "step", render: (row) => firstDefined(row.step, "-") },
              { key: "producer", title: "producer", render: (row) => shortPath(row.producer_operator) },
            ]}
          />
          {activeTrace && (
            <div className="trace-detail-card">
              <h3>追溯详情</h3>
              <KeyValue rows={[
                ["artifact", shortPath(activeTrace.artifact_id)],
                ["port", portDisplay(activeTrace.port_id)],
                ["producer operator", shortPath(activeTrace.producer_operator)],
                ["workflow node", shortPath(activeTrace.workflow_node)],
                ["resource lock", activeTrace.resource_lock],
                ["runtime events", fmtNumber(activeTrace.runtime_event_count, 0)],
                ["artifact uri", shortPath(activeTrace.artifact_uri)],
              ]} />
            </div>
          )}
        </div>
      </Panel>

      <Panel title="历史时间线" subtitle="run package 恢复后可查看主线、分支和回放时间点">
        <BranchExplorer timeline={timeline} runtime={runtime} dataplane={dataplane} selected={selected} onSelect={setSelected} />
      </Panel>
      <FieldGalleryV2 run={run} dataplane={dataplane} selected={selected} onSelect={setSelected} title="历史场云图" />

      <Panel title="Runtime Events" subtitle="运行事件、失败阶段和审计线索">
        <DataTable
          rows={eventRows.slice(-160)}
          emptyHint="当前 run 没有 runtime event。"
          columns={[
            { key: "time", title: "time", render: (row) => fmtTime(firstDefined(row.time_s, row.public_time_s, row.source_time_s)) },
            { key: "kind", title: "kind", render: (row) => firstDefined(row.kind, row.event_kind, row.type, "-") },
            { key: "target", title: "target", render: (row) => shortPath(firstDefined(row.node_id, row.branch_id, row.port_id, "-")) },
            { key: "message", title: "message", render: (row) => firstDefined(row.message, row.summary, row.status, "-") },
          ]}
        />
      </Panel>
    </div>
  );
}

function DiagnosticsPagePhase7({ runtime, timeline, dataplane, object, diagnostics }) {
  const rows = asArray(diagnostics && diagnostics.diagnostics);
  const summary = (diagnostics && diagnostics.summary) || {};
  const categories = groupBy(rows, (row) => row.category);
  return (
    <div className="diagnostics-layout">
      <Panel title="诊断中心" subtitle="按对象包、资源、算子、workflow、runtime、DataPlane、renderer 分类">
        <div className="metric-grid tight">
          <MetricCard label="blocking" value={fmtNumber(summary.blocking, 0)} tone={summary.blocking ? "bad" : "green"} />
          <MetricCard label="warning" value={fmtNumber(summary.warning, 0)} tone={summary.warning ? "amber" : "green"} />
          <MetricCard label="total" value={fmtNumber(summary.total, 0)} />
        </div>
        <DataTable
          rows={rows}
          emptyHint="当前没有诊断项。"
          columns={[
            { key: "category", title: "category", render: (row) => <Badge tone={row.severity === "blocking" ? "bad" : "neutral"}>{row.category}</Badge> },
            { key: "severity", title: "severity", render: (row) => row.severity },
            { key: "message", title: "message", render: (row) => row.message },
            { key: "target", title: "target", render: (row) => shortPath(firstDefined(row.target, row.code, "-")) },
          ]}
        />
      </Panel>

      <Panel title="分类摘要" subtitle="失败定位必须落到 resource / port / adapter / runtime / renderer 等类别">
        <DataTable
          rows={Object.entries(categories).map(([category, items]) => ({
            category,
            total: items.length,
            blocking: items.filter((item) => item.severity === "blocking").length,
            warning: items.filter((item) => item.severity === "warning").length,
          }))}
          emptyHint="暂无分类诊断。"
          columns={[
            { key: "category", title: "category" },
            { key: "blocking", title: "blocking", render: (row) => fmtNumber(row.blocking, 0) },
            { key: "warning", title: "warning", render: (row) => fmtNumber(row.warning, 0) },
            { key: "total", title: "total", render: (row) => fmtNumber(row.total, 0) },
          ]}
        />
      </Panel>

      <Panel title="运行与数据平面状态" subtitle="辅助定位运行失败阶段、事件、artifact 与 renderer 问题">
        <KeyValue rows={[
          ["object", firstDefined(object && object.object_id, object && object.name, "-")],
          ["runtime", firstDefined(runtime && runtime.status, "-")],
          ["backend", firstDefined(runtime && runtime.backend, "-")],
          ["current stage", firstDefined(runtime && runtime.blocking && runtime.blocking.current_stage, runtime && runtime.scheduler && runtime.scheduler.stage, "-")],
          ["timeline", `${fmtNumber(asArray(timeline && timeline.online_frames).length, 0)} online / ${fmtNumber(asArray(timeline && timeline.branch_steps).length, 0)} branch steps`],
          ["data-plane", `${fmtNumber(dataplane && dataplane.field_count, 0)} fields / ${fmtNumber(dataplane && dataplane.qoi_count, 0)} QoI`],
        ]} />
      </Panel>
    </div>
  );
}

function LoadingMask({ loading, error }) {
  if (!loading && !error) return null;
  const message = String(error || "");
  const backendDown = /Failed to fetch|NetworkError|Load failed/i.test(message);
  return (
    <div className="loading-mask">
      {loading && <span>加载平台数据...</span>}
      {error && backendDown && (
        <strong>WebUI 后端未连接。请在 PowerShell 中进入 F:\code\FlightEnvMultiRepo\flightenv-controller-ui\webui 后执行 python server.py。</strong>
      )}
      {error && !backendDown && <strong>{error}</strong>}
    </div>
  );
}

function RunConsolePage(props) {
  // 运行态唯一页：把“选择/编译/preflight/参数”与“prepare/start/stop/进度”合到一个控制台，
  // 消除原“运行配置 vs 运行控制台”双入口与跨页隐式依赖（compile/preflight 结果就在 start 上方产生）。
  return (
    <div className="run-console-layout">
      <div className="run-console-section">
        <div className="run-console-section-head">
          <h2>1 · 配置与编译</h2>
          <p>选择 workflow 与 run profile，设置时钟 / 帧数 / 分支参数，编译并通过 preflight。</p>
        </div>
        <RunConfigPage {...props} embedded />
      </div>
      <div className="run-console-section">
        <div className="run-console-section-head">
          <h2>2 · 启动与运行控制</h2>
          <p>prepare 初始化、start 启动、停止 / 暂停 / 检查点；分支树、场云图与 QoI 在“运行检视器”查看。</p>
        </div>
        <RuntimeHostPage {...props} />
      </div>
    </div>
  );
}

const PAGE_BOUNDARIES = {
  workspace: ["对象载入", "载入、卸载、校验对象包与草稿", "不编辑 workflow，不启动运行"],
  overview: ["平台总览", "查看当前对象、资源、workflow、run 的入口摘要", "不承载深度编辑和实时检视"],
  config: ["配置与数据源", "查看 workspace、对象包根目录、run 根目录和设置来源", "不修改对象语义"],
  object: ["对象画像", "查看对象、组件、资源和数据结构等对象声明", "不展示某次运行结果"],
  modeler: ["对象包建模器", "新建/读取/修改/保存对象包定义与草稿", "不执行校验、编译、运行，不展示运行结果"],
  resources: ["资源与模型", "维护对象资源、模型资产、版本与启用状态", "不编排运行流程"],
  operators: ["算子库", "查看/编辑算子 spec、端口、后端与 display descriptor", "不直接调用模型"],
  workflow: ["工作流编排", "新建、删除、编辑 workflow 算子图并做结构校验", "不设置时间/分支，不启动运行"],
  runtime: ["运行控制台", "选择 workflow/profile，校验对象包、编译、preflight、初始化、启动、停止", "不展示分支云图和历史审计"],
  inspector: ["运行检视器", "按 run × branch × step 查看场云图、数据平面、运行时、QoI、evidence", "不修改对象包，不启动运行"],
  diagnostics: ["诊断报告", "汇总对象包、runtime、data-plane、renderer 的检查结果", "不承载业务操作"],
};

function SurfaceBoundary({ page }) {
  const boundary = PAGE_BOUNDARIES[LEGACY_PAGE_ALIASES[page] || page];
  if (!boundary) return null;
  return (
    <div className="surface-boundary">
      <strong>{boundary[0]}</strong>
      <span>本页负责：{boundary[1]}</span>
      <em>边界：{boundary[2]}</em>
    </div>
  );
}

function modulePageForTreeNode(node) {
  const kind = node && (node.entity_kind || node.kind);
  if (kind === "resource" || kind === "asset_group") return "resources";
  if (kind === "operator") return "operators";
  if (kind === "workflow") return "workflow";
  if (kind === "run_profile" || kind === "runtime_profile" || kind === "compiled_workflow") return "runtime";
  if (kind === "run_package") return "inspector";
  if (kind === "project" || kind === "twin_object" || kind === "component" || kind === "domain_schema") return "modeler";
  return "modeler";
}

function flattenObjectTree(nodes, depth = 0, out = []) {
  asArray(nodes).forEach((node) => {
    out.push({ ...node, depth });
    flattenObjectTree(node.children, depth + 1, out);
  });
  return out;
}

function isObjectTreeUserHidden(node) {
  const kind = node && (node.entity_kind || node.kind);
  const id = String(firstDefined(node && node.id, node && node.entity_id, node && node.rel_path, "")).toLowerCase();
  return kind === "test_vector" || id === "test_vectors" || id.includes("/test_vectors/") || id.includes("\\test_vectors\\");
}

function filterObjectTreeForWorkbench(nodes) {
  return asArray(nodes)
    .filter((node) => !isObjectTreeUserHidden(node))
    .map((node) => ({ ...node, children: filterObjectTreeForWorkbench(node.children) }));
}

function firstEditableNode(nodes) {
  const flat = flattenObjectTree(nodes);
  return flat.find((node) => node.editable) || flat[0] || null;
}

function entityKindLabel(kind) {
  const labels = {
    project: "对象工程",
    twin_object: "对象清单",
    component: "组件",
    domain_schema: "数据结构",
    resource: "资源",
    asset_group: "资源分组",
    operator: "算子",
    workflow: "Workflow",
    run_profile: "运行配置",
    runtime_profile: "Runtime Profile",
    compiled_workflow: "编译产物",
    run_package: "运行结果",
    folder: "目录",
  };
  return labels[kind] || kind || "-";
}

function objectEntityTemplate(kind, id) {
  const entityId = String(id || `${kind}.draft.v1`).trim();
  if (kind === "component") {
    return {
      component_id: entityId,
      display_name: entityId,
      resource_ids: [],
      schema_ids: [],
      operator_ids: [],
      workflow_ids: [],
    };
  }
  if (kind === "resource") {
    return {
      resource_id: entityId,
      resource_type: "resource",
      uri: `catalog://flightenv/resource/${entityId}`,
      description: "",
    };
  }
  if (kind === "asset_group") {
    return {
      group_id: entityId,
      display_name: entityId,
      resources: [],
    };
  }
  if (kind === "domain_schema") {
    return {
      schema_id: entityId,
      schema_version: "flightenv.domain_schema.v1",
      display_name: entityId,
      component_id: "",
      quantity: entityId,
      unit: "",
      value_kind: "",
      layout_ref: "",
      artifact_policy: "",
      schema_role: "",
      fields: [],
      uncertainty_policy: {
        supported: true,
        representation: "optional_uncertainty_block",
      },
    };
  }
  if (kind === "operator") {
    return {
      operator_id: entityId,
      schema_version: "flightenv.operator.atomic.v1",
      display_name: entityId,
      operator_kind: "atomic",
      operator_family: "",
      execution: {
        kind: "dll_adapter.v1",
        adapter_id: entityId,
        lifecycle: ["initialize", "execute", "finalize"],
      },
      inputs: [],
      outputs: [],
      typed_io_contract: {
        status: "generated",
        operator_id: entityId,
        input_dto: "",
        output_dto: "",
        run_fn_type: "",
        codegen_ref: "_local_artifacts/platform-pdk/codegen/generated_manifest.json",
        json_operator_io_forbidden: true,
      },
      time_policy: { kind: "event_driven" },
      scheduler_policy: { execution_mode: "parallel_ready", deadline_s: 2, timeout_s: 5, retry_count: 0 },
      failure_policy: { on_error: "fail_fast" },
      checkpoint_responsibility: "stateless",
      test_vectors: { status: "draft", cases: [] },
    };
  }
  if (kind === "workflow") {
    return {
      workflow_id: entityId,
      schema_version: "flightenv.workflow.v1",
      phase: "draft",
      description: "由对象包建模器创建的 workflow 草稿。",
      clock: { public_period_s: 1.0 },
      phases: [{ phase_id: "main", stages: [], stage_edges: [] }],
    };
  }
  if (kind === "run_profile") {
    return {
      profile_id: entityId,
      schema_version: "flightenv.platform.run_profile.v1",
      title: entityId,
      workflow_ids: [],
      workflow_phases: [],
      default_feature_enabled: true,
      features: {},
      runtime_launch: {
        online_frames: 50,
        prediction_every_frames: 30,
        future_max_iterations: 0,
        branch_chunk_iterations: 1,
        replay_by_platform_clock: true,
      },
    };
  }
  if (kind === "runtime_profile") {
    return {
      schema_version: "flightenv.object.platform_runtime_profile.v1",
      description: "Object-owned runtime/UI profile.",
      workflow_roles: [],
      branch_templates: {},
      field_display_roles: [],
      health_ledger: {},
      termination_policy: {},
    };
  }
  return { id: entityId, schema_version: "flightenv.object_entity.v1" };
}

function parseJsonObject(text) {
  try {
    const parsed = JSON.parse(text || "{}");
    return parsed && typeof parsed === "object" && !Array.isArray(parsed) ? parsed : {};
  } catch (_err) {
    return {};
  }
}

function csvToArray(text) {
  return String(text || "")
    .split(",")
    .map((item) => item.trim())
    .filter(Boolean);
}

function arrayToCsv(value) {
  return asArray(value).join(", ");
}

function updateDocField(doc, key, value) {
  const next = { ...(doc || {}) };
  if (value === undefined || value === null) next[key] = "";
  else next[key] = value;
  return next;
}

function updateDocArrayField(doc, key, text) {
  return updateDocField(doc, key, csvToArray(text));
}

function updateDocObjectField(doc, key, value) {
  return updateDocField(doc, key, { ...((doc || {})[key] || {}), ...(value || {}) });
}

function JsonBlurEditor({ label, value, fallback, onChange, rows = 5 }) {
  const [text, setText] = useState(() => JSON.stringify(value ?? fallback ?? {}, null, 2));
  const [error, setError] = useState("");
  useEffect(() => {
    setText(JSON.stringify(value ?? fallback ?? {}, null, 2));
    setError("");
  }, [JSON.stringify(value ?? fallback ?? {})]);
  return (
    <label className="span-2 json-blur-field">
      <span>{label}</span>
      <textarea
        rows={rows}
        value={text}
        onChange={(event) => {
          setText(event.target.value);
          setError("");
        }}
        onBlur={() => {
          try {
            onChange(JSON.parse(text || "null"));
            setError("");
          } catch (err) {
            setError(err.message || String(err));
          }
        }}
      />
      {error && <em className="field-error">JSON 解析失败：{error}</em>}
    </label>
  );
}

function FeatureFlagsEditor({ features, onChange }) {
  const entries = Object.entries((features && typeof features === "object" && !Array.isArray(features)) ? features : {});
  const updateKey = (oldKey, nextKey) => {
    const next = {};
    entries.forEach(([key, value]) => {
      next[key === oldKey ? nextKey : key] = Boolean(value);
    });
    onChange(next);
  };
  const updateValue = (key, value) => onChange({ ...Object.fromEntries(entries), [key]: value });
  return (
    <div className="span-2 feature-flags-editor">
      <div className="operator-port-editor-head">
        <strong>features</strong>
        <Button onClick={() => onChange({ ...Object.fromEntries(entries), new_feature: true })}>新增开关</Button>
      </div>
      {entries.length === 0 ? (
        <div className="muted-note">未声明 feature 开关；默认按 default_feature_enabled 处理。</div>
      ) : entries.map(([key, value]) => (
        <div className="editor-grid compact" key={key}>
          <label><span>feature</span><input value={key} onChange={(event) => updateKey(key, event.target.value)} /></label>
          <label><span>enabled</span>
            <select value={String(Boolean(value))} onChange={(event) => updateValue(key, event.target.value === "true")}>
              <option value="true">true</option>
              <option value="false">false</option>
            </select>
          </label>
          <Button tone="danger" onClick={() => {
            const next = Object.fromEntries(entries.filter(([itemKey]) => itemKey !== key));
            onChange(next);
          }}>删除</Button>
        </div>
      ))}
    </div>
  );
}

function RunProfileStructuredEditor({ doc, object, onChange }) {
  const workflows = asArray(object && object.workflows);
  const selectedWorkflowId = firstDefined(doc.workflow_id, asArray(doc.workflow_ids)[0], "");
  const selectedWorkflow = workflows.find((wf) => workflowId(wf) === selectedWorkflowId) || null;
  const runtimeConfig = runtimeLaunchConfigFromProfile(doc, selectedWorkflow);
  const setField = (key, value) => onChange(updateDocField(doc, key, value));
  const setArray = (key, value) => onChange(updateDocArrayField(doc, key, value));
  const setRuntimeConfig = (updater) => {
    const nextConfig = typeof updater === "function" ? updater(runtimeConfig) : updater;
    onChange(applyRuntimeLaunchToProfile(doc, nextConfig));
  };
  return (
    <div className="structured-editor">
      <h3>Run Profile</h3>
      <p className="muted-note">Run Profile 声明同一对象/Workflow 的运行方案：它控制 feature 剪枝、允许的 workflow phase、在线帧数、分支触发和启动参数。</p>
      <div className="editor-grid compact">
        <label><span>profile_id</span><input value={firstDefined(doc.profile_id, "")} onChange={(e) => setField("profile_id", e.target.value)} /></label>
        <label><span>schema_version</span><input value={firstDefined(doc.schema_version, "flightenv.platform.run_profile.v1")} onChange={(e) => setField("schema_version", e.target.value)} /></label>
        <label className="span-2"><span>title</span><input value={firstDefined(doc.title, "")} onChange={(e) => setField("title", e.target.value)} /></label>
        <label className="span-2"><span>description</span><input value={firstDefined(doc.description, "")} onChange={(e) => setField("description", e.target.value)} /></label>
        <label><span>default workflow</span>
          <select value={selectedWorkflowId} onChange={(e) => {
            const nextId = e.target.value;
            const workflowIds = Array.from(new Set([nextId, ...asArray(doc.workflow_ids)].filter(Boolean)));
            onChange({ ...doc, workflow_id: nextId, workflow_ids: workflowIds });
          }}>
            <option value="">未绑定</option>
            {workflows.map((wf) => <option key={workflowId(wf)} value={workflowId(wf)}>{workflowId(wf)}</option>)}
          </select>
        </label>
        <label><span>default_feature_enabled</span>
          <select value={String(doc.default_feature_enabled !== false)} onChange={(e) => setField("default_feature_enabled", e.target.value === "true")}>
            <option value="true">true</option>
            <option value="false">false</option>
          </select>
        </label>
        <label className="span-2"><span>workflow_ids</span><input value={arrayToCsv(doc.workflow_ids)} onChange={(e) => setArray("workflow_ids", e.target.value)} /></label>
        <label className="span-2"><span>workflow_phases</span><input value={arrayToCsv(doc.workflow_phases)} onChange={(e) => setArray("workflow_phases", e.target.value)} /></label>
        <FeatureFlagsEditor features={doc.features} onChange={(features) => setField("features", features)} />
        <JsonBlurEditor label="termination_policy" value={doc.termination_policy || {}} fallback={{}} onChange={(value) => setField("termination_policy", value || {})} rows={4} />
      </div>
      <RuntimeBranchSettingsEditor workflows={workflows} config={runtimeConfig} setConfig={setRuntimeConfig} compact />
    </div>
  );
}

function RuntimeProfileStructuredEditor({ doc, object, onChange }) {
  const workflows = asArray(object && object.workflows);
  const profiles = asArray(object && object.run_profiles);
  const schemas = asArray(object && object.domain_schemas);
  const workflowIds = workflows.map((wf) => workflowId(wf)).filter(Boolean);
  const profileIds = profiles.map((profile) => profileId(profile)).filter(Boolean);
  const schemaIds = schemas.map((schema) => firstDefined(schema.schema_id, schema.id)).filter(Boolean);
  const workflowRoles = asArray(doc.workflow_roles);
  const fieldRoles = asArray(doc.field_display_roles);
  const setField = (key, value) => onChange(updateDocField(doc, key, value));
  const updateWorkflowRole = (index, patch) => {
    const next = workflowRoles.map((row, i) => (i === index ? { ...(row || {}), ...(patch || {}) } : row));
    setField("workflow_roles", next);
  };
  const updateFieldRole = (index, patch) => {
    const next = fieldRoles.map((row, i) => (i === index ? { ...(row || {}), ...(patch || {}) } : row));
    setField("field_display_roles", next);
  };
  return (
    <div className="structured-editor">
      <h3>Runtime Profile</h3>
      <p className="muted-note">Runtime Profile 是对象包给运行台和 UI 的对象侧说明：声明 workflow 角色、分支模板、显示角色、账本和终止策略。</p>
      <div className="editor-grid compact">
        <label className="span-2"><span>schema_version</span><input value={firstDefined(doc.schema_version, "flightenv.object.platform_runtime_profile.v1")} onChange={(e) => setField("schema_version", e.target.value)} /></label>
        <label className="span-2"><span>description</span><input value={firstDefined(doc.description, "")} onChange={(e) => setField("description", e.target.value)} /></label>
      </div>

      <div className="operator-port-editor">
        <div className="operator-port-editor-head">
          <strong>workflow_roles</strong>
          <Button onClick={() => setField("workflow_roles", [...workflowRoles, {
            role_id: "role",
            workflow_id: workflowIds[0] || "",
            default_run_profile_id: profileIds[0] || "",
            phase: "",
          }])}>新增角色</Button>
        </div>
        {workflowRoles.map((role, index) => (
          <div className="editor-grid compact" key={index}>
            <label><span>role_id</span><input value={firstDefined(role.role_id, role.id, "")} onChange={(e) => updateWorkflowRole(index, { role_id: e.target.value })} /></label>
            <label><span>workflow_id</span>
              <select value={firstDefined(role.workflow_id, "")} onChange={(e) => updateWorkflowRole(index, { workflow_id: e.target.value })}>
                <option value="">选择 workflow</option>
                {workflowIds.map((id) => <option key={id} value={id}>{id}</option>)}
              </select>
            </label>
            <label><span>run_profile</span>
              <select value={firstDefined(role.default_run_profile_id, role.run_profile_id, "")} onChange={(e) => updateWorkflowRole(index, { default_run_profile_id: e.target.value })}>
                <option value="">选择 run profile</option>
                {profileIds.map((id) => <option key={id} value={id}>{id}</option>)}
              </select>
            </label>
            <label><span>phase</span><input value={firstDefined(role.phase, "")} onChange={(e) => updateWorkflowRole(index, { phase: e.target.value })} /></label>
            <Button tone="danger" onClick={() => setField("workflow_roles", workflowRoles.filter((_row, i) => i !== index))}>删除角色</Button>
          </div>
        ))}
      </div>

      <div className="operator-port-editor">
        <div className="operator-port-editor-head">
          <strong>field_display_roles</strong>
          <Button onClick={() => setField("field_display_roles", [...fieldRoles, {
            role_id: "field.preview",
            contract_id: schemaIds[0] || "",
            workflow_id: workflowIds[0] || "",
            renderer_id: "field.surface.v1",
          }])}>新增显示角色</Button>
        </div>
        {fieldRoles.map((role, index) => (
          <div className="editor-grid compact" key={index}>
            <label><span>role_id</span><input value={firstDefined(role.role_id, role.id, "")} onChange={(e) => updateFieldRole(index, { role_id: e.target.value })} /></label>
            <label><span>contract_id</span>
              <select value={firstDefined(role.contract_id, role.schema_id, "")} onChange={(e) => updateFieldRole(index, { contract_id: e.target.value })}>
                <option value="">选择 contract/schema</option>
                {schemaIds.map((id) => <option key={id} value={id}>{id}</option>)}
              </select>
            </label>
            <label><span>workflow_id</span>
              <select value={firstDefined(role.workflow_id, "")} onChange={(e) => updateFieldRole(index, { workflow_id: e.target.value })}>
                <option value="">不绑定 workflow</option>
                {workflowIds.map((id) => <option key={id} value={id}>{id}</option>)}
              </select>
            </label>
            <label><span>renderer_id</span><input value={firstDefined(role.renderer_id, "")} onChange={(e) => updateFieldRole(index, { renderer_id: e.target.value })} /></label>
            <label className="span-2"><span>label</span><input value={firstDefined(role.label, role.display_name, "")} onChange={(e) => updateFieldRole(index, { label: e.target.value })} /></label>
            <Button tone="danger" onClick={() => setField("field_display_roles", fieldRoles.filter((_row, i) => i !== index))}>删除显示角色</Button>
          </div>
        ))}
      </div>

      <div className="editor-grid compact">
        <JsonBlurEditor label="branch_templates" value={doc.branch_templates || {}} fallback={{}} onChange={(value) => setField("branch_templates", value || {})} rows={5} />
        <JsonBlurEditor label="health_ledger" value={doc.health_ledger || {}} fallback={{}} onChange={(value) => setField("health_ledger", value || {})} rows={5} />
        <JsonBlurEditor label="termination_policy" value={doc.termination_policy || {}} fallback={{}} onChange={(value) => setField("termination_policy", value || {})} rows={4} />
      </div>
    </div>
  );
}

function updateOperatorPortContract(port, schemaId, schemaOptions) {
  const schema = schemaOptions.find((item) => item.schema_id === schemaId) || {};
  const next = { ...(port || {}), contract_id: schemaId };
  if (!next.value_kind && schema.value_kind) next.value_kind = String(schema.value_kind).toLowerCase();
  if (next.typed_io_contract) {
    next.typed_io_contract = { ...next.typed_io_contract, schema_id: schemaId };
  }
  return next;
}

function OperatorPortRowsEditor({ title, direction, ports, schemaOptions, onChange }) {
  const rows = asArray(ports);
  const updateRow = (index, patch) => {
    const next = rows.map((row, i) => (i === index ? { ...(row || {}), ...(patch || {}) } : row));
    onChange(next);
  };
  const updateTyped = (index, patch) => {
    const row = rows[index] || {};
    const typed = { ...(row.typed_io_contract || {}), ...(patch || {}) };
    updateRow(index, { typed_io_contract: typed });
  };
  const addRow = () => {
    onChange([
      ...rows,
      {
        port_id: direction === "inputs" ? "input" : "output",
        frame_contract: "",
        contract_id: "",
        value_kind: "",
        required: true,
      },
    ]);
  };
  return (
    <div className="operator-port-editor">
      <div className="operator-port-editor-head">
        <strong>{title}</strong>
        <Button onClick={addRow}>新增端口</Button>
      </div>
      {rows.length === 0 ? (
        <div className="muted-note">当前没有端口；保存前建议至少声明一个输出端口。</div>
      ) : rows.map((port, index) => {
        const typed = port.typed_io_contract || null;
        return (
          <div className="operator-port-row" key={`${direction}-${index}`}>
            <div className="editor-grid compact">
              <label><span>port_id</span><input value={firstDefined(port.port_id, "")} onChange={(e) => updateRow(index, { port_id: e.target.value })} /></label>
              <label><span>contract_id</span>
                <select value={firstDefined(port.contract_id, port.schema_id, "")} onChange={(e) => updateRow(index, updateOperatorPortContract(port, e.target.value, schemaOptions))}>
                  <option value="">选择 Domain Schema</option>
                  {schemaOptions.map((schema) => <option key={schema.schema_id} value={schema.schema_id}>{schema.schema_id}</option>)}
                  <option value="platform.state_snapshot.v1">platform.state_snapshot.v1</option>
                  <option value="platform.event_snapshot.v1">platform.event_snapshot.v1</option>
                </select>
              </label>
              <label><span>frame_contract</span>
                <input list="object-frame-contract-options" value={firstDefined(port.frame_contract, "")} onChange={(e) => updateRow(index, { frame_contract: e.target.value })} />
              </label>
              <label><span>value_kind</span>
                <input list="object-value-kind-options" value={firstDefined(port.value_kind, "")} onChange={(e) => updateRow(index, { value_kind: e.target.value })} />
              </label>
              <label><span>required</span>
                <select value={port.required === false ? "false" : "true"} onChange={(e) => updateRow(index, { required: e.target.value === "true" })}>
                  <option value="true">true</option>
                  <option value="false">false</option>
                </select>
              </label>
              <label><span>typed IO</span>
                <select value={typed ? "true" : "false"} onChange={(e) => {
                  if (e.target.value === "true") {
                    updateRow(index, {
                      typed_io_contract: {
                        status: "generated",
                        schema_id: firstDefined(port.contract_id, port.schema_id, ""),
                        dto_name: "",
                        type_name: "",
                        codegen_ref: "_local_artifacts/platform-pdk/codegen/generated_manifest.json",
                        json_operator_io_forbidden: true,
                      },
                    });
                  } else {
                    const next = rows.map((row, i) => (i === index ? { ...(row || {}) } : row));
                    delete next[index].typed_io_contract;
                    onChange(next);
                  }
                }}>
                  <option value="true">enabled</option>
                  <option value="false">disabled</option>
                </select>
              </label>
            </div>
            {typed && (
              <div className="editor-grid compact operator-typed-grid">
                <label><span>dto_name</span><input value={firstDefined(typed.dto_name, "")} onChange={(e) => updateTyped(index, { dto_name: e.target.value })} /></label>
                <label><span>type_name</span><input value={firstDefined(typed.type_name, "")} onChange={(e) => updateTyped(index, { type_name: e.target.value })} /></label>
                <label className="span-2"><span>codegen_ref</span><input value={firstDefined(typed.codegen_ref, "")} onChange={(e) => updateTyped(index, { codegen_ref: e.target.value })} /></label>
                <label><span>json_operator_io_forbidden</span>
                  <select value={typed.json_operator_io_forbidden === true ? "true" : "false"} onChange={(e) => updateTyped(index, { json_operator_io_forbidden: e.target.value === "true" })}>
                    <option value="true">true</option>
                    <option value="false">false</option>
                  </select>
                </label>
              </div>
            )}
            <div className="button-row">
              <Button tone="danger" onClick={() => onChange(rows.filter((_row, i) => i !== index))}>删除端口</Button>
            </div>
          </div>
        );
      })}
    </div>
  );
}

const VALUE_KIND_LABELS_ZH = {
  scalar: "标量",
  vector: "向量",
  tensor: "张量",
  field: "场",
  decision: "决策",
  event: "事件",
};

function humanizeObjectKey(value) {
  const text = String(value || "").trim();
  if (!text) return "";
  return text
    .replace(/_ids$/i, "")
    .replace(/_id$/i, "")
    .replace(/[_\-]+/g, " ")
    .replace(/\s+/g, " ")
    .replace(/\b\w/g, (match) => match.toUpperCase());
}

function schemaRoleLabel(value) {
  return humanizeObjectKey(value) || "数据结构";
}

function valueKindLabel(value) {
  return VALUE_KIND_LABELS_ZH[value] || humanizeObjectKey(value) || "-";
}

function referenceOptionId(item) {
  return firstDefined(item && item.schema_id, item && item.resource_id, item && item.operator_id, item && item.workflow_id, item && item.profile_id, item && item.id);
}

function componentReferenceEntries(doc) {
  return Object.entries(doc || {})
    .filter(([key, value]) => (
      Array.isArray(value)
      && !key.startsWith("_")
      && !["children", "fields"].includes(key)
    ))
    .map(([key, value]) => ({ key, value: asArray(value).filter(Boolean) }))
    .sort((left, right) => left.key.localeCompare(right.key));
}

function pickReferenceOptions(key, values, optionSets) {
  const text = String(key || "").toLowerCase();
  const ids = asArray(values);
  const knownIn = (set) => ids.length > 0 && ids.every((id) => set.has(id));
  if (text.includes("schema") || knownIn(optionSets.schemaIdSet)) return optionSets.schemaOptions;
  if (text.includes("operator") || knownIn(optionSets.operatorIdSet)) return optionSets.operatorOptions;
  if (text.includes("workflow") || knownIn(optionSets.workflowIdSet)) return optionSets.workflowOptions;
  return optionSets.resourceOptions;
}

function optionIds(options) {
  return new Set(asArray(options).map(referenceOptionId).filter(Boolean));
}

function SchemaOptionLabel({ schema }) {
  const id = firstDefined(schema && schema.schema_id, schema && schema.id, "");
  const display = firstDefined(schema && schema.display_name, "");
  const role = schemaRoleLabel(firstDefined(schema && schema.schema_role, schema && schema.value_kind, ""));
  const unit = firstDefined(schema && schema.unit, "");
  return `${display || role}${unit ? ` (${unit})` : ""} · ${id}`;
}

function optionLabelById(options, id, fallback = "") {
  const match = asArray(options).find((item) => firstDefined(item.schema_id, item.resource_id, item.operator_id, item.workflow_id, item.id) === id);
  if (!match) return fallback || id;
  if (match.schema_id) return SchemaOptionLabel({ schema: match });
  return firstDefined(match.display_name, match.title, match.name, id);
}

function componentMeshResources(doc, resources) {
  const byId = new Map(asArray(resources).map((item) => [resourceId(item), item]));
  const isMeshResource = (id) => {
    const item = byId.get(id);
    const text = [
      id,
      item && item.resource_type,
      item && item.layout_role,
      item && item.role,
      item && item.uri,
      item && item.path,
    ].filter(Boolean).join(" ").toLowerCase();
    return text.includes("mesh");
  };
  const dynamicResourceIds = componentReferenceEntries(doc)
    .flatMap((entry) => entry.value)
    .filter((id) => byId.has(id) && isMeshResource(id));
  const ids = Array.from(new Set([
    ...asArray(doc && doc.mesh_resource_ids),
    ...dynamicResourceIds,
  ].filter(Boolean)));
  return ids.map((id) => byId.get(id) || { resource_id: id, missing: true });
}

function UniqueArrayPicker({ title, value, options, optionId, optionLabel, onChange, emptyText = "未绑定" }) {
  const rows = asArray(value).filter(Boolean);
  const ids = asArray(options).map(optionId).filter(Boolean);
  const addValue = (id) => {
    if (!id) return;
    onChange(Array.from(new Set([...rows, id])));
  };
  return (
    <div className="modeler-picker">
      <div className="modeler-picker-head">
        <strong>{title}</strong>
        <select value="" onChange={(event) => addValue(event.target.value)}>
          <option value="">添加</option>
          {asArray(options).map((item) => {
            const id = optionId(item);
            if (!id || rows.includes(id)) return null;
            return <option key={id} value={id}>{optionLabel(item)}</option>;
          })}
        </select>
      </div>
      <div className="modeler-chip-list">
        {rows.length ? rows.map((id) => (
          <span className="modeler-chip" key={id}>
            <b>{optionLabelById(options, id)}</b>
            <small>{id}</small>
            <button type="button" onClick={() => onChange(rows.filter((item) => item !== id))}>删除</button>
          </span>
        )) : <em>{emptyText}</em>}
      </div>
      {ids.length === 0 && <small className="muted-text">当前对象包没有可选项。</small>}
    </div>
  );
}

function ComponentGraphEditor({ doc, object, onChange }) {
  const componentId = firstDefined(doc.component_id, doc.id, "");
  const resources = asArray(object && object.resources);
  const schemas = asArray(object && object.domain_schemas);
  const operators = asArray(object && object.operators);
  const workflows = asArray(object && object.workflows);
  const resourceOptions = resources.map((item) => ({ ...item, id: firstDefined(item.resource_id, item.id) })).filter((item) => item.id);
  const schemaOptions = schemas.map((item) => ({ ...item, id: firstDefined(item.schema_id, item.id) })).filter((item) => item.id);
  const operatorOptions = operators.map((item) => ({ ...item, id: firstDefined(item.operator_id, item.id) })).filter((item) => item.id);
  const workflowOptions = workflows.map((item) => ({ ...item, id: firstDefined(item.workflow_id, item.id) })).filter((item) => item.id);
  const optionSets = {
    resourceOptions,
    schemaOptions,
    operatorOptions,
    workflowOptions,
    resourceIdSet: optionIds(resourceOptions),
    schemaIdSet: optionIds(schemaOptions),
    operatorIdSet: optionIds(operatorOptions),
    workflowIdSet: optionIds(workflowOptions),
  };
  const referenceEntries = componentReferenceEntries(doc);
  const referencedValues = referenceEntries.flatMap((entry) => entry.value);
  const resourceIds = Array.from(new Set(referencedValues.filter((id) => optionSets.resourceIdSet.has(id))));
  const schemaIds = Array.from(new Set(referencedValues.filter((id) => optionSets.schemaIdSet.has(id))));
  const operatorIds = Array.from(new Set(referencedValues.filter((id) => optionSets.operatorIdSet.has(id))));
  const workflowIds = Array.from(new Set(referencedValues.filter((id) => optionSets.workflowIdSet.has(id))));
  const relatedOperators = operators.filter((op) => {
    const contracts = [...asArray(op.inputs), ...asArray(op.outputs)].map((port) => firstDefined(port.contract_id, port.schema_id));
    const refs = asArray(op.resource_refs);
    return refs.some((id) => resourceIds.includes(id)) || contracts.some((id) => schemaIds.includes(id)) || operatorIds.includes(op.operator_id);
  });
  const relatedOperatorIds = relatedOperators.map((op) => op.operator_id).filter(Boolean);
  const relatedWorkflows = workflows.filter((wf) => {
    const refs = asArray(wf.operator_refs);
    return refs.some((id) => relatedOperatorIds.includes(id)) || workflowIds.includes(wf.workflow_id);
  });
  const meshResources = componentMeshResources(doc, resources);
  const setArray = (key, next) => onChange(updateDocField(doc, key, next));

  return (
    <div className="component-designer">
      <ObjectGeometry3D
        resource={meshResources[0]}
        componentId={componentId}
        packageDir={object && object.package_dir}
        height={360}
        title="组件真实几何"
      />
      <div className="component-graph">
        <div className="component-graph-col">
          <strong>资源</strong>
          {resourceIds.length ? resourceIds.map((id) => <span key={id}>{id}</span>) : <em>未绑定资源</em>}
        </div>
        <div className="component-graph-center">
          <span>组件</span>
          <strong>{componentId || "未命名组件"}</strong>
          <small>{firstDefined(doc.display_name, "")}</small>
        </div>
        <div className="component-graph-col">
          <strong>数据结构</strong>
          {schemaIds.length ? schemaIds.map((id) => <span key={id}>{id}</span>) : <em>未绑定 schema</em>}
        </div>
        <div className="component-graph-col">
          <strong>算子 / Workflow</strong>
          {relatedOperatorIds.slice(0, 8).map((id) => <span key={id}>{id}</span>)}
          {relatedWorkflows.slice(0, 5).map((wf) => <span key={wf.workflow_id}>{wf.workflow_id}</span>)}
          {!relatedOperatorIds.length && !relatedWorkflows.length && <em>未关联计算链</em>}
        </div>
      </div>
      {referenceEntries.map(({ key, value }) => {
        const options = pickReferenceOptions(key, value, optionSets);
        return (
        <UniqueArrayPicker
          key={key}
          title={key}
          value={value}
          options={options}
          optionId={referenceOptionId}
          optionLabel={(item) => item.schema_id ? SchemaOptionLabel({ schema: item }) : firstDefined(item.display_name, item.title, item.name, referenceOptionId(item))}
          onChange={(next) => setArray(key, next)}
        />
        );
      })}
      {referenceEntries.length === 0 && (
        <div className="muted-note">当前组件没有数组型引用字段。可以在 JSON 草稿中添加 resource_ids、schema_ids、operator_ids 或对象包自定义的引用数组。</div>
      )}
    </div>
  );
}

function schemaDtoName(doc) {
  const explicit = firstDefined(doc && doc.dto_name, doc && doc.type_name, "");
  if (explicit) return explicit;
  const schemaId = firstDefined(doc && doc.schema_id, doc && doc.id, "domain_schema");
  return String(schemaId)
    .replace(/^.*?:\/\//, "")
    .replace(/\.v\d+$/i, "")
    .split(/[^A-Za-z0-9]+/)
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join("") || "DomainSchemaDto";
}

function schemaFieldTypeLabel(value) {
  const type = String(value || "double").trim();
  const labels = {
    f64: "double",
    f32: "float",
    i32: "int32",
    i64: "int64",
    u32: "uint32",
    u64: "uint64",
    string_ref: "StringRef",
    tensor_ref: "TensorRef",
    artifact_ref: "ArtifactRef",
  };
  return labels[type] || type;
}

function schemaValueKind(doc) {
  return String(firstDefined(doc && doc.value_kind, "")).toLowerCase();
}

function isVectorDomainSchema(doc) {
  return schemaValueKind(doc) === "vector";
}

function schemaElementTypeLabel(doc) {
  return schemaFieldTypeLabel(firstDefined(doc && doc.element_type, doc && doc.dtype, doc && doc.type, "f64"));
}

function schemaVectorMaxLength(doc) {
  const raw = firstDefined(
    doc && doc.max_length,
    doc && doc.vector_length_max,
    doc && doc.length,
    doc && doc.size,
    ""
  );
  const value = Number(raw);
  if (Number.isFinite(value) && value > 0) return Math.floor(value);
  return 64;
}

function schemaStructurePreview(doc) {
  const name = schemaDtoName(doc);
  const fields = asArray(doc && doc.fields);
  if (fields.length) {
    return [
      `struct ${name} {`,
      ...fields.map((field) => {
        const fieldName = firstDefined(field.name, field.field_id, "value");
        const type = schemaFieldTypeLabel(firstDefined(field.type, field.value_type, "double"));
        const meta = [
          firstDefined(field.unit, "") ? `unit=${field.unit}` : "",
          field.required === false ? "optional" : "required",
          firstDefined(field.role, "") ? `role=${field.role}` : "",
        ].filter(Boolean).join(", ");
        return `  ${type} ${fieldName};${meta ? ` // ${meta}` : ""}`;
      }),
      "};",
    ].join("\n");
  }

  const valueKind = String(firstDefined(doc && doc.value_kind, "")).toLowerCase();
  if (valueKind === "field" || valueKind === "tensor" || firstDefined(doc && doc.artifact_policy, "") === "artifact_ref") {
    return [
      `struct ${name} {`,
      `  TensorRef value; // dtype=${firstDefined(doc && doc.dtype, "f64")}, rank=${firstDefined(doc && doc.rank, "-")}`,
      `  ResourceRef layout; // ${firstDefined(doc && doc.layout_ref, doc && doc.mesh_ref, "未绑定")}`,
      `  ShapeRef shape; // ${firstDefined(doc && doc.shape_ref, "未声明")}`,
      "};",
    ].join("\n");
  }

  const variables = asArray(doc && doc.variables);
  if (variables.length) {
    return [
      `struct ${name} {`,
      `  double values[${variables.length}];`,
      `  // 顺序: ${variables.join(", ")}`,
      "};",
    ].join("\n");
  }

  if (isVectorDomainSchema(doc)) {
    const elementType = schemaElementTypeLabel(doc);
    const maxLength = schemaVectorMaxLength(doc);
    const shapeRef = firstDefined(doc && doc.shape_ref, doc && doc.layout_ref, "-");
    return [
      `#define ${name.toUpperCase()}_MAX_VALUES ${maxLength}`,
      `struct ${name} {`,
      `  ${elementType} values[${name.toUpperCase()}_MAX_VALUES];`,
      `  uint32_t size; // actual element count, constrained by ${shapeRef}`,
      "};",
    ].join("\n");
  }

  return [
    `struct ${name} {`,
    `  ${schemaFieldTypeLabel(doc && doc.dtype)} value;`,
    "};",
  ].join("\n");
}

function DomainSchemaDetail({ doc, object, relPath }) {
  const fields = asArray(doc && doc.fields);
  const variables = asArray(doc && doc.variables);
  const isVectorSchema = isVectorDomainSchema(doc);
  const uncertainty = (doc && doc.uncertainty_policy) || {};
  const componentId = firstDefined(doc && doc.component_id, "");
  const component = asArray(object && object.components).find((item) => firstDefined(item.component_id, item.id) === componentId);
  const componentLabel = component
    ? `${firstDefined(component.display_name, component.name, componentId)} · ${componentId}`
    : componentId || "未绑定组件";
  const dataShapeRows = [
    ["中文名", firstDefined(doc && doc.display_name, "-")],
    ["schema_id / 端口 contract", firstDefined(doc && doc.schema_id, doc && doc.id, "-")],
    ["所属组件", componentLabel],
    ["角色", schemaRoleLabel(firstDefined(doc && doc.schema_role, ""))],
    ["值类型", valueKindLabel(String(firstDefined(doc && doc.value_kind, "")).toLowerCase())],
    ["物理量 / 单位", `${firstDefined(doc && doc.quantity, "-")} / ${firstDefined(doc && doc.unit, "-")}`],
    ["存储/传输策略", firstDefined(doc && doc.artifact_policy, "inline")],
    ["layout / shape", `${firstDefined(doc && doc.layout_ref, doc && doc.mesh_ref, "-")} / ${firstDefined(doc && doc.shape_ref, "-")}`],
    ["dtype / rank", `${firstDefined(doc && doc.dtype, fields.length ? "fields" : "-")} / ${firstDefined(doc && doc.rank, fields.length ? fields.length : "-")}`],
    ["元素类型 / 最大长度", isVectorSchema ? `${schemaElementTypeLabel(doc)} / ${schemaVectorMaxLength(doc)}` : "-"],
    ["DTO 名称", schemaDtoName(doc)],
    ["文件", relPath || "-"],
  ];
  const variableRows = variables.map((name, index) => ({
    index,
    name,
    field: fields.find((field) => firstDefined(field.name, field.field_id) === name),
  }));
  const fieldRows = fields.map((field, index) => ({
    index,
    name: firstDefined(field.name, field.field_id, `field_${index}`),
    type: schemaFieldTypeLabel(firstDefined(field.type, field.value_type, "double")),
    unit: firstDefined(field.unit, "-"),
    required: field.required === false ? "可选" : "必填",
    role: firstDefined(field.role, field.description, "-"),
  }));

  return (
    <div className="domain-schema-detail">
      <div className="domain-schema-hero">
        <div>
          <span>数据结构 / Domain Schema</span>
          <h3>{firstDefined(doc && doc.display_name, doc && doc.schema_id, "未命名数据结构")}</h3>
          <p>这是对象包声明的端口数据契约。平台用 schema_id 做连线校验和 DTO 生成，界面用中文名帮助人理解。</p>
        </div>
        <div className="domain-schema-tags">
          <b>{schemaRoleLabel(firstDefined(doc && doc.schema_role, ""))}</b>
          <b>{valueKindLabel(String(firstDefined(doc && doc.value_kind, "")).toLowerCase())}</b>
          <b>{firstDefined(doc && doc.artifact_policy, "inline")}</b>
        </div>
      </div>

      <div className="domain-schema-grid">
        <section>
          <h4>基础信息</h4>
          <KeyValue rows={dataShapeRows} />
        </section>
        <section>
          <h4>结构体预览</h4>
          <pre className="schema-struct-preview">{schemaStructurePreview(doc)}</pre>
        </section>
      </div>

      <section className="domain-schema-section">
        <h4>字段结构</h4>
        {fieldRows.length ? (
          <DataTable
            rows={fieldRows}
            rowKey={(row) => row.name}
            columns={[
              { key: "index", title: "#", render: (row) => row.index + 1 },
              { key: "name", title: "字段名" },
              { key: "type", title: "类型" },
              { key: "unit", title: "单位" },
              { key: "required", title: "约束" },
              { key: "role", title: "角色/说明" },
            ]}
          />
        ) : (
          <div className="schema-ref-summary">
            <strong>{isVectorSchema ? "这是向量契约，不是单个 double。" : "该 schema 没有显式字段表。"}</strong>
            {isVectorSchema ? (
              <>
                <span>DTO 会生成为 values[] + size；一维数组型契约都走这种结构。</span>
                <span>真实维度优先由 shape_ref 或 layout_ref 约束；当前最大内联长度为 {schemaVectorMaxLength(doc)}。</span>
              </>
            ) : (
              <span>它更像是一个大场/张量/外部产物引用：值体通过 {firstDefined(doc && doc.artifact_policy, "artifact_ref")} 传递，结构由 layout、shape、dtype 和 rank 描述。</span>
            )}
          </div>
        )}
      </section>

      {variableRows.length > 0 && (
        <section className="domain-schema-section">
          <h4>变量顺序</h4>
          <div className="schema-variable-list">
            {variableRows.map((row) => (
              <span key={`${row.index}-${row.name}`}>
                <b>{row.index + 1}</b>
                {row.name}
                <small>{row.field ? schemaFieldTypeLabel(firstDefined(row.field.type, row.field.value_type)) : "未在 fields 中声明"}</small>
              </span>
            ))}
          </div>
        </section>
      )}

      <section className="domain-schema-section">
        <h4>不确定性与引用策略</h4>
        <KeyValue rows={[
          ["uncertainty.supported", uncertainty.supported === false ? "false" : firstDefined(uncertainty.supported, "未声明")],
          ["uncertainty.representation", firstDefined(uncertainty.representation, "未声明")],
          ["legacy_variables", asArray(doc && doc.legacy_variables).join(", ") || "-"],
          ["decision_fields", asArray(doc && doc.decision_fields).join(", ") || "-"],
        ]} />
      </section>
    </div>
  );
}

function SchemaFieldsEditor({ fields, onChange }) {
  const rows = asArray(fields);
  const updateRow = (index, patch) => {
    const next = rows.map((row, i) => (i === index ? { ...(row || {}), ...(patch || {}) } : row));
    onChange(next);
  };
  const addRow = () => onChange([...rows, { name: `value_${rows.length + 1}`, type: "double", unit: "", role: "value", required: true }]);
  return (
    <div className="schema-fields-editor">
      <div className="operator-port-editor-head">
        <strong>字段定义</strong>
        <Button onClick={addRow}>新增字段</Button>
      </div>
      {rows.length ? rows.map((field, index) => (
        <div className="schema-field-row" key={index}>
          <div className="editor-grid compact">
            <label><span>name</span><input value={firstDefined(field.name, field.field_id, "")} onChange={(event) => updateRow(index, { name: event.target.value })} /></label>
            <label><span>type</span>
              <select value={firstDefined(field.type, field.value_type, "double")} onChange={(event) => updateRow(index, { type: event.target.value })}>
                {["double", "float", "int32", "int64", "bool", "string", "vector<double>", "tensor_ref"].map((item) => <option key={item} value={item}>{item}</option>)}
              </select>
            </label>
            <label><span>unit</span><input value={firstDefined(field.unit, "")} onChange={(event) => updateRow(index, { unit: event.target.value })} /></label>
            <label><span>role</span><input value={firstDefined(field.role, "")} onChange={(event) => updateRow(index, { role: event.target.value })} /></label>
            <label><span>required</span>
              <select value={field.required === false ? "false" : "true"} onChange={(event) => updateRow(index, { required: event.target.value === "true" })}>
                <option value="true">true</option>
                <option value="false">false</option>
              </select>
            </label>
            <Button tone="danger" onClick={() => onChange(rows.filter((_row, i) => i !== index))}>删除字段</Button>
          </div>
        </div>
      )) : <div className="muted-note">没有字段。标量/场引用可以没有 fields，但结构体 DTO 建议显式列出关键字段。</div>}
    </div>
  );
}

function PrimitiveSummary({ doc }) {
  const rows = Object.entries(doc || {})
    .filter(([, value]) => value === null || ["string", "number", "boolean"].includes(typeof value))
    .slice(0, 14)
    .map(([key, value]) => [key, String(value ?? "-")]);
  return rows.length ? <KeyValue rows={rows} /> : <Empty title="无基础字段" hint="当前定义主要由数组或对象字段组成。" />;
}

function ReferenceListView({ title, entries }) {
  const rows = asArray(entries).flatMap((entry) => asArray(entry.value).map((value, index) => ({
    key: `${entry.key}:${value}:${index}`,
    group: entry.key,
    value,
  })));
  return (
    <section className="readonly-entity-section">
      <h4>{title}</h4>
      <DataTable
        rows={rows}
        rowKey={(row) => row.key}
        emptyHint="当前没有声明引用。"
        columns={[
          { key: "group", title: "分组" },
          { key: "value", title: "引用 ID" },
        ]}
      />
    </section>
  );
}

function JsonReadonlyBlock({ title, value }) {
  return (
    <details className="json-preview-details readonly-json-details">
      <summary>
        <strong>{title}</strong>
        <span>只读 JSON，点击展开查看原始声明。</span>
      </summary>
      <pre className="json-pre compact">{JSON.stringify(value || {}, null, 2)}</pre>
    </details>
  );
}

function StructuredEntityViewer({ kind, doc, object, meta }) {
  if (!doc) return null;
  const resources = asArray(object && object.resources);
  const identityId = firstDefined(
    doc.object_id,
    doc.component_id,
    doc.resource_id,
    doc.schema_id,
    doc.operator_id,
    doc.workflow_id,
    doc.profile_id,
    doc.id,
  );
  const identityRows = [
    ["类型", entityKindLabel(kind)],
    ["ID", identityId || "-"],
    ["中文名", firstDefined(doc.display_name, doc.name, "-")],
    ["文件", firstDefined(meta && meta.rel_path, "-")],
  ];

  if (kind === "component") {
    const meshResources = componentMeshResources(doc, resources);
    return (
      <div className="readonly-entity-view">
        <ObjectGeometry3D
          resource={meshResources[0]}
          componentId={firstDefined(doc.component_id, doc.id, "")}
          packageDir={object && object.package_dir}
          height={360}
          title="组件真实几何"
        />
        <section className="readonly-entity-section">
          <h4>组件信息</h4>
          <KeyValue rows={identityRows} />
        </section>
        <ReferenceListView title="组件引用" entries={componentReferenceEntries(doc)} />
      </div>
    );
  }

  if (kind === "domain_schema") {
    return (
      <div className="readonly-entity-view">
        <DomainSchemaDetail doc={doc} object={object} relPath={meta && meta.rel_path} />
      </div>
    );
  }

  if (kind === "resource") {
    return (
      <div className="readonly-entity-view">
        <ResourcePreview resource={doc} packageDir={objectPackageRoot(null, object)} />
        <section className="readonly-entity-section">
          <h4>资源信息</h4>
          <KeyValue rows={[
            ...identityRows,
            ["资源类型", resourceType(doc)],
            ["component_id", firstDefined(doc.component_id, "-")],
            ["layout_role", firstDefined(doc.layout_role, doc.role, "-")],
            ["uri / path", firstDefined(doc.uri, doc.path, "-")],
          ]} />
        </section>
      </div>
    );
  }

  if (kind === "operator") {
    const ports = [...asArray(doc.inputs).map((p) => ({ ...p, direction: "input" })), ...asArray(doc.outputs).map((p) => ({ ...p, direction: "output" }))];
    return (
      <div className="readonly-entity-view">
        <section className="readonly-entity-section">
          <h4>算子信息</h4>
          <KeyValue rows={[
            ...identityRows,
            ["算子族", firstDefined(doc.operator_family, doc.family, "-")],
            ["执行后端", firstDefined(doc.execution && doc.execution.kind, "-")],
            ["adapter_id", firstDefined(doc.execution && doc.execution.adapter_id, "-")],
          ]} />
        </section>
        <section className="readonly-entity-section">
          <h4>端口契约</h4>
          <DataTable
            rows={ports}
            rowKey={(row, index) => `${row.direction}:${row.port_id || index}`}
            emptyHint="当前算子没有声明端口。"
            columns={[
              { key: "direction", title: "方向" },
              { key: "port_id", title: "端口" },
              { key: "contract", title: "contract", render: (row) => firstDefined(row.contract_id, row.schema_id, "-") },
              { key: "frame", title: "frame", render: (row) => firstDefined(row.frame_contract, "-") },
              { key: "value_kind", title: "值类型", render: (row) => firstDefined(row.value_kind, "-") },
            ]}
          />
        </section>
        <ReferenceListView title="资源引用" entries={[{ key: "resource_refs", value: doc.resource_refs }]} />
      </div>
    );
  }

  return (
    <div className="readonly-entity-view">
      <section className="readonly-entity-section">
        <h4>基础信息</h4>
        <KeyValue rows={identityRows} />
      </section>
      <section className="readonly-entity-section">
        <h4>字段摘要</h4>
        <PrimitiveSummary doc={doc} />
      </section>
      <JsonReadonlyBlock title="原始声明" value={doc} />
    </div>
  );
}

function StructuredEntityEditor({ kind, doc, object, meta, onChange }) {
  if (!doc || !["twin_object", "component", "asset_group", "domain_schema", "resource", "operator", "run_profile", "runtime_profile"].includes(kind)) return null;
  const components = asArray(object && object.components);
  const resources = asArray(object && object.resources);
  const schemaOptions = asArray(object && object.domain_schemas);
  const componentOptions = components.map((item) => firstDefined(item.component_id, item.id)).filter(Boolean);
  const resourceOptions = resources.map((item) => firstDefined(item.resource_id, item.id)).filter(Boolean);
  const resourceOptionDocs = resources.map((item) => ({ ...item, id: firstDefined(item.resource_id, item.id) })).filter((item) => item.id);
  const valueKindOptions = Array.from(new Set([
    firstDefined(doc.value_kind, ""),
    ...GENERIC_VALUE_KIND_OPTIONS,
    ...schemaOptions.map((item) => firstDefined(item.value_kind, "")).filter(Boolean),
    ...asArray(object && object.operators).flatMap((op) => [...asArray(op.inputs), ...asArray(op.outputs)]).map((port) => firstDefined(port.value_kind, "")).filter(Boolean),
  ].filter(Boolean)));
  const frameContractOptions = Array.from(new Set([
    ...GENERIC_FRAME_CONTRACT_OPTIONS,
    ...asArray(object && object.operators).flatMap((op) => [...asArray(op.inputs), ...asArray(op.outputs)]).map((port) => firstDefined(port.frame_contract, "")).filter(Boolean),
  ].filter(Boolean)));
  const setField = (key, value) => onChange(updateDocField(doc, key, value));
  const setArray = (key, value) => onChange(updateDocArrayField(doc, key, value));
  const setObject = (key, value) => onChange(updateDocObjectField(doc, key, value));

  if (kind === "twin_object") {
    return (
      <div className="structured-editor">
        <h3>对象画像</h3>
        <div className="editor-grid compact">
          <label><span>object_id</span><input value={firstDefined(doc.object_id, "")} onChange={(e) => setField("object_id", e.target.value)} /></label>
          <label><span>object_type</span><input value={firstDefined(doc.object_type, "")} onChange={(e) => setField("object_type", e.target.value)} /></label>
          <label className="span-2"><span>中文名</span><input value={firstDefined(doc.display_name, doc.name, "")} onChange={(e) => setField("display_name", e.target.value)} /></label>
        </div>
      </div>
    );
  }

  if (kind === "component") {
    return (
      <div className="structured-editor">
        <h3>组件设计</h3>
        <p className="muted-note">组件是对象包里组织资源、数据结构、算子和 workflow 绑定的工程对象。这里按 CAE 树方式编辑对象包声明的数组引用字段，保存时仍落回 object/twin_object.json。</p>
        <div className="editor-grid compact">
          <label><span>component_id</span><input value={firstDefined(doc.component_id, doc.id, "")} onChange={(e) => setField("component_id", e.target.value)} /></label>
          <label><span>中文名</span><input value={firstDefined(doc.display_name, "")} onChange={(e) => setField("display_name", e.target.value)} /></label>
        </div>
        <ComponentGraphEditor doc={doc} object={object} onChange={onChange} />
        <datalist id="object-resource-ids">{resourceOptions.map((id) => <option key={id} value={id} />)}</datalist>
      </div>
    );
  }

  if (kind === "asset_group") {
    return (
      <div className="structured-editor">
        <h3>资源分组设计</h3>
        <p className="muted-note">资源分组只负责把对象资源按工程用途组织起来。组内成员仍然引用稳定的 resource_id，中文名只用于界面显示。</p>
        <div className="editor-grid compact">
          <label><span>group_id</span><input value={firstDefined(doc.group_id, doc.id, "")} onChange={(e) => setField("group_id", e.target.value)} /></label>
          <label><span>中文名</span><input value={firstDefined(doc.display_name, "")} onChange={(e) => setField("display_name", e.target.value)} /></label>
        </div>
        <UniqueArrayPicker
          title="组内资源"
          value={doc.resources}
          options={resourceOptionDocs}
          optionId={(item) => item.id}
          optionLabel={(item) => `${firstDefined(item.display_name, item.resource_type, item.id)} · ${item.id}`}
          onChange={(next) => setArray("resources", next)}
          emptyText="当前分组还没有资源。"
        />
      </div>
    );
  }

  if (kind === "domain_schema") {
    const fields = asArray(doc.fields);
    const uncertainty = doc.uncertainty_policy || {};
    const schemaRoleOptions = Array.from(new Set([
      firstDefined(doc.schema_role, ""),
      ...schemaOptions.map((item) => firstDefined(item.schema_role, "")).filter(Boolean),
    ].filter(Boolean)));
    const setUncertainty = (patch) => setObject("uncertainty_policy", patch);
    return (
      <div className="structured-editor">
        <DomainSchemaDetail doc={doc} object={object} relPath={meta && meta.rel_path} />
        <h3>数据结构编辑</h3>
        <p className="muted-note">上面是当前结构体/端口契约的只读说明；这里修改后会同步到 JSON 草稿，点击保存后写回对象包。</p>
        <div className="editor-grid compact">
          <label><span>schema_id</span><input value={firstDefined(doc.schema_id, doc.id, "")} onChange={(e) => setField("schema_id", e.target.value)} /></label>
          <label><span>component_id</span>
            <select value={firstDefined(doc.component_id, "")} onChange={(e) => setField("component_id", e.target.value)}>
              <option value="">选择 component</option>
              {componentOptions.map((id) => <option key={id} value={id}>{id}</option>)}
            </select>
          </label>
          <label><span>value_kind</span>
            <input list="object-value-kind-options" value={firstDefined(doc.value_kind, "")} onChange={(e) => setField("value_kind", e.target.value)} placeholder="由对象包/契约自定义" />
          </label>
          <label><span>schema_role</span>
            <input list="object-schema-role-options" value={firstDefined(doc.schema_role, "")} onChange={(e) => setField("schema_role", e.target.value)} placeholder="由对象包自定义，可为空" />
          </label>
          <label><span>artifact_policy</span>
            <select value={firstDefined(doc.artifact_policy, "")} onChange={(e) => setField("artifact_policy", e.target.value)}>
              <option value="">选择 artifact policy</option>
              {["inline", "artifact_ref", "tensor_ref", "external_ref"].map((item) => <option key={item} value={item}>{item}</option>)}
            </select>
          </label>
          <label><span>layout_ref / mesh</span><input list="object-resource-ids" value={firstDefined(doc.layout_ref, doc.mesh_ref, doc.shape_ref, "")} onChange={(e) => setField("layout_ref", e.target.value)} /></label>
          <label><span>quantity</span><input value={firstDefined(doc.quantity, "")} onChange={(e) => setField("quantity", e.target.value)} /></label>
          <label><span>unit</span><input value={firstDefined(doc.unit, "")} onChange={(e) => setField("unit", e.target.value)} /></label>
          <label><span>中文名</span><input value={firstDefined(doc.display_name, "")} onChange={(e) => setField("display_name", e.target.value)} /></label>
          <label><span>dto_name</span><input value={firstDefined(doc.dto_name, doc.type_name, "")} onChange={(e) => setField("dto_name", e.target.value)} /></label>
        </div>
        <SchemaFieldsEditor fields={fields} onChange={(next) => setField("fields", next)} />
        <div className="operator-policy-card">
          <h4>uncertainty_policy</h4>
          <div className="editor-grid compact">
            <label><span>supported</span>
              <select value={uncertainty.supported === false ? "false" : "true"} onChange={(e) => setUncertainty({ supported: e.target.value === "true" })}>
                <option value="true">true</option>
                <option value="false">false</option>
              </select>
            </label>
            <label><span>representation</span>
              <select value={firstDefined(uncertainty.representation, "optional_uncertainty_block")} onChange={(e) => setUncertainty({ representation: e.target.value })}>
                <option value="optional_uncertainty_block">optional_uncertainty_block</option>
                <option value="covariance_ref">covariance_ref</option>
                <option value="ensemble_ref">ensemble_ref</option>
                <option value="none">none</option>
              </select>
            </label>
          </div>
        </div>
        <p className="muted-note">校验门禁由 PDK 和对象包 schema 决定；UI 只展示并编辑声明字段，不根据固定对象类别推断必填项。</p>
        <datalist id="object-resource-ids">{resourceOptions.map((id) => <option key={id} value={id} />)}</datalist>
        <datalist id="object-schema-role-options">{schemaRoleOptions.map((id) => <option key={id} value={id} />)}</datalist>
        <datalist id="object-value-kind-options">{valueKindOptions.map((id) => <option key={id} value={id} />)}</datalist>
      </div>
    );
  }

  if (kind === "operator") {
    const execution = doc.execution || {};
    const typed = doc.typed_io_contract || {};
    const timePolicy = doc.time_policy || {};
    const schedulerPolicy = doc.scheduler_policy || {};
    const failurePolicy = doc.failure_policy || {};
    const refs = asArray(doc.resource_refs);
    const operatorFamilyOptions = Array.from(new Set([
      firstDefined(doc.operator_family, doc.family, ""),
      ...asArray(object && object.operators).map((item) => firstDefined(item.operator_family, item.family, "")).filter(Boolean),
    ].filter(Boolean)));
    const setExecution = (patch) => setObject("execution", patch);
    const setTyped = (patch) => setObject("typed_io_contract", patch);
    const setTimePolicy = (patch) => setObject("time_policy", patch);
    const setSchedulerPolicy = (patch) => setObject("scheduler_policy", patch);
    const setFailurePolicy = (patch) => setObject("failure_policy", patch);
    const toggleResource = (resourceId, enabled) => {
      const next = enabled
        ? Array.from(new Set([...refs, resourceId]))
        : refs.filter((item) => item !== resourceId);
      setField("resource_refs", next);
    };
    return (
      <div className="structured-editor operator-structured-editor">
        <h3>Operator Spec</h3>
        <div className="editor-grid compact">
          <label><span>operator_id</span><input value={firstDefined(doc.operator_id, doc.id, "")} onChange={(e) => setField("operator_id", e.target.value)} /></label>
          <label><span>operator_family</span>
            <input list="object-operator-family-options" value={firstDefined(doc.operator_family, doc.family, "")} onChange={(e) => setField("operator_family", e.target.value)} placeholder="由对象包/算子包自定义" />
          </label>
          <label><span>中文名</span><input value={firstDefined(doc.display_name, "")} onChange={(e) => setField("display_name", e.target.value)} /></label>
          <label><span>operator_kind</span><input value={firstDefined(doc.operator_kind, "atomic")} onChange={(e) => setField("operator_kind", e.target.value)} /></label>
          <label><span>execution.kind</span><input value={firstDefined(execution.kind, "")} onChange={(e) => setExecution({ kind: e.target.value })} /></label>
          <label><span>execution.adapter_id</span><input value={firstDefined(execution.adapter_id, "")} onChange={(e) => setExecution({ adapter_id: e.target.value })} /></label>
          <label className="span-2"><span>execution.lifecycle</span><input value={arrayToCsv(execution.lifecycle)} onChange={(e) => setExecution({ lifecycle: csvToArray(e.target.value) })} /></label>
        </div>
        <datalist id="object-operator-family-options">{operatorFamilyOptions.map((id) => <option key={id} value={id} />)}</datalist>

        <div className="operator-resource-selector">
          <strong>resource_refs</strong>
          <div>
            {resourceOptions.length ? resourceOptions.map((resourceId) => (
              <label key={resourceId}>
                <input type="checkbox" checked={refs.includes(resourceId)} onChange={(e) => toggleResource(resourceId, e.target.checked)} />
                <span>{resourceId}</span>
              </label>
            )) : <span className="muted-text">当前对象包没有可选资源。</span>}
          </div>
        </div>

        <OperatorPortRowsEditor
          title="输入端口"
          direction="inputs"
          ports={doc.inputs}
          schemaOptions={schemaOptions}
          onChange={(next) => setField("inputs", next)}
        />
        <OperatorPortRowsEditor
          title="输出端口"
          direction="outputs"
          ports={doc.outputs}
          schemaOptions={schemaOptions}
          onChange={(next) => setField("outputs", next)}
        />
        <datalist id="object-value-kind-options">{valueKindOptions.map((id) => <option key={id} value={id} />)}</datalist>
        <datalist id="object-frame-contract-options">{frameContractOptions.map((id) => <option key={id} value={id} />)}</datalist>

        <div className="operator-policy-grid">
          <div className="operator-policy-card">
            <h4>operator typed I/O</h4>
            <div className="editor-grid compact">
              <label><span>input_dto</span><input value={firstDefined(typed.input_dto, "")} onChange={(e) => setTyped({ input_dto: e.target.value })} /></label>
              <label><span>output_dto</span><input value={firstDefined(typed.output_dto, "")} onChange={(e) => setTyped({ output_dto: e.target.value })} /></label>
              <label><span>run_fn_type</span><input value={firstDefined(typed.run_fn_type, "")} onChange={(e) => setTyped({ run_fn_type: e.target.value })} /></label>
              <label><span>json_operator_io_forbidden</span>
                <select value={typed.json_operator_io_forbidden === true ? "true" : "false"} onChange={(e) => setTyped({ json_operator_io_forbidden: e.target.value === "true" })}>
                  <option value="true">true</option>
                  <option value="false">false</option>
                </select>
              </label>
              <label className="span-2"><span>codegen_ref</span><input value={firstDefined(typed.codegen_ref, "")} onChange={(e) => setTyped({ codegen_ref: e.target.value })} /></label>
            </div>
          </div>
          <div className="operator-policy-card">
            <h4>time / scheduler</h4>
            <div className="editor-grid compact">
              <label><span>time_policy.kind</span><input value={firstDefined(timePolicy.kind, "")} onChange={(e) => setTimePolicy({ kind: e.target.value })} /></label>
              <label><span>sample_period_s</span><input value={firstDefined(timePolicy.sample_period_s, "")} onChange={(e) => setTimePolicy({ sample_period_s: Number(e.target.value || 0) })} /></label>
              <label><span>execution_mode</span><input value={firstDefined(schedulerPolicy.execution_mode, "")} onChange={(e) => setSchedulerPolicy({ execution_mode: e.target.value })} /></label>
              <label><span>deadline_s</span><input value={firstDefined(schedulerPolicy.deadline_s, "")} onChange={(e) => setSchedulerPolicy({ deadline_s: Number(e.target.value || 0) })} /></label>
              <label><span>timeout_s</span><input value={firstDefined(schedulerPolicy.timeout_s, "")} onChange={(e) => setSchedulerPolicy({ timeout_s: Number(e.target.value || 0) })} /></label>
              <label><span>retry_count</span><input value={firstDefined(schedulerPolicy.retry_count, "")} onChange={(e) => setSchedulerPolicy({ retry_count: Number(e.target.value || 0) })} /></label>
            </div>
          </div>
          <div className="operator-policy-card">
            <h4>failure / checkpoint</h4>
            <div className="editor-grid compact">
              <label><span>failure.mode</span><input value={firstDefined(failurePolicy.mode, "")} onChange={(e) => setFailurePolicy({ mode: e.target.value })} /></label>
              <label><span>failure.on_error</span><input value={firstDefined(failurePolicy.on_error, "")} onChange={(e) => setFailurePolicy({ on_error: e.target.value })} /></label>
              <label><span>failure.retry_count</span><input value={firstDefined(failurePolicy.retry_count, "")} onChange={(e) => setFailurePolicy({ retry_count: Number(e.target.value || 0) })} /></label>
              <label><span>checkpoint_responsibility</span><input value={firstDefined(doc.checkpoint_responsibility, "")} onChange={(e) => setField("checkpoint_responsibility", e.target.value)} /></label>
            </div>
          </div>
        </div>
        <p className="muted-note">Phase3 门禁：operator_id 必须唯一；family 必须来自枚举；端口必须绑定存在的 contract；启用 typed IO 时必须声明 DTO/type/codegen 并禁止 JSON I/O。测试向量属于发布门禁资产，不在普通对象编辑表单中常驻展示。</p>
      </div>
    );
  }

  if (kind === "resource") {
    return (
      <div className="structured-editor">
        <h3>资源定义</h3>
        <div className="editor-grid compact">
          <label><span>resource_id</span><input value={firstDefined(doc.resource_id, doc.id, "")} onChange={(e) => setField("resource_id", e.target.value)} /></label>
          <label><span>resource_type</span><input value={firstDefined(doc.resource_type, doc.type, "resource")} onChange={(e) => setField("resource_type", e.target.value)} /></label>
          <label><span>component_id</span>
            <select value={firstDefined(doc.component_id, "")} onChange={(e) => setField("component_id", e.target.value)}>
              <option value="">不绑定 component</option>
              {componentOptions.map((id) => <option key={id} value={id}>{id}</option>)}
            </select>
          </label>
          <label><span>layout_role</span><input value={firstDefined(doc.layout_role, doc.role, "")} onChange={(e) => setField("layout_role", e.target.value)} /></label>
          <label className="span-2"><span>uri / path</span><input value={firstDefined(doc.uri, doc.path, "")} onChange={(e) => setField("uri", e.target.value)} /></label>
          <label><span>model_kind</span><input value={firstDefined(doc.model_kind, "")} onChange={(e) => setField("model_kind", e.target.value)} /></label>
          <label><span>source_database_ref</span><input value={firstDefined(doc.source_database_ref, "")} onChange={(e) => setField("source_database_ref", e.target.value)} /></label>
          <label className="span-2"><span>description</span><input value={firstDefined(doc.description, "")} onChange={(e) => setField("description", e.target.value)} /></label>
        </div>
      </div>
    );
  }

  if (kind === "run_profile") {
    return <RunProfileStructuredEditor doc={doc} object={object} onChange={onChange} />;
  }

  if (kind === "runtime_profile") {
    return <RuntimeProfileStructuredEditor doc={doc} object={object} onChange={onChange} />;
  }

  return null;
}

function ObjectModelerTree({ nodes, selectedId, onSelect, onAction, editing = false }) {
  const flat = flattenObjectTree(nodes);
  const [menu, setMenu] = useState(null);
  useEffect(() => {
    const close = () => setMenu(null);
    window.addEventListener("click", close);
    return () => window.removeEventListener("click", close);
  }, []);
  if (!flat.length) return <Empty title="对象树为空" hint="请先载入对象包或新建对象工程。" />;
  return (
    <div className="object-modeler-tree">
      {flat.map((node) => (
        <button
          key={node.id}
          className={cls("object-tree-node", selectedId === node.id && "active", !node.editable && "readonly")}
          style={{ paddingLeft: `${10 + Math.min(6, node.depth) * 14}px` }}
          onClick={() => onSelect(node)}
          onContextMenu={(event) => {
            event.preventDefault();
            setMenu({ node, x: event.clientX, y: event.clientY });
          }}
        >
          <span>{node.children && node.children.length ? "▸" : "·"}</span>
          <strong>{node.label}</strong>
          <small>{firstDefined(node.entity_id, node.rel_path, "")}</small>
          <em>{entityKindLabel(node.kind)}</em>
        </button>
      ))}
      {menu && (
        <div className="object-tree-menu" style={{ left: menu.x, top: menu.y }}>
          <strong>{menu.node.label}</strong>
          {asArray(menu.node.actions)
            .filter((action) => editing || !["new", "delete", "save_project_state"].includes(action.id))
            .map((action) => (
            <button
              key={action.id}
              disabled={action.enabled === false}
              onClick={(event) => {
                event.stopPropagation();
                setMenu(null);
                if (onAction) onAction(action, menu.node);
              }}
            >
              {!editing && action.id === "edit" ? "查看" : (action.label || action.id)}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

function ObjectModelerPage(props) {
  const {
    object,
    run,
    setRun,
    selectedWorkflowId,
    selectedRunProfileId,
    reloadAll,
    setPage,
    objectTreeReq,
    objectTreeData,
    objectTree,
    selectedObjectTreeNode,
    setSelectedObjectTreeNode,
    reloadObjectTree,
  } = props;
  const treeReq = objectTreeReq || {};
  const treeData = objectTreeData || {};
  const rawTree = asArray(objectTree || treeData.tree);
  const tree = useMemo(() => filterObjectTreeForWorkbench(rawTree), [treeData, rawTree.length]);
  const flat = useMemo(() => flattenObjectTree(tree), [treeData, tree.length]);
  const selectedNode = selectedObjectTreeNode;
  const setSelectedNode = setSelectedObjectTreeNode;
  const [entityText, setEntityText] = useState("");
  const [initialEntityText, setInitialEntityText] = useState("");
  const [entityMeta, setEntityMeta] = useState(null);
  const [message, setMessage] = useState("");
  const [busy, setBusy] = useState("");
  const [editing, setEditing] = useState(false);
  const [newKind, setNewKind] = useState("resource");
  const [newId, setNewId] = useState("");
  const dirty = entityText !== initialEntityText;
  const selectedEntityKey = selectedNode
    ? [
      selectedNode.id,
      selectedNode.entity_kind || selectedNode.kind || "",
      selectedNode.entity_id || "",
      selectedNode.rel_path || "",
      selectedNode.editable ? "editable" : "readonly",
    ].join("|")
    : "";

  useEffect(() => {
    const freshNode = selectedNode && flat.find((node) => node.id === selectedNode.id);
    if (freshNode) {
      if (
        freshNode !== selectedNode
        && (
          freshNode.entity_id !== selectedNode.entity_id
          || freshNode.rel_path !== selectedNode.rel_path
          || freshNode.label !== selectedNode.label
          || freshNode.kind !== selectedNode.kind
          || freshNode.entity_kind !== selectedNode.entity_kind
        )
      ) {
        setSelectedNode(freshNode);
      }
      return;
    }
    setSelectedNode(firstEditableNode(tree));
  }, [treeData && treeData.package_dir, flat.length]);

  useEffect(() => {
    setEditing(false);
  }, [selectedEntityKey]);

  useEffect(() => {
    let cancelled = false;
    async function loadEntity() {
      if (!selectedNode || !selectedNode.editable) {
        setEntityText("");
        setEntityMeta(null);
        return;
      }
      const params = new URLSearchParams({
        kind: selectedNode.entity_kind || selectedNode.kind || "",
        entity_id: selectedNode.entity_id || "",
        rel_path: selectedNode.rel_path || "",
      });
      try {
        const res = await fetch(`/api/object-project/entity?${params.toString()}`, { cache: "no-store" });
        const data = await res.json();
        if (cancelled) return;
        if (!res.ok || data.ok === false) throw new Error(data.message || data.error || `${res.status} ${res.statusText}`);
        setEntityMeta(data);
        const text = JSON.stringify(data.doc || {}, null, 2);
        setEntityText(text);
        setInitialEntityText(text);
        setMessage("");
      } catch (err) {
        if (!cancelled) {
          setEntityMeta({ ok: false, message: err.message || String(err) });
          setEntityText("");
          setInitialEntityText("");
          setMessage(err.message || String(err));
        }
      }
    }
    loadEntity();
    return () => { cancelled = true; };
  }, [selectedEntityKey, treeData && treeData.package_dir]);

  useEffect(() => {
    if (!dirty) return undefined;
    const handler = (event) => {
      event.preventDefault();
      event.returnValue = "对象工程存在未保存修改，确认关闭？";
      return event.returnValue;
    };
    window.addEventListener("beforeunload", handler);
    return () => window.removeEventListener("beforeunload", handler);
  }, [dirty]);

  async function runAction(url, body, onDone) {
    setBusy(url);
    setMessage("");
    try {
      const data = await apiPost(url, body);
      if (onDone) onDone(data);
      setMessage(data.message || "操作完成");
      return data;
    } catch (err) {
      setMessage(err.message || String(err));
      return null;
    } finally {
      setBusy("");
    }
  }

  function refreshModeler() {
    if (reloadObjectTree) reloadObjectTree();
    else if (treeReq.reload) treeReq.reload();
    if (reloadAll) reloadAll();
  }

  function selectNodeWithDirtyGuard(node) {
    if (dirty && !window.confirm("当前节点有未保存修改，确认切换节点并丢弃本次编辑？")) return false;
    setSelectedNode(node);
    return true;
  }

  function startNewEntity(explicitKind, explicitId) {
    if (!editing) {
      setMessage("请先点击编辑，再新建对象定义。");
      return;
    }
    const kind = explicitKind || newKind || (selectedNode && selectedNode.create_kind) || "resource";
    const id = explicitId || newId || `${kind}.draft.v1`;
    const doc = objectEntityTemplate(kind, id);
    setSelectedNode({
      id: `new:${kind}:${id}`,
      label: id,
      kind,
      entity_kind: kind,
      entity_id: id,
      editable: true,
      deletable: false,
      is_new: true,
    });
    setEntityMeta({ ok: true, kind, entity_id: id, rel_path: "" });
    setEntityText(JSON.stringify(doc, null, 2));
    setInitialEntityText("");
    setMessage(`已创建 ${entityKindLabel(kind)} 草稿，保存后进入对象草稿。`);
  }

  async function saveEntity() {
    if (!editing) {
      setMessage("请先点击编辑，再保存对象定义。");
      return;
    }
    if (!selectedNode || !selectedNode.editable) {
      setMessage("请选择可编辑节点");
      return;
    }
    let doc;
    try {
      doc = JSON.parse(entityText || "{}");
    } catch (err) {
      setMessage(`JSON 解析失败：${err.message || err}`);
      return;
    }
    const kind = selectedNode.entity_kind || selectedNode.kind;
    const entityId = firstDefined(
      selectedNode.entity_id,
      newId,
      doc.resource_id,
      doc.operator_id,
      doc.workflow_id,
      doc.profile_id,
      doc.schema_id,
      doc.component_id,
      doc.id,
    );
    await runAction("/api/object-project/save", {
      kind,
      entity_kind: kind,
      entity_id: entityId,
      rel_path: selectedNode.rel_path || (entityMeta && entityMeta.rel_path) || "",
      doc,
    }, () => {
      setInitialEntityText(JSON.stringify(doc, null, 2));
      refreshModeler();
    });
  }

  async function saveProjectState() {
    await runAction("/api/object-project/state", {
      selected_tree_path: selectedNode && selectedNode.id,
      last_opened_workflow: selectedWorkflowId || "",
      last_opened_run_profile: selectedRunProfileId || "",
      ui_state: {
        selected_node_kind: selectedKind || "",
        selected_node_label: selectedNode && selectedNode.label,
      },
    }, (data) => {
      if (data && data.semantic_unchanged === false) {
        setMessage("工程状态已保存，但语义文件 hash 发生变化，请检查。");
      }
      refreshModeler();
    });
  }

  async function closeProject() {
    if (dirty && !window.confirm("对象工程存在未保存修改，确认关闭？")) return;
    await runAction("/api/object/unload", {}, () => {
      setSelectedNode(null);
      setEntityText("");
      setInitialEntityText("");
      if (reloadAll) reloadAll();
    });
  }

  function handleTreeAction(action, node) {
    const id = action && action.id;
    if (id === "new") {
      if (!editing) {
        setMessage("请先点击编辑，再通过对象树新建定义。");
        return;
      }
      const targetKind = action.target_kind || node.create_kind || "resource";
      const draftId = `${targetKind}.draft.v1`;
      setNewKind(targetKind);
      setNewId(draftId);
      startNewEntity(targetKind, draftId);
      return;
    }
    if (id === "edit" || id === "view_json" || id === "inspect") {
      selectNodeWithDirtyGuard(node);
      return;
    }
    if (id === "delete") {
      if (!editing) {
        setMessage("请先点击编辑，再删除对象定义。");
        return;
      }
      if (!selectNodeWithDirtyGuard(node)) return;
      setTimeout(() => deleteEntityFor(node, false), 0);
      return;
    }
    if (id === "save_project_state") {
      saveProjectState();
      return;
    }
    if (id === "close_project") {
      closeProject();
      return;
    }
    if (id === "validate_project") {
      setMessage("对象包校验、编译和运行已统一移动到“作业”页。");
      if (setPage) setPage("runtime");
      return;
    }
    if (id === "open_run" && node.run && setRun) {
      setRun(node.run);
      if (setPage) setPage("inspector");
      return;
    }
    selectNodeWithDirtyGuard(node);
  }

  async function deleteEntity(force = false) {
    return deleteEntityFor(selectedNode, force);
  }

  async function deleteEntityFor(node, force = false) {
    if (!editing) {
      setMessage("请先点击编辑，再删除对象定义。");
      return;
    }
    if (!node || !node.deletable) {
      setMessage("当前节点不可删除");
      return;
    }
    const kind = node.entity_kind || node.kind;
    await runAction("/api/object-project/delete", {
      kind,
      entity_kind: kind,
      entity_id: node.entity_id,
      rel_path: node.rel_path || "",
      force,
    }, () => {
      setSelectedNode(null);
      refreshModeler();
    });
  }

  const references = entityMeta && entityMeta.references;
  const selectedKind = selectedNode && (selectedNode.entity_kind || selectedNode.kind);
  const canEdit = selectedNode && selectedNode.editable;
  const canDelete = selectedNode && selectedNode.deletable;
  const parsedEntityDoc = useMemo(() => parseJsonObject(entityText), [entityText]);
  const entityReadError = entityMeta && entityMeta.ok === false ? firstDefined(entityMeta.message, message, "实体读取失败") : "";
  const entityLoading = canEdit && !entityText && !entityReadError;
  const updateStructuredDoc = useCallback((nextDoc) => {
    setEntityText(JSON.stringify(nextDoc || {}, null, 2));
  }, []);

  return (
    <div className="object-modeler-layout object-modeler-layout-embedded">
      <Panel
        title="对象定义详情"
        subtitle={selectedNode ? `${entityKindLabel(selectedKind)} / ${selectedNode.label}` : "选择左侧对象树节点后查看和编辑"}
        className="object-modeler-editor-panel"
        toolbar={(
          <div className="button-row">
            <EditModeToolbar editing={editing} setEditing={setEditing} canEdit={!!treeData.package_dir && !!selectedNode} dirty={dirty} />
            {editing && <Button disabled={!canEdit} busy={busy === "/api/object-project/save"} tone="primary" onClick={saveEntity}>保存到对象草稿</Button>}
            {editing && canDelete && <Button busy={busy === "/api/object-project/delete"} tone="danger" onClick={() => deleteEntity(false)}>删除</Button>}
          </div>
        )}
      >
        {selectedNode && (
          <div className="entity-location-strip">
            <span>当前位置</span>
            <strong>{selectedNode.label}</strong>
            <code>{firstDefined(selectedNode.rel_path, selectedNode.entity_id, selectedNode.id)}</code>
          </div>
        )}
        {dirty && <div className="dirty-banner">当前定义有未保存修改</div>}
        {editing && (
          <details className="modeler-new-details">
            <summary>
              <strong>新建定义</strong>
              <span>也可以在左侧对象树对应目录上右键新建</span>
            </summary>
            <div className="modeler-new-row">
              <label><span>新建类型</span>
                <select value={newKind} onChange={(e) => setNewKind(e.target.value)}>
                  {["component", "resource", "asset_group", "domain_schema", "operator", "workflow", "run_profile", "runtime_profile"].map((kind) => (
                    <option key={kind} value={kind}>{entityKindLabel(kind)}</option>
                  ))}
                </select>
              </label>
              <label><span>id</span><input value={newId} onChange={(e) => setNewId(e.target.value)} placeholder="例如 model.transition.demo" /></label>
              <Button onClick={startNewEntity}>创建定义草稿</Button>
            </div>
          </details>
        )}
        {canEdit && entityReadError ? (
          <Empty title="实体读取失败" hint={entityReadError} />
        ) : entityLoading ? (
          <Empty title="正在读取定义" hint="等待对象树节点对应的 JSON 或数组实体加载。" />
        ) : canEdit && editing ? (
          <>
            <StructuredEntityEditor kind={selectedKind} doc={parsedEntityDoc} object={object} meta={entityMeta} onChange={updateStructuredDoc} />
            <details className="json-preview-details">
              <summary>
                <strong>高级 JSON</strong>
                <span>表单和 JSON 共享同一份对象草稿；未覆盖的高级字段可在这里编辑。</span>
              </summary>
              <textarea className="json-editor modeler-json-editor" value={entityText} onChange={(e) => setEntityText(e.target.value)} />
            </details>
          </>
        ) : canEdit ? (
          <StructuredEntityViewer kind={selectedKind} doc={parsedEntityDoc} object={object} meta={entityMeta} />
        ) : selectedNode && selectedNode.kind === "compiled_workflow" ? (
          <div className="modeler-boundary-empty">
            <Empty title="编译产物归作业页查看" hint="对象定义页不展示编译报告。请到“作业”页查看编译计划、preflight 与运行控制。" />
            <Button tone="primary" onClick={() => setPage("runtime")}>打开作业页</Button>
          </div>
        ) : selectedNode && selectedNode.kind === "run_package" ? (
          <div className="modeler-boundary-empty">
            <Empty title="运行结果归结果回放查看" hint="对象定义页不展示运行包、云图或 evidence。请到“结果回放”页查看分支时间线和场云图。" />
            <Button tone="primary" onClick={() => {
              if (selectedNode.run && setRun) setRun(selectedNode.run);
              setPage("inspector");
            }}>打开结果回放</Button>
          </div>
        ) : (
          <Empty title="不可编辑节点" hint="目录、编译产物和运行结果为只读节点。" />
        )}
      </Panel>

      <div className="object-modeler-right">
        <Panel title="对象定义页边界" subtitle="本页只负责对象包建模、查看、编辑和保存草稿">
          <KeyValue rows={[
            ["对象包", shortPath(treeData.package_dir)],
            ["对象域", treeData.package_domain || "-"],
            ["当前节点", selectedNode ? selectedNode.label : "-"],
            ["当前类型", entityKindLabel(selectedKind)],
            ["草稿状态", dirty ? "有未保存修改" : "无未保存修改"],
          ]} />
          <div className="button-row wrap">
            <Button tone="primary" onClick={() => setPage("runtime")}>打开作业控制台</Button>
            <Button disabled={!run} onClick={() => setPage("inspector")}>打开结果回放</Button>
          </div>
          {message && <div className="inline-message">{message}</div>}
        </Panel>

        <Panel title="引用关系" subtitle="只用于删除前确认当前定义是否被其他对象包文件引用">
          {references ? (
            <div className="reference-box">
              <strong>引用文件：{fmtNumber(references.count, 0)}</strong>
              {asArray(references.files).slice(0, 8).map((row) => <span key={row.rel_path}>{row.rel_path}</span>)}
              {editing && canDelete && references.count > 0 && <Button tone="danger" onClick={() => deleteEntity(true)}>强制删除当前实体</Button>}
            </div>
          ) : (
            <Empty title="暂无引用信息" hint="选择具体定义后显示引用文件，便于删除前确认。" />
          )}
        </Panel>
      </div>
    </div>
  );
}

function App() {
  const initialPage = initialPageFromLocation();
  const [page, setPage] = useState(initialPage);
  const [mode, setModeState] = useState(modeForPage(initialPage));
  const [run, setRun] = useState(initialRunFromLocation);
  const [selected, setSelected] = useState({ branch: "", port: "", step: "" });
  const [followLive, setFollowLive] = useState(true);
  const [selectedWorkflowId, setSelectedWorkflowId] = useState("");
  const [selectedRunProfileId, setSelectedRunProfileId] = useState("");
  const [compileResult, setCompileResult] = useState(null);
  const [preflightResult, setPreflightResult] = useState(null);
  const [selectedObjectTreeNode, setSelectedObjectTreeNode] = useState(null);
  const [sidebarWidth, setSidebarWidth] = useState(() => {
    const saved = Number(window.localStorage && window.localStorage.getItem("flightenv.sidebar.width"));
    return Number.isFinite(saved) && saved >= 260 && saved <= 560 ? saved : 324;
  });
  const dragSidebarRef = useRef(null);
  const sidebarWidthRef = useRef(sidebarWidth);
  const activePoll = (mode === "live" || page === "runtime") ? 1800 : 0;

  const workspaceReq = useJson("/api/workspace", [], { pollMs: 0 });
  const settingsReq = useJson("/api/settings", [], { pollMs: 0 });
  const objectReq = useJson("/api/object", [], { pollMs: 0 });
  const objectTreeReq = useJson("/api/object-project/tree", [objectReq.data && objectReq.data.package_dir, objectReq.data && objectReq.data.active_draft_id], { pollMs: 0 });
  const draftReq = useJson("/api/draft/status", [], { pollMs: 0 });
  const packagesReq = useJson("/api/object/packages", [], { pollMs: 0 });
  const runsReq = useJson("/api/runs", [], { pollMs: activePoll || 0 });
  const statusReq = useJson(run ? `/api/runtime/status?run=${encodeURIComponent(run)}` : "/api/runtime/status", [run], { pollMs: activePoll || 0 });

  const workspace = workspaceReq.data || {};
  const settings = settingsReq.data || {};
  const object = objectReq.data || {};
  const objectTreeData = objectTreeReq.data || {};
  const objectTreeRaw = asArray(objectTreeData.tree);
  const objectTree = useMemo(() => filterObjectTreeForWorkbench(objectTreeRaw), [objectTreeData, objectTreeRaw.length]);
  const activeObjectPackageLabel = shortPath(objectTreeData.package_dir || objectPackageRoot(workspace, object));
  const objectTreeFlat = useMemo(() => flattenObjectTree(objectTree), [objectTreeData, objectTree.length]);
  const draft = draftReq.data || {};
  const packages = packagesReq.data || {};
  const objectLoaded = isObjectLoaded(workspace, object);
  const objectValidation = workspace && workspace.object_validation;
  const runs = asArray(runsReq.data && runsReq.data.runs);

  useEffect(() => {
    const freshNode = selectedObjectTreeNode && objectTreeFlat.find((node) => node.id === selectedObjectTreeNode.id);
    if (freshNode) {
      if (
        freshNode !== selectedObjectTreeNode
        && (
          freshNode.entity_id !== selectedObjectTreeNode.entity_id
          || freshNode.rel_path !== selectedObjectTreeNode.rel_path
          || freshNode.label !== selectedObjectTreeNode.label
          || freshNode.kind !== selectedObjectTreeNode.kind
          || freshNode.entity_kind !== selectedObjectTreeNode.entity_kind
        )
      ) {
        setSelectedObjectTreeNode(freshNode);
      }
      return;
    }
    setSelectedObjectTreeNode(firstEditableNode(objectTree));
  }, [objectTreeData && objectTreeData.package_dir, objectTreeFlat.length]);

  useEffect(() => {
    if (!run && runs[0]) setRun(runs[0].run);
  }, [runs.length, run]);

  useEffect(() => {
    if (!objectLoaded) {
      setSelectedWorkflowId("");
      setSelectedRunProfileId("");
      setCompileResult(null);
      setPreflightResult(null);
      return;
    }
    const workflowList = asArray(object && object.workflows);
    const profileList = asArray(object && object.run_profiles);
    const workflowIds = new Set(workflowList.map(workflowId).filter(Boolean));
    const profileIds = new Set(profileList.map(profileId).filter(Boolean));
    let nextWorkflowId = selectedWorkflowId;
    if (nextWorkflowId && !workflowIds.has(nextWorkflowId)) nextWorkflowId = "";
    if (!nextWorkflowId && workflowList.length) nextWorkflowId = workflowId(workflowList[0]);
    let nextProfileId = selectedRunProfileId;
    if (nextProfileId && !profileIds.has(nextProfileId)) nextProfileId = "";
    if (!nextProfileId && profileList.length) {
      const selectedWorkflow = workflowList.find((wf) => workflowId(wf) === nextWorkflowId) || workflowList[0];
      const compatibleProfile = profileList.find((profile) => profileAllowsWorkflow(profile, selectedWorkflow)) || profileList[0];
      nextProfileId = profileId(compatibleProfile);
    }
    if (nextWorkflowId !== selectedWorkflowId) {
      setSelectedWorkflowId(nextWorkflowId || "");
      setCompileResult(null);
      setPreflightResult(null);
    }
    if (nextProfileId !== selectedRunProfileId) {
      setSelectedRunProfileId(nextProfileId || "");
      setCompileResult(null);
      setPreflightResult(null);
    }
  }, [objectLoaded, object && object.object_package_root, selectedWorkflowId, selectedRunProfileId]);

  const encodedRun = encodeURIComponent(run || "");
  const timelineReq = useJson(run ? `/api/timeline?run=${encodedRun}` : "", [run], { pollMs: activePoll, enabled: !!run });
  const dataplaneReq = useJson(run ? `/api/dataplane?run=${encodedRun}` : "", [run], { pollMs: activePoll, enabled: !!run });
  const runtimeReq = useJson(run ? `/api/runtime?run=${encodedRun}` : "", [run], { pollMs: activePoll, enabled: !!run });
  const evidenceReq = useJson(run ? `/api/evidence?run=${encodedRun}` : "", [run], { pollMs: page === "inspector" ? activePoll : 0, enabled: !!run });
  const diagnosticsReq = useJson(run ? `/api/diagnostics?run=${encodedRun}` : "/api/diagnostics", [run], { pollMs: page === "diagnostics" ? activePoll : 0 });

  const timeline = timelineReq.data || {};
  const dataplane = dataplaneReq.data || {};
  const runtime = runtimeReq.data || {};
  const evidence = evidenceReq.data || {};
  const diagnostics = diagnosticsReq.data || {};
  const currentRunActive = isActiveRun(run, runs, runtime, statusReq.data);

  const switchPage = useCallback((id) => {
    setPage(id);
    if (id === "inspector") {
      setFollowLive(currentRunActive);
      setModeState(currentRunActive ? "live" : "replay");
    }
    if (id === "runtime") setModeState("live");
    if (id === "workflow" || id === "modeler") setModeState("edit");
  }, [currentRunActive]);

  const selectObjectTreeNode = useCallback((node) => {
    setSelectedObjectTreeNode(node);
    const nextPage = modulePageForTreeNode(node);
    if (nextPage) switchPage(nextPage);
    if (node && node.kind === "run_package" && node.run) setRun(node.run);
  }, [switchPage]);

  const startSidebarResize = useCallback((event) => {
    event.preventDefault();
    dragSidebarRef.current = {
      startX: event.clientX,
      startWidth: sidebarWidth,
    };
    document.body.classList.add("sidebar-resizing");
  }, [sidebarWidth]);

  useEffect(() => {
    sidebarWidthRef.current = sidebarWidth;
  }, [sidebarWidth]);

  useEffect(() => {
    const onMove = (event) => {
      const drag = dragSidebarRef.current;
      if (!drag) return;
      const next = Math.max(260, Math.min(560, drag.startWidth + event.clientX - drag.startX));
      sidebarWidthRef.current = next;
      setSidebarWidth(next);
    };
    const onUp = () => {
      if (!dragSidebarRef.current) return;
      dragSidebarRef.current = null;
      document.body.classList.remove("sidebar-resizing");
      if (window.localStorage) window.localStorage.setItem("flightenv.sidebar.width", String(sidebarWidthRef.current));
    };
    window.addEventListener("pointermove", onMove);
    window.addEventListener("pointerup", onUp);
    return () => {
      window.removeEventListener("pointermove", onMove);
      window.removeEventListener("pointerup", onUp);
      document.body.classList.remove("sidebar-resizing");
    };
  }, []);

  const handleGlobalTreeAction = useCallback((action, node) => {
    if (!node) return;
    setSelectedObjectTreeNode(node);
    if (action && action.id === "open_run" && node.run) {
      setRun(node.run);
      switchPage("inspector");
      return;
    }
    switchPage(modulePageForTreeNode(node));
  }, [switchPage]);

  const setMode = useCallback((nextMode) => {
    setModeState(nextMode);
    if (nextMode === "live") {
      setFollowLive(true);
      setPage("inspector");
    }
    if (nextMode === "replay") {
      setFollowLive(false);
      setPage("inspector");
    }
    if (nextMode === "edit") setPage("workflow");
  }, []);

  useEffect(() => {
    if (!run || page !== "inspector") return;
    if (!currentRunActive && mode !== "replay") {
      setFollowLive(false);
      setModeState("replay");
      return;
    }
    if (currentRunActive && followLive && mode !== "live") {
      setModeState("live");
    }
  }, [run, page, currentRunActive, followLive, mode]);

  useEffect(() => {
    setSelected((old) => {
      if (mode === "live" && !followLive) return old;
      const next = mode === "live"
        ? latestOnlineSelection(timeline, dataplane, old)
        : defaultSelection(timeline, dataplane, old);
      if (next.branch === old.branch && next.port === old.port && String(next.step) === String(old.step)) return old;
      return next;
    });
  }, [run, timelineReq.data, dataplaneReq.data, mode, followLive]);

  const reloadAll = useCallback(() => {
    workspaceReq.reload();
    settingsReq.reload();
    objectReq.reload();
    objectTreeReq.reload();
    draftReq.reload();
    packagesReq.reload();
    runsReq.reload();
    statusReq.reload();
    timelineReq.reload();
    dataplaneReq.reload();
    runtimeReq.reload();
    evidenceReq.reload();
    diagnosticsReq.reload();
  }, [workspaceReq.reload, settingsReq.reload, objectReq.reload, objectTreeReq.reload, draftReq.reload, packagesReq.reload, runsReq.reload, statusReq.reload, timelineReq.reload, dataplaneReq.reload, runtimeReq.reload, evidenceReq.reload, diagnosticsReq.reload]);

  const pageProps = {
    run,
    setRun,
    runs,
    workspace,
    settings,
    object,
    draft,
    packages,
    objectLoaded,
    objectValidation,
    timeline,
    dataplane,
    runtime,
    evidence,
    diagnostics,
    status: statusReq.data,
    selected,
    setSelected,
    selectedWorkflowId,
    setSelectedWorkflowId,
    selectedRunProfileId,
    setSelectedRunProfileId,
    compileResult,
    setCompileResult,
    preflightResult,
    setPreflightResult,
    reloadAll,
    mode,
    setMode,
    followLive,
    setFollowLive,
    setPage,
    objectTreeReq,
    objectTreeData,
    objectTree,
    selectedObjectTreeNode,
    setSelectedObjectTreeNode,
    reloadObjectTree: objectTreeReq.reload,
  };
  const blockingLoading = !workspaceReq.data && !settingsReq.data && !objectReq.data
    && (workspaceReq.loading || settingsReq.loading || objectReq.loading);
  const renderPage = () => {
    if (page === "workspace") return <WorkspacePage {...pageProps} />;
    if (!objectLoaded && !["workspace", "modeler", "config", "inspector", "diagnostics"].includes(page)) {
      return <ObjectRequiredPage setPage={setPage} />;
    }
    if (page === "overview") return <OverviewPage {...pageProps} />;
    if (page === "inspector") return <RunInspectorPage {...pageProps} />;
    if (page === "runtime") return <RunConsolePage {...pageProps} />;
    if (page === "modeler") return <ObjectModelerPage {...pageProps} />;
    if (page === "object") return <ObjectPage {...pageProps} />;
    if (page === "resources") return <ResourceTreePage {...pageProps} />;
    if (page === "operators") return <OperatorsPage {...pageProps} />;
    if (page === "workflow") return <WorkflowPage {...pageProps} />;
    if (page === "config") return <ConfigPage {...pageProps} />;
    return <DiagnosticsPagePhase7 {...pageProps} />;
  };

  return (
    <div className="app-frame" style={{ "--sidebar-width": `${sidebarWidth}px` }}>
      <aside className="sidebar">
        <div className="brand">
          <span className="brand-mark">FE</span>
          <div>
            <strong>FlightEnv</strong>
            <small>Abaqus-style Object Workbench</small>
          </div>
        </div>
        <div className="sidebar-project-actions">
          <Button onClick={() => switchPage("workspace")}>打开 / 新建</Button>
          <Button disabled={!objectPackageRoot(workspace, object)} onClick={() => switchPage("modeler")}>编辑对象树</Button>
        </div>
        <div className="sidebar-tree-head">
          <strong>Model Tree</strong>
          <span>{objectPackageRoot(workspace, object) ? activeObjectPackageLabel : "未载入对象包"}</span>
        </div>
        <ObjectModelerTree
          nodes={objectTree}
          selectedId={selectedObjectTreeNode && selectedObjectTreeNode.id}
          onSelect={selectObjectTreeNode}
          onAction={handleGlobalTreeAction}
        />
        <div className="side-foot">Object Tree · PDK/Runtime gated</div>
        <div
          className="sidebar-resizer"
          role="separator"
          aria-orientation="vertical"
          aria-label="调整对象树宽度"
          onPointerDown={startSidebarResize}
        />
      </aside>

      <main className="main">
        <header className="topbar">
          <div className="module-card-strip">
            {MODULE_CARDS.map(([id, label, hint]) => (
              <button
                key={id}
                className={cls("module-card", (LEGACY_PAGE_ALIASES[page] || page) === id && "active")}
                onClick={() => switchPage(id)}
              >
                <strong>{label}</strong>
                <span>{hint}</span>
              </button>
            ))}
          </div>
          <div className="context-strip">
            <div><span>object</span><strong>{objectLoaded ? firstDefined(object.object_id, object.name, "-") : "未载入"}</strong></div>
            <div><span>run</span><strong>{run || "-"}</strong></div>
            <div><span>workflow</span><strong>{firstDefined(selectedWorkflowId, timeline.workflow_id, "-")}</strong></div>
            <div><span>profile</span><strong>{firstDefined(selectedRunProfileId, runtime.run_profile_id, timeline.run_profile_id, "-")}</strong></div>
            <div><span>runtime</span><strong>{displayStatus(firstDefined(runtime.status, statusReq.data && statusReq.data.status))}</strong></div>
            <div><span>clock</span><strong>{fmtTime(firstDefined(runtime.clock_s, runtime.public_time_s, timeline.public_time_s))}</strong></div>
            <div><span>evidence</span><strong>{shortPath(firstDefined(runtime.evidence_root, timeline.evidence_root, workspace.artifacts_root))}</strong></div>
            <div><span>run state</span><strong>{runScopeLabel(run, runs, runtime, statusReq.data, followLive)}</strong></div>
          </div>
          <div className="top-actions">
            <Button onClick={reloadAll}>刷新</Button>
            <Button onClick={() => switchPage("workspace")}>载入对象</Button>
          </div>
        </header>

        <section className="content">
          <LoadingMask
            loading={blockingLoading}
            error={workspaceReq.error || settingsReq.error || objectReq.error || draftReq.error || packagesReq.error || runsReq.error || timelineReq.error || dataplaneReq.error || runtimeReq.error}
          />
          <SurfaceBoundary page={page} />
          {renderPage()}
        </section>
      </main>
    </div>
  );
}

ReactDOM.createRoot(document.getElementById("root")).render(<App />);
