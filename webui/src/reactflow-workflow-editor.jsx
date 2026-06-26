import React, { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import {
  addEdge,
  applyEdgeChanges,
  applyNodeChanges,
  Background,
  Controls,
  Handle,
  MarkerType,
  MiniMap,
  Position,
  ReactFlow,
  ReactFlowProvider,
} from "@xyflow/react";
import "@xyflow/react/dist/style.css";
import "./reactflow-workflow-editor.css";

const NODE_TYPE = "operatorNode";
const BOUNDARY_NODE_TYPE = "boundaryNode";

const WORKFLOW_TEMPLATES = {
  blank: {
    label: "空白 workflow",
    phase: "main",
    phase_id: "main",
    stages: [],
  },
  single_stage: {
    label: "单阶段 workflow",
    phase: "main",
    phase_id: "main",
    stages: [["stage_1", "stage"]],
  },
  two_stage: {
    label: "双阶段 workflow",
    phase: "main",
    phase_id: "main",
    stages: [["stage_1", "stage"], ["stage_2", "stage"]],
  },
};

function arr(value) {
  return Array.isArray(value) ? value : [];
}

function clone(value) {
  return JSON.parse(JSON.stringify(value || {}));
}

function safeId(text, fallback = "workflow") {
  const cleaned = String(text || "").trim().replace(/[^\w.\-]+/g, "_").replace(/^_+|_+$/g, "");
  return cleaned || fallback;
}

function humanizeId(text) {
  const value = String(text || "").trim();
  if (!value) return "";
  return value
    .replace(/_ids$/i, "")
    .replace(/_id$/i, "")
    .replace(/[_\-]+/g, " ")
    .replace(/\s+/g, " ")
    .replace(/\b\w/g, (match) => match.toUpperCase());
}

function stageFamilyLabel(value) {
  return humanizeId(value) || "Stage";
}

function portLabel(port) {
  if (!port) return "-";
  const contract = port.contract_id ? ` · ${port.contract_id}` : "";
  return `${port.port_id || "-"}${contract}`;
}

function typedContract(port) {
  const typed = port?.typed_io_contract || {};
  return {
    contract_id: port?.contract_id || typed.schema_id || "",
    frame_contract: port?.frame_contract || "",
    dto_name: typed.dto_name || typed.type_name || "",
    status: typed.status || "",
    value_kind: port?.value_kind || "",
  };
}

function findPort(node, direction, portId) {
  const ports = direction === "output" ? arr(node?.data?.outputs) : arr(node?.data?.inputs);
  return ports.find((port) => port.port_id === portId) || null;
}

function validatePortContract(sourceNode, sourcePortId, targetNode, targetPortId) {
  const sourcePort = findPort(sourceNode, "output", sourcePortId);
  const targetPort = findPort(targetNode, "input", targetPortId);
  if (!sourcePortId || !targetPortId) {
    return { ok: false, message: "连线必须选择明确的源输出端口和目标输入端口。" };
  }
  if (!sourcePort) {
    return { ok: false, message: `源端口 ${sourcePortId} 不是 ${sourceNode?.data?.node_id || "source"} 的输出端口。` };
  }
  if (!targetPort) {
    return { ok: false, message: `目标端口 ${targetPortId} 不是 ${targetNode?.data?.node_id || "target"} 的输入端口。` };
  }
  const sourceContract = typedContract(sourcePort);
  const targetContract = typedContract(targetPort);
  if (!sourceContract.contract_id || !targetContract.contract_id) {
    return { ok: false, message: `端口缺少 contract_id：${portLabel(sourcePort)} → ${portLabel(targetPort)}` };
  }
  if (sourceContract.contract_id !== targetContract.contract_id) {
    return {
      ok: false,
      message: `端口 contract 不匹配：${sourceContract.contract_id} → ${targetContract.contract_id}`,
    };
  }
  return { ok: true, sourcePort, targetPort };
}

function nodeKey(phaseId, stageId, nodeId) {
  return `${phaseId || "phase"}::${stageId || "stage"}::${nodeId || "node"}`;
}

function splitNodeKey(key) {
  const [phase_id = "", stage_id = "", node_id = ""] = String(key || "").split("::");
  return { phase_id, stage_id, node_id };
}

function stageEntries(workflow) {
  const rows = [];
  arr(workflow?.phases).forEach((phase) => {
    const incoming = new Map();
    const outgoing = new Map();
    arr(phase.stage_edges).forEach((edge) => {
      const fromStage = edge?.from?.stage_id || "";
      const toStage = edge?.to?.stage_id || "";
      if (fromStage) outgoing.set(fromStage, (outgoing.get(fromStage) || 0) + 1);
      if (toStage) incoming.set(toStage, (incoming.get(toStage) || 0) + 1);
    });
    arr(phase.stages).forEach((stage) => {
      const nodes = arr(stage.subgraph?.nodes);
      const edges = arr(stage.subgraph?.edges);
      rows.push({
        phase_id: phase.phase_id || "",
        stage_id: stage.stage_id || "",
        stage_family: stage.stage_family || "",
        node_count: nodes.length,
        edge_count: edges.length,
        cross_in_count: incoming.get(stage.stage_id || "") || 0,
        cross_out_count: outgoing.get(stage.stage_id || "") || 0,
        input_count: arr(stage.subgraph?.stage_inputs).length,
        output_count: arr(stage.subgraph?.stage_outputs).length,
        label: `${phase.phase_id || "phase"} / ${stage.stage_id || "stage"}`,
      });
    });
  });
  return rows;
}

function stageKeyOf(stage) {
  return `${stage?.phase_id || ""}::${stage?.stage_id || ""}`;
}

function firstStageKey(workflow) {
  const phase = arr(workflow?.phases)[0];
  const stage = arr(phase?.stages)[0];
  return phase && stage ? `${phase.phase_id || ""}::${stage.stage_id || ""}` : "";
}

function workflowLabel(workflow) {
  const id = workflow.workflow_id || workflow.id || "workflow";
  const stageText = workflow.stage_count ? ` · ${workflow.stage_count} stages` : "";
  const fileText = workflow.spec_file ? ` · ${workflow.spec_file}` : "";
  return `${id}${stageText}${fileText}`;
}

function buildWorkflowTemplate({ workflowId, objectId, templateId }) {
  const template = WORKFLOW_TEMPLATES[templateId] || WORKFLOW_TEMPLATES.blank;
  const phaseId = template.phase_id || template.phase || "phase";
  const workflowSafeId = safeId(workflowId, `${objectId || "object"}.workflow.${Date.now()}.v1`);
  return {
    workflow_id: workflowSafeId,
    object_id: objectId || "",
    phase: template.phase,
    description: "Created in FlightEnv WebUI workflow studio. Add registered operators, declare stage boundary ports, then save as an object draft before running.",
    clock: {
      source: "simulation",
      time_unit: "s",
    },
    phases: [
      {
        phase_id: phaseId,
        stages: template.stages.map(([stageId, family]) => ({
          stage_id: stageId,
          stage_family: family,
          operator_refs: [],
          subgraph: {
            nodes: [],
            edges: [],
            stage_inputs: [],
            stage_outputs: [],
          },
        })),
        stage_edges: [],
      },
    ],
  };
}

function stagePortRef(ref) {
  const text = String(ref || "");
  const dot = text.indexOf(".");
  if (dot <= 0) return { node_id: "", port_id: text, label: text, ref: text };
  return {
    node_id: text.slice(0, dot),
    port_id: text.slice(dot + 1),
    label: text.slice(dot + 1),
    ref: text,
  };
}

function normalizeStagePortRefs(refs) {
  return Array.from(new Set(arr(refs).map((ref) => String(ref || "").trim()).filter(Boolean)));
}

function rewriteStagePortNodeRef(ref, oldNodeId, newNodeId) {
  const text = String(ref || "");
  const prefix = `${oldNodeId}.`;
  return text.startsWith(prefix) ? `${newNodeId}.${text.slice(prefix.length)}` : text;
}

function isMacroHiddenStage(stage) {
  return Boolean(stage?.macro_hidden || stage?.ui_hidden || stage?.materialization?.macro_hidden);
}

function macroStageEntries(stages) {
  return stages.filter((stage) => !isMacroHiddenStage(stage));
}

function stageConnectivityText(stage) {
  return `${stage.node_count} nodes / 内部 ${stage.edge_count} edges / 跨阶段 入${stage.cross_in_count || 0} 出${stage.cross_out_count || 0}`;
}

function hiddenStageDetails(stages, parentStage) {
  return stages.filter((stage) => (
    isMacroHiddenStage(stage)
    && stage.phase_id === parentStage.phase_id
    && (stage.parent_stage_id === parentStage.stage_id || stage.stage_family === parentStage.stage_family)
  ));
}

function macroStageEdges(workflow, macroStages) {
  const macroKeys = new Set(macroStages.map(stageKeyOf));
  const rows = [];
  arr(workflow?.phases).forEach((phase) => {
    const phaseId = phase.phase_id || "";
    arr(phase.stage_edges).forEach((edge, index) => {
      const fromStage = edge?.from?.stage_id || "";
      const toStage = edge?.to?.stage_id || "";
      const fromKey = `${phaseId}::${fromStage}`;
      const toKey = `${phaseId}::${toStage}`;
      if (!macroKeys.has(fromKey) || !macroKeys.has(toKey)) return;
      rows.push({
        id: `${phaseId}:${fromStage}->${toStage}:${index}`,
        phase_id: phaseId,
        from_stage_id: fromStage,
        to_stage_id: toStage,
        from_port: `${edge?.from?.node_id || "-"}.${edge?.from?.port_id || "-"}`,
        to_port: `${edge?.to?.node_id || "-"}.${edge?.to?.port_id || "-"}`,
      });
    });
  });
  return rows;
}

function policyText(value) {
  if (value === undefined || value === null || value === "") return "-";
  if (Array.isArray(value)) return value.join(", ");
  if (typeof value === "object") return JSON.stringify(value);
  return String(value);
}

function policyField(policy, keys) {
  for (const key of keys) {
    if (policy && policy[key] !== undefined && policy[key] !== null && policy[key] !== "") return policy[key];
  }
  return "";
}

function internalPolicyText(row) {
  if (row.internal_dt_s !== undefined && row.internal_dt_s !== null && row.internal_dt_s !== "") {
    const substeps = row.substeps !== undefined && row.substeps !== null && row.substeps !== ""
      ? `${row.substeps} substeps`
      : "substeps 未声明";
    return `${row.internal_dt_s}s / ${substeps}`;
  }
  return "无内部子步";
}

function collectNodeTimePolicies(workflow, opMap) {
  const rows = [];
  arr(workflow?.phases).forEach((phase) => {
    arr(phase.stages).forEach((stage) => {
      arr(stage.subgraph?.nodes).forEach((node) => {
        const op = opMap.get(node.operator_ref) || {};
        const timePolicy = op.time_policy || {};
        const override = node.time_policy_override || {};
        const integrationPolicy = op.integration_policy || {};
        rows.push({
          key: `${phase.phase_id || ""}.${stage.stage_id || ""}.${node.node_id || ""}`,
          phase_id: phase.phase_id || "",
          stage_id: stage.stage_id || "",
          node_id: node.node_id || "",
          operator_ref: node.operator_ref || "",
          operator_display_name: operatorDisplayName(op, node.operator_ref || ""),
          kind: policyField(override, ["kind"]) || policyField(timePolicy, ["kind"]) || "-",
          sample_period_s: policyField(override, ["sample_period_s"]) || policyField(timePolicy, ["sample_period_s"]),
          output_time_role: policyField(override, ["output_time_role"]) || policyField(timePolicy, ["output_time_role"]),
          alignment: policyField(override, ["alignment", "input_resampling"]) || policyField(timePolicy, ["alignment", "input_resampling"]),
          internal_dt_s: policyField(integrationPolicy, ["internal_dt_s"]),
          substeps: policyField(integrationPolicy, ["substeps_per_output", "max_substeps"]),
          override,
        });
      });
    });
  });
  return rows;
}

function operatorMap(operators) {
  return new Map(arr(operators).map((op) => [op.operator_id || op.id, op]));
}

function operatorDisplayName(op, fallback = "") {
  return op?.display_name || op?.title || op?.name || fallback || op?.operator_id || op?.id || "operator";
}

function nodeNameFromOperator(operatorId) {
  const parts = String(operatorId || "operator").split(".");
  const base = parts.length > 3 ? parts[parts.length - 3] : parts[0];
  return String(base || "operator").replace(/[^a-zA-Z0-9_]+/g, "_").replace(/^_+|_+$/g, "") || "operator";
}

function uniqueNodeId(nodes, base) {
  const used = new Set(nodes.map((node) => node.data.node_id));
  let id = base;
  let i = 2;
  while (used.has(id)) {
    id = `${base}_${i}`;
    i += 1;
  }
  return id;
}

function workflowToFlow(workflow, opMap) {
  const stages = stageEntries(workflow);
  const stageIndex = new Map(stages.map((stage, index) => [`${stage.phase_id}::${stage.stage_id}`, index]));
  const perStageCount = new Map();
  const nodes = [];
  const edges = [];

  arr(workflow?.phases).forEach((phase) => {
    arr(phase.stages).forEach((stage) => {
      const phaseId = phase.phase_id || "";
      const stageId = stage.stage_id || "";
      const stageKey = `${phaseId}::${stageId}`;
      const baseX = 80 + (stageIndex.get(stageKey) || 0) * 330;

      arr(stage.subgraph?.nodes).forEach((node) => {
        const count = perStageCount.get(stageKey) || 0;
        perStageCount.set(stageKey, count + 1);
        const op = opMap.get(node.operator_ref) || {};
        const pos = node.ui?.position || {};
        const id = nodeKey(phaseId, stageId, node.node_id);
        nodes.push({
          id,
          type: NODE_TYPE,
          position: {
            x: Number.isFinite(Number(pos.x)) ? Number(pos.x) : baseX,
            y: Number.isFinite(Number(pos.y)) ? Number(pos.y) : 70 + count * 145,
          },
          data: {
            phase_id: phaseId,
            stage_id: stageId,
            stage_family: stage.stage_family || "",
            node_id: node.node_id || "",
            operator_ref: node.operator_ref || "",
            operator_display_name: operatorDisplayName(op, node.operator_ref || ""),
            family: op.family || op.operator_family || stage.stage_family || "",
            inputs: arr(op.inputs),
            outputs: arr(op.outputs),
            typed_io_contract: clone(op.typed_io_contract || {}),
            activation_policy: clone(node.activation_policy || {}),
            raw: clone(node),
          },
        });
      });

      arr(stage.subgraph?.edges).forEach((edge, index) => {
        const from = edge.from || {};
        const to = edge.to || {};
        const source = nodeKey(phaseId, stageId, from.node_id);
        const target = nodeKey(phaseId, stageId, to.node_id);
        edges.push({
          id: `${source}:${from.port_id || ""}->${target}:${to.port_id || ""}:${index}`,
          source,
          target,
          sourceHandle: from.port_id || "",
          targetHandle: to.port_id || "",
          type: "smoothstep",
          markerEnd: { type: MarkerType.ArrowClosed },
          label: `${from.port_id || "-"} → ${to.port_id || "-"}`,
          data: { phase_id: phaseId, stage_id: stageId },
        });
      });
    });
  });

  return { nodes, edges };
}

function flowToWorkflow(workflow, nodes, edges) {
  const next = clone(workflow);
  const nodesByStage = new Map();
  const edgesByStage = new Map();

  nodes.forEach((node) => {
    const key = `${node.data.phase_id}::${node.data.stage_id}`;
    if (!nodesByStage.has(key)) nodesByStage.set(key, []);
    const raw = clone(node.data.raw || {});
    raw.node_id = node.data.node_id;
    raw.operator_ref = node.data.operator_ref;
    raw.activation_policy = clone(node.data.activation_policy || {});
    raw.ui = { ...(raw.ui || {}), position: { x: Math.round(node.position.x), y: Math.round(node.position.y) } };
    nodesByStage.get(key).push(raw);
  });

  edges.forEach((edge) => {
    const key = `${edge.data?.phase_id || splitNodeKey(edge.source).phase_id}::${edge.data?.stage_id || splitNodeKey(edge.source).stage_id}`;
    if (!edgesByStage.has(key)) edgesByStage.set(key, []);
    edgesByStage.get(key).push({
      from: { node_id: splitNodeKey(edge.source).node_id, port_id: edge.sourceHandle || "" },
      to: { node_id: splitNodeKey(edge.target).node_id, port_id: edge.targetHandle || "" },
    });
  });

  arr(next.phases).forEach((phase) => {
    arr(phase.stages).forEach((stage) => {
      const key = `${phase.phase_id || ""}::${stage.stage_id || ""}`;
      const stageNodes = nodesByStage.get(key) || [];
      stage.subgraph = stage.subgraph || {};
      stage.subgraph.nodes = stageNodes;
      stage.subgraph.edges = edgesByStage.get(key) || [];
      stage.operator_refs = Array.from(new Set(stageNodes.map((node) => node.operator_ref).filter(Boolean)));
    });
  });
  return next;
}

function withWiringStatus(nodes, edges) {
  const degree = new Map();
  nodes.forEach((node) => degree.set(node.id, { in: 0, out: 0 }));
  edges.forEach((edge) => {
    if (degree.has(edge.source)) degree.get(edge.source).out += 1;
    if (degree.has(edge.target)) degree.get(edge.target).in += 1;
  });
  return nodes.map((node) => {
    const d = degree.get(node.id) || { in: 0, out: 0 };
    const hasInputs = arr(node.data.inputs).length > 0;
    const hasOutputs = arr(node.data.outputs).length > 0;
    const isUnwired = hasInputs && hasOutputs && d.in === 0 && d.out === 0;
    return { ...node, data: { ...node.data, wiring: d, unwired: isUnwired } };
  });
}

function stageFromWorkflow(workflow, stageKey) {
  const [phaseId = "", stageId = ""] = String(stageKey || "").split("::");
  for (const phase of arr(workflow?.phases)) {
    if ((phase.phase_id || "") !== phaseId) continue;
    for (const stage of arr(phase.stages)) {
      if ((stage.stage_id || "") === stageId) return stage;
    }
  }
  return null;
}

function stageBoundaryGraph(stageKey, stage, stageNodes) {
  if (!stage || !stageKey) return { nodes: [], edges: [] };
  const [phaseId = "", stageId = ""] = String(stageKey).split("::");
  const minX = stageNodes.length ? Math.min(...stageNodes.map((node) => node.position.x)) : 240;
  const maxX = stageNodes.length ? Math.max(...stageNodes.map((node) => node.position.x)) : 580;
  const inputs = arr(stage.subgraph?.stage_inputs).map(stagePortRef);
  const outputs = arr(stage.subgraph?.stage_outputs).map(stagePortRef);
  const nodeIds = new Set(stageNodes.map((node) => node.data.node_id));
  const boundaryNodes = [];
  const boundaryEdges = [];

  const inputNodeId = `${stageKey}::__stage_input`;
  boundaryNodes.push({
    id: inputNodeId,
    type: BOUNDARY_NODE_TYPE,
    position: { x: minX - 300, y: 70 },
    draggable: false,
    connectable: true,
    selectable: true,
    data: {
      boundary_kind: "input",
      title: "阶段输入",
      subtitle: "上游阶段、外部输入或运行时端口",
      ports: inputs,
    },
  });
  inputs.forEach((port, index) => {
    if (!port.node_id || !nodeIds.has(port.node_id)) return;
    boundaryEdges.push({
      id: `${inputNodeId}->${nodeKey(phaseId, stageId, port.node_id)}:${port.port_id}:${index}`,
      source: inputNodeId,
      target: nodeKey(phaseId, stageId, port.node_id),
      sourceHandle: port.ref,
      targetHandle: port.port_id,
      type: "smoothstep",
      animated: true,
      selectable: true,
      markerEnd: { type: MarkerType.ArrowClosed },
      label: port.label,
      className: "rf-boundary-edge",
      data: { synthetic: true, boundary_kind: "input", port_ref: port.ref, phase_id: phaseId, stage_id: stageId },
    });
  });

  const outputNodeId = `${stageKey}::__stage_output`;
  boundaryNodes.push({
    id: outputNodeId,
    type: BOUNDARY_NODE_TYPE,
    position: { x: maxX + 330, y: 70 },
    draggable: false,
    connectable: true,
    selectable: true,
    data: {
      boundary_kind: "output",
      title: "阶段输出",
      subtitle: "下游阶段、公开输出或证据端口",
      ports: outputs,
    },
  });
  outputs.forEach((port, index) => {
    if (!port.node_id || !nodeIds.has(port.node_id)) return;
    boundaryEdges.push({
      id: `${nodeKey(phaseId, stageId, port.node_id)}:${port.port_id}->${outputNodeId}:${index}`,
      source: nodeKey(phaseId, stageId, port.node_id),
      target: outputNodeId,
      sourceHandle: port.port_id,
      targetHandle: port.ref,
      type: "smoothstep",
      animated: true,
      selectable: true,
      markerEnd: { type: MarkerType.ArrowClosed },
      label: port.label,
      className: "rf-boundary-edge",
      data: { synthetic: true, boundary_kind: "output", port_ref: port.ref, phase_id: phaseId, stage_id: stageId },
    });
  });

  return { nodes: boundaryNodes, edges: boundaryEdges };
}

function OperatorNode({ data, selected }) {
  const inPorts = arr(data.inputs);
  const outPorts = arr(data.outputs);
  const handleTop = (index, total) => `${((index + 1) / (total + 1)) * 100}%`;
  return (
    <div className={`rf-operator-node ${selected ? "selected" : ""} ${data.unwired ? "unwired" : ""}`}>
      {inPorts.map((port, index) => (
        <Handle
          key={`in-${port.port_id}`}
          id={port.port_id}
          type="target"
          position={Position.Left}
          style={{ top: handleTop(index, inPorts.length) }}
          title={portLabel(port)}
        />
      ))}
      {outPorts.map((port, index) => (
        <Handle
          key={`out-${port.port_id}`}
          id={port.port_id}
          type="source"
          position={Position.Right}
          style={{ top: handleTop(index, outPorts.length) }}
          title={portLabel(port)}
        />
      ))}
      <div className="rf-node-top">
        <strong>{data.operator_display_name || data.node_id}</strong>
        <span>{data.stage_id}</span>
      </div>
      <small>{data.node_id} · {data.operator_ref}</small>
      <div className="rf-node-bottom">
        <span>{data.family || "-"}</span>
        <em>{data.unwired ? "未接线" : (data.activation_policy?.enabled === false ? "disabled" : "enabled")}</em>
      </div>
    </div>
  );
}

function BoundaryNode({ data, selected }) {
  const ports = arr(data.ports);
  const isInput = data.boundary_kind === "input";
  const handleTop = (index, total) => `${((index + 1) / (total + 1)) * 100}%`;
  return (
    <div className={`rf-boundary-node ${isInput ? "input" : "output"} ${selected ? "selected" : ""}`}>
      {ports.map((port, index) => (
        <Handle
          key={port.ref || port.label || port.port_id || index}
          id={port.ref || port.label || port.port_id}
          type={isInput ? "source" : "target"}
          position={isInput ? Position.Right : Position.Left}
          style={{ top: handleTop(index, ports.length) }}
          title={port.ref || port.label || port.port_id}
        />
      ))}
      <div className="rf-boundary-head">
        <strong>{data.title}</strong>
        <span>{data.subtitle}</span>
      </div>
      <div className="rf-boundary-ports">
        {ports.map((port, index) => (
          <small key={`${port.label}-${index}`}>{port.label || port.port_id}</small>
        ))}
        {!ports.length && <small className="empty">尚未声明端口，选中后可在右侧新增</small>}
      </div>
    </div>
  );
}

function PortContractTable({ title, ports, direction }) {
  return (
    <div className="rf-port-contracts">
      <h4>{title}</h4>
      {arr(ports).map((port) => {
        const typed = typedContract(port);
        return (
          <div className="rf-port-row" key={`${direction}-${port.port_id}`}>
            <strong>{port.port_id || "-"}</strong>
            <span>{typed.contract_id || "-"}</span>
            <small>{typed.frame_contract || "-"} · {typed.dto_name || "typed DTO 未声明"}</small>
          </div>
        );
      })}
      {!arr(ports).length && <p>未声明{direction === "input" ? "输入" : "输出"}端口。</p>}
    </div>
  );
}

function PolicyCard({ title, policy, rows }) {
  return (
    <div className="rf-policy-card">
      <strong>{title}</strong>
      {rows.map(([label, value]) => (
        <span key={label}><em>{label}</em>{policyText(value)}</span>
      ))}
      {!rows.length && <span><em>状态</em>未声明</span>}
      {policy?.notes && <small>{policy.notes}</small>}
    </div>
  );
}

function TimeBranchPolicyPanel({ workflow, opMap }) {
  const clock = workflow?.clock || {};
  const solver = workflow?.solver_policy || {};
  const scheduler = workflow?.scheduler_policy || {};
  const branch = workflow?.branching_policy || {};
  const checkpoint = workflow?.checkpoint_policy || {};
  const stop = workflow?.stop_policy || {};
  const nodeTimeRows = collectNodeTimePolicies(workflow, opMap);
  const overrideCount = nodeTimeRows.filter((row) => Object.keys(row.override || {}).length > 0).length;

  return (
    <div className="rf-policy-panel">
      <div className="rf-policy-head">
        <div>
          <strong>时间与分支策略</strong>
          <span>读取 workflow clock/solver/branching/stop/checkpoint，以及 operator time_policy / node time_policy_override。</span>
        </div>
        <small>{nodeTimeRows.length} nodes · {overrideCount} overrides</small>
      </div>
      <div className="rf-policy-grid">
        <PolicyCard title="全局时钟" policy={clock} rows={[
          ["clock_id", clock.clock_id],
          ["source", clock.source],
          ["source_ref", clock.source_ref],
          ["start_time_s", clock.start_time_s],
        ]} />
        <PolicyCard title="求解步进" policy={solver} rows={[
          ["kind", solver.kind],
          ["base_dt_s", solver.base_dt_s],
          ["max_steps", solver.max_steps],
          ["max_substeps", solver.max_substeps],
          ["event_rollback", solver.event_rollback],
        ]} />
        <PolicyCard title="调度策略" policy={scheduler} rows={[
          ["max_parallelism", scheduler.max_parallelism],
          ["ready_queue", scheduler.ready_queue_policy],
          ["resource_conflict", scheduler.resource_conflict_policy],
          ["deadline", scheduler.deadline_policy],
          ["record_timeline", scheduler.record_timeline],
        ]} />
        <PolicyCard title="分支策略" policy={branch} rows={[
          ["enabled", branch.enabled],
          ["target", branch.target_workflow_id],
          ["trigger", branch.trigger_kind],
          ["every_n_frames", branch.every_n_frames],
          ["seed", branch.seed_policy],
          ["max_concurrent", branch.max_concurrent_branches],
          ["cancel", branch.cancel_policy],
        ]} />
        <PolicyCard title="停止策略" policy={stop} rows={[
          ["kind", stop.kind],
          ["alternatives", arr(stop.alternatives).map((item) => item.kind || item.stop_reason || "condition")],
          ["max_iterations", stop.max_iterations],
          ["max_time_s", stop.max_time_s],
        ]} />
        <PolicyCard title="检查点" policy={checkpoint} rows={[
          ["enabled", checkpoint.enabled],
          ["interval_steps", checkpoint.interval_steps],
          ["on_shutdown", checkpoint.on_shutdown],
          ["replay_mode", checkpoint.replay_mode],
        ]} />
      </div>
      <div className="rf-node-time-table">
        <strong>节点时间策略明细</strong>
        <div className="rf-node-time-header">
          <span>节点</span><span>kind</span><span>period</span><span>output role</span><span>alignment</span><span>内部子步</span>
        </div>
        {nodeTimeRows.map((row) => (
          <div className="rf-node-time-row" key={row.key}>
            <span title={`${row.operator_ref}\n${row.phase_id}/${row.stage_id}`}>{row.operator_display_name || row.node_id}</span>
            <span>{policyText(row.kind)}</span>
            <span>{policyText(row.sample_period_s)}</span>
            <span>{policyText(row.output_time_role)}</span>
            <span>{policyText(row.alignment)}</span>
            <span>{internalPolicyText(row)}</span>
          </div>
        ))}
        {!nodeTimeRows.length && <p>当前 workflow 没有可读取的节点时间策略。</p>}
      </div>
    </div>
  );
}

function MacroWorkflowOverview({ workflow, stages, activeStageKey, onOpenStage, opMap }) {
  const phaseCount = arr(workflow?.phases).length;
  const macroStages = macroStageEntries(stages);
  const hiddenCount = stages.length - macroStages.length;
  const stageEdges = macroStageEdges(workflow, macroStages);
  const upstream = new Map();
  const downstream = new Map();
  stageEdges.forEach((edge) => {
    const fromKey = `${edge.phase_id}::${edge.from_stage_id}`;
    const toKey = `${edge.phase_id}::${edge.to_stage_id}`;
    if (!downstream.has(fromKey)) downstream.set(fromKey, []);
    if (!upstream.has(toKey)) upstream.set(toKey, []);
    downstream.get(fromKey).push(edge.to_stage_id);
    upstream.get(toKey).push(edge.from_stage_id);
  });
  return (
    <div className="rf-macro-overview">
      <div className="rf-macro-summary">
        <strong>{workflow?.workflow_id || "workflow"}</strong>
        <span>{phaseCount} phases / {macroStages.length} macro stages / {stageEdges.length} stage edges{hiddenCount ? ` / ${hiddenCount} materialization stages hidden` : ""}</span>
      </div>
      <WorkflowSemanticsPanel />
      <WorkflowGovernancePanel workflow={workflow} stages={stages} />
      <div className="rf-stage-flow dag">
        {macroStages.map((stage) => {
          const key = stageKeyOf(stage);
          const details = hiddenStageDetails(stages, stage);
          const ins = Array.from(new Set(upstream.get(key) || []));
          const outs = Array.from(new Set(downstream.get(key) || []));
          return (
            <button key={key} className={`rf-stage-card ${activeStageKey === key ? "active" : ""}`} onClick={() => onOpenStage(key)}>
              <span>{stage.phase_id || "phase"}</span>
              <strong>{stageFamilyLabel(stage.stage_family || stage.stage_id)}</strong>
              <em>{stage.stage_id || "stage"} · {stage.stage_family || "stage_family 未声明"}</em>
              <small>{stageConnectivityText(stage)}</small>
              <small>{stage.input_count}/{stage.output_count} boundary ports</small>
              <small>上游：{ins.length ? ins.join("、") : "workflow input"}</small>
              <small>下游：{outs.length ? outs.join("、") : "workflow output"}</small>
              {!!details.length && <small>包含对象声明的隐藏阶段：{details.map((item) => item.stage_id).join("、")}</small>}
            </button>
          );
        })}
      </div>
      <div className="rf-stage-edge-list">
        <strong>真实 stage_edges DAG</strong>
        {!stageEdges.length && <span>当前 workflow 未声明 stage_edges，宏观图仅显示阶段集合，不推断串行顺序。</span>}
        {stageEdges.slice(0, 18).map((edge) => (
          <span key={edge.id}>{edge.from_stage_id}.{edge.from_port} → {edge.to_stage_id}.{edge.to_port}</span>
        ))}
        {stageEdges.length > 18 && <span>还有 {stageEdges.length - 18} 条 stage_edges，进入 JSON 查看完整列表。</span>}
      </div>
    </div>
  );
}

function WorkflowSemanticsPanel() {
  return (
    <div className="rf-semantics-panel">
      <strong>对象编排规范</strong>
      <span>宏观层只展示对象包 workflow 声明的 phase、stage 与 stage_edges；UI 不替对象定义阶段物理含义。</span>
      <span>stage_family、端口角色、停止条件和公开输出都来自对象包或编译产物；平台只负责结构展示、连接校验和草稿保存。</span>
      <span>阶段内部图展示该 stage 的算子、端口与边界引用；对象是否把某些 stage 隐藏到宏观图外，由对象包显式声明。</span>
    </div>
  );
}

function WorkflowGovernancePanel({ workflow, stages }) {
  const stageEdges = arr(workflow?.phases).reduce((sum, phase) => sum + arr(phase.stage_edges).length, 0);
  const families = Array.from(new Set(stages.map((stage) => stage.stage_family).filter(Boolean)));
  const hiddenDetails = stages.filter(isMacroHiddenStage).map((stage) => stage.stage_id);
  return (
    <div className="rf-semantics-panel ok">
      <strong>平台监管</strong>
      <span>stage 由对象包 workflow 声明；平台只监管 stage_family 字段是否存在、端口契约、跨阶段 stage_edges、运行 profile 和 evidence 闭环。</span>
      <span>跨阶段连线：{stageEdges} 条；宏观卡片的“内部边”和“跨阶段入/出边”分开统计。</span>
      <span>{families.length ? `对象声明的 stage_family：${families.join("、")}` : "当前 workflow 未声明 stage_family。"}</span>
      {!!hiddenDetails.length && <span>对象细节 stage 不作为平台宏观层级单独展示：{hiddenDetails.join("、")}</span>}
    </div>
  );
}

function EditorApp({ initialObject, initialWorkflowId, onWorkflowSaved, onWorkflowSelected, onWorkflowDeleted, readOnly = false }) {
  const [object, setObject] = useState(initialObject || null);
  const [workflowId, setWorkflowId] = useState(initialWorkflowId || "");
  const [rawDoc, setRawDoc] = useState(null);
  const [nodes, setNodesState] = useState([]);
  const [edges, setEdgesState] = useState([]);
  const workflowRef = useRef(null);
  const nodesRef = useRef([]);
  const edgesRef = useRef([]);
  const [selected, setSelected] = useState({ kind: "", id: "" });
  const [validation, setValidation] = useState(null);
  const [message, setMessage] = useState("");
  const [drafts, setDrafts] = useState([]);
  const [draftId, setDraftId] = useState("");
  const [stageKey, setStageKey] = useState("");
  const [viewMode, setViewMode] = useState("overview");
  const [operatorFilter, setOperatorFilter] = useState("");
  const [newBoundaryRef, setNewBoundaryRef] = useState("");
  const [newWorkflowId, setNewWorkflowId] = useState("");
  const [newWorkflowTemplate, setNewWorkflowTemplate] = useState("blank");
  const [newStageId, setNewStageId] = useState("");
  const [newStageFamily, setNewStageFamily] = useState("stage");
  const [stageEdgeDraft, setStageEdgeDraft] = useState({
    from_stage_id: "",
    from_ref: "",
    to_stage_id: "",
    to_ref: "",
  });
  const [displayOutputDraft, setDisplayOutputDraft] = useState({
    stage_id: "",
    node_ref: "",
    role: "timeline",
  });
  const [stopPolicyDraft, setStopPolicyDraft] = useState({
    kind: "any",
    max_time_s: "",
    max_iterations: "",
  });

  function requireEditable() {
    if (!readOnly) return true;
    setMessage("当前为查看模式。请先点击页面上的“编辑”再修改 workflow。");
    return false;
  }

  useEffect(() => {
    if (!initialObject) return;
    setObject(initialObject);
  }, [initialObject]);

  const setNodes = useCallback((updater, options = {}) => {
    const next = typeof updater === "function" ? updater(nodesRef.current) : updater;
    nodesRef.current = arr(next);
    if (options.sync !== false) syncWorkflow(nodesRef.current, edgesRef.current);
    setNodesState(nodesRef.current);
  }, []);

  const setEdges = useCallback((updater) => {
    const next = typeof updater === "function" ? updater(edgesRef.current) : updater;
    edgesRef.current = arr(next);
    syncWorkflow(nodesRef.current, edgesRef.current);
    setEdgesState(edgesRef.current);
  }, []);

  const onNodesChange = useCallback((changes) => {
    if (readOnly) {
      setMessage("当前为查看模式。请先点击编辑，再拖拽或修改节点。");
      return;
    }
    const dragOnly = changes.length > 0 && changes.every((change) => change.type === "position" && change.dragging);
    setNodes((current) => applyNodeChanges(changes, current), { sync: !dragOnly });
  }, [readOnly, setNodes]);

  const operators = arr(object?.operators);
  const workflows = arr(object?.workflows);
  const opMap = useMemo(() => operatorMap(operators), [operators]);
  const stages = useMemo(() => stageEntries(rawDoc), [rawDoc]);
  const stageFamilyOptions = useMemo(() => Array.from(new Set([
    newStageFamily,
    ...stages.map((stage) => stage.stage_family).filter(Boolean),
    ...operators.map((op) => op.family || op.operator_family || op.kind).filter(Boolean),
    "stage",
  ].filter(Boolean))).map((id) => [id, stageFamilyLabel(id)]), [newStageFamily, stages, operators]);
  const displayRoleOptions = useMemo(() => Array.from(new Set([
    displayOutputDraft.role,
    ...arr(rawDoc?.display_outputs).map((item) => item.role).filter(Boolean),
    "timeline",
    "field_view",
    "evidence",
  ].filter(Boolean))), [displayOutputDraft.role, rawDoc]);
  const workflowChoices = useMemo(() => {
    const rows = [...workflows];
    if (workflowId && !rows.some((wf) => (wf.workflow_id || wf.id) === workflowId)) {
      rows.unshift({
        workflow_id: workflowId,
        id: workflowId,
        phase: rawDoc?.phase || "draft",
        stage_count: arr(arr(rawDoc?.phases)[0]?.stages).length,
        spec_file: "editing draft",
      });
    }
    return rows;
  }, [workflows, workflowId, rawDoc]);
  const overviewStages = useMemo(() => macroStageEntries(stages), [stages]);
  const nodeTypes = useMemo(() => ({ [NODE_TYPE]: OperatorNode, [BOUNDARY_NODE_TYPE]: BoundaryNode }), []);
  const selectedNode = nodes.find((node) => selected.kind === "node" && node.id === selected.id);
  const selectedEdge = edges.find((edge) => selected.kind === "edge" && edge.id === selected.id);
  const stageCoreNodes = useMemo(() => {
    const [phaseId = "", stageId = ""] = String(stageKey || "").split("::");
    return withWiringStatus(
      nodes.filter((node) => node.data.phase_id === phaseId && node.data.stage_id === stageId),
      edges,
    );
  }, [nodes, edges, stageKey]);
  const stageBoundary = useMemo(() => stageBoundaryGraph(stageKey, stageFromWorkflow(rawDoc, stageKey), stageCoreNodes), [rawDoc, stageKey, stageCoreNodes]);
  const visibleNodes = useMemo(() => [...stageBoundary.nodes, ...stageCoreNodes], [stageBoundary.nodes, stageCoreNodes]);
  const visibleEdges = useMemo(() => {
    const coreIds = new Set(stageCoreNodes.map((node) => node.id));
    return [
      ...stageBoundary.edges,
      ...edges.filter((edge) => coreIds.has(edge.source) && coreIds.has(edge.target)),
    ];
  }, [edges, stageCoreNodes, stageBoundary.edges]);
  const currentStage = useMemo(() => stageFromWorkflow(rawDoc, stageKey), [rawDoc, stageKey]);
  const currentStageInputs = arr(currentStage?.subgraph?.stage_inputs);
  const currentStageOutputs = arr(currentStage?.subgraph?.stage_outputs);
  const allStageEdges = arr(arr(rawDoc?.phases)[0]?.stage_edges);
  const displayOutputs = arr(rawDoc?.display_outputs);
  const selectedBoundaryNode = visibleNodes.find((node) => (
    selected.kind === "node" && node.id === selected.id && node.type === BOUNDARY_NODE_TYPE
  ));
  const selectedBoundaryEdge = visibleEdges.find((edge) => (
    selected.kind === "edge" && edge.id === selected.id && edge.data?.synthetic
  ));
  const onEdgesChange = useCallback((changes) => {
    if (readOnly) {
      setMessage("当前为查看模式。请先点击编辑，再修改连线。");
      return;
    }
    const coreChanges = [];
    changes.forEach((change) => {
      const syntheticEdge = visibleEdges.find((edge) => edge.id === change.id && edge.data?.synthetic);
      if (syntheticEdge) {
        if (change.type === "remove") removeStagePortRef(syntheticEdge.data.boundary_kind, syntheticEdge.data.port_ref);
        return;
      }
      coreChanges.push(change);
    });
    if (coreChanges.length) setEdges((current) => applyEdgeChanges(coreChanges, current));
  }, [readOnly, setEdges, visibleEdges]);

  function syncWorkflow(nextNodes = nodesRef.current, nextEdges = edgesRef.current) {
    const base = workflowRef.current || rawDoc;
    if (!base) return null;
    const next = flowToWorkflow(base, nextNodes, nextEdges);
    workflowRef.current = next;
    setRawDoc(next);
    setValidation(null);
    return next;
  }

  const syncCanvasToWorkflow = useCallback(() => {
    if (readOnly) return;
    syncWorkflow(nodesRef.current, edgesRef.current);
  }, [readOnly]);

  function replaceGraph(workflow, graph) {
    workflowRef.current = clone(workflow);
    nodesRef.current = arr(graph.nodes);
    edgesRef.current = arr(graph.edges);
    setRawDoc(workflowRef.current);
    setNodesState(nodesRef.current);
    setEdgesState(edgesRef.current);
    setStageKey(firstStageKey(workflowRef.current));
  }

  const fetchJson = useCallback(async (url, options) => {
    const res = await fetch(url, options);
    const data = await res.json().catch(() => ({}));
    if (!res.ok || data.ok === false) throw new Error(data.error || data.message || `${res.status} ${res.statusText}`);
    return data;
  }, []);

  const loadDrafts = useCallback(async () => {
    const data = await fetchJson("/api/workflow/drafts");
    setDrafts(arr(data.drafts));
  }, [fetchJson]);

  useEffect(() => {
    if (object) return;
    fetchJson("/api/object").then(setObject).catch((err) => setMessage(err.message));
  }, [object, fetchJson]);

  useEffect(() => {
    if (!workflowId && workflows[0]) setWorkflowId(workflows[0].workflow_id || workflows[0].id);
  }, [workflowId, workflows]);

  useEffect(() => {
    if (!initialWorkflowId || workflowId === initialWorkflowId) return;
    if (workflows.some((wf) => (wf.workflow_id || wf.id) === initialWorkflowId)) {
      selectWorkflow(initialWorkflowId);
    }
  }, [initialWorkflowId, workflowId, workflows]);

  const loadWorkflow = useCallback(async (id = workflowId) => {
    if (!id) return;
    setMessage("读取 workflow...");
    const data = await fetchJson(`/api/workflow/raw?id=${encodeURIComponent(id)}`);
    const graph = workflowToFlow(data.workflow, opMap);
    replaceGraph(data.workflow, graph);
    setValidation(data.validation || null);
    setSelected({ kind: "", id: "" });
    setMessage("已读取对象包 workflow");
    loadDrafts().catch(() => {});
  }, [workflowId, fetchJson, opMap, setNodes, setEdges, loadDrafts]);

  useEffect(() => {
    if (workflowId && opMap.size) loadWorkflow(workflowId).catch((err) => setMessage(err.message));
  }, [workflowId, opMap.size]);

  useEffect(() => {
    loadDrafts().catch(() => {});
  }, [loadDrafts]);

  useEffect(() => {
    const keys = stages.map(stageKeyOf).filter((key) => key !== "::");
    if (!keys.length) {
      if (stageKey) setStageKey("");
      return;
    }
    if (!keys.includes(stageKey)) setStageKey(keys[0]);
  }, [stageKey, stages]);

  function currentWorkflow() {
    return syncWorkflow();
  }

  function selectWorkflow(nextWorkflowId) {
    setWorkflowId(nextWorkflowId);
    if (onWorkflowSelected) onWorkflowSelected(nextWorkflowId);
    setRawDoc(null);
    workflowRef.current = null;
    nodesRef.current = [];
    edgesRef.current = [];
    setNodesState([]);
    setEdgesState([]);
    setStageKey("");
    setSelected({ kind: "", id: "" });
    setValidation(null);
    setNewBoundaryRef("");
    setViewMode("overview");
    setMessage("切换 workflow...");
  }

  function createWorkflowFromTemplate() {
    if (!requireEditable()) return;
    const objectId = object?.object_id || object?.id || "object";
    const stamp = new Date().toISOString().replace(/[-:TZ.]/g, "").slice(0, 14);
    const fallbackId = `${objectId}.workflow.${stamp}.v1`;
    const workflow = buildWorkflowTemplate({
      workflowId: newWorkflowId || fallbackId,
      objectId,
      templateId: newWorkflowTemplate,
    });
    const graph = workflowToFlow(workflow, opMap);
    setWorkflowId(workflow.workflow_id);
    if (onWorkflowSelected) onWorkflowSelected(workflow.workflow_id);
    replaceGraph(workflow, graph);
    setValidation(null);
    setSelected({ kind: "", id: "" });
    setStageKey(firstStageKey(workflow));
    setViewMode("overview");
    setMessage("已创建新 workflow 草稿骨架；请添加算子、声明阶段输入输出并保存草稿。");
  }

  function addStageToWorkflow() {
    if (!requireEditable()) return;
    const base = syncWorkflow();
    if (!base) return;
    const next = clone(base);
    next.phases = arr(next.phases).length ? next.phases : [{ phase_id: "phase", stages: [], stage_edges: [] }];
    const phase = next.phases[0];
    const stageId = safeId(newStageId, newStageFamily || "stage");
    if (arr(phase.stages).some((stage) => stage.stage_id === stageId)) {
      setMessage(`stage '${stageId}' 已存在，不能重复创建。`);
      return;
    }
    phase.stages = [
      ...arr(phase.stages),
      {
        stage_id: stageId,
        stage_family: newStageFamily || "stage",
        operator_refs: [],
        subgraph: {
          nodes: [],
          edges: [],
          stage_inputs: [],
          stage_outputs: [],
        },
      },
    ];
    phase.stage_edges = arr(phase.stage_edges);
    workflowRef.current = next;
    setRawDoc(next);
    replaceGraph(next, workflowToFlow(next, opMap));
    setStageKey(`${phase.phase_id || ""}::${stageId}`);
    setViewMode("stage");
    setSelected({ kind: "", id: "" });
    setValidation(null);
    setNewStageId("");
    setMessage(`已新增 stage：${stageId}；请添加算子并声明 stage 输入/输出。`);
  }

  function removeCurrentStage() {
    if (!requireEditable()) return;
    const base = syncWorkflow();
    if (!base || !stageKey) return;
    const { phase_id: phaseId, stage_id: stageId } = splitNodeKey(stageKey);
    const next = clone(base);
    const phase = arr(next.phases).find((item) => item.phase_id === phaseId);
    if (!phase) return;
    phase.stages = arr(phase.stages).filter((stage) => stage.stage_id !== stageId);
    phase.stage_edges = arr(phase.stage_edges).filter((edge) => (
      edge?.from?.stage_id !== stageId && edge?.to?.stage_id !== stageId
    ));
    workflowRef.current = next;
    setRawDoc(next);
    replaceGraph(next, workflowToFlow(next, opMap));
    setStageKey(firstStageKey(next));
    setSelected({ kind: "", id: "" });
    setValidation(null);
    setMessage(`已删除 stage：${stageId}，并清理关联 stage_edges。`);
  }

  function updateStagePorts(kind, updater) {
    if (!requireEditable()) return;
    const base = syncWorkflow();
    if (!base) return;
    const next = clone(base);
    const stage = stageFromWorkflow(next, stageKey);
    if (!stage) return;
    stage.subgraph = stage.subgraph || {};
    const key = kind === "output" ? "stage_outputs" : "stage_inputs";
    stage.subgraph[key] = normalizeStagePortRefs(updater(arr(stage.subgraph[key])));
    workflowRef.current = next;
    setRawDoc(next);
    setValidation(null);
  }

  function addStagePortRef(kind, ref) {
    const cleaned = String(ref || "").trim();
    if (!cleaned) return;
    updateStagePorts(kind, (refs) => [...refs, cleaned]);
    setNewBoundaryRef("");
    setMessage(`${kind === "output" ? "阶段输出" : "阶段输入"}已更新：${cleaned}`);
  }

  function removeStagePortRef(kind, ref) {
    updateStagePorts(kind, (refs) => refs.filter((item) => String(item) !== String(ref)));
    setMessage(`${kind === "output" ? "阶段输出" : "阶段输入"}已删除：${ref}`);
  }

  function rewriteStageBoundaryRefs(oldNodeId, newNodeId) {
    updateStagePorts("input", (refs) => refs.map((ref) => rewriteStagePortNodeRef(ref, oldNodeId, newNodeId)));
    updateStagePorts("output", (refs) => refs.map((ref) => rewriteStagePortNodeRef(ref, oldNodeId, newNodeId)));
  }

  function removeStageBoundaryRefsForNode(nodeId) {
    const prefix = `${nodeId}.`;
    updateStagePorts("input", (refs) => refs.filter((ref) => !String(ref).startsWith(prefix)));
    updateStagePorts("output", (refs) => refs.filter((ref) => !String(ref).startsWith(prefix)));
  }

  function splitStagePortRef(ref) {
    const text = String(ref || "").trim();
    const dot = text.indexOf(".");
    if (dot <= 0 || dot >= text.length - 1) return null;
    return { node_id: text.slice(0, dot), port_id: text.slice(dot + 1) };
  }

  function firstPhaseId() {
    return arr(rawDoc?.phases)[0]?.phase_id || String(stageKey || "").split("::")[0] || "";
  }

  function stageRefExists(stageId, ref, direction) {
    const port = splitStagePortRef(ref);
    if (!port) return { ok: false, message: "端口引用必须是 node_id.port_id" };
    const phaseId = firstPhaseId();
    const node = nodesRef.current.find((item) => (
      item.data.phase_id === phaseId
      && item.data.stage_id === stageId
      && item.data.node_id === port.node_id
    ));
    if (!node) return { ok: false, message: `stage '${stageId}' 中不存在节点 '${port.node_id}'` };
    const portList = direction === "output" ? arr(node.data.outputs) : arr(node.data.inputs);
    const matchedPort = portList.find((item) => item.port_id === port.port_id);
    if (!matchedPort) {
      return { ok: false, message: `节点 '${port.node_id}' 没有${direction === "output" ? "输出" : "输入"}端口 '${port.port_id}'` };
    }
    return { ok: true, ...port, port: matchedPort };
  }

  function updateStageEdges(updater) {
    const base = syncWorkflow();
    if (!base) return;
    const next = clone(base);
    const phase = arr(next.phases)[0];
    if (!phase) return;
    phase.stage_edges = updater(arr(phase.stage_edges));
    workflowRef.current = next;
    setRawDoc(next);
    setValidation(null);
  }

  function addStageEdge() {
    if (!requireEditable()) return;
    const sourceCheck = stageRefExists(stageEdgeDraft.from_stage_id, stageEdgeDraft.from_ref, "output");
    if (!sourceCheck.ok) {
      setMessage(sourceCheck.message);
      return;
    }
    const targetCheck = stageRefExists(stageEdgeDraft.to_stage_id, stageEdgeDraft.to_ref, "input");
    if (!targetCheck.ok) {
      setMessage(targetCheck.message);
      return;
    }
    const sourceContract = typedContract(sourceCheck.port);
    const targetContract = typedContract(targetCheck.port);
    if (!sourceContract.contract_id || !targetContract.contract_id || sourceContract.contract_id !== targetContract.contract_id) {
      setMessage(`跨 stage contract 不匹配：${sourceContract.contract_id || "-"} → ${targetContract.contract_id || "-"}`);
      return;
    }
    updateStageEdges((stageEdges) => [
      ...stageEdges,
      {
        from: {
          stage_id: stageEdgeDraft.from_stage_id,
          node_id: sourceCheck.node_id,
          port_id: sourceCheck.port_id,
        },
        to: {
          stage_id: stageEdgeDraft.to_stage_id,
          node_id: targetCheck.node_id,
          port_id: targetCheck.port_id,
        },
      },
    ]);
    setMessage("跨阶段连接已加入 stage_edges；保存草稿后可参与编译运行。");
  }

  function removeStageEdge(index) {
    if (!requireEditable()) return;
    updateStageEdges((stageEdges) => stageEdges.filter((_, i) => i !== index));
    setMessage("跨阶段连接已删除。");
  }

  function updateDisplayOutputs(updater) {
    if (!requireEditable()) return;
    const base = syncWorkflow();
    if (!base) return;
    const next = clone(base);
    next.display_outputs = updater(arr(next.display_outputs));
    workflowRef.current = next;
    setRawDoc(next);
    setValidation(null);
  }

  function addDisplayOutput() {
    if (!requireEditable()) return;
    const ref = splitStagePortRef(displayOutputDraft.node_ref);
    if (!ref) {
      setMessage("display output 必须填写 node_id.port_id。");
      return;
    }
    updateDisplayOutputs((items) => [
      ...items,
      {
        stage_id: displayOutputDraft.stage_id || splitNodeKey(stageKey).stage_id,
        node_id: ref.node_id,
        port_id: ref.port_id,
        role: displayOutputDraft.role || "timeline",
      },
    ]);
    setDisplayOutputDraft({ stage_id: "", node_ref: "", role: "timeline" });
    setMessage("已新增 workflow display_outputs。");
  }

  function removeDisplayOutput(index) {
    if (!requireEditable()) return;
    updateDisplayOutputs((items) => items.filter((_, i) => i !== index));
    setMessage("已删除 workflow display_outputs 项。");
  }

  function applyStopPolicy() {
    if (!requireEditable()) return;
    const base = syncWorkflow();
    if (!base) return;
    const next = clone(base);
    const alternatives = [];
    if (stopPolicyDraft.max_time_s !== "") alternatives.push({ kind: "max_horizon_time", max_time_s: Number(stopPolicyDraft.max_time_s) });
    if (stopPolicyDraft.max_iterations !== "") alternatives.push({ kind: "max_iterations", max_iterations: Number(stopPolicyDraft.max_iterations) });
    next.stop_policy = {
      kind: stopPolicyDraft.kind || "any",
      alternatives,
    };
    workflowRef.current = next;
    setRawDoc(next);
    setValidation(null);
    setMessage("已更新 workflow stop_policy。");
  }

  function clearStopPolicy() {
    if (!requireEditable()) return;
    const base = syncWorkflow();
    if (!base) return;
    const next = clone(base);
    delete next.stop_policy;
    workflowRef.current = next;
    setRawDoc(next);
    setValidation(null);
    setMessage("已清空 workflow stop_policy；future workflow 将无法通过 Phase4 校验。");
  }

  async function validateCurrent() {
    const workflow = currentWorkflow();
    if (!workflow) return;
    const data = await fetchJson("/api/workflow/validate", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ workflow }),
    });
    setValidation(data);
    setMessage(data.ok ? "校验通过" : "校验未通过");
  }

  async function saveDraft() {
    if (!requireEditable()) return;
    const workflow = currentWorkflow();
    if (!workflow) return;
    const data = await fetchJson("/api/workflow/draft", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ workflow }),
    });
    setValidation(data.validation || null);
    if (data.object) setObject(data.object);
    setMessage(`已保存草稿：${data.draft_id}`);
    if (onWorkflowSaved) {
      onWorkflowSaved({
        workflow_id: workflow.workflow_id || workflowId,
        draft_id: data.draft_id,
        object_draft_id: data.object_draft_id || "",
        object_draft_path: data.object_draft_path || "",
      });
    }
    await loadDrafts();
  }

  async function deleteCurrentWorkflow() {
    if (!requireEditable()) return;
    const workflow = currentWorkflow();
    const targetWorkflowId = workflow?.workflow_id || workflowId;
    if (!targetWorkflowId) return;
    if (!window.confirm(`确认从对象草稿删除 workflow：${targetWorkflowId}？`)) return;
    const data = await fetchJson("/api/workflow/delete", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ workflow_id: targetWorkflowId }),
    });
    const nextObject = data.object || object || {};
    const nextWorkflows = arr(nextObject.workflows);
    const nextWorkflowId = nextWorkflows.length ? (nextWorkflows[0].workflow_id || nextWorkflows[0].id || "") : "";
    setObject(nextObject);
    setWorkflowId(nextWorkflowId);
    if (onWorkflowSelected) onWorkflowSelected(nextWorkflowId);
    if (onWorkflowDeleted) onWorkflowDeleted({
      workflow_id: targetWorkflowId,
      next_workflow_id: nextWorkflowId,
      object_draft_id: data.draft_id || "",
    });
    workflowRef.current = null;
    nodesRef.current = [];
    edgesRef.current = [];
    setRawDoc(null);
    setNodesState([]);
    setEdgesState([]);
    setSelected({ kind: "", id: "" });
    setValidation(null);
    setStageKey("");
    setMessage(data.message || "workflow 已删除；请选择或新建 workflow。");
    await loadDrafts();
  }

  async function loadDraft(id = draftId) {
    if (!id) return;
    const data = await fetchJson(`/api/workflow/raw?draft=${encodeURIComponent(id)}`);
    const graph = workflowToFlow(data.workflow, opMap);
    setWorkflowId(data.workflow.workflow_id || workflowId);
    replaceGraph(data.workflow, graph);
    setValidation(data.validation || null);
    setMessage("已读取草稿");
  }

  function exportJson() {
    const workflow = currentWorkflow();
    if (!workflow) return;
    const blob = new Blob([JSON.stringify(workflow, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = `${workflow.workflow_id || "workflow"}.draft.json`;
    link.click();
    URL.revokeObjectURL(url);
  }

  function importJson(event) {
    if (!requireEditable()) {
      event.target.value = "";
      return;
    }
    const file = event.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      try {
        const doc = JSON.parse(String(reader.result || "{}"));
        const graph = workflowToFlow(doc, opMap);
        setWorkflowId(doc.workflow_id || workflowId);
        replaceGraph(doc, graph);
        setValidation(null);
        setMessage("已导入 JSON，保存前请校验");
      } catch (err) {
        setMessage(`导入失败：${err.message}`);
      }
    };
    reader.readAsText(file, "utf-8");
    event.target.value = "";
  }

  function addOperatorNode(operatorId) {
    if (!requireEditable()) return;
    const op = opMap.get(operatorId);
    if (!op || !stageKey) return;
    const [phaseId, stageId] = stageKey.split("::");
    const stageIndex = Math.max(0, stages.findIndex((stage) => `${stage.phase_id}::${stage.stage_id}` === stageKey));
    const stageFamily = stages[stageIndex]?.stage_family || "";
    const opFamily = op.family || op.operator_family || "";
    if (stageFamily && opFamily && stageFamily !== opFamily) {
      setMessage(`不能加入：stage_family=${stageFamily} 与 operator_family=${opFamily} 不匹配。`);
      return;
    }
    const stageNodes = nodes.filter((node) => node.data.phase_id === phaseId && node.data.stage_id === stageId);
    const nodeId = uniqueNodeId(stageNodes, nodeNameFromOperator(operatorId));
    const id = nodeKey(phaseId, stageId, nodeId);
    const maxY = stageNodes.reduce((acc, node) => Math.max(acc, node.position.y), 70);
    const newNode = {
      id,
      type: NODE_TYPE,
      position: { x: 80 + stageIndex * 330, y: maxY + 145 },
      data: {
        phase_id: phaseId,
        stage_id: stageId,
        stage_family: stages[stageIndex]?.stage_family || "",
        node_id: nodeId,
        operator_ref: operatorId,
        operator_display_name: operatorDisplayName(op, operatorId),
        family: op.family || op.operator_family || "",
        inputs: arr(op.inputs),
        outputs: arr(op.outputs),
        typed_io_contract: clone(op.typed_io_contract || {}),
        activation_policy: { enabled: true, required: false, on_disabled: "remove" },
        raw: { node_id: nodeId, operator_ref: operatorId, activation_policy: { enabled: true, required: false, on_disabled: "remove" } },
      },
    };
    setNodes((current) => [...current, newNode]);
    setSelected({ kind: "node", id });
  }

  function updateNodeData(nodeId, patch) {
    if (!requireEditable()) return;
    setNodes((current) => current.map((node) => (node.id === nodeId ? { ...node, data: { ...node.data, ...patch } } : node)));
  }

  function renameNode(nodeId, nextNodeId) {
    if (!requireEditable()) return;
    const node = nodes.find((item) => item.id === nodeId);
    if (!node) return;
    const oldNodeId = node.data.node_id;
    const nextId = nodeKey(node.data.phase_id, node.data.stage_id, nextNodeId);
    setNodes((current) => current.map((item) => (item.id === nodeId ? { ...item, id: nextId, data: { ...item.data, node_id: nextNodeId } } : item)));
    setEdges((current) => current.map((edge) => ({
      ...edge,
      source: edge.source === nodeId ? nextId : edge.source,
      target: edge.target === nodeId ? nextId : edge.target,
    })));
    rewriteStageBoundaryRefs(oldNodeId, nextNodeId);
    setSelected({ kind: "node", id: nextId });
  }

  function deleteSelection() {
    if (!requireEditable()) return;
    if (selectedBoundaryEdge) {
      removeStagePortRef(selectedBoundaryEdge.data.boundary_kind, selectedBoundaryEdge.data.port_ref);
      setSelected({ kind: "", id: "" });
      return;
    }
    if (selectedBoundaryNode) {
      setMessage("边界节点不可删除；请在右侧删除具体阶段输入/输出端口。");
      return;
    }
    if (selected.kind === "node") {
      const node = nodes.find((item) => item.id === selected.id);
      setNodes((current) => current.filter((node) => node.id !== selected.id));
      setEdges((current) => current.filter((edge) => edge.source !== selected.id && edge.target !== selected.id));
      if (node) removeStageBoundaryRefsForNode(node.data.node_id);
    }
    if (selected.kind === "edge") {
      setEdges((current) => current.filter((edge) => edge.id !== selected.id));
    }
    setSelected({ kind: "", id: "" });
  }

  const onNodesDelete = useCallback((deletedNodes) => {
    if (readOnly) {
      setMessage("当前为查看模式。请先点击编辑，再删除节点。");
      return;
    }
    const deletedIds = new Set(arr(deletedNodes).map((node) => node.id));
    const deletedNodeIds = arr(deletedNodes).map((node) => node.data?.node_id).filter(Boolean);
    if (!deletedIds.size) return;
    setEdges((current) => current.filter((edge) => !deletedIds.has(edge.source) && !deletedIds.has(edge.target)));
    deletedNodeIds.forEach((nodeId) => removeStageBoundaryRefsForNode(nodeId));
    setSelected({ kind: "", id: "" });
    setMessage(`已删除 ${deletedIds.size} 个节点，并清理相关边、stage 输入输出引用。`);
  }, [readOnly, setEdges]);

  function updateEdgeEndpoint(edgeId, patch) {
    if (!requireEditable()) return;
    const edge = edgesRef.current.find((item) => item.id === edgeId);
    if (!edge) return;
    const nextEdge = { ...edge, ...patch };
    const source = nodesRef.current.find((node) => node.id === nextEdge.source);
    const target = nodesRef.current.find((node) => node.id === nextEdge.target);
    const check = validatePortContract(source, nextEdge.sourceHandle, target, nextEdge.targetHandle);
    if (!check.ok) {
      setMessage(check.message);
      return;
    }
    setEdges((current) => current.map((item) => (
      item.id === edgeId
        ? { ...item, ...patch, label: `${nextEdge.sourceHandle || "-"} → ${nextEdge.targetHandle || "-"}` }
        : item
    )));
  }

  const onConnect = useCallback((params) => {
    if (readOnly) {
      setMessage("当前为查看模式。请先点击编辑，再连接端口。");
      return;
    }
    const source = visibleNodes.find((node) => node.id === params.source) || nodesRef.current.find((node) => node.id === params.source);
    const target = visibleNodes.find((node) => node.id === params.target) || nodesRef.current.find((node) => node.id === params.target);
    if (!source || !target) return;
    const sourceIsBoundary = source.type === BOUNDARY_NODE_TYPE;
    const targetIsBoundary = target.type === BOUNDARY_NODE_TYPE;
    if (sourceIsBoundary || targetIsBoundary) {
      if (sourceIsBoundary && targetIsBoundary) {
        setMessage("边界到边界的直通需要显式 stage 规则，当前请连接到具体算子端口。");
        return;
      }
      if (sourceIsBoundary && source.data.boundary_kind === "input" && !targetIsBoundary) {
        if (!params.targetHandle) {
          setMessage("请把阶段输入连接到一个明确的算子输入端口。");
          return;
        }
        addStagePortRef("input", `${target.data.node_id}.${params.targetHandle || ""}`);
        return;
      }
      if (targetIsBoundary && target.data.boundary_kind === "output" && !sourceIsBoundary) {
        if (!params.sourceHandle) {
          setMessage("请从一个明确的算子输出端口连接到阶段输出。");
          return;
        }
        addStagePortRef("output", `${source.data.node_id}.${params.sourceHandle || ""}`);
        return;
      }
      setMessage("阶段输入只能连到算子输入，算子输出只能连到阶段输出。");
      return;
    }
    if (source.data.phase_id !== target.data.phase_id || source.data.stage_id !== target.data.stage_id) {
      setMessage("只允许同一 stage 内连线");
      return;
    }
    const contractCheck = validatePortContract(source, params.sourceHandle, target, params.targetHandle);
    if (!contractCheck.ok) {
      setMessage(contractCheck.message);
      return;
    }
    setEdges((current) => addEdge({
      ...params,
      type: "smoothstep",
      markerEnd: { type: MarkerType.ArrowClosed },
      label: `${params.sourceHandle || "-"} → ${params.targetHandle || "-"}`,
      data: { phase_id: source.data.phase_id, stage_id: source.data.stage_id },
    }, current));
  }, [readOnly, setEdges, visibleNodes]);

  const filteredOperators = operators.filter((op) => {
    const text = `${op.operator_id || ""} ${op.display_name || ""} ${op.family || ""}`.toLowerCase();
    return text.includes(operatorFilter.toLowerCase());
  });

  return (
    <div className="rf-editor-shell">
      <div className="rf-toolbar">
        <span className={`rf-readonly-pill ${readOnly ? "readonly" : "editable"}`}>{readOnly ? "查看模式" : "编辑模式"}</span>
        <label>
          <span>对象包 workflow</span>
          <select value={workflowId || ""} onChange={(e) => selectWorkflow(e.target.value)}>
            {workflowChoices.map((wf) => <option key={wf.workflow_id || wf.id} value={wf.workflow_id || wf.id}>{workflowLabel(wf)}</option>)}
          </select>
        </label>
        <label>
          <span>新建对象包 workflow id</span>
          <input disabled={readOnly} value={newWorkflowId} onChange={(e) => setNewWorkflowId(e.target.value)} placeholder="object.workflow.custom.v1" />
        </label>
        <label>
          <span>workflow 模板</span>
          <select disabled={readOnly} value={newWorkflowTemplate} onChange={(e) => setNewWorkflowTemplate(e.target.value)}>
            {Object.entries(WORKFLOW_TEMPLATES).map(([id, item]) => <option key={id} value={id}>{item.label}</option>)}
          </select>
        </label>
        <button disabled={readOnly} onClick={createWorkflowFromTemplate}>新建 workflow 草稿</button>
        <label>
          <span>新增 stage id</span>
          <input disabled={readOnly} value={newStageId} onChange={(e) => setNewStageId(e.target.value)} placeholder="stage_ext" />
        </label>
        <label>
          <span>stage_family</span>
          <input disabled={readOnly} list="rf-stage-family-options" value={newStageFamily} onChange={(e) => setNewStageFamily(e.target.value)} placeholder="stage" />
        </label>
        <datalist id="rf-stage-family-options">{stageFamilyOptions.map(([id, label]) => <option key={id} value={id}>{label}</option>)}</datalist>
        <button disabled={readOnly || !rawDoc} onClick={addStageToWorkflow}>新增 stage</button>
        <div className="rf-view-switch">
          <button className={viewMode === "overview" ? "active" : ""} onClick={() => setViewMode("overview")}>宏观流程</button>
          <button className={viewMode === "stage" ? "active" : ""} onClick={() => setViewMode("stage")}>阶段内部</button>
        </div>
        <button onClick={() => loadWorkflow()}>读取源 JSON</button>
        <label>
          <span>草稿</span>
          <select value={draftId} onChange={(e) => setDraftId(e.target.value)}>
            <option value="">选择已保存草稿</option>
            {drafts.map((d) => <option key={d.draft_id} value={d.draft_id}>{d.draft_id}</option>)}
          </select>
        </label>
        <button onClick={() => loadDraft()}>读取草稿</button>
        <button onClick={validateCurrent}>校验</button>
        <button className="primary" disabled={readOnly} onClick={saveDraft}>保存草稿</button>
        <button className="danger" disabled={readOnly || !workflowId} onClick={deleteCurrentWorkflow}>删除当前 workflow</button>
        <button onClick={exportJson}>导出 JSON</button>
        <label className={`rf-file-button ${readOnly ? "disabled" : ""}`}>导入 JSON<input disabled={readOnly} type="file" accept="application/json,.json" onChange={importJson} /></label>
      </div>

      <div className="rf-editor-grid">
        <aside className="rf-palette">
          <h2>{viewMode === "overview" ? "流程层级" : "算子库"}</h2>
          <label>
            <span>目标 stage</span>
            <select value={stageKey} onChange={(e) => setStageKey(e.target.value)}>
              {stages.map((stage) => <option key={`${stage.phase_id}::${stage.stage_id}`} value={`${stage.phase_id}::${stage.stage_id}`}>{stage.label}</option>)}
            </select>
          </label>
          {viewMode === "overview" && (
            <div className="rf-stage-list">
              {overviewStages.map((stage) => (
                <button key={stageKeyOf(stage)} className={stageKey === stageKeyOf(stage) ? "active" : ""} onClick={() => {
                  setStageKey(stageKeyOf(stage));
                  setViewMode("stage");
                }}>
                  <strong>{stageFamilyLabel(stage.stage_family || stage.stage_id)}</strong>
                  <span>{stage.stage_id} · {stage.stage_family || "-"}</span>
                  <small>{stageConnectivityText(stage)}</small>
                  {hiddenStageDetails(stages, stage).map((detail) => (
                    <small key={detail.stage_id}>细节：{detail.stage_id} · {stageConnectivityText(detail)}</small>
                  ))}
                </button>
              ))}
            </div>
          )}
          {viewMode === "stage" && (
            <>
              <input value={operatorFilter} onChange={(e) => setOperatorFilter(e.target.value)} placeholder="搜索 operator / family" />
              <div className="rf-operator-list">
                {filteredOperators.map((op) => (
                  <button key={op.operator_id} disabled={readOnly} onClick={() => addOperatorNode(op.operator_id)}>
                    <strong>{op.display_name || op.operator_id}</strong>
                    <span>{op.operator_id}</span>
                    <small>{op.family || op.kind || "-"}</small>
                  </button>
                ))}
              </div>
            </>
          )}
        </aside>

        <section className="rf-canvas-wrap">
          <div className="rf-canvas-head">
            <div>
              <h2>{viewMode === "overview" ? "宏观 workflow 阶段图" : "阶段内部算子图"}</h2>
              <p>{viewMode === "overview" ? "点击阶段进入内部原子算子图；候选算子不会混入主流程。" : "拖拽节点、从端口拉线、选择节点/连线后在右侧编辑参数。"}</p>
            </div>
            <span>{message || rawDoc?.workflow_id || "-"}</span>
          </div>
          <div className="rf-canvas">
            {viewMode === "overview" ? (
              <MacroWorkflowOverview workflow={rawDoc} stages={stages} activeStageKey={stageKey} opMap={opMap} onOpenStage={(key) => {
                setStageKey(key);
                setViewMode("stage");
              }} />
            ) : (
              <ReactFlow
                nodes={visibleNodes}
                edges={visibleEdges}
                nodeTypes={nodeTypes}
                onNodesChange={onNodesChange}
                onNodesDelete={onNodesDelete}
                onEdgesChange={onEdgesChange}
                onConnect={onConnect}
                onNodeDragStop={readOnly ? undefined : syncCanvasToWorkflow}
                onNodeClick={(_, node) => setSelected({ kind: "node", id: node.id })}
                onEdgeClick={(_, edge) => setSelected({ kind: "edge", id: edge.id })}
                fitView
                nodesDraggable={!readOnly}
                nodesConnectable={!readOnly}
                elementsSelectable
                defaultEdgeOptions={{ type: "smoothstep", markerEnd: { type: MarkerType.ArrowClosed } }}
              >
                <Background />
                <Controls />
                <MiniMap pannable zoomable />
              </ReactFlow>
            )}
          </div>
        </section>

        <aside className="rf-inspector">
          <h2>参数与校验</h2>
          {readOnly && <p className="rf-readonly-note">当前为查看模式：可浏览节点、端口、校验结果和 JSON；修改、保存、删除需要先点击页面上的“编辑”。</p>}
          {currentStage && (
            <section>
              <h3>当前 stage 函数输入/输出</h3>
              <p>这里定义的是 stage boundary：当前阶段对外暴露的输入输出端口引用。端口含义由对象包 contract 和算子 spec 决定。</p>
              <div className="rf-boundary-editor-list">
                <strong>输入</strong>
                {currentStageInputs.map((ref) => (
                  <div key={`in-${ref}`}>
                    <span>{ref}</span>
                    <button disabled={readOnly} onClick={() => removeStagePortRef("input", ref)}>删除</button>
                  </div>
                ))}
                {!currentStageInputs.length && <p>尚未声明输入。</p>}
              </div>
              <div className="rf-boundary-editor-list">
                <strong>输出</strong>
                {currentStageOutputs.map((ref) => (
                  <div key={`out-${ref}`}>
                    <span>{ref}</span>
                    <button disabled={readOnly} onClick={() => removeStagePortRef("output", ref)}>删除</button>
                  </div>
                ))}
                {!currentStageOutputs.length && <p>尚未声明输出。</p>}
              </div>
              <label>
                <span>新增输入/输出引用</span>
                <input disabled={readOnly} value={newBoundaryRef} onChange={(e) => setNewBoundaryRef(e.target.value)} placeholder="node_id.port_id" />
              </label>
              <div className="button-row">
                <button disabled={readOnly} onClick={() => addStagePortRef("input", newBoundaryRef)}>作为 stage 输入</button>
                <button disabled={readOnly} onClick={() => addStagePortRef("output", newBoundaryRef)}>作为 stage 输出</button>
              </div>
              <details>
                <summary>从当前 stage 节点端口选择</summary>
                <div className="rf-port-picker">
                  {stageCoreNodes.map((node) => (
                    <div key={node.id}>
                      <strong>{node.data.node_id}</strong>
                      <div>
                        {arr(node.data.inputs).map((port) => (
                          <button disabled={readOnly} key={`i-${node.id}-${port.port_id}`} onClick={() => addStagePortRef("input", `${node.data.node_id}.${port.port_id}`)}>
                            输入 {port.port_id}
                          </button>
                        ))}
                      </div>
                      <div>
                        {arr(node.data.outputs).map((port) => (
                          <button disabled={readOnly} key={`o-${node.id}-${port.port_id}`} onClick={() => addStagePortRef("output", `${node.data.node_id}.${port.port_id}`)}>
                            输出 {port.port_id}
                          </button>
                        ))}
                      </div>
                    </div>
                  ))}
                  {!stageCoreNodes.length && <p>当前 stage 还没有算子节点。</p>}
                </div>
              </details>
              <div className="button-row">
                <button className="danger" disabled={readOnly || !currentStage} onClick={removeCurrentStage}>删除当前 stage</button>
              </div>
            </section>
          )}

          {viewMode === "overview" && rawDoc && (
            <section>
              <h3>跨 stage 连接</h3>
              <p>这里编辑 workflow 的 stage_edges，用来把一个 stage 的输出接到另一个 stage 的输入。</p>
              <div className="rf-boundary-editor-list">
                {allStageEdges.map((edge, index) => (
                  <div key={`stage-edge-${index}`}>
                    <span>{edge?.from?.stage_id}.{edge?.from?.node_id}.{edge?.from?.port_id} → {edge?.to?.stage_id}.{edge?.to?.node_id}.{edge?.to?.port_id}</span>
                    <button disabled={readOnly} onClick={() => removeStageEdge(index)}>删除</button>
                  </div>
                ))}
                {!allStageEdges.length && <p>尚未声明跨 stage 连接。</p>}
              </div>
              <label>
                <span>源 stage</span>
                <select disabled={readOnly} value={stageEdgeDraft.from_stage_id} onChange={(e) => setStageEdgeDraft({ ...stageEdgeDraft, from_stage_id: e.target.value })}>
                  <option value="">选择源 stage</option>
                  {stages.map((stage) => <option key={`from-${stage.stage_id}`} value={stage.stage_id}>{stage.stage_id}</option>)}
                </select>
              </label>
              <label><span>源输出 node.port</span><input disabled={readOnly} value={stageEdgeDraft.from_ref} onChange={(e) => setStageEdgeDraft({ ...stageEdgeDraft, from_ref: e.target.value })} placeholder="node_id.output_port" /></label>
              <label>
                <span>目标 stage</span>
                <select disabled={readOnly} value={stageEdgeDraft.to_stage_id} onChange={(e) => setStageEdgeDraft({ ...stageEdgeDraft, to_stage_id: e.target.value })}>
                  <option value="">选择目标 stage</option>
                  {stages.map((stage) => <option key={`to-${stage.stage_id}`} value={stage.stage_id}>{stage.stage_id}</option>)}
                </select>
              </label>
              <label><span>目标输入 node.port</span><input disabled={readOnly} value={stageEdgeDraft.to_ref} onChange={(e) => setStageEdgeDraft({ ...stageEdgeDraft, to_ref: e.target.value })} placeholder="node_id.input_port" /></label>
              <button disabled={readOnly} onClick={addStageEdge}>新增跨 stage 连接</button>
            </section>
          )}

          {rawDoc && (
            <section>
              <h3>输出展示 display_outputs</h3>
              <p>这里声明哪些 workflow 输出进入 UI 时间线、evidence 或云图候选；运行配置只选择方案，不在这里补业务含义。</p>
              <div className="rf-boundary-editor-list">
                {displayOutputs.map((item, index) => (
                  <div key={`display-output-${index}`}>
                    <span>{item.stage_id || "-"} · {item.node_id || "-"}.{item.port_id || "-"} · {item.role || "timeline"}</span>
                    <button disabled={readOnly} onClick={() => removeDisplayOutput(index)}>删除</button>
                  </div>
                ))}
                {!displayOutputs.length && <p>尚未声明 display_outputs。</p>}
              </div>
              <label>
                <span>stage</span>
                <select disabled={readOnly} value={displayOutputDraft.stage_id} onChange={(e) => setDisplayOutputDraft({ ...displayOutputDraft, stage_id: e.target.value })}>
                  <option value="">使用当前 stage</option>
                  {stages.map((stage) => <option key={`display-${stage.stage_id}`} value={stage.stage_id}>{stage.stage_id}</option>)}
                </select>
              </label>
              <label><span>输出 node.port</span><input disabled={readOnly} value={displayOutputDraft.node_ref} onChange={(e) => setDisplayOutputDraft({ ...displayOutputDraft, node_ref: e.target.value })} placeholder="node_id.output_port" /></label>
              <label>
                <span>role</span>
                <input disabled={readOnly} list="rf-display-role-options" value={displayOutputDraft.role} onChange={(e) => setDisplayOutputDraft({ ...displayOutputDraft, role: e.target.value })} />
              </label>
              <datalist id="rf-display-role-options">{displayRoleOptions.map((role) => <option key={role} value={role} />)}</datalist>
              <button disabled={readOnly} onClick={addDisplayOutput}>新增 display output</button>
            </section>
          )}

          {rawDoc && (
            <section>
              <h3>停止策略 stop_policy</h3>
              <p>需要有限运行边界的 workflow 应显式声明停止条件；运行配置也可以在启动时追加外部约束。</p>
              <div className="rf-policy-card">
                <strong>当前 stop_policy</strong>
                <span>{policyText(rawDoc.stop_policy || "未声明")}</span>
              </div>
              <label>
                <span>组合规则</span>
                <select disabled={readOnly} value={stopPolicyDraft.kind} onChange={(e) => setStopPolicyDraft({ ...stopPolicyDraft, kind: e.target.value })}>
                  <option value="any">any</option>
                  <option value="all">all</option>
                </select>
              </label>
              <label><span>max_time_s</span><input disabled={readOnly} value={stopPolicyDraft.max_time_s} onChange={(e) => setStopPolicyDraft({ ...stopPolicyDraft, max_time_s: e.target.value })} placeholder="60" /></label>
              <label><span>max_iterations</span><input disabled={readOnly} value={stopPolicyDraft.max_iterations} onChange={(e) => setStopPolicyDraft({ ...stopPolicyDraft, max_iterations: e.target.value })} placeholder="30" /></label>
              <div className="button-row">
                <button disabled={readOnly} onClick={applyStopPolicy}>应用 stop_policy</button>
                <button disabled={readOnly} className="danger" onClick={clearStopPolicy}>清空</button>
              </div>
            </section>
          )}

          {selectedNode && (
            <section>
              <h3>节点参数</h3>
              <label><span>node_id</span><input disabled={readOnly} value={selectedNode.data.node_id} onChange={(e) => renameNode(selectedNode.id, e.target.value)} /></label>
              <label>
                <span>operator_ref</span>
                <select
                  disabled={readOnly}
                  value={selectedNode.data.operator_ref}
                  onChange={(e) => {
                    const op = opMap.get(e.target.value) || {};
                    const opFamily = op.family || op.operator_family || "";
                    const stageFamily = selectedNode.data.stage_family || "";
                    if (stageFamily && opFamily && stageFamily !== opFamily) {
                      setMessage(`不能切换：stage_family=${stageFamily} 与 operator_family=${opFamily} 不匹配。`);
                      return;
                    }
                    updateNodeData(selectedNode.id, {
                      operator_ref: e.target.value,
                      operator_display_name: operatorDisplayName(op, e.target.value),
                      family: op.family || op.operator_family || "",
                      inputs: arr(op.inputs),
                      outputs: arr(op.outputs),
                      typed_io_contract: clone(op.typed_io_contract || {}),
                    });
                  }}
                >
                  {operators.map((op) => <option key={op.operator_id} value={op.operator_id}>{operatorDisplayName(op, op.operator_id)} · {op.operator_id}</option>)}
                </select>
              </label>
              <div className="rf-operator-typed-summary">
                <strong>operator typed I/O</strong>
                <span>{selectedNode.data.typed_io_contract?.input_dto || "-"} → {selectedNode.data.typed_io_contract?.output_dto || "-"}</span>
                <small>{selectedNode.data.typed_io_contract?.run_fn_type || "run_fn_type 未声明"}</small>
              </div>
              <PortContractTable title="输入端口结构" ports={selectedNode.data.inputs} direction="input" />
              <PortContractTable title="输出端口结构" ports={selectedNode.data.outputs} direction="output" />
              <label className="check"><input disabled={readOnly} type="checkbox" checked={selectedNode.data.activation_policy?.enabled !== false} onChange={(e) => updateNodeData(selectedNode.id, { activation_policy: { ...selectedNode.data.activation_policy, enabled: e.target.checked } })} />启用</label>
              <label className="check"><input disabled={readOnly} type="checkbox" checked={!!selectedNode.data.activation_policy?.required} onChange={(e) => updateNodeData(selectedNode.id, { activation_policy: { ...selectedNode.data.activation_policy, required: e.target.checked } })} />required</label>
              <label><span>feature</span><input disabled={readOnly} value={selectedNode.data.activation_policy?.feature || ""} onChange={(e) => updateNodeData(selectedNode.id, { activation_policy: { ...selectedNode.data.activation_policy, feature: e.target.value } })} /></label>
              <label>
                <span>on_disabled</span>
                <select disabled={readOnly} value={selectedNode.data.activation_policy?.on_disabled || "remove"} onChange={(e) => updateNodeData(selectedNode.id, { activation_policy: { ...selectedNode.data.activation_policy, on_disabled: e.target.value } })}>
                  <option value="remove">remove</option>
                  <option value="fail_compile">fail_compile</option>
                  <option value="skip">skip</option>
                </select>
              </label>
              <button disabled={readOnly} className="danger" onClick={deleteSelection}>删除节点</button>
            </section>
          )}

          {selectedBoundaryNode && (
            <section>
              <h3>{selectedBoundaryNode.data.boundary_kind === "output" ? "阶段输出边界" : "阶段输入边界"}</h3>
              <p>
                这里编辑的是当前 stage 的
                {selectedBoundaryNode.data.boundary_kind === "output" ? " stage_outputs" : " stage_inputs"}
                ，格式为 node_id.port_id。
              </p>
              <div className="rf-boundary-editor-list">
                {arr(selectedBoundaryNode.data.ports).map((port, index) => (
                  <div key={`${port.ref || port.label}-${index}`}>
                    <span title={port.ref || port.label}>{port.ref || port.label}</span>
                    <button disabled={readOnly} onClick={() => removeStagePortRef(selectedBoundaryNode.data.boundary_kind, port.ref || port.label)}>删除</button>
                  </div>
                ))}
                {!arr(selectedBoundaryNode.data.ports).length && <p>当前没有声明端口。</p>}
              </div>
              <label>
                <span>新增端口引用</span>
                <input
                  disabled={readOnly}
                  value={newBoundaryRef}
                  onChange={(e) => setNewBoundaryRef(e.target.value)}
                  placeholder="node_id.port_id"
                />
              </label>
              <button disabled={readOnly} onClick={() => addStagePortRef(selectedBoundaryNode.data.boundary_kind, newBoundaryRef)}>
                新增到{selectedBoundaryNode.data.boundary_kind === "output" ? "阶段输出" : "阶段输入"}
              </button>
            </section>
          )}

          {selectedBoundaryEdge && (
            <section>
              <h3>{selectedBoundaryEdge.data.boundary_kind === "output" ? "阶段输出连线" : "阶段输入连线"}</h3>
              <p>{selectedBoundaryEdge.data.port_ref}</p>
              <button disabled={readOnly} className="danger" onClick={deleteSelection}>删除该边界引用</button>
            </section>
          )}

          {selectedEdge && (
            <section>
              <h3>连线参数</h3>
              <p>{splitNodeKey(selectedEdge.source).node_id} → {splitNodeKey(selectedEdge.target).node_id}</p>
              <label>
                <span>source_port</span>
                <select disabled={readOnly} value={selectedEdge.sourceHandle || ""} onChange={(e) => updateEdgeEndpoint(selectedEdge.id, { sourceHandle: e.target.value })}>
                  {arr(nodes.find((node) => node.id === selectedEdge.source)?.data.outputs).map((port) => <option key={port.port_id} value={port.port_id}>{portLabel(port)}</option>)}
                </select>
              </label>
              <label>
                <span>target_port</span>
                <select disabled={readOnly} value={selectedEdge.targetHandle || ""} onChange={(e) => updateEdgeEndpoint(selectedEdge.id, { targetHandle: e.target.value })}>
                  {arr(nodes.find((node) => node.id === selectedEdge.target)?.data.inputs).map((port) => <option key={port.port_id} value={port.port_id}>{portLabel(port)}</option>)}
                </select>
              </label>
              <button disabled={readOnly} className="danger" onClick={deleteSelection}>删除连线</button>
            </section>
          )}

          <section>
            <h3>校验结果</h3>
            {!validation && <p>尚未校验。</p>}
            {validation && (
              <div className={`rf-validation ${validation.ok ? "ok" : "bad"}`}>
                <strong>{validation.ok ? "通过" : "未通过"}</strong>
                <span>{validation.summary?.node_count || 0} nodes / {validation.summary?.edge_count || 0} edges</span>
                {arr(validation.errors).map((err, index) => <p key={`e-${index}`} className="err">{err}</p>)}
                {arr(validation.warnings).map((warn, index) => <p key={`w-${index}`} className="warn">{warn}</p>)}
              </div>
            )}
          </section>
        </aside>
      </div>
    </div>
  );
}

function mount(element, options = {}) {
  const root = createRoot(element);
  root.render(
    <ReactFlowProvider>
      <EditorApp
        initialObject={options.object || null}
        initialWorkflowId={options.workflowId || ""}
        onWorkflowSaved={options.onWorkflowSaved}
        onWorkflowSelected={options.onWorkflowSelected}
        onWorkflowDeleted={options.onWorkflowDeleted}
        readOnly={!!options.readOnly}
      />
    </ReactFlowProvider>
  );
  return { unmount: () => root.unmount() };
}

window.FlightEnvReactFlowEditor = { mount };

export { mount };
