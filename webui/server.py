#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""FlightEnv Web Twin Workbench local backend.

这个服务只做 UI 本地桥接：
- 读取对象包作为对象/资源/算子/workflow 的真源；
- 读取 Runtime Host 生成的 evidence/run package；
- 触发对象包 tools 下的已有运行脚本；
- 把大场 artifact 作为文件引用读取给 three.js 渲染。

注意：这里不实现任何算法，也不绕过平台 RuntimeHost。真实算子仍由
FlightEnvPlatformRuntimeHost.exe 和对象包声明的 adapter 执行。
"""

import datetime as _dt
import difflib
import hashlib
import json
import os
import re
import shutil
import shlex
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))


def find_workspace_root(start):
    d = start
    for _ in range(8):
        if (os.path.isdir(os.path.join(d, "_local_artifacts"))
                or os.path.isdir(os.path.join(d, "flightenv-platform-pdk"))
                or os.path.isdir(os.path.join(d, "flightenv-controller-ui"))):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            break
        d = parent
    return os.path.abspath(os.path.join(HERE, "..", ".."))


ROOT = find_workspace_root(HERE)
def find_object_package(root):
    # Phase 1 gate: WebUI must start as an empty platform. Object packages are
    # loaded only through the workspace API, so startup never silently binds a
    # concrete object from the current repository or environment.
    return ""


OBJECT_PACKAGE = find_object_package(ROOT)
ARTIFACTS = os.path.join(ROOT, "_local_artifacts")
DRAFT_ROOT = os.environ.get("FLIGHTENV_WEBUI_DRAFT_ROOT") or os.path.join(ARTIFACTS, "webui-workflow-drafts")
DRAFT_FALLBACK_ROOT = os.path.join(tempfile.gettempdir(), "flightenv-webui-workflow-drafts")
OBJECT_DRAFT_ROOT = os.environ.get("FLIGHTENV_WEBUI_OBJECT_DRAFT_ROOT") or os.path.join(ARTIFACTS, "webui-object-drafts")
OBJECT_RELEASE_ROOT = os.environ.get("FLIGHTENV_WEBUI_OBJECT_RELEASE_ROOT") or os.path.join(ARTIFACTS, "webui-object-releases")
OBJECT_PROJECT_ROOT = os.environ.get("FLIGHTENV_WEBUI_OBJECT_PROJECT_ROOT") or os.path.join(ARTIFACTS, "webui-object-projects")
ACTIVE_OBJECT_DRAFT_ID = ""
def mesh_search_dirs():
    dirs = [
        os.path.join(ROOT, "_deps", "example"),
        os.path.join(ROOT, "_deps", "data"),
    ]
    if OBJECT_PACKAGE:
        dirs.append(OBJECT_PACKAGE)
    return dirs

_JOBS = {}
_LATEST_JOB_ID = ""
_JOBS_LOCK = threading.Lock()
COMMAND_RECORD_SCHEMA = "flightenv.webui.runtime_command_records.v1"
COMPILE_JOB_META_FILE = "webui_compile_job.json"
STANDARD_COMPILED_ARTIFACTS = [
    "execution_plan.json",
    "activation_snapshot.json",
    "graph_snapshot.json",
    "edge_binding_plan.json",
    "operator_snapshot.json",
    "resource_lock.json",
    "model_snapshot.json",
    "workflow_snapshot.json",
    "time_plan.json",
    "scheduler_plan.json",
    "uncertainty_plan.json",
    "state_store_plan.json",
    "data_plane_plan.json",
    "compile_evidence.json",
]


# ---------------------------------------------------------------------------
# basic helpers
# ---------------------------------------------------------------------------
def load_json(path):
    with open(path, "r", encoding="utf-8-sig") as f:
        return json.load(f)


def try_load_json(path):
    try:
        return load_json(path)
    except Exception:
        return None


def norm_path(p):
    if not p:
        return ""
    p = str(p).replace("\\", "/")
    for prefix in ("//?/", "file:///"):
        if p.startswith(prefix):
            p = p[len(prefix):]
    return p


def resolve_package_file(path_value, package_dir):
    p = norm_path(path_value).strip()
    if not p:
        return ""
    if os.path.isabs(p):
        return p
    package_dir = package_dir or active_object_package()
    if package_dir:
        return os.path.abspath(os.path.join(package_dir, p))
    return os.path.abspath(p)


def run_dir_abs(run):
    if not run:
        return ""
    run = norm_path(run)
    if os.path.isabs(run):
        return run
    return os.path.join(ARTIFACTS, run)


def run_rel_to_dir(run_rel):
    return run_dir_abs(run_rel) if run_rel else ""


def write_json_atomic(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")
    os.replace(tmp, path)


def copytree_clean(src, dst):
    def ignore(_dir, names):
        ignored = {".git", "__pycache__", ".vs", "build", "x64", "Debug", "Release"}
        return [name for name in names if name in ignored or name.endswith(".zip")]

    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=ignore)


def package_id_for(package_dir):
    twin = try_load_json(os.path.join(package_dir or "", "object", "twin_object.json")) or {}
    return _safe_id(twin.get("object_id") or os.path.basename(os.path.abspath(package_dir or "object")))


def object_draft_dir(draft_id="", package_dir=None):
    package_dir = package_dir or OBJECT_PACKAGE
    object_id = package_id_for(package_dir) if package_dir else "object"
    base = os.path.join(OBJECT_DRAFT_ROOT, object_id)
    return os.path.join(base, _safe_id(draft_id)) if draft_id else base


def object_draft_package_dir(draft_id="", package_dir=None):
    return os.path.join(object_draft_dir(draft_id, package_dir), "package") if draft_id else ""


def active_object_package():
    if ACTIVE_OBJECT_DRAFT_ID:
        draft_pkg = object_draft_package_dir(ACTIVE_OBJECT_DRAFT_ID)
        if os.path.isdir(draft_pkg):
            return draft_pkg
    return OBJECT_PACKAGE


def package_for_payload(payload=None):
    payload = payload or {}
    draft_id = str(payload.get("draft_id") or ACTIVE_OBJECT_DRAFT_ID or "").strip()
    if draft_id:
        draft_pkg = object_draft_package_dir(draft_id)
        if os.path.isdir(draft_pkg):
            return draft_pkg
    explicit_pkg = str(payload.get("package_dir") or payload.get("object_package") or "").strip()
    if explicit_pkg and os.path.isdir(explicit_pkg):
        return os.path.abspath(explicit_pkg)
    return active_object_package()


def package_domain(package_dir):
    package_dir = os.path.abspath(package_dir or "")
    formal = os.path.abspath(OBJECT_PACKAGE) if OBJECT_PACKAGE else ""
    if formal and package_dir == formal:
        return "object_package"
    if package_dir and os.path.abspath(package_dir).startswith(os.path.abspath(OBJECT_DRAFT_ROOT)):
        return "object_draft"
    if package_dir and os.path.abspath(package_dir).startswith(os.path.abspath(OBJECT_RELEASE_ROOT)):
        return "object_release"
    return "workspace"


def rel_to_artifacts(path):
    try:
        return os.path.relpath(path, ARTIFACTS).replace("\\", "/")
    except Exception:
        return path


def resolve_artifact(uri, run_dir):
    p = norm_path(uri)
    m = re.search(r"[A-Za-z]:/", p)
    if m:
        return p[m.start():]
    if p.startswith("artifact://") or p.startswith("inline://"):
        return ""
    if os.path.isabs(p):
        return p
    return os.path.join(run_dir, p)


def resolve_mesh_file(name):
    name = norm_path(name)
    if os.path.isabs(name) and os.path.exists(name):
        return name
    base = os.path.basename(name)
    for d in mesh_search_dirs():
        cand = os.path.join(d, base)
        if os.path.exists(cand):
            return cand
    return ""


def file_mtime(path):
    try:
        return os.path.getmtime(path)
    except Exception:
        return 0.0


def latest_mtime(d):
    latest = file_mtime(d)
    for name in (
        "runtime_host_evidence.json",
        "run_timeline_index.json",
        "branch_registry.json",
        "mainline_progress.json",
        "data_plane_manifest.json",
        "runtime_snapshot.json",
    ):
        latest = max(latest, file_mtime(os.path.join(d, name)))
    return latest


def looks_like_run(d):
    return any(os.path.exists(os.path.join(d, name)) for name in (
        "runtime_host_evidence.json",
        "run_timeline_index.json",
        "branch_registry.json",
        "mainline_progress.json",
        "runtime_snapshot.json",
        "data_plane_manifest.json",
        "mainline_summary.json",
        "workflow_timeline.json",
    ))


def _object_package_from_known_path(path_value):
    path = os.path.abspath(norm_path(path_value)) if path_value else ""
    if not path:
        return ""
    current = path if os.path.isdir(path) else os.path.dirname(path)
    for _ in range(8):
        if os.path.exists(os.path.join(current, "object", "twin_object.json")):
            return current
        parent = os.path.dirname(current)
        if parent == current:
            break
        current = parent
    return ""


def _object_package_from_run(run_dir):
    if not run_dir:
        return ""
    evidence = try_load_json(os.path.join(run_dir, "runtime_host_evidence.json")) or {}
    inputs = evidence.get("inputs", {}) if isinstance(evidence, dict) else {}
    for key in (
            "object_runtime_profile",
            "adapter_registry",
            "external_observation_stream",
            "compiled_online_workflow",
            "compiled_future_workflow"):
        package_dir = _object_package_from_known_path(inputs.get(key, ""))
        if package_dir:
            return package_dir
    summary = try_load_json(os.path.join(run_dir, "mainline_summary.json")) or {}
    online = summary.get("online", {}) if isinstance(summary, dict) else {}
    return _object_package_from_known_path(online.get("external_observation_stream", ""))


def snapshot_for_run(run_dir):
    if run_dir:
        snap = try_load_json(os.path.join(run_dir, "runtime_snapshot.json"))
        if snap:
            return snap
    package_dir = active_object_package() or _object_package_from_run(run_dir)
    if not package_dir:
        return {}
    return try_load_json(os.path.join(package_dir, "assets", "runtime_snapshot.json")) or {}


def read_run_timeline(run_dir):
    return try_load_json(os.path.join(run_dir, "run_timeline_index.json")) or {}


def read_branch_registry(run_dir):
    reg = try_load_json(os.path.join(run_dir, "branch_registry.json")) or {}
    idx = read_run_timeline(run_dir)
    if not reg and isinstance(idx.get("branches"), list):
        reg = {"branches": idx.get("branches", [])}
    return reg


def command_record_path(run_dir):
    if run_dir:
        return os.path.join(run_dir, "runtime_command_records.json")
    return os.path.join(ARTIFACTS, "webui-runtime-commands", "runtime_command_records.json")


def read_command_records(run_dir):
    doc = try_load_json(command_record_path(run_dir)) or {}
    commands = doc.get("commands", []) if isinstance(doc, dict) else []
    return commands if isinstance(commands, list) else []


def append_command_record(action, payload, result=None, run_rel="", job_id="", status="requested"):
    payload = payload or {}
    result = result or {}
    run_dir = run_rel_to_dir(run_rel or result.get("run", ""))
    path = command_record_path(run_dir)
    doc = try_load_json(path) or {
        "schema_version": COMMAND_RECORD_SCHEMA,
        "commands": [],
    }
    if not isinstance(doc.get("commands"), list):
        doc["commands"] = []
    command_id = "cmd_%s_%d" % (_safe_id(action), int(time.time() * 1000))
    record = {
        "command_id": command_id,
        "action": action,
        "status": status,
        "generated_at_utc": _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z",
        "workflow_id": payload.get("workflow_id", ""),
        "run_profile_id": payload.get("run_profile_id", ""),
        "compiled_dir": payload.get("compiled_dir", ""),
        "run_id": result.get("run_id", payload.get("run_id", "")),
        "run": run_rel or result.get("run", ""),
        "job_id": job_id or result.get("job_id", ""),
        "pid": result.get("pid", ""),
        "ok": result.get("ok", None),
        "message": result.get("message", ""),
    }
    doc["commands"].append(record)
    doc["updated_at_utc"] = record["generated_at_utc"]
    write_json_atomic(path, doc)
    return record


def utc_now_iso():
    return _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"


def _as_dict(value):
    return value if isinstance(value, dict) else {}


def _as_list(value):
    return value if isinstance(value, list) else []


def _runtime_event_path(run_dir):
    return os.path.join(run_dir, "runtime_events.json")


def _read_runtime_events(run_dir):
    return normalize_runtime_events(try_load_json(_runtime_event_path(run_dir)) or [])


def _sync_timeline_runtime_events(run_dir, events=None):
    if not run_dir:
        return {}
    idx_path = os.path.join(run_dir, "run_timeline_index.json")
    idx = try_load_json(idx_path) or {}
    if not isinstance(idx, dict):
        idx = {}
    idx.setdefault("schema_version", "flightenv.webui.run_timeline_index.v1")
    idx["runtime_events"] = _as_list(events if events is not None else _read_runtime_events(run_dir))
    reg = read_branch_registry(run_dir)
    if reg.get("branches") and not idx.get("branches"):
        idx["branches"] = reg.get("branches", [])
    idx["updated_at_utc"] = utc_now_iso()
    write_json_atomic(idx_path, idx)
    return idx


def append_runtime_event(run_dir, event_kind, message="", severity="info", **extra):
    if not run_dir:
        return {}
    os.makedirs(run_dir, exist_ok=True)
    events = _read_runtime_events(run_dir)
    event = {
        "time_utc": utc_now_iso(),
        "severity": severity,
        "event": event_kind,
        "message": message,
    }
    event.update({k: v for k, v in extra.items() if v not in (None, "")})
    events.append(event)
    write_json_atomic(_runtime_event_path(run_dir), events)
    _sync_timeline_runtime_events(run_dir, events)
    return event


def _upsert_main_branch(run_dir, run_id, workflow_id, run_profile_id, status, message=""):
    reg_path = os.path.join(run_dir, "branch_registry.json")
    reg = try_load_json(reg_path) or {}
    if not isinstance(reg, dict):
        reg = {}
    branches = _as_list(reg.get("branches"))
    now = utc_now_iso()
    main_index = -1
    for i, branch in enumerate(branches):
        if branch.get("branch_id") in ("main.online", "main") or branch.get("branch_kind") == "mainline":
            main_index = i
            break
    row = branches[main_index] if main_index >= 0 else {}
    row.update({
        "branch_id": row.get("branch_id") or "main.online",
        "branch_kind": row.get("branch_kind") or "mainline",
        "status": status,
        "workflow_id": workflow_id,
        "run_profile_id": run_profile_id,
        "parent_branch_id": row.get("parent_branch_id", ""),
        "cause_event_id": row.get("cause_event_id", "runtime_submit"),
        "trigger_frame": row.get("trigger_frame", 0),
        "created_at_utc": row.get("created_at_utc", now),
        "updated_at_utc": now,
    })
    if message:
        row["message"] = message
    if main_index >= 0:
        branches[main_index] = row
    else:
        branches.insert(0, row)
    reg.update({
        "schema_version": reg.get("schema_version") or "flightenv.webui.branch_registry.v1",
        "run_id": run_id,
        "workflow_id": workflow_id,
        "run_profile_id": run_profile_id,
        "branches": branches,
        "updated_at_utc": now,
    })
    write_json_atomic(reg_path, reg)
    return reg


def ensure_runtime_run_package(
    run_dir,
    run_rel,
    payload,
    run_id,
    workflow_id,
    run_profile_id,
    status,
    runtime_launch=None,
    backend="",
    package_dir="",
    compiled_dir="",
    job_id="",
    message="",
):
    payload = payload or {}
    runtime_launch = runtime_launch or {}
    os.makedirs(run_dir, exist_ok=True)
    now = utc_now_iso()

    progress_path = os.path.join(run_dir, "mainline_progress.json")
    progress = try_load_json(progress_path) or {}
    if not isinstance(progress, dict):
        progress = {}
    progress.setdefault("schema_version", "flightenv.webui.runtime_progress.v1")
    progress.update({
        "status": status,
        "stage": progress.get("stage") or status,
        "run_id": run_id,
        "workflow_id": workflow_id,
        "run_profile_id": run_profile_id,
        "clock": progress.get("clock") if isinstance(progress.get("clock"), dict) else {
            "tick_index": 0,
            "run_time_s": 0.0,
            "source_time_s": 0.0,
        },
        "runtime_launch": runtime_launch,
        "updated_at_utc": now,
    })
    if message:
        progress["message"] = message
    write_json_atomic(progress_path, progress)

    reg = _upsert_main_branch(run_dir, run_id, workflow_id, run_profile_id, status, message)

    dp_path = os.path.join(run_dir, "data_plane_manifest.json")
    dp = try_load_json(dp_path) or {}
    if not isinstance(dp, dict):
        dp = {}
    entries = _as_list(dp.get("entries"))
    fields = _as_list(dp.get("fields"))
    qois = _as_list(dp.get("qois"))
    dp.update({
        "schema_version": dp.get("schema_version") or "flightenv.webui.data_plane_manifest.v1",
        "run_id": run_id,
        "workflow_id": workflow_id,
        "run_profile_id": run_profile_id,
        "entries": entries,
        "fields": fields,
        "qois": qois,
        "ports": _as_list(dp.get("ports")),
        "updated_at_utc": now,
    })
    summary = _as_dict(dp.get("summary"))
    summary.update({
        "entry_count": len(entries),
        "field_count": len(fields),
        "qoi_count": len(qois),
    })
    dp["summary"] = summary
    write_json_atomic(dp_path, dp)

    idx_path = os.path.join(run_dir, "run_timeline_index.json")
    idx = try_load_json(idx_path) or {}
    if not isinstance(idx, dict):
        idx = {}
    online_frames = _as_list(idx.get("online_frames"))
    prediction_runs = _as_list(idx.get("prediction_runs"))
    branch_steps = _as_list(idx.get("branch_steps"))
    artifact_refs = _as_list(idx.get("artifact_refs"))
    qoi_refs = _as_list(idx.get("qoi_refs"))
    idx.update({
        "schema_version": idx.get("schema_version") or "flightenv.webui.run_timeline_index.v1",
        "run_id": run_id,
        "run": run_rel,
        "workflow_id": workflow_id,
        "run_profile_id": run_profile_id,
        "status": status,
        "branches": reg.get("branches", []),
        "online_frames": online_frames,
        "prediction_runs": prediction_runs,
        "branch_steps": branch_steps,
        "artifact_refs": artifact_refs,
        "qoi_refs": qoi_refs,
        "runtime_events": _read_runtime_events(run_dir),
        "updated_at_utc": now,
    })
    timeline_summary = _as_dict(idx.get("summary"))
    timeline_summary.update({
        "online_frame_count": len(online_frames),
        "prediction_run_count": len(prediction_runs),
        "branch_step_count": len(branch_steps),
        "artifact_ref_count": len(artifact_refs),
        "qoi_ref_count": len(qoi_refs),
    })
    idx["summary"] = timeline_summary
    write_json_atomic(idx_path, idx)

    evidence_path = os.path.join(run_dir, "runtime_host_evidence.json")
    evidence = try_load_json(evidence_path) or {}
    if not isinstance(evidence, dict):
        evidence = {}
    host = _as_dict(evidence.get("host"))
    host.update({
        "execution_backend": backend,
        "bridge": "webui_runtime_job",
        "object_package": package_dir,
        "compiled_dir": compiled_dir,
    })
    summary = _as_dict(evidence.get("summary"))
    summary.update({
        "command_count": len(read_command_records(run_dir)),
        "runtime_event_count": len(_read_runtime_events(run_dir)),
        "branch_count": len(reg.get("branches", []) or []),
        "data_plane_entry_count": len(entries),
        "has_timeline": True,
        "has_branch_registry": True,
        "has_data_plane_manifest": True,
    })
    evidence.update({
        "schema_version": evidence.get("schema_version") or "flightenv.webui.runtime_host_evidence.v1",
        "run_id": run_id,
        "run": run_rel,
        "job_id": job_id,
        "workflow_id": workflow_id,
        "run_profile_id": run_profile_id,
        "status": status,
        "host": host,
        "runtime_launch": runtime_launch,
        "inputs": {
            "workflow_id": workflow_id,
            "run_profile_id": run_profile_id,
            "compiled_dir": compiled_dir,
            "script": payload.get("script", ""),
        },
        "outputs": {
            "timeline": "run_timeline_index.json",
            "branch_registry": "branch_registry.json",
            "data_plane_manifest": "data_plane_manifest.json",
            "runtime_events": "runtime_events.json",
            "command_records": "runtime_command_records.json",
        },
        "summary": summary,
        "updated_at_utc": now,
    })
    if message:
        evidence["message"] = message
    write_json_atomic(evidence_path, evidence)
    return {
        "ok": True,
        "run_dir": run_dir,
        "run": run_rel,
        "run_id": run_id,
        "status": status,
        "files": [
            "runtime_host_evidence.json",
            "run_timeline_index.json",
            "branch_registry.json",
            "data_plane_manifest.json",
            "runtime_events.json",
            "mainline_progress.json",
        ],
    }


def update_runtime_run_package_status(run_rel, status, message="", event_kind="", job_id="", payload=None, result=None):
    run_dir = run_rel_to_dir(run_rel)
    if not run_dir:
        return {}
    evidence = try_load_json(os.path.join(run_dir, "runtime_host_evidence.json")) or {}
    progress = try_load_json(os.path.join(run_dir, "mainline_progress.json")) or {}
    idx = try_load_json(os.path.join(run_dir, "run_timeline_index.json")) or {}
    payload = payload or {}
    result = result or {}
    run_id = result.get("run_id") or payload.get("run_id") or evidence.get("run_id") or progress.get("run_id") or idx.get("run_id") or os.path.basename(run_dir)
    workflow_id = payload.get("workflow_id") or evidence.get("workflow_id") or progress.get("workflow_id") or idx.get("workflow_id", "")
    run_profile_id = payload.get("run_profile_id") or evidence.get("run_profile_id") or progress.get("run_profile_id") or idx.get("run_profile_id", "")
    runtime_launch = result.get("runtime_launch") or evidence.get("runtime_launch") or progress.get("runtime_launch") or {}
    host = _as_dict(evidence.get("host"))
    ensure_runtime_run_package(
        run_dir,
        run_rel,
        payload,
        run_id,
        workflow_id,
        run_profile_id,
        status,
        runtime_launch=runtime_launch,
        backend=host.get("execution_backend", ""),
        package_dir=host.get("object_package", ""),
        compiled_dir=payload.get("compiled_dir") or host.get("compiled_dir", ""),
        job_id=job_id or evidence.get("job_id", ""),
        message=message,
    )
    if event_kind:
        append_runtime_event(run_dir, event_kind, message, run_id=run_id, job_id=job_id, status=status)
    return {"ok": True, "run": run_rel, "run_id": run_id, "status": status}


def job_snapshot(job_id=""):
    with _JOBS_LOCK:
        if not job_id:
            job_id = _LATEST_JOB_ID
        job = _JOBS.get(job_id)
    if not job:
        return {}
    proc = job.get("proc")
    waiting_external = bool(job.get("waiting_external_input"))
    if proc is None:
        code = job.get("exit_code", None)
        state = job.get("status", "waiting_external_input" if waiting_external else "idle")
        running = waiting_external and state not in ("stopped", "failed", "completed")
        pid = ""
    else:
        code = proc.poll()
        running = code is None
        pid = getattr(proc, "pid", "")
        state = "running" if code is None else ("completed" if code == 0 else "failed")
    action = job.get("action", "")
    if proc is not None and code is None and action == "prepare":
        state = "preparing"
    if proc is not None and code is None and job.get("status") in ("paused", "resume_requested", "checkpoint_requested"):
        state = job.get("status")
    if job.get("status") == "stopped":
        state = "stopped"
    return {
        "job_id": job_id,
        "run_id": job.get("run_id", ""),
        "run": job.get("run", ""),
        "action": action,
        "pid": pid,
        "running": running,
        "exit_code": code,
        "status": state,
        "started": job.get("started", 0.0),
        "elapsed_s": round(time.time() - job.get("started", time.time()), 1),
        "script": job.get("script", ""),
        "cmd": job.get("cmd", []),
        "workflow_id": job.get("workflow_id", ""),
        "run_profile_id": job.get("run_profile_id", ""),
        "compiled_dir": job.get("compiled_dir", ""),
        "backend": job.get("backend", ""),
        "external_observation_stream": job.get("external_observation_stream", ""),
        "log": job.get("log", ""),
    }


def job_for_run(run_rel="", run_id=""):
    with _JOBS_LOCK:
        items = list(_JOBS.items())
    for job_id, job in reversed(items):
        if run_rel and job.get("run") == run_rel:
            return job_snapshot(job_id)
        if run_id and job.get("run_id") == run_id:
            return job_snapshot(job_id)
    return {}


def normalize_runtime_events(raw):
    if isinstance(raw, list):
        return raw
    if isinstance(raw, dict):
        value = raw.get("events", [])
        return value if isinstance(value, list) else []
    return []


def summarize_initialization(initialization, compiled_dir=""):
    workflows = initialization.get("workflows", []) if isinstance(initialization, dict) else []
    resource_lock_count = model_snapshot_count = operator_snapshot_count = 0
    for item in workflows:
        resource_doc = try_load_json(item.get("resource_lock", "")) or {}
        model_doc = try_load_json(item.get("model_snapshot", "")) or {}
        operator_doc = try_load_json(item.get("operator_snapshot", "")) or {}
        resource_lock_count += len(resource_doc.get("resources", []) or [])
        model_snapshot_count += len(model_doc.get("models", []) or [])
        operator_snapshot_count += len(operator_doc.get("operators", []) or [])
    if not workflows and compiled_dir:
        resource_doc = try_load_json(os.path.join(compiled_dir, "resource_lock.json")) or {}
        model_doc = try_load_json(os.path.join(compiled_dir, "model_snapshot.json")) or {}
        operator_doc = try_load_json(os.path.join(compiled_dir, "operator_snapshot.json")) or {}
        resource_lock_count = len(resource_doc.get("resources", []) or [])
        model_snapshot_count = len(model_doc.get("models", []) or [])
        operator_snapshot_count = len(operator_doc.get("operators", []) or [])
    status = initialization.get("status", "") if isinstance(initialization, dict) else ""
    return {
        "status": status or ("prepared" if resource_lock_count or model_snapshot_count else "idle"),
        "workflow_count": len(workflows),
        "resource_lock_count": resource_lock_count,
        "model_snapshot_count": model_snapshot_count,
        "operator_snapshot_count": operator_snapshot_count,
        "preflight_run_count": len(initialization.get("preflight_runs", []) if isinstance(initialization, dict) else []),
        "workflows": workflows,
    }


def runtime_blocking_state(run_dir, job, events):
    latest = latest_mtime(run_dir) if run_dir else 0.0
    age_s = round(time.time() - latest, 1) if latest else ""
    recent = events[-1] if events else {}
    stalled = bool(job.get("running") and latest and age_s != "" and age_s > 30)
    return {
        "stalled": stalled,
        "artifact_age_s": age_s,
        "current_stage": first_non_empty(
            (try_load_json(os.path.join(run_dir, "mainline_progress.json")) or {}).get("stage", "") if run_dir else "",
            job.get("action", ""),
            job.get("status", ""),
        ),
        "elapsed_s": job.get("elapsed_s", ""),
        "recent_event": recent,
        "message": "运行中超过 30s 未发现 run artifact 更新时间" if stalled else "",
    }


def first_non_empty(*values):
    for value in values:
        if value not in (None, ""):
            return value
    return ""


def data_entries(run_dir):
    idx = read_run_timeline(run_dir)
    if idx:
        entries = []
        entries.extend(idx.get("artifact_refs", []) or [])
        entries.extend(idx.get("qoi_refs", []) or [])
        return entries, "run_timeline_index", idx
    dp = try_load_json(os.path.join(run_dir, "data_plane_manifest.json")) or {}
    return dp.get("entries", []) or [], "data_plane_manifest", dp


def is_field_entry(e):
    port = str(e.get("port_id", ""))
    rep = e.get("representation")
    return (port.startswith("field.")
            and rep in ("artifact_ref", "dense_scalar_field", "field_tensor")
            and int(e.get("node_count") or 0) > 0)


def is_qoi_entry(e):
    port = str(e.get("port_id", ""))
    contract = str(e.get("contract_id", ""))
    value_kind = str(e.get("value_kind", ""))
    return port.startswith("qoi.") or value_kind.startswith("qoi") or "qoi" in contract.lower()


def entry_step(e):
    for key in ("loop_iteration_index", "step_index", "mainline_frame_index"):
        if e.get(key) is not None:
            try:
                return int(e.get(key))
            except Exception:
                pass
    return 0


def entry_time(e):
    tp = e.get("time_point") or {}
    return tp.get("source_time_s", tp.get("run_time_s", None))


def normalize_field_entry(e):
    stats = e.get("statistics", {}) or {}
    step = entry_step(e)
    port_id = e.get("port_id", "")
    node_id = e.get("node_id", "")
    artifact_uri = e.get("artifact_uri") or e.get("ref") or ""
    return {
        "artifact_id": e.get("artifact_id") or "%s/%s/%s" % (node_id, port_id, step),
        "kind": e.get("kind", "field_tensor"),
        "value_kind": e.get("value_kind", "field_tensor"),
        "branch_id": e.get("branch_id", ""),
        "port_id": port_id,
        "node_id": node_id,
        "operator_id": e.get("operator_id", ""),
        "field_name": e.get("field_name", ""),
        "component_id": e.get("component_id", ""),
        "contract_id": e.get("contract_id", ""),
        "representation": e.get("representation", ""),
        "mesh_ref": e.get("mesh_ref", ""),
        "layout_ref": e.get("layout_ref", ""),
        "unit": e.get("unit", ""),
        "node_count": e.get("node_count"),
        "step": step,
        "loop_iteration_index": e.get("loop_iteration_index"),
        "step_index": e.get("step_index"),
        "mainline_frame_index": e.get("mainline_frame_index"),
        "time_s": entry_time(e),
        "min": stats.get("min"),
        "max": stats.get("max"),
        "mean": stats.get("mean"),
        "uri": artifact_uri,
        "artifact_uri": artifact_uri,
        "inline_byte_size": e.get("inline_byte_size", 0),
        "buffer_bytes": e.get("buffer_bytes", 0),
        "producer": node_id or e.get("operator_id", ""),
        "source_run_dir": e.get("source_run_dir", ""),
    }


def normalize_qoi_entry(e):
    stats = e.get("statistics", {}) or {}
    step = entry_step(e)
    port_id = e.get("port_id", "")
    node_id = e.get("node_id", "")
    return {
        "artifact_id": e.get("artifact_id") or "%s/%s/%s" % (node_id, port_id, step),
        "kind": e.get("kind", "qoi_record"),
        "value_kind": e.get("value_kind", "qoi"),
        "branch_id": e.get("branch_id", ""),
        "port_id": port_id,
        "node_id": node_id,
        "operator_id": e.get("operator_id", ""),
        "contract_id": e.get("contract_id", ""),
        "representation": e.get("representation", ""),
        "step": step,
        "mainline_frame_index": e.get("mainline_frame_index"),
        "time_s": entry_time(e),
        "summary": e.get("summary", e.get("decision", "")),
        "min": stats.get("min"),
        "max": stats.get("max"),
        "mean": stats.get("mean"),
        "ref": e.get("ref", ""),
        "producer": node_id or e.get("operator_id", ""),
    }


# ---------------------------------------------------------------------------
# run discovery and evidence readers
# ---------------------------------------------------------------------------
def list_runs():
    mainline_roots = [
        os.path.join(ARTIFACTS, "platform-pdk", "mainline-runs"),
        os.path.join(ARTIFACTS, "platform-runtime", "mainline-runs"),
    ]
    candidate_dirs = []
    for root in mainline_roots:
        if not os.path.isdir(root):
            continue
        for name in os.listdir(root):
            d = os.path.join(root, name)
            if os.path.isdir(d):
                candidate_dirs.append(d)

    found = {}
    for cur in candidate_dirs:
        if not looks_like_run(cur):
            continue
        rel = rel_to_artifacts(cur)
        reg = try_load_json(os.path.join(cur, "branch_registry.json")) or {}
        progress = try_load_json(os.path.join(cur, "mainline_progress.json")) or {}
        has_timeline = os.path.exists(os.path.join(cur, "run_timeline_index.json"))
        has_dataplane = os.path.exists(os.path.join(cur, "data_plane_manifest.json"))
        has_field_dir = os.path.isdir(os.path.join(cur, "field_artifacts"))
        summary = progress.get("summary", {}) if isinstance(progress.get("summary", {}), dict) else {}
        found[rel] = {
            "run": rel,
            "name": os.path.basename(cur),
            "abs_path": cur,
            "has_snapshot": os.path.exists(os.path.join(cur, "runtime_snapshot.json")),
            "has_dataplane": has_dataplane,
            "has_timeline": has_timeline,
            "has_runtime_host": os.path.exists(os.path.join(cur, "runtime_host_evidence.json")),
            "has_branch_registry": bool(reg.get("branches")),
            # 列表页必须轻量：不在这里读取大型 run_timeline_index；
            # 选中 run 后由 /api/dataplane 再完整解析 artifact_refs。
            "has_fields": bool(has_field_dir or has_dataplane or has_timeline),
            "artifact_count": summary.get("artifact_ref_count", ""),
            "qoi_count": summary.get("qoi_ref_count", ""),
            "branch_count": len(reg.get("branches", []) or []),
            "status": progress.get("status", ""),
            "stage": progress.get("stage", ""),
            "mtime": latest_mtime(cur),
        }
    out = list(found.values())
    out.sort(key=lambda r: (
        not r["has_runtime_host"],
        not r["has_fields"],
        -r["mtime"],
        r["run"],
    ))
    return out[:200]


def _layout_token_values(layout):
    values = []
    for key in ("layout_ref", "mesh_ref", "resource_id", "fieldnodeset_ref", "name", "subject"):
        value = layout.get(key)
        if value not in (None, ""):
            values.append(str(value))
    return values


def geometry(run_dir, subject="", layout_ref="", mesh_ref="", node_count=None, visual_surface=False):
    snap = snapshot_for_run(run_dir)
    layouts = snap.get("field_layouts", [])
    wanted_count = None
    try:
        wanted_count = int(node_count) if node_count not in (None, "") else None
    except Exception:
        wanted_count = None
    tokens = [str(v) for v in (layout_ref, mesh_ref) if v]
    layout = None
    if tokens:
        layout = next((
            l for l in layouts
            if any(token == candidate or token.endswith(candidate) or candidate.endswith(token)
                   for token in tokens for candidate in _layout_token_values(l))
        ), None)
    if layout is None and wanted_count:
        layout = next((l for l in layouts if int(l.get("node_count") or len(l.get("nodes", []) or [])) == wanted_count), None)
    if layout is None and subject:
        layout = next((l for l in layouts if l.get("subject") == subject), None)
    if layout is None and layouts:
        layout = layouts[0]
        subject = layout.get("subject", subject)
    if layout is None:
        return {"ok": False, "message": "runtime_snapshot 中没有 field_layouts"}

    nodes = layout.get("nodes", [])
    positions = []
    node_ids = []
    for i, n in enumerate(nodes):
        positions.extend([float(n.get("x", 0.0)), float(n.get("y", 0.0)), float(n.get("z", 0.0))])
        raw_node_id = n.get("node_id", n.get("nodeId", n.get("id", n.get("index", i + 1))))
        try:
            node_ids.append(int(raw_node_id))
        except (TypeError, ValueError):
            node_ids.append(i + 1)
    node_count = len(nodes)

    subject = layout.get("subject", subject)
    mesh = None
    exact_mesh = False
    if mesh_ref:
        mesh = next((
            m for m in snap.get("meshes", [])
            if mesh_ref == str(m.get("name", "")) or mesh_ref == str(m.get("path", "")) or str(m.get("path", "")).endswith(str(mesh_ref))
        ), None)
        exact_mesh = mesh is not None
    subject_meshes = [m for m in snap.get("meshes", []) if subject and subject in (m.get("subjects") or [])]
    if mesh is None and visual_surface:
        mesh = next((
            m for m in subject_meshes
            if "surf" in str(m.get("path", "")).lower()
            or "surface" in str(m.get("name", "")).lower()
            or "surface" in str(m.get("layout_role", "")).lower()
        ), None)
    if mesh is None:
        mesh = subject_meshes[0] if subject_meshes else None
    indices = []
    mesh_file = ""
    if mesh:
        mesh_file = resolve_mesh_file(mesh.get("path", ""))
        if mesh_file:
            with open(mesh_file, "r", encoding="utf-8-sig", errors="ignore") as f:
                for line in f:
                    parts = line.split()
                    if len(parts) < 4:
                        continue
                    try:
                        a, b, c = int(parts[1]) - 1, int(parts[2]) - 1, int(parts[3]) - 1
                    except ValueError:
                        continue
                    if 0 <= a < node_count and 0 <= b < node_count and 0 <= c < node_count:
                        indices.extend([a, b, c])

    bounds = None
    if node_count:
        xs, ys, zs = positions[0::3], positions[1::3], positions[2::3]
        bounds = {"min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]}
    return {
        "ok": True,
        "subject": subject,
        "subject_label": str(subject or layout_ref or mesh_ref or "layout"),
        "layout_ref": layout_ref or layout.get("layout_ref", "") or layout.get("name", "") or str(subject or ""),
        "mesh_ref": mesh_ref or (mesh.get("name", "") if mesh else ""),
        "node_count": node_count,
        "node_ids": node_ids,
        "triangle_count": len(indices) // 3,
        "positions": positions,
        "indices": indices,
        "bounds": bounds,
        "mesh_file": os.path.basename(mesh_file) if mesh_file else "",
        "visual_surface": bool(visual_surface),
        "exact_mesh": bool(exact_mesh),
    }


def dataplane_fields(run_dir):
    entries, source, raw = data_entries(run_dir)
    fields = [normalize_field_entry(e) for e in entries if is_field_entry(e)]
    qois = [normalize_qoi_entry(e) for e in entries if is_qoi_entry(e)]
    fields.sort(key=lambda f: (
        f.get("branch_id", ""),
        int(f.get("mainline_frame_index") or 0),
        int(f.get("step") or 0),
        f.get("port_id", ""),
    ))
    qois.sort(key=lambda q: (q.get("branch_id", ""), int(q.get("step") or 0), q.get("port_id", "")))
    branch_ids = sorted({f.get("branch_id", "") for f in fields if f.get("branch_id")})
    ports = sorted({f.get("port_id", "") for f in fields if f.get("port_id")})
    summary = raw.get("summary", {}) if isinstance(raw, dict) else {}
    return {
        "ok": True,
        "schema_source": source,
        "workflow_id": raw.get("workflow_id", "") if isinstance(raw, dict) else "",
        "run_id": raw.get("run_id", os.path.basename(run_dir)) if isinstance(raw, dict) else os.path.basename(run_dir),
        "summary": summary,
        "fields": fields,
        "qois": qois,
        "ports": ports,
        "branch_ids": branch_ids,
        "field_count": len(fields),
        "qoi_count": len(qois),
    }


def _candidate_score(e, wanted_branch):
    branch = e.get("branch_id", "")
    branch_score = 2 if wanted_branch and branch == wanted_branch else (1 if not wanted_branch and branch == "main.online" else 0)
    return (
        branch_score,
        int(e.get("mainline_frame_index") or 0),
        int(e.get("loop_iteration_index") or 0),
        int(e.get("step_index") or 0),
    )


def _entry_step_values(e):
    values = []
    for key in ("loop_iteration_index", "step_index", "mainline_frame_index"):
        if e.get(key) is None:
            continue
        try:
            values.append(int(e.get(key)))
        except Exception:
            pass
    return values or [entry_step(e)]


def _matches_field_filters(e, node_id, port_id, branch_id):
    if not is_field_entry(e):
        return False
    if port_id and e.get("port_id") != port_id:
        return False
    if node_id and e.get("node_id") != node_id:
        return False
    if branch_id and e.get("branch_id") != branch_id:
        return False
    return True


def _field_entry_artifact_id(e):
    step = entry_step(e)
    return e.get("artifact_id") or "%s/%s/%s" % (e.get("node_id", ""), e.get("port_id", ""), step)


def field_values(run_dir, node_id, port_id, step, branch_id, artifact_id=""):
    entries, _source, _raw = data_entries(run_dir)
    candidates = []
    for e in entries:
        if not is_field_entry(e):
            continue
        if artifact_id:
            exact_ids = {
                str(e.get("artifact_id") or ""),
                str(_field_entry_artifact_id(e) or ""),
                str(e.get("artifact_uri") or ""),
                str(e.get("uri") or ""),
                str(e.get("ref") or ""),
            }
            if artifact_id in exact_ids:
                candidates.append(e)
            continue
        if _matches_field_filters(e, node_id, port_id, branch_id):
            if step is not None and int(step) not in _entry_step_values(e):
                continue
            candidates.append(e)
    if not candidates:
        relaxed = [e for e in entries if _matches_field_filters(e, node_id, port_id, branch_id)]
        if step is not None and relaxed:
            wanted = int(step)
            relaxed.sort(key=lambda e: (
                min(abs(v - wanted) for v in _entry_step_values(e)),
                0 if any(v <= wanted for v in _entry_step_values(e)) else 1,
                -_candidate_score(e, branch_id)[1],
                -_candidate_score(e, branch_id)[2],
                -_candidate_score(e, branch_id)[3],
            ))
            candidates = [relaxed[0]]
    if not candidates:
        return {"ok": False, "message": "没有找到匹配的场 artifact 端口", "port_id": port_id, "branch_id": branch_id}
    candidates.sort(key=lambda e: _candidate_score(e, branch_id), reverse=True)
    best = candidates[0]
    requested_step = int(step) if step is not None else None
    resolved_step = entry_step(best)
    path = resolve_artifact(best.get("artifact_uri") or best.get("ref") or "", run_dir)
    if not path or not os.path.exists(path):
        return {"ok": False, "message": "artifact 文件不存在或不可解析", "uri": best.get("artifact_uri") or best.get("ref")}
    art = load_json(path)
    values = art.get("values")
    if not isinstance(values, list):
        return {"ok": False, "message": "artifact 中没有逐节点 values，不能渲染真实场", "uri": path}
    stats = art.get("statistics", {}) or best.get("statistics", {}) or {}
    node_count = art.get("node_count", len(values))
    mesh_ref = art.get("mesh_ref") or best.get("mesh_ref", "")
    layout_ref = art.get("layout_ref") or best.get("layout_ref", "")
    return {
        "ok": True,
        "artifact_id": best.get("artifact_id") or "%s/%s/%s" % (best.get("node_id", ""), best.get("port_id", ""), entry_step(best)),
        "kind": "field_tensor",
        "value_kind": best.get("value_kind", "field_tensor"),
        "field_name": art.get("field_name") or best.get("field_name"),
        "component_id": art.get("component_id") or best.get("component_id", ""),
        "contract_id": art.get("contract_id") or best.get("contract_id", ""),
        "unit": art.get("unit") or best.get("unit", ""),
        "node_count": node_count,
        "node_ids": art.get("node_ids"),
        "values": values,
        "min": stats.get("min"),
        "max": stats.get("max"),
        "mean": stats.get("mean"),
        "mesh_ref": mesh_ref,
        "layout_ref": layout_ref,
        "port_id": best.get("port_id"),
        "node_id": best.get("node_id"),
        "operator_id": best.get("operator_id", ""),
        "branch_id": best.get("branch_id"),
        "mainline_frame_index": best.get("mainline_frame_index"),
        "step": entry_step(best),
        "requested_step": requested_step,
        "resolved_step": resolved_step,
        "step_resolution": "exact" if requested_step is None or requested_step == resolved_step else "nearest_available",
        "artifact_path": path,
        "artifact_uri": best.get("artifact_uri") or best.get("ref") or "",
        "geometry_query": {
            "layout_ref": layout_ref,
            "mesh_ref": mesh_ref,
            "node_count": node_count,
        },
    }


def _state_from_frame(frame_entry):
    fr = frame_entry.get("frame", frame_entry)
    state = fr.get("selected_state") or fr.get("posterior_state") or frame_entry.get("selected_state") or {}
    return fr, state


def timeline(run_dir):
    idx = read_run_timeline(run_dir)
    reg = read_branch_registry(run_dir)
    progress = try_load_json(os.path.join(run_dir, "mainline_progress.json")) or {}
    events = try_load_json(os.path.join(run_dir, "runtime_events.json")) or idx.get("runtime_events", []) or []
    frames = []
    for i, item in enumerate(idx.get("online_frames", []) or []):
        fr, state = _state_from_frame(item)
        frames.append({
            "frame": fr.get("frame_index", item.get("frame_index", i)),
            "step": fr.get("loop_iteration_index", item.get("loop_iteration_index", i)),
            "t": fr.get("sample_time_s", fr.get("runtime_time_s", state.get("time_s"))),
            "h": state.get("h", state.get("altitude_m")),
            "ma": state.get("ma", state.get("mach")),
            "alpha": state.get("alpha"),
            "sensor_count": fr.get("sensor_count", item.get("sensor_count")),
            "freshness": fr.get("input_status", item.get("input_status", "")),
            "branch": item.get("branch_id", "main.online"),
        })
    return {
        "ok": True,
        "run": os.path.basename(run_dir),
        "workflow_id": idx.get("workflow_id", ""),
        "summary": idx.get("summary", {}),
        "progress": progress,
        "branches": reg.get("branches", []) or idx.get("branches", []),
        "online_frames": frames,
        "prediction_runs": idx.get("prediction_runs", []) or [],
        "branch_steps": idx.get("branch_steps", []) or [],
        "runtime_events": events,
    }


def runtime_info(run_dir):
    if not run_dir or not os.path.isdir(run_dir):
        return {
            "ok": True,
            "run": "",
            "status": "idle",
            "host": {},
            "process": {},
            "backend": "",
            "clock": {},
            "scheduler": {},
            "workers": [],
            "operator_initialization": summarize_initialization({}, ""),
            "initialization": {},
            "runtime_events": [],
            "events": [],
            "adapter_sessions": [],
            "command_records": read_command_records(""),
            "blocking": runtime_blocking_state("", {}, []),
        }
    evidence = try_load_json(os.path.join(run_dir, "runtime_host_evidence.json")) or {}
    progress = try_load_json(os.path.join(run_dir, "mainline_progress.json")) or {}
    initialization = try_load_json(os.path.join(run_dir, "operator_initialization.json")) or {}
    reg = read_branch_registry(run_dir)
    events = normalize_runtime_events(try_load_json(os.path.join(run_dir, "runtime_events.json")) or [])
    idx = read_run_timeline(run_dir)
    summary = evidence.get("summary", {}) or {}
    online_sessions = ((summary.get("online_native_session_summary") or {}).get("sessions") or [])
    branch_sessions = []
    for key, val in summary.items():
        if isinstance(val, dict) and key.endswith("_native_session_summary"):
            branch_sessions.extend(val.get("sessions", []) or [])
    sessions = online_sessions + [s for s in branch_sessions if s not in online_sessions]
    run_rel = rel_to_artifacts(run_dir) if run_dir else ""
    current_job = job_for_run(run_rel, evidence.get("run_id", idx.get("run_id", ""))) or job_snapshot("")
    init_summary = summarize_initialization(initialization, current_job.get("compiled_dir", ""))
    command_records = read_command_records(run_dir)
    clock = progress.get("clock", {}) if isinstance(progress.get("clock", {}), dict) else {}
    scheduler = {
        "stage": progress.get("stage", ""),
        "online": progress.get("online", {}),
        "prediction": progress.get("prediction", {}),
        "total_progress_percent": progress.get("total_progress_percent", ""),
    }
    process = {
        "job_id": current_job.get("job_id", ""),
        "pid": current_job.get("pid", ""),
        "running": current_job.get("running", False),
        "exit_code": current_job.get("exit_code", None),
        "elapsed_s": current_job.get("elapsed_s", ""),
        "script": os.path.basename(current_job.get("script", "")),
    }
    workers = []
    for branch in reg.get("branches", []) or idx.get("branches", []) or []:
        workers.append({
            "worker_id": branch.get("branch_id", ""),
            "kind": branch.get("branch_kind", branch.get("kind", "")),
            "status": branch.get("status", ""),
            "progress": first_non_empty(branch.get("progress_percent", ""), branch.get("progress", "")),
        })
    return {
        "ok": True,
        "run": os.path.basename(run_dir),
        "host": evidence.get("host", {}),
        "status": first_non_empty(current_job.get("status", ""), evidence.get("status", ""), progress.get("status", "")),
        "run_id": evidence.get("run_id", idx.get("run_id", "")),
        "workflow_id": first_non_empty(idx.get("workflow_id", ""), progress.get("online", {}).get("workflow_id", "")),
        "run_profile_id": first_non_empty(current_job.get("run_profile_id", ""), progress.get("run_profile_id", "")),
        "object_id": evidence.get("object_id", idx.get("object_id", "")),
        "backend": first_non_empty(current_job.get("backend", ""), evidence.get("host", {}).get("execution_backend", ""), progress.get("execution_backend", "")),
        "process": process,
        "clock": clock,
        "scheduler": scheduler,
        "workers": workers,
        "inputs": {
            **(evidence.get("inputs", {}) if isinstance(evidence.get("inputs", {}), dict) else {}),
            "external_observation_stream": current_job.get("external_observation_stream", ""),
        },
        "outputs": evidence.get("outputs", {}),
        "summary": summary,
        "progress": progress,
        "initialization": initialization,
        "operator_initialization": init_summary,
        "branches": reg.get("branches", []) or idx.get("branches", []),
        "runtime_events": events,
        "events": events,
        "adapter_sessions": sessions,
        "command_records": command_records,
        "blocking": runtime_blocking_state(run_dir, current_job, events),
    }


def _json_file_summary(path):
    exists = os.path.exists(path)
    size = os.path.getsize(path) if exists else 0
    doc = try_load_json(path) if exists else None
    count = ""
    if isinstance(doc, dict):
        for key in ("resources", "models", "operators", "nodes", "events", "artifact_refs", "qoi_refs", "branches"):
            value = doc.get(key)
            if isinstance(value, list):
                count = len(value)
                break
    elif isinstance(doc, list):
        count = len(doc)
    return {
        "name": os.path.basename(path),
        "path": path,
        "exists": exists,
        "size_bytes": size,
        "record_count": count,
    }


def _known_evidence_files(run_dir):
    names = [
        "runtime_host_evidence.json",
        "runtime_snapshot.json",
        "workflow_snapshot.json",
        "resource_lock.json",
        "model_snapshot.json",
        "operator_snapshot.json",
        "branch_registry.json",
        "run_timeline_index.json",
        "data_plane_manifest.json",
        "mainline_progress.json",
        "runtime_events.json",
        "operator_initialization.json",
        "runtime_command_records.json",
    ]
    rows = [_json_file_summary(os.path.join(run_dir, name)) for name in names]
    runtime = try_load_json(os.path.join(run_dir, "runtime_host_evidence.json")) or {}
    compiled_dirs = []
    inputs = runtime.get("inputs", {}) if isinstance(runtime.get("inputs", {}), dict) else {}
    for key in ("compiled_dir", "online_compiled_dir", "prediction_compiled_dir"):
        value = inputs.get(key) or runtime.get(key, "")
        if value and os.path.isdir(value):
            compiled_dirs.append(value)
    progress = try_load_json(os.path.join(run_dir, "mainline_progress.json")) or {}
    for key in ("compiled_dir", "online_compiled_dir", "prediction_compiled_dir"):
        value = progress.get(key, "")
        if value and os.path.isdir(value):
            compiled_dirs.append(value)
    for compiled_dir in sorted(set(compiled_dirs)):
        for name in ("workflow_snapshot.json", "resource_lock.json", "model_snapshot.json", "operator_snapshot.json", "compile_evidence.json"):
            path = os.path.join(compiled_dir, name)
            row = _json_file_summary(path)
            row["scope"] = "compiled"
            rows.append(row)
    return rows


def _load_first_json(run_dir, name):
    direct = try_load_json(os.path.join(run_dir, name))
    if direct:
        return direct
    for row in _known_evidence_files(run_dir):
        if row.get("name") == name and row.get("exists"):
            doc = try_load_json(row.get("path", ""))
            if doc:
                return doc
    return {}


def _artifact_trace(row, runtime, events):
    producer = row.get("producer") or row.get("operator_id") or row.get("node_id") or ""
    artifact_id = row.get("artifact_id") or row.get("qoi_id") or row.get("id") or row.get("port_id") or ""
    branch_id = row.get("branch_id", "")
    step = entry_step(row)
    related_events = []
    for event in events:
        if not isinstance(event, dict):
            continue
        if producer and producer in json.dumps(event, ensure_ascii=False):
            related_events.append(event)
            continue
        if branch_id and event.get("branch_id") == branch_id:
            related_events.append(event)
    return {
        "artifact_id": artifact_id,
        "port_id": row.get("port_id", ""),
        "branch_id": branch_id,
        "step": step,
        "producer_operator": producer,
        "workflow_node": row.get("node_id", producer),
        "resource_lock": "见 resource_lock snapshot",
        "runtime_event_count": len(related_events),
        "runtime_events": related_events[:12],
        "artifact_uri": row.get("artifact_uri") or row.get("uri") or row.get("ref") or "",
    }


def evidence_view(run_dir):
    if not run_dir or not os.path.isdir(run_dir):
        return {"ok": False, "message": "run 不存在或尚未选择", "run": run_dir}
    runtime = runtime_info(run_dir)
    timeline_doc = timeline(run_dir)
    dataplane = dataplane_fields(run_dir)
    files = _known_evidence_files(run_dir)
    events = runtime.get("runtime_events", []) or []
    fields = dataplane.get("fields", []) or []
    qois = dataplane.get("qois", []) or []
    trace_targets = []
    for row in fields[:80]:
        trace_targets.append(_artifact_trace(row, runtime, events))
    for row in qois[:80]:
        trace_targets.append(_artifact_trace(row, runtime, events))
    workflow_snapshot = _load_first_json(run_dir, "workflow_snapshot.json")
    resource_lock = _load_first_json(run_dir, "resource_lock.json")
    model_snapshot = _load_first_json(run_dir, "model_snapshot.json")
    operator_snapshot = _load_first_json(run_dir, "operator_snapshot.json")
    return {
        "ok": True,
        "run": rel_to_artifacts(run_dir),
        "run_dir": run_dir,
        "runtime": runtime,
        "timeline": {
            "workflow_id": timeline_doc.get("workflow_id", runtime.get("workflow_id", "")),
            "online_frame_count": len(timeline_doc.get("online_frames", []) or []),
            "branch_step_count": len(timeline_doc.get("branch_steps", []) or []),
            "event_count": len(events),
        },
        "snapshots": {
            "workflow": workflow_snapshot,
            "resource_lock": resource_lock,
            "model": model_snapshot,
            "operator": operator_snapshot,
        },
        "snapshot_files": files,
        "runtime_events": events,
        "adapter_sessions": runtime.get("adapter_sessions", []),
        "operator_initialization": runtime.get("operator_initialization", {}),
        "artifact_manifest": {
            "field_count": len(fields),
            "qoi_count": len(qois),
            "fields": fields,
            "qois": qois,
        },
        "trace_targets": trace_targets,
    }


def _diagnostic(category, severity, message, **extra):
    out = {"category": category, "severity": severity, "message": message}
    out.update(extra)
    return out


def diagnostics_view(run_dir):
    rows = []
    validation = validate_object_package_dir(OBJECT_PACKAGE) if OBJECT_PACKAGE else {"ok": False, "diagnostics": []}
    if not OBJECT_PACKAGE:
        rows.append(_diagnostic("object_package", "blocking", "尚未载入对象包"))
        rows.append(_diagnostic("resource", "blocking", "对象包未载入，资源、算子与 workflow 无法校验", target="object_package"))
    for item in validation.get("diagnostics", []) or []:
        rows.append(_diagnostic("object_package", item.get("severity", "info"), item.get("message", ""), code=item.get("code", ""), target=item.get("file", "")))

    resources = validate_resources() if OBJECT_PACKAGE else {"ok": False, "diagnostics": []}
    for item in resources.get("diagnostics", []) or []:
        rows.append(_diagnostic("resource", item.get("severity", "info"), item.get("message", ""), target=item.get("resource_id", "")))

    obj = object_summary() if OBJECT_PACKAGE else {}
    for op in obj.get("operators", []) or []:
        preflight = operator_preflight({"operator_id": op.get("operator_id", op.get("id", ""))})
        for item in preflight.get("diagnostics", []) or []:
            rows.append(_diagnostic("operator", item.get("severity", "info"), item.get("message", ""), target=op.get("operator_id", op.get("id", ""))))

    for workflow in obj.get("workflows", []) or []:
        wf_id = workflow.get("workflow_id", workflow.get("id", ""))
        raw = workflow_raw(wf_id)
        validation_doc = raw.get("validation", {})
        for message in validation_doc.get("errors", []) or []:
            rows.append(_diagnostic("workflow", "blocking", message, target=wf_id))
        for message in validation_doc.get("warnings", []) or []:
            rows.append(_diagnostic("workflow", "warning", message, target=wf_id))

    if run_dir and os.path.isdir(run_dir):
        runtime = runtime_info(run_dir)
        blocking = runtime.get("blocking", {}) or {}
        if blocking.get("stalled"):
            rows.append(_diagnostic("runtime", "warning", blocking.get("message", "运行可能停滞"), target=blocking.get("current_stage", "")))
        if not runtime.get("backend"):
            rows.append(_diagnostic("adapter", "warning", "runtime backend 未记录", target=runtime.get("run_id", "")))
        dataplane = dataplane_fields(run_dir)
        if not dataplane.get("field_count"):
            rows.append(_diagnostic("data_plane", "warning", "当前 run 没有 field artifact", target=rel_to_artifacts(run_dir)))
        for field in dataplane.get("fields", []) or []:
            if not field.get("mesh_ref") or not field.get("node_count"):
                rows.append(_diagnostic("renderer", "blocking", "field artifact 缺少 mesh_ref 或 node_count", target=field.get("artifact_id", "")))
    else:
        rows.append(_diagnostic("runtime", "warning", "尚未选择历史 run"))

    rows.sort(key=lambda row: (
        {"blocking": 0, "error": 1, "warning": 2}.get(row.get("severity"), 3),
        row.get("category", ""),
        row.get("target", ""),
    ))
    return {
        "ok": not any(row.get("severity") in ("blocking", "error") for row in rows),
        "diagnostics": rows,
        "summary": {
            "blocking": sum(1 for row in rows if row.get("severity") == "blocking"),
            "warning": sum(1 for row in rows if row.get("severity") == "warning"),
            "total": len(rows),
        },
    }


def export_evidence_bundle(run_dir):
    if not run_dir or not os.path.isdir(run_dir):
        return {"ok": False, "message": "run 不存在或尚未选择"}
    export_root = os.path.join(ARTIFACTS, "webui-evidence-exports")
    os.makedirs(export_root, exist_ok=True)
    run_name = _safe_id(os.path.basename(run_dir) or "run")
    stamp = _dt.datetime.utcnow().strftime("%Y%m%dT%H%M%SZ")
    zip_path = os.path.join(export_root, "%s_%s_evidence.zip" % (run_name, stamp))
    evidence = evidence_view(run_dir)
    manifest = {
        "schema_version": "flightenv.webui.evidence_bundle.v1",
        "generated_at_utc": stamp,
        "run": rel_to_artifacts(run_dir),
        "included_files": [],
        "summary": {
            "snapshot_file_count": len([f for f in evidence.get("snapshot_files", []) if f.get("exists")]),
            "field_count": evidence.get("artifact_manifest", {}).get("field_count", 0),
            "qoi_count": evidence.get("artifact_manifest", {}).get("qoi_count", 0),
            "runtime_event_count": len(evidence.get("runtime_events", []) or []),
        },
    }
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for row in evidence.get("snapshot_files", []):
            path = row.get("path", "")
            if not row.get("exists") or not path or not os.path.isfile(path):
                continue
            arcname = "evidence/%s" % os.path.basename(path)
            bundle.write(path, arcname)
            manifest["included_files"].append(arcname)
        bundle.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))
        slim = dict(evidence)
        slim["artifact_manifest"] = {
            "field_count": evidence.get("artifact_manifest", {}).get("field_count", 0),
            "qoi_count": evidence.get("artifact_manifest", {}).get("qoi_count", 0),
            "fields": evidence.get("artifact_manifest", {}).get("fields", [])[:200],
            "qois": evidence.get("artifact_manifest", {}).get("qois", [])[:200],
        }
        bundle.writestr("evidence_view.json", json.dumps(slim, ensure_ascii=False, indent=2))
    return {
        "ok": True,
        "bundle_path": zip_path,
        "bundle": rel_to_artifacts(zip_path),
        "manifest": manifest,
    }


# ---------------------------------------------------------------------------
# object package readers
# ---------------------------------------------------------------------------
def _group_lookup(asset_groups):
    out = {}
    for group in asset_groups:
        gid = group.get("group_id", "")
        for rid in group.get("resources", []) or []:
            out[rid] = gid
    return out


def _is_object_package(path):
    return bool(path and os.path.isdir(os.path.join(path, "object")))


def _empty_object_summary(message="尚未载入对象包"):
    return {
        "ok": True,
        "object_loaded": False,
        "message": message,
        "object_package_root": "",
        "package_dir": "",
        "object_id": "",
        "object_type": "",
        "schema_version": "",
        "display_name": "",
        "components": [],
        "resources": [],
        "asset_groups": [],
        "operators": [],
        "domain_schemas": [],
        "workflows": [],
        "run_profiles": [],
        "diagnostics": [{
            "severity": "info",
            "code": "object_package_not_loaded",
            "message": message,
        }],
    }


def validate_object_package_dir(package_dir):
    package_dir = os.path.abspath(package_dir or "")
    diagnostics = []
    if not package_dir:
        diagnostics.append({
            "severity": "blocking",
            "code": "object_package_missing",
            "message": "尚未选择对象包",
        })
        return {"ok": False, "package_dir": "", "diagnostics": diagnostics}
    if not os.path.isdir(package_dir):
        diagnostics.append({
            "severity": "blocking",
            "code": "object_package_dir_not_found",
            "message": "对象包目录不存在",
            "path": package_dir,
        })
        return {"ok": False, "package_dir": package_dir, "diagnostics": diagnostics}

    required = [
        ("object/twin_object.json", "object_manifest_missing", "缺少 object/twin_object.json"),
        ("assets/resources.json", "resources_manifest_missing", "缺少 assets/resources.json"),
        ("operators", "operators_dir_missing", "缺少 operators 目录"),
        ("workflows", "workflows_dir_missing", "缺少 workflows 目录"),
    ]
    for rel, code, message in required:
        path = os.path.join(package_dir, *rel.split("/"))
        exists = os.path.isdir(path) if "." not in os.path.basename(rel) else os.path.isfile(path)
        if not exists:
            diagnostics.append({
                "severity": "blocking",
                "code": code,
                "message": message,
                "path": path,
            })

    twin = try_load_json(os.path.join(package_dir, "object", "twin_object.json")) or {}
    if not twin.get("object_id"):
        diagnostics.append({
            "severity": "blocking",
            "code": "object_id_missing",
            "message": "object/twin_object.json 缺少 object_id",
        })

    res_doc = try_load_json(os.path.join(package_dir, "assets", "resources.json")) or {}
    resource_ids = set()
    for res in twin.get("resources", []) or []:
        rid = res.get("resource_id") or res.get("id")
        if rid:
            resource_ids.add(rid)
    for group in res_doc.get("asset_groups", []) or []:
        for rid in group.get("resources", []) or []:
            if rid not in resource_ids:
                diagnostics.append({
                    "severity": "warning",
                    "code": "asset_group_unknown_resource",
                    "message": "asset_group 引用了 twin_object 未声明的 resource",
                    "resource_id": rid,
                    "group_id": group.get("group_id", ""),
                })

    schema_dir = os.path.join(package_dir, "domain_schemas")
    if os.path.isdir(schema_dir):
        for name in sorted(os.listdir(schema_dir)):
            if not name.endswith(".json"):
                continue
            schema_path = os.path.join(schema_dir, name)
            schema_doc = try_load_json(schema_path) or {}
            for item in validate_domain_schema_doc(schema_doc).get("diagnostics", []):
                diagnostics.append({
                    **item,
                    "path": schema_path,
                    "rel_path": _rel_to_package(schema_path, package_dir),
                })

    blocking = any(item.get("severity") == "blocking" for item in diagnostics)
    return {
        "ok": not blocking,
        "package_dir": package_dir,
        "object_id": twin.get("object_id", ""),
        "object_type": twin.get("object_type", ""),
        "display_name": twin.get("display_name", twin.get("name", "")),
        "diagnostics": diagnostics,
    }


def validate_domain_schema_doc(doc):
    doc = doc if isinstance(doc, dict) else {}
    diagnostics = []
    schema_id = str(doc.get("schema_id") or doc.get("id") or "").strip()
    value_kind = str(doc.get("value_kind") or "").strip().lower()
    if not schema_id:
        diagnostics.append({"severity": "blocking", "code": "schema_id_missing", "message": "domain schema 缺少 schema_id"})
    if not str(doc.get("component_id") or "").strip():
        diagnostics.append({"severity": "blocking", "code": "schema_component_missing", "message": "domain schema 缺少 component_id", "schema_id": schema_id})
    if not value_kind:
        diagnostics.append({"severity": "blocking", "code": "schema_value_kind_missing", "message": "domain schema 缺少 value_kind", "schema_id": schema_id})
    if not str(doc.get("artifact_policy") or "").strip():
        diagnostics.append({"severity": "blocking", "code": "schema_artifact_policy_missing", "message": "domain schema 缺少 artifact_policy", "schema_id": schema_id})
    schema_role = str(doc.get("schema_role") or "").strip().lower()
    layout = first_nonempty(doc.get("layout_ref"), doc.get("mesh_ref"), doc.get("mesh_resource_id"), doc.get("shape_ref"))
    if (value_kind == "field" or schema_role == "field_state") and not layout:
        diagnostics.append({"severity": "blocking", "code": "schema_layout_missing", "message": "field schema 必须声明 layout_ref、mesh_ref 或 shape_ref", "schema_id": schema_id})
    return {"ok": not any(item.get("severity") == "blocking" for item in diagnostics), "diagnostics": diagnostics}


def first_nonempty(*values):
    for value in values:
        if value not in (None, ""):
            return value
    return ""


ALLOWED_OPERATOR_FAMILIES = {
    "state_transition",
    "observation_equation",
    "filter_algorithm",
    "qoi",
    "data_source",
    "materialization",
    "diagnostic",
}

PLATFORM_BUILTIN_CONTRACT_IDS = {
    "platform.state_snapshot.v1",
    "platform.observation_snapshot.v1",
    "platform.event_snapshot.v1",
}


def domain_schema_ids(package_dir):
    ids = set()
    schema_dir = os.path.join(package_dir or "", "domain_schemas")
    if not os.path.isdir(schema_dir):
        return ids
    for name in os.listdir(schema_dir):
        if not name.endswith(".json"):
            continue
        doc = try_load_json(os.path.join(schema_dir, name)) or {}
        sid = str(doc.get("schema_id") or doc.get("id") or "").strip()
        if sid:
            ids.add(sid)
    return ids


def workflow_docs_by_id(package_dir):
    rows = {}
    wf_dir = os.path.join(package_dir or "", "workflows")
    if not os.path.isdir(wf_dir):
        return rows
    for name in os.listdir(wf_dir):
        if not name.endswith(".json"):
            continue
        path = os.path.join(wf_dir, name)
        doc = try_load_json(path) or {}
        workflow_id = str(doc.get("workflow_id") or os.path.splitext(name)[0]).strip()
        if workflow_id:
            rows[workflow_id] = {"path": path, "doc": doc}
    return rows


def _collect_string_values(value):
    if isinstance(value, str):
        text = value.strip()
        return [text] if text else []
    if isinstance(value, (list, tuple, set)):
        out = []
        for item in value:
            out.extend(_collect_string_values(item))
        return out
    return []


def _collect_refs_by_key(value, key_predicate):
    refs = []
    if isinstance(value, dict):
        for key, child in value.items():
            if key_predicate(str(key)):
                refs.extend(_collect_string_values(child))
            refs.extend(_collect_refs_by_key(child, key_predicate))
    elif isinstance(value, list):
        for item in value:
            refs.extend(_collect_refs_by_key(item, key_predicate))
    return refs


def _runtime_profile_docs(package_dir):
    runtime_dir = os.path.join(package_dir or "", "runtime")
    if not os.path.isdir(runtime_dir):
        return []
    docs = []
    for name in sorted(os.listdir(runtime_dir)):
        if not name.endswith(".json"):
            continue
        path = os.path.join(runtime_dir, name)
        doc = try_load_json(path) or {}
        docs.append({"path": path, "rel_path": _rel_to_package(path, package_dir), "doc": doc})
    return docs


def normalize_run_profile_doc(doc, profile_id=""):
    next_doc = dict(doc or {})
    pid = str(profile_id or next_doc.get("profile_id") or "").strip()
    if pid:
        next_doc["profile_id"] = pid
    if not next_doc.get("schema_version") or next_doc.get("schema_version") == "flightenv.run_profile.v1":
        next_doc["schema_version"] = "flightenv.platform.run_profile.v1"
    if "features" not in next_doc or not isinstance(next_doc.get("features"), dict):
        next_doc["features"] = {}
    if "default_feature_enabled" not in next_doc:
        next_doc["default_feature_enabled"] = True
    return next_doc


def validate_run_profile_doc(doc, package_dir=None):
    diagnostics = []

    def issue(severity, code, message, **extra):
        row = {"severity": severity, "code": code, "message": message}
        row.update({k: v for k, v in extra.items() if v not in (None, "")})
        diagnostics.append(row)

    doc = doc if isinstance(doc, dict) else {}
    profile_id = str(doc.get("profile_id") or "").strip()
    if not profile_id:
        issue("blocking", "run_profile_id_missing", "run profile 缺少 profile_id")
    if str(doc.get("schema_version") or "") != "flightenv.platform.run_profile.v1":
        issue("blocking", "run_profile_schema_version_invalid", "run profile schema_version 必须是 flightenv.platform.run_profile.v1", schema_version=doc.get("schema_version", ""))
    if not isinstance(doc.get("features", {}), dict):
        issue("blocking", "run_profile_features_not_object", "run profile features 必须是对象")

    workflows = workflow_docs_by_id(package_dir)
    workflow_ids = []
    workflow_ids.extend(_collect_string_values(doc.get("workflow_id")))
    workflow_ids.extend(_collect_string_values(doc.get("workflow_ids")))
    branch = doc.get("branching_policy") if isinstance(doc.get("branching_policy"), dict) else {}
    workflow_ids.extend(_collect_string_values(branch.get("target_workflow_id")))
    for workflow_id in sorted(set(workflow_ids)):
        if workflow_id and workflow_id not in workflows:
            issue("blocking", "run_profile_workflow_unknown", "run profile 引用了不存在的 workflow", workflow_id=workflow_id, profile_id=profile_id)

    for key in ("runtime_launch", "branching_policy", "termination_policy", "metadata"):
        if key in doc and doc[key] is not None and not isinstance(doc[key], dict):
            issue("blocking", "run_profile_%s_not_object" % key, "run profile %s 必须是对象" % key, profile_id=profile_id)

    blocking = any(item.get("severity") == "blocking" for item in diagnostics)
    return {"ok": not blocking, "diagnostics": diagnostics}


def _workflow_display_contracts(workflow):
    contracts = set()
    if not isinstance(workflow, dict):
        return contracts
    for item in workflow.get("display_outputs", []) or []:
        if not isinstance(item, dict):
            continue
        contract_id = str(item.get("contract_id") or item.get("schema_id") or "").strip()
        if contract_id:
            contracts.add(contract_id)
    return contracts


def validate_runtime_profile_doc(doc, package_dir=None):
    diagnostics = []

    def issue(severity, code, message, **extra):
        row = {"severity": severity, "code": code, "message": message}
        row.update({k: v for k, v in extra.items() if v not in (None, "")})
        diagnostics.append(row)

    doc = doc if isinstance(doc, dict) else {}
    workflows = workflow_docs_by_id(package_dir)
    workflow_ids = set(workflows.keys())
    run_profile_ids = {str(item.get("profile_id") or "").strip() for item in _load_run_profiles(package_dir)}
    run_profile_ids.discard("")
    contract_ids = domain_schema_ids(package_dir) | PLATFORM_BUILTIN_CONTRACT_IDS

    for workflow_id in sorted(set(_collect_refs_by_key(doc, lambda key: key.endswith("workflow_id") or key == "workflow_ids"))):
        if workflow_id and workflow_id not in workflow_ids:
            issue("blocking", "runtime_profile_workflow_unknown", "runtime profile 引用了不存在的 workflow", workflow_id=workflow_id)

    for profile_id in sorted(set(_collect_refs_by_key(doc, lambda key: "run_profile" in key))):
        if profile_id and profile_id not in run_profile_ids:
            issue("blocking", "runtime_profile_run_profile_unknown", "runtime profile 引用了不存在的 run profile", run_profile_id=profile_id)

    roles = doc.get("field_display_roles", [])
    if roles is None:
        roles = []
    if not isinstance(roles, list):
        issue("blocking", "runtime_profile_field_display_roles_not_array", "field_display_roles 必须是数组")
        roles = []
    for index, role in enumerate(roles):
        if not isinstance(role, dict):
            issue("blocking", "runtime_profile_field_display_role_not_object", "field_display_roles[%d] 必须是对象" % index)
            continue
        contract_id = _id(role, "contract_id", "schema_id", "field_contract_id", "output_contract_id")
        workflow_id = str(role.get("workflow_id") or role.get("source_workflow_id") or "").strip()
        if not contract_id:
            issue("blocking", "runtime_profile_field_display_contract_missing", "field_display_roles[%d] 缺少 contract_id/schema_id" % index)
        elif contract_id not in contract_ids:
            issue("blocking", "runtime_profile_field_display_contract_unknown", "field_display_roles 引用了不存在的 contract/schema", contract_id=contract_id)
        if workflow_id:
            wf = workflows.get(workflow_id, {}).get("doc", {})
            display_contracts = _workflow_display_contracts(wf)
            if display_contracts and contract_id and contract_id not in display_contracts:
                issue(
                    "warning",
                    "runtime_profile_field_display_not_in_workflow_outputs",
                    "field_display_roles 与 workflow display_outputs 不一致，请确认是否为跨 workflow 显示项",
                    workflow_id=workflow_id,
                    contract_id=contract_id,
                )

    for key in ("branch_templates", "health_ledger", "termination_policy"):
        if key in doc and doc[key] is not None and not isinstance(doc[key], (dict, list)):
            issue("blocking", "runtime_profile_%s_invalid" % key, "%s 必须是对象或数组" % key)

    blocking = any(item.get("severity") == "blocking" for item in diagnostics)
    return {"ok": not blocking, "diagnostics": diagnostics}


def runtime_profile_references_run_profile(package_dir, profile_id):
    profile_id = str(profile_id or "").strip()
    if not profile_id:
        return []
    refs = []
    for item in _runtime_profile_docs(package_dir):
        doc = item.get("doc") or {}
        found = set(_collect_refs_by_key(doc, lambda key: "run_profile" in key))
        if profile_id in found:
            refs.append({"rel_path": item.get("rel_path", ""), "path": item.get("path", "")})
    return refs


def _operator_port_diagnostics(port, direction, index, contract_ids):
    diagnostics = []
    port = port if isinstance(port, dict) else {}
    label = "%s[%s]" % (direction, index)
    port_id = str(port.get("port_id") or "").strip()
    frame_contract = str(port.get("frame_contract") or "").strip()
    contract_id = str(port.get("contract_id") or port.get("schema_id") or "").strip()
    value_kind = str(port.get("value_kind") or "").strip()
    if not port_id:
        diagnostics.append({"severity": "blocking", "code": "operator_port_id_missing", "message": "operator 端口缺少 port_id", "port": label})
    if not frame_contract:
        diagnostics.append({"severity": "blocking", "code": "operator_port_frame_contract_missing", "message": "operator 端口缺少 frame_contract", "port_id": port_id, "port": label})
    if not contract_id:
        diagnostics.append({"severity": "blocking", "code": "operator_port_contract_id_missing", "message": "operator 端口缺少 contract_id", "port_id": port_id, "port": label})
    elif contract_id not in contract_ids and contract_id not in PLATFORM_BUILTIN_CONTRACT_IDS:
        diagnostics.append({"severity": "blocking", "code": "operator_port_unknown_contract", "message": "operator 端口引用了不存在的 contract_id", "port_id": port_id, "contract_id": contract_id})
    if not value_kind:
        diagnostics.append({"severity": "blocking", "code": "operator_port_value_kind_missing", "message": "operator 端口缺少 value_kind", "port_id": port_id, "port": label})

    typed = port.get("typed_io_contract")
    if isinstance(typed, dict) and typed:
        for key in ("dto_name", "type_name", "codegen_ref"):
            if not str(typed.get(key) or "").strip():
                diagnostics.append({"severity": "blocking", "code": "operator_port_typed_missing_" + key, "message": "启用 typed IO 的端口缺少 %s" % key, "port_id": port_id})
        if typed.get("json_operator_io_forbidden") is not True:
            diagnostics.append({"severity": "blocking", "code": "operator_port_json_io_not_forbidden", "message": "启用 typed IO 的端口必须声明 json_operator_io_forbidden=true", "port_id": port_id})
    return diagnostics


def validate_operator_doc(doc, package_dir=None, current_rel_path=""):
    doc = doc if isinstance(doc, dict) else {}
    package_dir = package_dir or active_object_package()
    diagnostics = []
    operator_id = str(doc.get("operator_id") or doc.get("id") or "").strip()
    if not operator_id:
        diagnostics.append({"severity": "blocking", "code": "operator_id_missing", "message": "operator 缺少 operator_id"})

    family = str(doc.get("operator_family") or doc.get("family") or "").strip()
    if not family:
        diagnostics.append({"severity": "blocking", "code": "operator_family_missing", "message": "operator 缺少 operator_family"})
    elif family not in ALLOWED_OPERATOR_FAMILIES:
        diagnostics.append({"severity": "blocking", "code": "operator_family_not_allowed", "message": "operator_family 不在允许枚举内", "operator_family": family, "allowed": sorted(ALLOWED_OPERATOR_FAMILIES)})

    execution = doc.get("execution") if isinstance(doc.get("execution"), dict) else {}
    if not str(execution.get("kind") or doc.get("execution_kind") or "").strip():
        diagnostics.append({"severity": "blocking", "code": "operator_execution_kind_missing", "message": "operator 缺少 execution.kind"})
    if not str(execution.get("adapter_id") or "").strip():
        diagnostics.append({"severity": "blocking", "code": "operator_adapter_id_missing", "message": "operator 缺少 execution.adapter_id"})

    if package_dir and operator_id:
        path, _existing = find_operator_file(package_dir, operator_id)
        rel = _rel_to_package(path, package_dir) if path else ""
        own_rel = str(current_rel_path or "").replace("\\", "/")
        if path and (not own_rel or rel != own_rel):
            diagnostics.append({"severity": "blocking", "code": "operator_id_not_unique", "message": "operator_id 已存在，不能从模板重复新建", "operator_id": operator_id, "rel_path": rel})

    contract_ids = domain_schema_ids(package_dir)
    inputs = doc.get("inputs") if isinstance(doc.get("inputs"), list) else []
    outputs = doc.get("outputs") if isinstance(doc.get("outputs"), list) else []
    for index, port in enumerate(inputs):
        diagnostics.extend(_operator_port_diagnostics(port, "inputs", index, contract_ids))
    for index, port in enumerate(outputs):
        diagnostics.extend(_operator_port_diagnostics(port, "outputs", index, contract_ids))

    typed = doc.get("typed_io_contract")
    if isinstance(typed, dict) and typed:
        for key in ("input_dto", "output_dto", "run_fn_type", "codegen_ref"):
            if not str(typed.get(key) or "").strip():
                diagnostics.append({"severity": "blocking", "code": "operator_typed_missing_" + key, "message": "启用 typed IO 的 operator 缺少 %s" % key})
        if typed.get("json_operator_io_forbidden") is not True:
            diagnostics.append({"severity": "blocking", "code": "operator_json_io_not_forbidden", "message": "启用 typed IO 的 operator 必须声明 json_operator_io_forbidden=true"})

    resource_ids = {_id(res, "resource_id", "id") for res in (try_load_json(os.path.join(package_dir or "", "object", "twin_object.json")) or {}).get("resources", []) or []}
    for rid in _collect_resource_refs(doc):
        if rid and rid not in resource_ids:
            diagnostics.append({"severity": "blocking", "code": "operator_unknown_resource", "message": "operator 引用了对象包未声明的 resource", "resource_id": rid})

    release_gate = {"ok": not any(item.get("severity") == "blocking" for item in diagnostics), "diagnostics": diagnostics}
    return release_gate


def settings_view():
    return {
        "ok": True,
        "settings": [
            {
                "key": "workspace.root",
                "label": "工作空间根目录",
                "value": ROOT,
                "source": "workspace",
                "editable": False,
                "requires_recompile": False,
                "requires_reinitialize": False,
            },
            {
                "key": "workspace.artifacts_root",
                "label": "本地运行产物根目录",
                "value": ARTIFACTS,
                "source": "workspace",
                "editable": False,
                "requires_recompile": False,
                "requires_reinitialize": False,
            },
            {
                "key": "object.package_root",
                "label": "当前对象包",
                "value": OBJECT_PACKAGE,
                "source": "object_package" if OBJECT_PACKAGE else "workspace",
                "editable": False,
                "requires_recompile": True,
                "requires_reinitialize": True,
            },
            {
                "key": "ui.session.default_page",
                "label": "默认页面",
                "value": "workspace",
                "source": "ui_session",
                "editable": True,
                "requires_recompile": False,
                "requires_reinitialize": False,
            },
            {
                "key": "ui.session.poll_ms",
                "label": "实时刷新间隔",
                "value": 1800,
                "source": "ui_session",
                "editable": True,
                "requires_recompile": False,
                "requires_reinitialize": False,
            },
        ],
        "source_precedence": [
            "PDK default",
            "object package",
            "workflow",
            "run profile",
            "runtime launch",
            "UI session override",
        ],
    }


def workspace_view():
    validation = validate_object_package_dir(OBJECT_PACKAGE) if OBJECT_PACKAGE else {
        "ok": False,
        "diagnostics": [{
            "severity": "info",
            "code": "object_package_not_loaded",
            "message": "尚未载入对象包",
        }],
    }
    packages = list_object_packages()
    return {
        "ok": True,
        "api_version": "flightenv.ui.api.v1",
        "workspace_root": ROOT,
        "artifacts_root": ARTIFACTS,
        "draft_root": DRAFT_ROOT,
        "object_loaded": bool(OBJECT_PACKAGE and validation.get("ok")),
        "object_package_root": OBJECT_PACKAGE,
        "object_validation": validation,
        "package_count": len(packages.get("packages", [])),
        "settings": settings_view().get("settings", []),
    }


def list_object_packages():
    rows = []
    seen = set()
    candidates = []
    if OBJECT_PACKAGE:
        candidates.append(OBJECT_PACKAGE)
    for name in sorted(os.listdir(ROOT)):
        d = os.path.join(ROOT, name)
        if name.startswith("flightenv-object-"):
            candidates.append(d)
    for d in candidates:
        path = os.path.abspath(d)
        if path in seen or not _is_object_package(path):
            continue
        seen.add(path)
        twin = try_load_json(os.path.join(path, "object", "twin_object.json")) or {}
        rows.append({
            "package_dir": path,
            "object_id": twin.get("object_id", os.path.basename(path)),
            "object_type": twin.get("object_type", ""),
            "display_name": twin.get("display_name", twin.get("name", os.path.basename(path))),
            "active": os.path.abspath(path) == os.path.abspath(OBJECT_PACKAGE),
        })
    rows.sort(key=lambda row: (not row["active"], row["object_id"]))
    return {"ok": True, "packages": rows, "active": OBJECT_PACKAGE}


def select_object_package(payload):
    global OBJECT_PACKAGE, ACTIVE_OBJECT_DRAFT_ID
    package_dir = os.path.abspath((payload or {}).get("package_dir") or "")
    validation = validate_object_package_dir(package_dir)
    if not validation.get("ok"):
        return {
            "ok": False,
            "message": "对象包校验失败，不能载入",
            "validation": validation,
        }
    OBJECT_PACKAGE = package_dir
    ACTIVE_OBJECT_DRAFT_ID = ""
    return {
        "ok": True,
        "object_package": OBJECT_PACKAGE,
        "validation": validation,
        "object": object_summary(),
    }


def unload_object_package():
    global OBJECT_PACKAGE, ACTIVE_OBJECT_DRAFT_ID
    OBJECT_PACKAGE = ""
    ACTIVE_OBJECT_DRAFT_ID = ""
    return {"ok": True, "message": "对象包已卸载", "workspace": workspace_view(), "object": object_summary()}


def validate_active_object_package(payload=None):
    package_dir = ((payload or {}).get("package_dir") or OBJECT_PACKAGE or "")
    validation = validate_object_package_dir(package_dir)
    return {
        "ok": validation.get("ok", False),
        "object_loaded": bool(OBJECT_PACKAGE and os.path.abspath(package_dir) == os.path.abspath(OBJECT_PACKAGE) and validation.get("ok")),
        "validation": validation,
    }


def _id(value, *keys):
    if not isinstance(value, dict):
        return ""
    for key in keys:
        text = value.get(key)
        if text:
            return str(text)
    return ""


def _strings(value):
    if isinstance(value, str):
        return [value]
    if isinstance(value, list):
        out = []
        for item in value:
            out.extend(_strings(item))
        return out
    if isinstance(value, dict):
        out = []
        for item in value.values():
            out.extend(_strings(item))
        return out
    return []


def _collect_named_refs(value, key_predicate):
    refs = []
    if isinstance(value, dict):
        for key, item in value.items():
            if key_predicate(str(key)):
                refs.extend(_strings(item))
            refs.extend(_collect_named_refs(item, key_predicate))
    elif isinstance(value, list):
        for item in value:
            refs.extend(_collect_named_refs(item, key_predicate))
    return list(dict.fromkeys(refs))


def _collect_resource_refs(doc):
    keys = {
        "resource_ref", "resource_refs", "required_resource", "required_resources",
        "source_database_ref", "noise_model_ref", "calibration_model_ref",
    }

    def pred(key):
        return key in keys or key.endswith("_resource_ref") or key.endswith("_resource_refs")

    return _collect_named_refs(doc, pred)


def _collect_component_resource_ids(component):
    refs = []
    for key, value in (component or {}).items():
        if str(key).endswith("_resource_ids") or str(key) in ("resource_ids", "resources"):
            refs.extend(_strings(value))
    return list(dict.fromkeys(refs))


def _collect_component_schema_ids(component):
    refs = []
    for key, value in (component or {}).items():
        if str(key).endswith("_schema_ids"):
            refs.extend(_strings(value))
    return list(dict.fromkeys(refs))


def _workflow_operator_refs(workflow):
    refs = []
    for phase in workflow.get("phases", []) or []:
        for stage in (phase or {}).get("stages", []) or []:
            refs.extend(_strings(stage.get("operator_refs", [])))
            sub = stage.get("subgraph", {}) or {}
            for node in sub.get("nodes", []) or []:
                ref = node.get("operator_ref")
                if ref:
                    refs.append(ref)
    return list(dict.fromkeys(refs))


def _renderer_resolution(display_descriptor):
    known = {
        "field.vtk.scalar.v1",
        "coefficient_series.v1",
        "sensor.projection.v1",
        "filter.particle_summary.v1",
        "health.accumulation.v1",
        "qoi.decision.v1",
        "generic_operator_ports.v1",
    }
    desc = display_descriptor if isinstance(display_descriptor, dict) else {}
    requested = desc.get("renderer_id", "")
    fallback = desc.get("fallback_renderer", "generic_operator_ports.v1")
    if requested in known:
        return {
            "requested_renderer": requested,
            "resolved_renderer": requested,
            "resolution": "exact",
            "diagnostics": [],
        }
    if requested and fallback in known:
        return {
            "requested_renderer": requested,
            "resolved_renderer": fallback,
            "resolution": "fallback",
            "diagnostics": [{"severity": "warning", "message": "renderer_id 未注册，已使用 fallback_renderer"}],
        }
    return {
        "requested_renderer": requested,
        "resolved_renderer": "generic_operator_ports.v1",
        "resolution": "generic",
        "diagnostics": [{"severity": "warning", "message": "缺少可解析 display_descriptor，使用通用端口展示"}],
    }


def _resolve_resource_path(path):
    if not path:
        return ""
    text = str(path)
    if re.match(r"^[a-zA-Z]+://", text):
        return ""
    return text if os.path.isabs(text) else os.path.abspath(os.path.join(ROOT, text))


def _resource_path_checks(resource):
    checks = []
    for key in (
            "path", "file", "root", "model_package_root", "pod_text_dump_dir",
            "pred_train_model_dir", "pod_basis_dir", "dataset_dir", "mesh_path"):
        value = resource.get(key)
        if not value:
            continue
        resolved = _resolve_resource_path(value)
        if not resolved:
            checks.append({"key": key, "value": value, "status": "external_ref"})
        else:
            checks.append({
                "key": key,
                "value": value,
                "resolved_path": resolved,
                "exists": os.path.exists(resolved),
                "status": "ok" if os.path.exists(resolved) else "missing",
            })
    uri = resource.get("uri", "")
    if uri and re.match(r"^[a-zA-Z]+://", str(uri)):
        checks.append({"key": "uri", "value": uri, "status": "external_ref"})
    return checks


def _resource_validation_for(resources, operators, workflows):
    declared = {_id(res, "resource_id", "id") for res in resources}
    op_by_resource = {}
    wf_by_resource = {}
    op_refs = {}
    for op in operators:
        oid = _id(op, "operator_id", "id")
        refs = _collect_resource_refs(op)
        op_refs[oid] = refs
        for rid in refs:
            op_by_resource.setdefault(rid, []).append(oid)
    wf_ops = {wf.get("workflow_id", ""): wf.get("operator_refs", []) for wf in workflows}
    for wf_id, refs in wf_ops.items():
        for op_id in refs:
            for rid in op_refs.get(op_id, []):
                wf_by_resource.setdefault(rid, []).append(wf_id)

    rows = []
    diagnostics = []
    for res in resources:
        rid = _id(res, "resource_id", "id")
        path_checks = _resource_path_checks(res)
        missing_paths = [item for item in path_checks if item.get("status") == "missing"]
        row = {
            "resource_id": rid,
            "resource_type": res.get("resource_type", res.get("type", "")),
            "component_id": res.get("component_id", ""),
            "uri": res.get("uri", res.get("path", "")),
            "checksum": res.get("checksum", ""),
            "path_checks": path_checks,
            "used_by_operators": sorted(set(op_by_resource.get(rid, []))),
            "used_by_workflows": sorted(set(wf_by_resource.get(rid, []))),
            "status": "missing" if missing_paths else "ok",
        }
        rows.append(row)
        for item in missing_paths:
            diagnostics.append({
                "severity": "blocking",
                "code": "resource_path_missing",
                "resource_id": rid,
                "operator_refs": row["used_by_operators"],
                "workflow_refs": row["used_by_workflows"],
                "message": "资源路径不存在",
                "path": item.get("resolved_path", ""),
            })

    for op_id, refs in op_refs.items():
        for rid in refs:
            if rid not in declared:
                diagnostics.append({
                    "severity": "blocking",
                    "code": "operator_unknown_resource",
                    "resource_id": rid,
                    "operator_refs": [op_id],
                    "workflow_refs": sorted(set(wf_by_resource.get(rid, []))),
                    "message": "算子引用了对象包未声明的资源",
                })
    return {"ok": not any(d.get("severity") == "blocking" for d in diagnostics), "resources": rows, "diagnostics": diagnostics}


def _load_object_payload(package_dir=None):
    package_dir = package_dir or active_object_package()
    if not package_dir:
        return None
    twin = try_load_json(os.path.join(package_dir, "object", "twin_object.json")) or {}
    res_doc = try_load_json(os.path.join(package_dir, "assets", "resources.json")) or {}
    groups = res_doc.get("asset_groups", []) or []
    group_by_resource = _group_lookup(groups)
    resources = []
    for r in twin.get("resources", []) or []:
        rr = dict(r)
        rr["group_id"] = group_by_resource.get(rr.get("resource_id"), "")
        resources.append(rr)

    raw_operators = []
    operators = []
    op_dir = os.path.join(package_dir, "operators")
    if os.path.isdir(op_dir):
        for fn in sorted(os.listdir(op_dir)):
            if not fn.endswith(".atomic.json"):
                continue
            op = try_load_json(os.path.join(op_dir, fn)) or {}
            op["spec_file"] = fn
            raw_operators.append(op)

    raw_workflows = []
    workflows = []
    wf_dir = os.path.join(package_dir, "workflows")
    if os.path.isdir(wf_dir):
        for fn in sorted(os.listdir(wf_dir)):
            if not fn.endswith(".json"):
                continue
            wf = try_load_json(os.path.join(wf_dir, fn)) or {}
            wf["spec_file"] = fn
            raw_workflows.append(wf)

    workflow_operator_map = {}
    for wf in raw_workflows:
        workflow_operator_map[wf.get("workflow_id", wf.get("spec_file", ""))] = _workflow_operator_refs(wf)

    used_by_workflows = {}
    for wf_id, refs in workflow_operator_map.items():
        for op_id in refs:
            used_by_workflows.setdefault(op_id, []).append(wf_id)

    for op in raw_operators:
        ex = op.get("execution", {}) if isinstance(op.get("execution"), dict) else {}
        disp = op.get("display_descriptor", {}) if isinstance(op.get("display_descriptor"), dict) else {}
        oid = op.get("operator_id", op.get("spec_file", ""))
        operators.append({
            "operator_id": oid,
            "display_name": op.get("display_name", oid),
            "operator_type": op.get("operator_kind", op.get("operator_type", "")),
            "family": op.get("operator_family"),
            "kind": ex.get("kind") or op.get("execution_kind"),
            "backend": ex.get("kind") or op.get("execution_kind"),
            "adapter_id": ex.get("adapter_id"),
            "lifecycle": ex.get("lifecycle", []),
            "phases": op.get("phases", []),
            "inputs": op.get("inputs", []),
            "outputs": op.get("outputs", []),
            "time_policy": op.get("time_policy", {}),
            "state_policy": op.get("state_policy", {}),
            "integration_policy": op.get("integration_policy", {}),
            "scheduler_policy": op.get("scheduler_policy", {}),
            "renderer_id": disp.get("renderer_id"),
            "display_descriptor": disp,
            "renderer_resolution": _renderer_resolution(disp),
            "resource_refs": _collect_resource_refs(op),
            "spec_file": op.get("spec_file"),
            "used_by_workflows": sorted(set(used_by_workflows.get(oid, []))),
        })

    for wf in raw_workflows:
        phases = wf.get("phases", []) or []
        stages = []
        for phase in phases:
            stages.extend(((phase or {}).get("stages", []) or []))
        workflows.append({
            "workflow_id": wf.get("workflow_id", wf.get("spec_file", "")),
            "phase": wf.get("phase", ""),
            "description": wf.get("description", ""),
            "spec_file": wf.get("spec_file", ""),
            "phase_count": len(phases),
            "stage_count": len(stages),
            "node_count": sum(len(((stage or {}).get("subgraph", {}) or {}).get("nodes", []) or []) for stage in stages),
            "operator_refs": workflow_operator_map.get(wf.get("workflow_id", ""), []),
        })

    validation = _resource_validation_for(resources, raw_operators, workflows)
    resource_validation_by_id = {row["resource_id"]: row for row in validation.get("resources", [])}
    operator_by_id = {op["operator_id"]: op for op in operators}
    resource_by_id = {_id(res, "resource_id", "id"): res for res in resources}
    op_refs_by_id = {op["operator_id"]: set(op.get("resource_refs", [])) for op in operators}

    for res in resources:
        rid = _id(res, "resource_id", "id")
        row = resource_validation_by_id.get(rid, {})
        res["checksum"] = res.get("checksum", "")
        res["path_checks"] = row.get("path_checks", [])
        res["resource_status"] = row.get("status", "ok")
        res["used_by_operators"] = row.get("used_by_operators", [])
        res["used_by_workflows"] = row.get("used_by_workflows", [])

    component_views = []
    for comp in twin.get("components", []) or []:
        cid = _id(comp, "component_id", "id")
        schema_ids = set(_collect_component_schema_ids(comp))
        resource_ids = set(_collect_component_resource_ids(comp))
        resource_ids.update(_id(res, "resource_id", "id") for res in resources if res.get("component_id") == cid)
        operator_ids = set()
        for op in operators:
            ports = (op.get("inputs", []) or []) + (op.get("outputs", []) or [])
            port_contracts = {p.get("contract_id") or p.get("schema_id") for p in ports if isinstance(p, dict)}
            if op_refs_by_id.get(op["operator_id"], set()).intersection(resource_ids) or schema_ids.intersection(port_contracts):
                operator_ids.add(op["operator_id"])
        workflow_ids = set()
        for wf in workflows:
            if set(wf.get("operator_refs", [])).intersection(operator_ids):
                workflow_ids.add(wf.get("workflow_id", ""))
        cc = dict(comp)
        cc["resource_ids"] = sorted(rid for rid in resource_ids if rid)
        cc["operator_ids"] = sorted(operator_ids)
        cc["workflow_ids"] = sorted(wf_id for wf_id in workflow_ids if wf_id)
        component_views.append(cc)

    relationships = {
        "component_count": len(component_views),
        "resource_count": len(resources),
        "operator_count": len(operators),
        "workflow_count": len(workflows),
        "components": component_views,
        "resources": resources,
        "operators": operators,
        "workflows": workflows,
    }
    return {
        "twin": twin,
        "groups": groups,
        "resources": resources,
        "operators": operators,
        "workflows": workflows,
        "relationships": relationships,
        "resource_validation": validation,
    }


def _run_profile_file(profile_entry, package_dir=None):
    package_dir = package_dir or active_object_package()
    if not package_dir:
        return "", "", {}
    manifest_dir = os.path.join(package_dir, "object")
    profile_id = _id(profile_entry, "profile_id", "id")
    raw_path = str(profile_entry.get("path", ""))
    candidates = []
    if raw_path:
        candidates.append(os.path.abspath(os.path.join(manifest_dir, raw_path)))
        candidates.append(os.path.abspath(os.path.join(package_dir, raw_path)))
    if profile_id:
        candidates.append(os.path.join(package_dir, "run_profiles", profile_id + ".json"))
    for path in candidates:
        doc = try_load_json(path) or None
        if doc is not None:
            return path, os.path.basename(path), doc
    return "", "", {}


def _load_run_profiles(package_dir=None):
    package_dir = package_dir or active_object_package()
    if not package_dir:
        return []
    twin = try_load_json(os.path.join(package_dir, "object", "twin_object.json")) or {}
    rows = []
    seen = set()
    for entry in twin.get("run_profiles", []) or []:
        path, fn, doc = _run_profile_file(entry if isinstance(entry, dict) else {}, package_dir)
        profile_id = str((doc or {}).get("profile_id") or _id(entry, "profile_id", "id"))
        if not profile_id or profile_id in seen:
            continue
        seen.add(profile_id)
        row = dict(doc or {})
        row["profile_id"] = profile_id
        row["spec_file"] = fn
        row["path"] = path
        row["source"] = "object_package"
        row["available"] = bool(doc)
        rows.append(row)

    profile_dir = os.path.join(package_dir, "run_profiles")
    if os.path.isdir(profile_dir):
        for fn in sorted(os.listdir(profile_dir)):
            if not fn.endswith(".json"):
                continue
            path = os.path.join(profile_dir, fn)
            doc = try_load_json(path) or {}
            profile_id = str(doc.get("profile_id") or os.path.splitext(fn)[0])
            if not profile_id or profile_id in seen:
                continue
            seen.add(profile_id)
            row = dict(doc)
            row["profile_id"] = profile_id
            row["spec_file"] = fn
            row["path"] = path
            row["source"] = "run_profiles_dir"
            row["available"] = True
            rows.append(row)
    rows.sort(key=lambda item: (str(item.get("metadata", {}).get("ui_group", "")), str(item.get("profile_id", ""))))
    return rows


def _run_profile_by_id(profile_id, package_dir=None):
    profile_id = str(profile_id or "").strip()
    if not profile_id:
        return {}
    for item in _load_run_profiles(package_dir):
        if str(item.get("profile_id", "")).strip() == profile_id:
            return item
    return {}


def _bool_setting(value, default=False):
    if isinstance(value, bool):
        return value
    if value is None or value == "":
        return default
    text = str(value).strip().lower()
    if text in ("1", "true", "yes", "on", "enabled"):
        return True
    if text in ("0", "false", "no", "off", "disabled"):
        return False
    return default


def _int_setting(value, default=0, minimum=None):
    try:
        result = int(float(value))
    except Exception:
        result = int(default)
    if minimum is not None:
        result = max(int(minimum), result)
    return result


def _profile_runtime_launch(profile):
    profile = profile or {}
    launch = profile.get("runtime_launch") or profile.get("runtime") or profile.get("launch") or {}
    branch = profile.get("branching_policy") or profile.get("branch_policy") or {}
    prediction_every = _int_setting(
        launch.get("prediction_every_frames", branch.get("every_n_frames", 30)),
        30,
        1,
    )
    return {
        "online_frames": _int_setting(launch.get("online_frames", launch.get("requested_frames", 50)), 50, 1),
        "prediction_every_frames": prediction_every,
        "future_max_iterations": _int_setting(launch.get("future_max_iterations", launch.get("future_max_steps", 0)), 0, 0),
        "branch_chunk_iterations": _int_setting(launch.get("branch_chunk_iterations", launch.get("branch_chunk", 1)), 1, 1),
        "replay_by_platform_clock": _bool_setting(launch.get("replay_by_platform_clock", launch.get("replay_clock", True)), True),
        "external_observation_stream": str(launch.get("external_observation_stream") or launch.get("external_input_stream") or "").strip(),
        "branch_enabled": _bool_setting(branch.get("enabled", launch.get("branch_enabled", False)), False),
        "branch_target_workflow_id": str(branch.get("target_workflow_id") or launch.get("branch_target_workflow_id") or "").strip(),
        "branch_trigger_kind": str(branch.get("trigger_kind") or "every_n_frames").strip(),
        "branch_every_n_frames": _int_setting(branch.get("every_n_frames", prediction_every), prediction_every, 1),
        "branch_max_concurrent": _int_setting(branch.get("max_concurrent_branches", launch.get("branch_max_concurrent", 1)), 1, 1),
        "branch_seed_policy": str(branch.get("seed_policy") or "latest_checkpoint").strip(),
        "branch_cancel_policy": str(branch.get("cancel_policy") or "never").strip(),
        "branching_policy": branch,
    }


def _payload_or_profile(payload, profile_launch, key, default=None):
    value = (payload or {}).get(key, None)
    if value is not None and value != "":
        return value
    return (profile_launch or {}).get(key, default)


def _workflow_branch_policy(workflow_id, package_dir=None):
    _path, _fn, workflow = _workflow_file(workflow_id, package_dir)
    if not isinstance(workflow, dict):
        return {}
    branch = workflow.get("branching_policy") or workflow.get("branch_policy") or {}
    return branch if isinstance(branch, dict) else {}


def _with_workflow_branch_defaults(profile_launch, workflow_id, package_dir=None):
    merged = dict(profile_launch or {})
    if merged.get("branching_policy"):
        return merged
    branch = _workflow_branch_policy(workflow_id, package_dir)
    if not branch:
        return merged
    workflow_defaults = _profile_runtime_launch({"branching_policy": branch})
    for key, value in workflow_defaults.items():
        if key == "branching_policy" or key.startswith("branch_"):
            merged[key] = value
    if "prediction_every_frames" not in merged and workflow_defaults.get("branch_every_n_frames"):
        merged["prediction_every_frames"] = workflow_defaults["branch_every_n_frames"]
    return merged


def validate_resources(package_dir=None):
    payload = _load_object_payload(package_dir)
    if not payload:
        return {"ok": False, "message": "尚未载入对象包", "resources": [], "diagnostics": []}
    return payload.get("resource_validation", {"ok": True, "resources": [], "diagnostics": []})


def _resource_geometry_path(resource):
    for key in ("path", "file", "mesh_path", "geometry_path"):
        value = (resource or {}).get(key)
        if not value:
            continue
        resolved = _resolve_resource_path(value)
        if resolved and os.path.exists(resolved):
            return resolved
    return ""


def _mesh_resource_candidates(component_id, resource_id, payload):
    resources = as_list((payload or {}).get("resources"))
    by_id = {_id(item, "resource_id", "id"): item for item in resources}
    if resource_id:
        return [by_id.get(resource_id) or {"resource_id": resource_id, "missing": True}]
    component = next((item for item in as_list((payload or {}).get("twin", {}).get("components")) if _id(item, "component_id", "id") == component_id), {})
    ids = []
    ids.extend(_strings(component.get("mesh_resource_ids")))
    for res in resources:
        rid = _id(res, "resource_id", "id")
        if res.get("component_id") == component_id:
            text = " ".join(str(res.get(key, "")) for key in ("resource_type", "layout_role", "uri", "path")).lower()
            if "mesh" in text:
                ids.append(rid)
    return [by_id.get(rid) or {"resource_id": rid, "missing": True} for rid in list(dict.fromkeys(ids)) if rid]


def _bounds_from_positions(positions):
    if not positions:
        return None
    xs, ys, zs = positions[0::3], positions[1::3], positions[2::3]
    return {"min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]}


def _parse_ascii_stl(text, max_triangles):
    vertices = []
    for line in text.splitlines():
        parts = line.strip().split()
        if len(parts) == 4 and parts[0].lower() == "vertex":
            try:
                vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
            except ValueError:
                continue
    total_triangles = len(vertices) // 3
    if total_triangles <= 0:
        return [], [], 0, 0
    stride = max(1, int((total_triangles + max_triangles - 1) // max_triangles)) if max_triangles else 1
    positions = []
    indices = []
    out_index = 0
    for tri_index in range(0, total_triangles, stride):
        tri = vertices[tri_index * 3:tri_index * 3 + 3]
        if len(tri) != 3:
            continue
        for vertex in tri:
            positions.extend(vertex)
            indices.append(out_index)
            out_index += 1
    return positions, indices, total_triangles, len(indices) // 3


def _parse_binary_stl(data, max_triangles):
    if len(data) < 84:
        return [], [], 0, 0
    total_triangles = struct.unpack_from("<I", data, 80)[0]
    available = max(0, (len(data) - 84) // 50)
    total_triangles = min(total_triangles, available)
    stride = max(1, int((total_triangles + max_triangles - 1) // max_triangles)) if max_triangles else 1
    positions = []
    indices = []
    out_index = 0
    for tri_index in range(0, total_triangles, stride):
        offset = 84 + tri_index * 50 + 12
        if offset + 36 > len(data):
            break
        coords = struct.unpack_from("<9f", data, offset)
        positions.extend(coords)
        indices.extend([out_index, out_index + 1, out_index + 2])
        out_index += 3
    return positions, indices, total_triangles, len(indices) // 3


_GEOMETRY_CACHE = {}
_GEOMETRY_CACHE_MAX_ITEMS = 6


def _geometry_cache_key(path, max_triangles):
    stat = os.stat(path)
    return (os.path.abspath(path), stat.st_mtime_ns, stat.st_size, int(max_triangles or 0))


def _load_stl_geometry(path, max_triangles=15000):
    key = _geometry_cache_key(path, max_triangles)
    cached = _GEOMETRY_CACHE.get(key)
    if cached:
        return cached
    with open(path, "rb") as f:
        data = f.read()
    looks_ascii = data[:256].lstrip().lower().startswith(b"solid") and b"\n" in data[:512]
    if looks_ascii:
        try:
            text = data.decode("utf-8", errors="ignore")
            positions, indices, total, shown = _parse_ascii_stl(text, max_triangles)
            if shown:
                result = (positions, indices, total, shown)
                _GEOMETRY_CACHE[key] = result
                if len(_GEOMETRY_CACHE) > _GEOMETRY_CACHE_MAX_ITEMS:
                    _GEOMETRY_CACHE.pop(next(iter(_GEOMETRY_CACHE)))
                return result
        except Exception:
            pass
    result = _parse_binary_stl(data, max_triangles)
    _GEOMETRY_CACHE[key] = result
    if len(_GEOMETRY_CACHE) > _GEOMETRY_CACHE_MAX_ITEMS:
        _GEOMETRY_CACHE.pop(next(iter(_GEOMETRY_CACHE)))
    return result


def object_geometry(payload):
    payload = payload or {}
    package_dir = package_for_payload(payload)
    if not package_dir:
        return {"ok": False, "message": "尚未载入对象包"}
    component_id = str(payload.get("component_id") or "").strip()
    resource_id = str(payload.get("resource_id") or "").strip()
    try:
        max_triangles = int(payload.get("max_triangles") or 15000)
    except Exception:
        max_triangles = 15000
    data = _load_object_payload(package_dir) or {}
    candidates = _mesh_resource_candidates(component_id, resource_id, data)
    checked = []
    for resource in candidates:
        rid = _id(resource, "resource_id", "id")
        path = _resource_geometry_path(resource)
        checked.append({
            "resource_id": rid,
            "display_name": resource.get("display_name", rid),
            "path": path,
            "uri": resource.get("uri", ""),
            "exists": bool(path and os.path.exists(path)),
        })
        if not path:
            continue
        ext = os.path.splitext(path)[1].lower()
        if ext != ".stl":
            return {"ok": False, "message": "当前组件预览暂只支持 STL 几何资源", "resource": resource, "path": path, "checked": checked}
        positions, indices, total_triangles, shown_triangles = _load_stl_geometry(path, max_triangles)
        if not positions:
            return {"ok": False, "message": "STL 文件没有解析出三角面", "resource": resource, "path": path, "checked": checked}
        return {
            "ok": True,
            "component_id": component_id,
            "resource_id": rid,
            "resource": resource,
            "source_file": path,
            "source_file_name": os.path.basename(path),
            "positions": positions,
            "indices": indices,
            "triangle_count": shown_triangles,
            "source_triangle_count": total_triangles,
            "decimated": shown_triangles < total_triangles,
            "bounds": _bounds_from_positions(positions),
            "checked": checked,
        }
    return {"ok": False, "message": "当前组件没有绑定可读取的真实 STL 几何资源", "component_id": component_id, "resource_id": resource_id, "checked": checked}


def operator_preflight(payload):
    if not OBJECT_PACKAGE:
        return {"ok": False, "message": "尚未载入对象包", "diagnostics": []}
    operator_id = (payload or {}).get("operator_id", "")
    data = _load_object_payload() or {}
    operators = data.get("operators", [])
    resources = {_id(res, "resource_id", "id"): res for res in data.get("resources", [])}
    op = next((item for item in operators if item.get("operator_id") == operator_id), None)
    if not op:
        return {"ok": False, "message": "operator not found", "diagnostics": [{"severity": "blocking", "message": "未找到算子声明"}]}
    diagnostics = []
    if not op.get("backend"):
        diagnostics.append({"severity": "blocking", "message": "缺少 execution.kind/backend"})
    if not op.get("lifecycle"):
        diagnostics.append({"severity": "warning", "message": "缺少 lifecycle 声明"})
    for port in (op.get("inputs", []) or []) + (op.get("outputs", []) or []):
        if not (port.get("contract_id") or port.get("frame_contract") or port.get("typed_io_contract")):
            diagnostics.append({"severity": "blocking", "message": "端口缺少 contract 声明", "port_id": port.get("port_id", "")})
    for rid in op.get("resource_refs", []) or []:
        if rid not in resources:
            diagnostics.append({"severity": "blocking", "message": "算子引用了未声明资源", "resource_id": rid})
        elif resources[rid].get("resource_status") == "missing":
            diagnostics.append({"severity": "blocking", "message": "算子引用的资源路径缺失", "resource_id": rid})
    diagnostics.extend(op.get("renderer_resolution", {}).get("diagnostics", []))
    return {
        "ok": not any(item.get("severity") == "blocking" for item in diagnostics),
        "operator_id": operator_id,
        "backend": op.get("backend", ""),
        "lifecycle": op.get("lifecycle", []),
        "resource_refs": op.get("resource_refs", []),
        "renderer_resolution": op.get("renderer_resolution", {}),
        "diagnostics": diagnostics,
    }


def object_summary(package_dir=None):
    package_dir = package_dir or active_object_package()
    if not package_dir:
        return _empty_object_summary()
    payload = _load_object_payload(package_dir) or {}
    twin = payload.get("twin", {})
    resources = payload.get("resources", [])
    groups = payload.get("groups", [])
    operators = payload.get("operators", [])
    workflows = payload.get("workflows", [])

    domain_schemas = []
    schema_dir = os.path.join(package_dir, "domain_schemas")
    if os.path.isdir(schema_dir):
        for fn in sorted(os.listdir(schema_dir)):
            if not fn.endswith(".json"):
                continue
            schema = try_load_json(os.path.join(schema_dir, fn)) or {}
            if schema.get("schema_id"):
                ss = dict(schema)
                ss["spec_file"] = fn
                domain_schemas.append(ss)

    return {
        "ok": True,
        "object_loaded": True,
        "object_package_root": OBJECT_PACKAGE,
        "package_dir": package_dir,
        "package_domain": package_domain(package_dir),
        "formal_package_root": OBJECT_PACKAGE,
        "active_draft_id": ACTIVE_OBJECT_DRAFT_ID,
        "object_id": twin.get("object_id"),
        "object_type": twin.get("object_type"),
        "schema_version": twin.get("schema_version"),
        "display_name": twin.get("display_name", twin.get("name", "")),
        "components": payload.get("relationships", {}).get("components", twin.get("components", [])),
        "resources": resources,
        "asset_groups": groups,
        "operators": operators,
        "domain_schemas": domain_schemas,
        "workflows": workflows,
        "run_profiles": _load_run_profiles(package_dir),
        "relationships": payload.get("relationships", {}),
        "resource_validation": payload.get("resource_validation", {}),
        "diagnostics": validate_object_package_dir(package_dir).get("diagnostics", []),
    }


# ---------------------------------------------------------------------------
# object package modeler / CAE-style project APIs
# ---------------------------------------------------------------------------
def _json_mtime(path):
    try:
        return _dt.datetime.fromtimestamp(os.path.getmtime(path)).isoformat(timespec="seconds")
    except OSError:
        return ""


def _rel_to_package(path, package_dir):
    try:
        return os.path.relpath(path, package_dir).replace("\\", "/")
    except Exception:
        return ""


def _safe_package_path(package_dir, rel_path):
    package_dir = os.path.abspath(package_dir or "")
    rel_path = str(rel_path or "").replace("\\", "/").lstrip("/")
    if not package_dir or not rel_path:
        return ""
    path = os.path.abspath(os.path.join(package_dir, *[part for part in rel_path.split("/") if part]))
    if path != package_dir and not path.startswith(package_dir + os.sep):
        return ""
    return path


def _json_file_summary(path, package_dir):
    doc = try_load_json(path) or {}
    rel = _rel_to_package(path, package_dir)
    return {
        "path": path,
        "rel_path": rel,
        "exists": os.path.isfile(path),
        "updated_at": _json_mtime(path),
        "schema_version": doc.get("schema_version", "") if isinstance(doc, dict) else "",
        "id": _entity_id_from_doc(doc, rel) if isinstance(doc, dict) else os.path.splitext(os.path.basename(path))[0],
    }


def _entity_id_from_doc(doc, rel_path=""):
    if not isinstance(doc, dict):
        return os.path.splitext(os.path.basename(rel_path or "entity"))[0]
    for key in (
            "schema_id", "operator_id", "workflow_id", "profile_id", "resource_id",
            "group_id", "component_id", "project_id", "model_id", "manifest_id",
            "object_id", "id"):
        value = doc.get(key)
        if value:
            return str(value)
    return os.path.splitext(os.path.basename(rel_path or "entity"))[0]


def _entity_display_name(doc, fallback):
    if not isinstance(doc, dict):
        return fallback
    return str(doc.get("display_name") or doc.get("title") or doc.get("name") or fallback)


def _schema_role_label(role):
    labels = {
        "object_state": "对象状态",
        "latent_state": "潜在状态",
        "field_state": "场状态",
        "accumulated_state": "累积状态",
        "observation": "观测",
        "qoi": "QoI",
        "decision": "决策",
        "event": "事件",
    }
    return labels.get(str(role or "").strip(), str(role or "").strip() or "数据结构")


def _entity_tree_label(kind, doc, entity_id, rel_path):
    entity_id = str(entity_id or "").strip()
    rel_path = str(rel_path or "").strip()
    display = _entity_display_name(doc, entity_id or os.path.basename(rel_path))
    if kind == "domain_schema":
        if display and display not in (entity_id, rel_path, os.path.basename(rel_path)):
            return display
        role = _schema_role_label((doc or {}).get("schema_role") or (doc or {}).get("value_kind"))
        return "%s · %s" % (role, entity_id or rel_path)
    if kind == "operator":
        family = str((doc or {}).get("operator_family") or "operator").strip()
        if display and display not in (entity_id, rel_path, os.path.basename(rel_path)):
            return "%s · %s" % (display, entity_id or rel_path)
        return "%s · %s" % (family, entity_id or rel_path)
    if kind in ("workflow", "run_profile", "runtime_profile", "test_vector"):
        return entity_id or rel_path or display
    if display and entity_id and display != entity_id:
        return "%s · %s" % (display, entity_id)
    return entity_id or display or rel_path


def _object_project_file(package_dir):
    return os.path.join(package_dir or "", "object_project.json")


def object_project_doc(package_dir=None):
    package_dir = package_dir or active_object_package()
    project_path = _object_project_file(package_dir)
    project = try_load_json(project_path) or {}
    twin = try_load_json(os.path.join(package_dir or "", "object", "twin_object.json")) or {}
    if not project:
        object_id = twin.get("object_id") or os.path.basename(os.path.abspath(package_dir or "object"))
        project = {
            "schema_version": "flightenv.webui.object_project.v1",
            "project_id": "%s.project" % object_id,
            "object_id": object_id,
            "display_name": twin.get("display_name") or twin.get("name") or object_id,
            "package_dir": package_dir or "",
            "source": "derived_from_object_package",
            "cae_analogy": {
                "source_project": "object package directory",
                "compiled_input": "platform-pdk compiled workflow artifacts",
                "result_database": "runtime run package / evidence / data-plane artifacts",
            },
        }
    project["project_file"] = project_path
    project["project_file_exists"] = os.path.isfile(project_path)
    project["package_dir"] = package_dir or project.get("package_dir", "")
    return project


def _file_sha256(path):
    try:
        import hashlib
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b""):
                h.update(chunk)
        return h.hexdigest()
    except Exception:
        return ""


def object_semantic_hashes(package_dir):
    rels = [
        "object/twin_object.json",
        "assets/resources.json",
    ]
    hashes = {}
    for rel in rels:
        path = os.path.join(package_dir, *rel.split("/"))
        hashes[rel] = _file_sha256(path)
    return hashes


def save_object_project_state(payload):
    payload = payload or {}
    package_dir = package_for_payload(payload)
    if not package_dir:
        return {"ok": False, "message": "尚未载入对象包或对象工程"}
    before = object_semantic_hashes(package_dir)
    project = object_project_doc(package_dir)
    ui_state = project.get("ui_state") if isinstance(project.get("ui_state"), dict) else {}
    incoming_ui_state = payload.get("ui_state") if isinstance(payload.get("ui_state"), dict) else {}
    ui_state.update(incoming_ui_state)
    for key in ("selected_tree_path", "last_opened_workflow", "last_opened_run_profile"):
        if key in payload:
            project[key] = payload.get(key)
    if "expanded_tree_paths" in payload:
        ui_state["expanded_tree_paths"] = payload.get("expanded_tree_paths") if isinstance(payload.get("expanded_tree_paths"), list) else []
    if "selected_tree_path" in payload:
        ui_state["selected_tree_path"] = payload.get("selected_tree_path")
    project["ui_state"] = ui_state
    project["updated_at_utc"] = _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"
    project["schema_version"] = project.get("schema_version") or "flightenv.webui.object_project.v1"
    project.pop("project_file", None)
    project.pop("project_file_exists", None)
    project["package_dir"] = package_dir
    path = _object_project_file(package_dir)
    write_json_atomic(path, project)
    after = object_semantic_hashes(package_dir)
    return {
        "ok": True,
        "message": "工程状态已保存；对象语义 JSON 未被修改" if before == after else "工程状态已保存，但语义 hash 发生变化",
        "project_file": path,
        "project": object_project_doc(package_dir),
        "semantic_hashes_before": before,
        "semantic_hashes_after": after,
        "semantic_unchanged": before == after,
    }


def _tree_node(node_id, label, kind, **kwargs):
    node = {
        "id": node_id,
        "label": label,
        "kind": kind,
        "children": kwargs.pop("children", []),
    }
    node.update(kwargs)
    node.setdefault("actions", _tree_node_actions(node))
    return node


def _tree_node_actions(node):
    kind = node.get("kind", "")
    actions = []
    if node.get("create_kind"):
        actions.append({"id": "new", "label": "新建", "enabled": True, "target_kind": node.get("create_kind")})
    if node.get("editable"):
        actions.extend([
            {"id": "edit", "label": "编辑", "enabled": True},
            {"id": "view_json", "label": "查看 JSON", "enabled": True},
        ])
    if node.get("deletable"):
        actions.extend([
            {"id": "references", "label": "查看引用关系", "enabled": True},
            {"id": "delete", "label": "删除", "enabled": True, "requires_reference_check": True},
        ])
    if kind == "project":
        actions.extend([
            {"id": "save_project_state", "label": "保存工程状态", "enabled": True},
            {"id": "validate_project", "label": "校验对象包", "enabled": True},
            {"id": "close_project", "label": "关闭工程", "enabled": True, "requires_dirty_check": True},
        ])
    if kind == "folder":
        actions.append({"id": "refresh", "label": "刷新", "enabled": True})
    if kind == "compiled_workflow":
        actions.append({"id": "open_compiled_output", "label": "查看编译产物", "enabled": True})
    if kind == "run_package":
        actions.extend([
            {"id": "open_run", "label": "打开运行包", "enabled": True},
            {"id": "open_field_viewer", "label": "查看云图", "enabled": True},
        ])
    if not actions:
        actions.append({"id": "inspect", "label": "查看", "enabled": True})
    return actions


def _json_file_nodes(package_dir, rel_dir, kind, suffix=".json"):
    root = os.path.join(package_dir, *rel_dir.split("/"))
    rows = []
    if not os.path.isdir(root):
        return rows
    for fn in sorted(os.listdir(root)):
        if suffix and not fn.endswith(suffix):
            continue
        path = os.path.join(root, fn)
        if not os.path.isfile(path):
            continue
        doc = try_load_json(path) or {}
        rel = _rel_to_package(path, package_dir)
        eid = _entity_id_from_doc(doc, rel)
        rows.append(_tree_node(
            "%s:%s" % (kind, rel),
            _entity_tree_label(kind, doc, eid, rel),
            kind,
            entity_kind=kind,
            entity_id=eid,
            rel_path=rel,
            editable=True,
            deletable=True,
            summary=_json_file_summary(path, package_dir),
        ))
    return rows


def _json_file_nodes_recursive(package_dir, rel_dir, kind, suffix=".json"):
    root = os.path.join(package_dir, *rel_dir.split("/"))
    rows = []
    if not os.path.isdir(root):
        return rows
    for current_root, _dirs, files in os.walk(root):
        for fn in sorted(files):
            if suffix and not fn.endswith(suffix):
                continue
            path = os.path.join(current_root, fn)
            doc = try_load_json(path) or {}
            rel = _rel_to_package(path, package_dir)
            eid = _entity_id_from_doc(doc, rel)
            rows.append(_tree_node(
                "%s:%s" % (kind, rel),
                rel,
                kind,
                entity_kind=kind,
                entity_id=eid,
                rel_path=rel,
                editable=True,
                deletable=True,
                summary=_json_file_summary(path, package_dir),
            ))
    rows.sort(key=lambda item: item.get("rel_path", ""))
    return rows


def _compiled_workflow_nodes():
    root = os.path.join(ARTIFACTS, "platform-pdk", "compiled-workflows")
    rows = []
    if not os.path.isdir(root):
        return rows
    for name in sorted(os.listdir(root)):
        path = os.path.join(root, name)
        if not os.path.isdir(path):
            continue
        summary = _compiled_plan_summary(path)
        rows.append(_tree_node(
            "compiled:%s" % name,
            name,
            "compiled_workflow",
            editable=False,
            deletable=False,
            compiled_dir=path,
            summary=summary.get("summary", {}),
        ))
    return rows


def _run_package_nodes():
    rows = []
    for row in list_runs():
        run_id = row.get("run") or row.get("run_id") or ""
        rows.append(_tree_node(
            "run:%s" % run_id,
            run_id,
            "run_package",
            editable=False,
            deletable=False,
            run=run_id,
            summary=row,
        ))
    return rows


def object_model_tree(payload=None):
    package_dir = package_for_payload(payload or {})
    if not package_dir:
        return {"ok": False, "message": "尚未载入对象包或对象工程", "tree": []}
    validation = validate_object_package_dir(package_dir)
    summary = object_summary(package_dir) if os.path.isdir(package_dir) else {}
    twin = try_load_json(os.path.join(package_dir, "object", "twin_object.json")) or {}
    assets = try_load_json(os.path.join(package_dir, "assets", "resources.json")) or {}
    project = object_project_doc(package_dir)

    component_nodes = []
    for comp in as_list(twin.get("components")):
        cid = _id(comp, "component_id", "id")
        component_nodes.append(_tree_node(
            "component:%s" % cid,
            _entity_display_name(comp, cid),
            "component",
            entity_kind="component",
            entity_id=cid,
            rel_path="object/twin_object.json",
            editable=True,
            deletable=True,
            summary={
                "schema_count": len(_collect_component_schema_ids(comp)),
                "resource_count": len(_collect_component_resource_ids(comp)),
            },
        ))

    resource_by_id = {}
    for res in as_list(twin.get("resources")):
        rid = _id(res, "resource_id", "id")
        if rid:
            resource_by_id[rid] = res

    def resource_tree_node(res, group_id=""):
        rid = _id(res, "resource_id", "id")
        missing = bool(res.get("missing"))
        return _tree_node(
            "resource:%s" % rid,
            _entity_display_name(res, rid),
            "resource",
            entity_kind="resource",
            entity_id=rid,
            rel_path="object/twin_object.json",
            editable=not missing,
            deletable=not missing,
            summary={
                "type": res.get("resource_type", res.get("type", "")),
                "component_id": res.get("component_id", ""),
                "uri": res.get("uri", res.get("path", "")),
                "group_id": group_id or res.get("group_id", ""),
                "missing": missing,
            },
        )

    asset_group_nodes = []
    grouped_resource_ids = set()
    for group in as_list(assets.get("asset_groups")):
        gid = _id(group, "group_id", "id")
        children = []
        for rid in as_list(group.get("resources")):
            grouped_resource_ids.add(rid)
            res = resource_by_id.get(rid) or {
                "resource_id": rid,
                "display_name": rid,
                "resource_type": "missing_resource",
                "missing": True,
            }
            children.append(resource_tree_node(res, gid))
        asset_group_nodes.append(_tree_node(
            "asset_group:%s" % gid,
            _entity_display_name(group, gid),
            "asset_group",
            entity_kind="asset_group",
            entity_id=gid,
            rel_path="assets/resources.json",
            editable=True,
            deletable=True,
            summary={"resource_count": len(as_list(group.get("resources")))},
            create_kind="resource",
            children=children,
        ))
    ungrouped_nodes = []
    for rid, res in sorted(resource_by_id.items()):
        if rid not in grouped_resource_ids:
            ungrouped_nodes.append(resource_tree_node(res, ""))
    if ungrouped_nodes:
        asset_group_nodes.append(_tree_node(
            "asset_group:__ungrouped__",
            "未分组资源",
            "folder",
            create_kind="resource",
            children=ungrouped_nodes,
            summary={"resource_count": len(ungrouped_nodes)},
        ))

    tree = [
        _tree_node(
            "project",
            project.get("display_name") or project.get("object_id") or "Object Project",
            "project",
            entity_kind="project",
            entity_id=project.get("project_id", "project"),
            rel_path="object_project.json",
            editable=True,
            deletable=False,
            summary={
                "package_dir": package_dir,
                "domain": package_domain(package_dir),
                "project_file_exists": project.get("project_file_exists", False),
            },
            children=[
                _tree_node(
                    "object",
                    "Object",
                    "folder",
                    children=[
                        _tree_node(
                            "twin_object",
                            twin.get("display_name") or twin.get("object_id") or "twin_object",
                            "twin_object",
                            entity_kind="twin_object",
                            entity_id=twin.get("object_id", "twin_object"),
                            rel_path="object/twin_object.json",
                            editable=True,
                            deletable=False,
                            summary=_json_file_summary(os.path.join(package_dir, "object", "twin_object.json"), package_dir),
                        ),
                    ],
                ),
                _tree_node("components", "Components", "folder", create_kind="component", children=component_nodes),
                _tree_node(
                    "domain_schemas",
                    "Domain Schemas",
                    "folder",
                    create_kind="domain_schema",
                    children=_json_file_nodes(package_dir, "domain_schemas", "domain_schema", ".json"),
                ),
                _tree_node(
                    "resources",
                    "资源与模型资产",
                    "folder",
                    create_kind="resource",
                    children=asset_group_nodes,
                ),
                _tree_node(
                    "operators",
                    "Operators",
                    "folder",
                    create_kind="operator",
                    children=_json_file_nodes(package_dir, "operators", "operator", ".atomic.json"),
                ),
                _tree_node(
                    "workflows",
                    "Workflows",
                    "folder",
                    create_kind="workflow",
                    children=_json_file_nodes(package_dir, "workflows", "workflow", ".json"),
                ),
                _tree_node(
                    "run_profiles",
                    "Run Profiles",
                    "folder",
                    create_kind="run_profile",
                    children=_json_file_nodes(package_dir, "run_profiles", "run_profile", ".json"),
                ),
                _tree_node(
                    "runtime_profile",
                    "Runtime Profile",
                    "folder",
                    children=_json_file_nodes(package_dir, "runtime", "runtime_profile", ".json"),
                ),
                _tree_node(
                    "test_vectors",
                    "Test Vectors",
                    "folder",
                    create_kind="test_vector",
                    children=_json_file_nodes_recursive(package_dir, "test_vectors", "test_vector", ".json"),
                ),
                _tree_node(
                    "compiled_outputs",
                    "编译产物",
                    "folder",
                    children=_compiled_workflow_nodes(),
                ),
                _tree_node(
                    "run_packages",
                    "运行结果 / 云图",
                    "folder",
                    children=_run_package_nodes(),
                ),
            ],
        )
    ]
    return {
        "ok": validation.get("ok", False),
        "package_dir": package_dir,
        "package_domain": package_domain(package_dir),
        "project": project,
        "object": summary,
        "validation": validation,
        "tree": tree,
        "delivery_status": object_modeler_delivery_status(package_dir, summary),
    }


def as_list(value):
    return value if isinstance(value, list) else []


def _find_array_entity(package_dir, kind, entity_id):
    twin_path = os.path.join(package_dir, "object", "twin_object.json")
    assets_path = os.path.join(package_dir, "assets", "resources.json")
    if kind == "component":
        twin = try_load_json(twin_path) or {}
        item = next((row for row in as_list(twin.get("components")) if _id(row, "component_id", "id") == entity_id), None)
        return twin_path, item or {}
    if kind == "resource":
        twin = try_load_json(twin_path) or {}
        item = next((row for row in as_list(twin.get("resources")) if _id(row, "resource_id", "id") == entity_id), None)
        return twin_path, item or {}
    if kind == "asset_group":
        assets = try_load_json(assets_path) or {}
        item = next((row for row in as_list(assets.get("asset_groups")) if _id(row, "group_id", "id") == entity_id), None)
        return assets_path, item or {}
    return "", {}


def _entity_default_rel_path(kind, entity_id):
    safe = _safe_id(entity_id)
    mapping = {
        "project": "object_project.json",
        "twin_object": "object/twin_object.json",
        "domain_schema": "domain_schemas/%s.schema.json" % safe,
        "operator": "operators/%s.atomic.json" % safe,
        "workflow": "workflows/%s.json" % safe,
        "run_profile": "run_profiles/%s.json" % safe,
        "runtime_profile": "runtime/platform_runtime_profile.json" if safe in ("", "platform_runtime_profile") else "runtime/%s.json" % safe,
    }
    return mapping.get(kind, "")


def _find_file_entity_path(package_dir, kind, entity_id, rel_path=""):
    rel_path = str(rel_path or "").strip()
    entity_id = str(entity_id or "").strip()
    if rel_path:
        path = _safe_package_path(package_dir, rel_path)
        if path and os.path.isfile(path):
            return path

    roots = {
        "domain_schema": [("domain_schemas", ".json", False)],
        "operator": [("operators", ".json", False)],
        "workflow": [("workflows", ".json", False)],
        "run_profile": [("run_profiles", ".json", False)],
        "runtime_profile": [("runtime", ".json", False)],
        "test_vector": [("test_vectors", ".json", True)],
    }.get(kind, [])
    if package_dir and entity_id:
        for root_rel, suffix, recursive in roots:
            root = os.path.join(package_dir, *root_rel.split("/"))
            if not os.path.isdir(root):
                continue
            if recursive:
                files = (
                    os.path.join(current_root, fn)
                    for current_root, _dirs, names in os.walk(root)
                    for fn in names
                    if not suffix or fn.endswith(suffix)
                )
            else:
                files = (
                    os.path.join(root, fn)
                    for fn in os.listdir(root)
                    if (not suffix or fn.endswith(suffix)) and os.path.isfile(os.path.join(root, fn))
                )
            for path in files:
                doc = try_load_json(path)
                if isinstance(doc, dict) and _entity_id_from_doc(doc, _rel_to_package(path, package_dir)) == entity_id:
                    return path

    default_path = _safe_package_path(package_dir, _entity_default_rel_path(kind, entity_id))
    return default_path if default_path and os.path.isfile(default_path) else ""


def object_entity_read(payload):
    payload = payload or {}
    package_dir = package_for_payload(payload)
    kind = str(payload.get("kind") or payload.get("entity_kind") or "").strip()
    entity_id = str(payload.get("entity_id") or payload.get("id") or "").strip()
    rel_path = str(payload.get("rel_path") or _entity_default_rel_path(kind, entity_id)).strip()
    if not package_dir:
        return {"ok": False, "message": "尚未载入对象包"}
    if kind == "project":
        doc = object_project_doc(package_dir)
        path = _object_project_file(package_dir)
        return {
            "ok": True,
            "kind": kind,
            "entity_id": entity_id or doc.get("project_id", "project"),
            "rel_path": "object_project.json",
            "path": path,
            "doc": doc,
            "references": scan_entity_references(package_dir, entity_id or doc.get("project_id", ""), "object_project.json"),
        }
    if kind in ("component", "resource", "asset_group"):
        path, doc = _find_array_entity(package_dir, kind, entity_id)
        if not doc:
            return {"ok": False, "message": "实体不存在", "kind": kind, "entity_id": entity_id}
        return {
            "ok": True,
            "kind": kind,
            "entity_id": entity_id,
            "rel_path": _rel_to_package(path, package_dir),
            "path": path,
            "doc": doc,
            "references": scan_entity_references(package_dir, entity_id, _rel_to_package(path, package_dir)),
        }
    path = _find_file_entity_path(package_dir, kind, entity_id, rel_path)
    if not path or not os.path.isfile(path):
        return {"ok": False, "message": "实体文件不存在", "kind": kind, "entity_id": entity_id, "rel_path": rel_path}
    doc = try_load_json(path)
    if doc is None:
        return {"ok": False, "message": "JSON 读取失败", "path": path}
    return {
        "ok": True,
        "kind": kind,
        "entity_id": _entity_id_from_doc(doc, _rel_to_package(path, package_dir)),
        "rel_path": _rel_to_package(path, package_dir),
        "path": path,
        "doc": doc,
        "references": scan_entity_references(package_dir, _entity_id_from_doc(doc, _rel_to_package(path, package_dir)), _rel_to_package(path, package_dir)),
    }


def _upsert_array_entity(doc, array_key, id_keys, entity_id, entity_doc):
    rows = doc.setdefault(array_key, [])
    target_index = -1
    for index, row in enumerate(rows):
        if _id(row, *id_keys) == entity_id:
            target_index = index
            break
    if target_index >= 0:
        rows[target_index] = entity_doc
    else:
        rows.append(entity_doc)


def register_file_entity_in_object_manifest(package_dir, kind, entity_id, rel_path, doc):
    twin_path = os.path.join(package_dir, "object", "twin_object.json")
    twin = try_load_json(twin_path) or {}
    if not twin:
        return
    if kind == "workflow":
        register_workflow_in_object_manifest(package_dir, entity_id, os.path.join(package_dir, rel_path.replace("/", os.sep)), doc)
        return
    if kind == "run_profile":
        entries = twin.setdefault("run_profiles", [])
        rel_from_object = os.path.relpath(os.path.join(package_dir, rel_path.replace("/", os.sep)), os.path.dirname(twin_path)).replace("\\", "/")
        entry = next((item for item in entries if isinstance(item, dict) and _id(item, "profile_id", "id") == entity_id), None)
        if entry is None:
            entries.append({"profile_id": entity_id, "path": rel_from_object})
        else:
            entry["path"] = rel_from_object
        write_json_atomic(twin_path, twin)


def object_entity_save(payload):
    payload = payload or {}
    kind = str(payload.get("kind") or payload.get("entity_kind") or "").strip()
    entity_id = str(payload.get("entity_id") or payload.get("id") or "").strip()
    doc = payload.get("doc")
    if not kind or not isinstance(doc, dict):
        return {"ok": False, "message": "缺少 kind 或 doc"}
    if not entity_id:
        entity_id = _entity_id_from_doc(doc)
    draft_id, package_dir, error = ensure_editable_draft(payload)
    if error:
        return error
    if kind == "component":
        twin_path = os.path.join(package_dir, "object", "twin_object.json")
        twin = try_load_json(twin_path) or {}
        doc["component_id"] = entity_id
        _upsert_array_entity(twin, "components", ("component_id", "id"), entity_id, doc)
        write_json_atomic(twin_path, twin)
        touch_object_draft_manifest(draft_id, "component", entity_id)
        return {"ok": True, "message": "组件已保存到对象草稿", "draft_id": draft_id, "object": object_summary(package_dir), "entity": doc}
    if kind == "resource":
        doc["resource_id"] = entity_id
        return save_resource_draft({**payload, "resource_id": entity_id, "resource": doc, "create": True, "draft_id": draft_id})
    if kind == "asset_group":
        assets_path = os.path.join(package_dir, "assets", "resources.json")
        assets = try_load_json(assets_path) or {"asset_groups": []}
        doc["group_id"] = entity_id
        _upsert_array_entity(assets, "asset_groups", ("group_id", "id"), entity_id, doc)
        write_json_atomic(assets_path, assets)
        touch_object_draft_manifest(draft_id, "asset_group", entity_id)
        return {"ok": True, "message": "资源分组已保存到对象草稿", "draft_id": draft_id, "object": object_summary(package_dir), "entity": doc}
    if kind == "domain_schema":
        requested_schema_id = _id(doc, "schema_id", "id")
        validation = validate_domain_schema_doc(doc)
        if not validation.get("ok"):
            return {"ok": False, "message": "domain schema 未通过 Phase2 必填项校验", "diagnostics": validation.get("diagnostics", [])}
        if requested_schema_id and entity_id and requested_schema_id != entity_id:
            own_rel = str(payload.get("rel_path") or _entity_default_rel_path(kind, entity_id)).strip()
            refs = scan_entity_references(package_dir, entity_id, own_rel)
            if refs.get("count"):
                return {
                    "ok": False,
                    "message": "schema_id 已被其他文件引用，当前版本禁止直接改名；请后续通过引用重命名流程处理",
                    "references": refs,
                    "old_schema_id": entity_id,
                    "new_schema_id": requested_schema_id,
                }
            entity_id = requested_schema_id
        doc["schema_id"] = entity_id
    if kind == "operator":
        requested_operator_id = _id(doc, "operator_id", "id")
        own_rel = str(payload.get("rel_path") or "").strip()
        if requested_operator_id and entity_id and requested_operator_id != entity_id:
            refs = scan_entity_references(package_dir, entity_id, own_rel or _entity_default_rel_path(kind, entity_id))
            if refs.get("count"):
                return {
                    "ok": False,
                    "message": "operator_id 已被 workflow 等对象包文件引用，当前版本禁止直接改名；请后续通过引用重命名流程处理",
                    "references": refs,
                    "old_operator_id": entity_id,
                    "new_operator_id": requested_operator_id,
                }
            entity_id = requested_operator_id
        doc["operator_id"] = entity_id
        validation = validate_operator_doc(doc, package_dir, own_rel)
        if not validation.get("ok"):
            return {"ok": False, "message": "operator spec 未通过 Phase3 release gate", "diagnostics": validation.get("diagnostics", [])}
    if kind == "run_profile":
        requested_profile_id = _id(doc, "profile_id", "id")
        if requested_profile_id and entity_id and requested_profile_id != entity_id:
            refs = runtime_profile_references_run_profile(package_dir, entity_id)
            if refs:
                return {
                    "ok": False,
                    "message": "run profile 已被 runtime profile 引用，当前版本禁止直接改名；请先解除 runtime profile 绑定",
                    "references": {"count": len(refs), "files": refs},
                    "old_profile_id": entity_id,
                    "new_profile_id": requested_profile_id,
                }
            entity_id = requested_profile_id
        doc = normalize_run_profile_doc(doc, entity_id)
        validation = validate_run_profile_doc(doc, package_dir)
        if not validation.get("ok"):
            return {"ok": False, "message": "run profile 未通过 Phase5 引用校验", "diagnostics": validation.get("diagnostics", [])}
    if kind == "runtime_profile":
        validation = validate_runtime_profile_doc(doc, package_dir)
        if not validation.get("ok"):
            return {"ok": False, "message": "runtime profile 未通过 Phase5 引用校验", "diagnostics": validation.get("diagnostics", [])}

    rel_path = str(payload.get("rel_path") or _entity_default_rel_path(kind, entity_id)).strip()
    path = _safe_package_path(package_dir, rel_path)
    if not path:
        return {"ok": False, "message": "目标路径非法", "rel_path": rel_path}
    write_json_atomic(path, doc)
    register_file_entity_in_object_manifest(package_dir, kind, entity_id, _rel_to_package(path, package_dir), doc)
    touch_object_draft_manifest(draft_id, kind, entity_id)
    return {
        "ok": True,
        "message": "实体已保存到对象草稿",
        "draft_id": draft_id,
        "kind": kind,
        "entity_id": entity_id,
        "rel_path": _rel_to_package(path, package_dir),
        "path": path,
        "object": object_summary(package_dir),
        "tree": object_model_tree({"draft_id": draft_id}),
    }


def scan_entity_references(package_dir, entity_id, own_rel_path=""):
    entity_id = str(entity_id or "").strip()
    if not entity_id or not package_dir:
        return {"count": 0, "files": []}
    files = []
    if isinstance(own_rel_path, (list, tuple, set)):
        own_rel_paths = {str(item or "").replace("\\", "/") for item in own_rel_path}
    else:
        own_rel_paths = {str(own_rel_path or "").replace("\\", "/")}
    for current_root, _dirs, names in os.walk(package_dir):
        if any(part in (".git", "__pycache__") for part in current_root.split(os.sep)):
            continue
        for name in names:
            if not name.endswith(".json"):
                continue
            path = os.path.join(current_root, name)
            rel = _rel_to_package(path, package_dir)
            if rel in own_rel_paths:
                continue
            try:
                with open(path, "r", encoding="utf-8-sig", errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            if entity_id in text:
                files.append({"rel_path": rel, "path": path})
    return {"count": len(files), "files": files[:80]}


def object_entity_delete(payload):
    payload = payload or {}
    kind = str(payload.get("kind") or payload.get("entity_kind") or "").strip()
    entity_id = str(payload.get("entity_id") or payload.get("id") or "").strip()
    force = bool(payload.get("force"))
    draft_id, package_dir, error = ensure_editable_draft(payload)
    if error:
        return error
    own_rel = str(payload.get("rel_path") or _entity_default_rel_path(kind, entity_id)).strip()
    if kind == "resource":
        own_rel = ["object/twin_object.json", "assets/resources.json"]
    if kind == "run_profile":
        runtime_refs = runtime_profile_references_run_profile(package_dir, entity_id)
        if runtime_refs:
            return {
                "ok": False,
                "message": "该 run profile 正被 runtime profile 使用，必须先解除绑定后再删除",
                "references": {"count": len(runtime_refs), "files": runtime_refs},
            }
    refs = scan_entity_references(package_dir, entity_id, own_rel)
    if refs.get("count") and not force:
        return {
            "ok": False,
            "message": "该实体仍被其他对象包文件引用，未删除；确认后可 force 删除",
            "references": refs,
        }
    if kind == "resource":
        return delete_resource_draft({**payload, "resource_id": entity_id, "draft_id": draft_id})
    if kind == "component":
        twin_path = os.path.join(package_dir, "object", "twin_object.json")
        twin = try_load_json(twin_path) or {}
        twin["components"] = [row for row in as_list(twin.get("components")) if _id(row, "component_id", "id") != entity_id]
        write_json_atomic(twin_path, twin)
        touch_object_draft_manifest(draft_id, "component_delete", entity_id)
        return {"ok": True, "message": "组件已从对象草稿删除", "draft_id": draft_id, "object": object_summary(package_dir)}
    if kind == "asset_group":
        assets_path = os.path.join(package_dir, "assets", "resources.json")
        assets = try_load_json(assets_path) or {"asset_groups": []}
        assets["asset_groups"] = [row for row in as_list(assets.get("asset_groups")) if _id(row, "group_id", "id") != entity_id]
        write_json_atomic(assets_path, assets)
        touch_object_draft_manifest(draft_id, "asset_group_delete", entity_id)
        return {"ok": True, "message": "资源分组已从对象草稿删除", "draft_id": draft_id, "object": object_summary(package_dir)}
    path = _safe_package_path(package_dir, own_rel)
    if not path or not os.path.isfile(path):
        return {"ok": False, "message": "实体文件不存在", "rel_path": own_rel}
    os.remove(path)
    if kind == "run_profile":
        twin_path = os.path.join(package_dir, "object", "twin_object.json")
        twin = try_load_json(twin_path) or {}
        twin["run_profiles"] = [
            row for row in as_list(twin.get("run_profiles"))
            if _id(row, "profile_id", "id") != entity_id
        ]
        write_json_atomic(twin_path, twin)
    touch_object_draft_manifest(draft_id, "%s_delete" % kind, entity_id)
    return {"ok": True, "message": "实体文件已从对象草稿删除", "draft_id": draft_id, "rel_path": own_rel, "object": object_summary(package_dir)}


def create_object_project(payload):
    global OBJECT_PACKAGE, ACTIVE_OBJECT_DRAFT_ID
    payload = payload or {}
    object_id = _safe_id(payload.get("object_id") or "new_object")
    if not object_id:
        object_id = "new_object"
    display_name = str(payload.get("display_name") or object_id)
    project_root = os.path.join(OBJECT_PROJECT_ROOT, object_id)
    package_dir = os.path.join(project_root, "package")
    if os.path.exists(package_dir) and not payload.get("overwrite"):
        return {"ok": False, "message": "对象工程已存在", "package_dir": package_dir}
    for rel in ("object", "assets", "domain_schemas", "operators", "workflows", "run_profiles", "runtime", "test_vectors", "manifests", "docs"):
        os.makedirs(os.path.join(package_dir, rel), exist_ok=True)
    now = _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"
    project = {
        "schema_version": "flightenv.webui.object_project.v1",
        "project_id": "%s.project" % object_id,
        "object_id": object_id,
        "display_name": display_name,
        "created_at_utc": now,
        "updated_at_utc": now,
        "package_layout": "flightenv.object_package.v1",
        "authoring_model": "cae_like_object_package_source",
    }
    twin = {
        "object_id": object_id,
        "object_type": payload.get("object_type") or "flightenv.generic_object",
        "schema_version": "object_manifest.v1",
        "display_name": display_name,
        "platform_runtime_profile": "../runtime/platform_runtime_profile.json",
        "components": [],
        "resources": [],
        "workflows": [],
        "run_profiles": [],
    }
    assets = {"object_id": object_id, "asset_groups": []}
    runtime_profile = {
        "schema_version": "flightenv.object.platform_runtime_profile.v1",
        "description": "Object-owned runtime/UI profile created by FlightEnv WebUI modeler.",
        "workflow_roles": [],
        "branch_templates": {},
        "field_display_roles": [],
    }
    write_json_atomic(os.path.join(package_dir, "object_project.json"), project)
    write_json_atomic(os.path.join(package_dir, "object", "twin_object.json"), twin)
    write_json_atomic(os.path.join(package_dir, "assets", "resources.json"), assets)
    write_json_atomic(os.path.join(package_dir, "runtime", "platform_runtime_profile.json"), runtime_profile)
    OBJECT_PACKAGE = package_dir
    ACTIVE_OBJECT_DRAFT_ID = ""
    return {
        "ok": True,
        "message": "对象工程已创建并载入",
        "package_dir": package_dir,
        "project": project,
        "workspace": workspace_view(),
        "object": object_summary(package_dir),
        "tree": object_model_tree({}),
    }


def object_modeler_delivery_status(package_dir, summary=None):
    summary = summary or object_summary(package_dir)
    compiled = _compiled_workflow_nodes()
    runs = _run_package_nodes()
    checks = [
        ("project_create", True, "可通过 /api/object-project/create 新建本地对象工程"),
        ("open_existing", bool(summary.get("object_loaded")), "可载入当前对象包并生成语义树"),
        ("resource_crud", True, "资源声明支持草稿保存和引用保护删除"),
        ("operator_crud", True, "算子 spec 支持 JSON 草稿保存"),
        ("workflow_crud", True, "workflow 图编辑器和 JSON 草稿保存共用对象草稿"),
        ("standard_json_layout", bool(package_dir), "保存目标为对象包标准 JSON 目录"),
        ("pdk_validate", True, "对象包/workflow 校验接口可由 UI 调用"),
        ("pdk_compile", True, "workflow 编译接口生成 execution/time/scheduler/data-plane plan"),
        ("runtime_compiled_only", True, "运行前 preflight 要求编译计划文件存在"),
        ("run_replay_attach", True, "运行包会挂载到对象工程树 run_packages"),
        ("automation_smoke", os.path.isfile(os.path.join(ROOT, "tools", "verify_webui_object_modeler.ps1")), "自动化验收脚本存在"),
        ("cloud_visualization", bool(runs) or True, "云图入口复用运行检视器 Field3D/data-plane artifact"),
    ]
    return {
        "ok": all(item[1] for item in checks),
        "checks": [{"id": cid, "ok": bool(ok), "note": note} for cid, ok, note in checks],
        "compiled_count": len(compiled),
        "run_count": len(runs),
    }


def workflow_dag(workflow_id, package_dir=None):
    package_dir = package_dir or active_object_package()
    if not OBJECT_PACKAGE:
        return {"ok": False, "message": "尚未载入对象包，无法读取 workflow"}
    wf_dir = os.path.join(package_dir, "workflows")
    target = None
    for fn in os.listdir(wf_dir) if os.path.isdir(wf_dir) else []:
        if not fn.endswith(".json"):
            continue
        wf = try_load_json(os.path.join(wf_dir, fn)) or {}
        if wf.get("workflow_id") == workflow_id or fn.startswith(workflow_id):
            target = wf
            break
    if target is None:
        return {"ok": False, "message": "workflow not found"}

    stages, nodes, edges = [], [], []

    def q(stage, node):
        return f"{stage}.{node}"

    for ph in target.get("phases", []) or []:
        for st in ph.get("stages", []) or []:
            sid = st.get("stage_id")
            if not sid:
                continue
            if sid not in stages:
                stages.append(sid)
            fam = st.get("stage_family")
            sg = st.get("subgraph", {}) or {}
            for nd in sg.get("nodes", []) or []:
                nodes.append({
                    "id": q(sid, nd.get("node_id")),
                    "label": nd.get("node_id"),
                    "stage": sid,
                    "family": fam,
                    "operator_ref": nd.get("operator_ref"),
                    "phase": ph.get("phase_id", ""),
                })
            for ed in sg.get("edges", []) or []:
                fr = (ed.get("from") or {}).get("node_id")
                to = (ed.get("to") or {}).get("node_id")
                if fr and to:
                    edges.append({"from": q(sid, fr), "to": q(sid, to)})
        for ed in ph.get("stage_edges", []) or []:
            fr, to = ed.get("from", {}), ed.get("to", {})
            edges.append({"from": q(fr.get("stage_id"), fr.get("node_id")),
                          "to": q(to.get("stage_id"), to.get("node_id"))})
    return {
        "ok": True,
        "workflow_id": target.get("workflow_id", workflow_id),
        "phase": target.get("phase", ""),
        "stages": stages,
        "nodes": nodes,
        "edges": edges,
    }


def _safe_id(value):
    text = str(value or "workflow").strip() or "workflow"
    return "".join(c if c.isalnum() or c in ("-", "_", ".") else "_" for c in text)[:180]


def _workflow_file(workflow_id, package_dir=None):
    package_dir = package_dir or active_object_package()
    wf_dir = os.path.join(package_dir, "workflows")
    for fn in os.listdir(wf_dir) if os.path.isdir(wf_dir) else []:
        if not fn.endswith(".json"):
            continue
        path = os.path.join(wf_dir, fn)
        wf = try_load_json(path) or {}
        if wf.get("workflow_id") == workflow_id or fn.startswith(workflow_id):
            return path, fn, wf
    return None, None, None


def _operator_specs(package_dir=None):
    package_dir = package_dir or active_object_package()
    out = {}
    op_dir = os.path.join(package_dir, "operators")
    if not os.path.isdir(op_dir):
        return out
    for fn in os.listdir(op_dir):
        if not fn.endswith(".atomic.json"):
            continue
        op = try_load_json(os.path.join(op_dir, fn)) or {}
        oid = op.get("operator_id")
        if oid:
            out[oid] = op
    return out


def _port_map(ports):
    return {p.get("port_id"): p for p in (ports or []) if isinstance(p, dict) and p.get("port_id")}


def _port_contract(port):
    if not isinstance(port, dict):
        return {}
    typed = port.get("typed_io_contract") or {}
    return {
        "contract_id": port.get("contract_id") or typed.get("schema_id") or "",
        "frame_contract": port.get("frame_contract") or "",
        "value_kind": port.get("value_kind") or "",
        "dto_name": typed.get("dto_name") or typed.get("type_name") or "",
    }


def _contracts_compatible(source, target):
    source = source or {}
    target = target or {}
    source_contract = source.get("contract_id") or ""
    target_contract = target.get("contract_id") or ""
    if not source_contract or not target_contract:
        return True
    if source_contract == target_contract:
        return True
    source_frame = source.get("frame_contract") or ""
    compatible_frame = {
        "platform.state_snapshot.v1": "StateSnapshot.v1",
        "platform.observation_snapshot.v1": "ObservationSnapshot.v1",
        "platform.event_snapshot.v1": "EventSnapshot.v1",
    }
    return compatible_frame.get(target_contract) == source_frame


def _workflow_ref_parts(ref):
    text = str(ref or "").strip()
    dot = text.find(".")
    if dot <= 0 or dot >= len(text) - 1:
        return None
    return {"node_id": text[:dot], "port_id": text[dot + 1:]}


def _input_requirement_satisfied(op, node_id, port_id, supplied_ports):
    req = op.get("input_requirements", {}) or {}
    for group in req.get("required_any", []) or []:
        ports = [p for p in (group.get("ports", []) or []) if p]
        if port_id not in ports:
            continue
        return any("%s.%s" % (node_id, alt_port) in supplied_ports for alt_port in ports)
    return False


def _edge_endpoint(edge, side):
    value = edge.get(side) or {}
    if not isinstance(value, dict):
        return {}
    return value


def _edge_key(edge, index):
    fr = _edge_endpoint(edge, "from")
    to = _edge_endpoint(edge, "to")
    return "%s:%s:%s:%s:%d" % (
        fr.get("node_id", ""),
        fr.get("port_id", ""),
        to.get("node_id", ""),
        to.get("port_id", ""),
        index,
    )


def _workflow_draft_roots():
    roots = []
    for root in (DRAFT_ROOT, DRAFT_FALLBACK_ROOT):
        if not root:
            continue
        abs_root = os.path.abspath(root)
        if abs_root not in roots:
            roots.append(abs_root)
    return roots


def _find_workflow_draft(draft_id):
    name = _safe_id(draft_id) + ".json"
    for root in _workflow_draft_roots():
        path = os.path.join(root, name)
        if os.path.exists(path):
            return path
    return os.path.join(os.path.abspath(DRAFT_ROOT), name)


def _write_workflow_draft(root, draft_id, workflow):
    os.makedirs(root, exist_ok=True)
    path = os.path.join(root, _safe_id(draft_id) + ".json")
    tmp_path = path + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(workflow, f, ensure_ascii=False, indent=2)
        f.write("\n")
    os.replace(tmp_path, path)
    return path


def validate_workflow_doc(workflow, package_dir=None):
    errors, warnings, diagnostics = [], [], []

    def issue(severity, code, message, **extra):
        row = {"severity": severity, "code": code, "message": message}
        row.update({k: v for k, v in extra.items() if v not in (None, "")})
        diagnostics.append(row)
        if severity == "blocking":
            errors.append(message)
        else:
            warnings.append(message)

    if not isinstance(workflow, dict):
        issue("blocking", "workflow_not_object", "workflow must be an object")
        return {"ok": False, "errors": errors, "warnings": warnings, "diagnostics": diagnostics, "summary": {}}

    ops = _operator_specs(package_dir)
    node_count = edge_count = internal_edge_count = stage_edge_count = stage_count = 0
    workflow_id_text = str(workflow.get("workflow_id") or "").lower()
    workflow_phase_text = str(workflow.get("phase") or "").lower()
    phase_ids_text = " ".join(str(phase.get("phase_id") or "") for phase in workflow.get("phases", []) or []).lower()
    is_future_workflow = any(
        marker in " ".join([workflow_id_text, workflow_phase_text, phase_ids_text])
        for marker in ("future", "posterior_future_prediction")
    )
    if is_future_workflow:
        stop_policy = workflow.get("stop_policy") or workflow.get("stop_guard") or workflow.get("termination_policy") or {}
        if not isinstance(stop_policy, dict) or not stop_policy:
            issue(
                "blocking",
                "workflow_future_stop_guard_missing",
                "future prediction workflow must declare stop_policy / stop_guard / termination_policy",
                workflow_id=workflow.get("workflow_id", ""),
            )

    for phase in workflow.get("phases", []) or []:
        phase_id = phase.get("phase_id", "")
        stages = phase.get("stages", []) or []
        phase_stage_ids = {stage.get("stage_id", "") for stage in stages}
        phase_node_ids = {}
        phase_nodes_by_stage = {}
        external_in_ports, external_in_nodes, external_out_nodes = {}, {}, {}

        for stage in stages:
            stage_id = stage.get("stage_id", "")
            stage_nodes = {
                node.get("node_id", ""): node
                for node in ((stage.get("subgraph", {}) or {}).get("nodes", []) or [])
                if node.get("node_id")
            }
            phase_nodes_by_stage[stage_id] = stage_nodes
            phase_node_ids[stage_id] = set(stage_nodes.keys())

        for idx, edge in enumerate(phase.get("stage_edges", []) or []):
            stage_edge_count += 1
            edge_count += 1
            edge_id = _edge_key(edge, idx)
            fr, to = _edge_endpoint(edge, "from"), _edge_endpoint(edge, "to")
            src_stage, dst_stage = fr.get("stage_id", ""), to.get("stage_id", "")
            src_node, dst_node = fr.get("node_id", ""), to.get("node_id", "")
            src_port, dst_port = fr.get("port_id", ""), to.get("port_id", "")

            if src_stage not in phase_stage_ids:
                issue("blocking", "workflow_stage_edge_source_stage_missing", "stage_edge '%s' source stage '%s' not found in phase '%s'" % (edge_id, src_stage, phase_id), edge_id=edge_id, stage_id=src_stage)
                continue
            if dst_stage not in phase_stage_ids:
                issue("blocking", "workflow_stage_edge_target_stage_missing", "stage_edge '%s' target stage '%s' not found in phase '%s'" % (edge_id, dst_stage, phase_id), edge_id=edge_id, stage_id=dst_stage)
                continue
            if src_node and src_node not in phase_node_ids.get(src_stage, set()):
                issue("blocking", "workflow_stage_edge_source_node_missing", "stage_edge '%s' source node '%s' not found in stage '%s'" % (edge_id, src_node, src_stage), edge_id=edge_id, node_id=src_node)
                continue
            if dst_node and dst_node not in phase_node_ids.get(dst_stage, set()):
                issue("blocking", "workflow_stage_edge_target_node_missing", "stage_edge '%s' target node '%s' not found in stage '%s'" % (edge_id, dst_node, dst_stage), edge_id=edge_id, node_id=dst_node)
                continue

            src_op = ops.get((phase_nodes_by_stage.get(src_stage, {}).get(src_node) or {}).get("operator_ref"), {})
            dst_op = ops.get((phase_nodes_by_stage.get(dst_stage, {}).get(dst_node) or {}).get("operator_ref"), {})
            src_outputs = _port_map(src_op.get("outputs", []))
            dst_inputs = _port_map(dst_op.get("inputs", []))
            src_contract = _port_contract(src_outputs.get(src_port)).get("contract_id")
            dst_contract = _port_contract(dst_inputs.get(dst_port)).get("contract_id")
            src_contract_info = _port_contract(src_outputs.get(src_port))
            dst_contract_info = _port_contract(dst_inputs.get(dst_port))
            if src_node and src_port and src_port not in src_outputs:
                issue("blocking", "workflow_stage_edge_source_port_missing", "stage_edge '%s' source port '%s' is not an output of '%s'" % (edge_id, src_port, src_node), edge_id=edge_id, port_id=src_port)
            if dst_node and dst_port and dst_port not in dst_inputs:
                issue("blocking", "workflow_stage_edge_target_port_missing", "stage_edge '%s' target port '%s' is not an input of '%s'" % (edge_id, dst_port, dst_node), edge_id=edge_id, port_id=dst_port)
            if src_contract and dst_contract and not _contracts_compatible(src_contract_info, dst_contract_info):
                issue("blocking", "workflow_stage_edge_contract_mismatch", "stage_edge '%s' contract mismatch: %s -> %s" % (edge_id, src_contract, dst_contract), edge_id=edge_id, source_contract=src_contract, target_contract=dst_contract)

            external_out_nodes.setdefault(src_stage, set()).add(src_node)
            external_in_nodes.setdefault(dst_stage, set()).add(dst_node)
            if dst_node and dst_port:
                external_in_ports.setdefault(dst_stage, set()).add("%s.%s" % (dst_node, dst_port))

        for stage in stages:
            stage_count += 1
            stage_id = stage.get("stage_id", "")
            if not stage_id:
                issue("blocking", "workflow_stage_id_missing", "phase '%s' has a stage without stage_id" % phase_id, phase_id=phase_id)
            subgraph = stage.get("subgraph", {}) or {}
            nodes = subgraph.get("nodes", []) or []
            edges = subgraph.get("edges", []) or []
            stage_inputs = set(subgraph.get("stage_inputs", []) or [])
            stage_outputs = set(subgraph.get("stage_outputs", []) or [])
            node_by_id = {}
            adjacency = {}

            for node in nodes:
                node_count += 1
                node_id = node.get("node_id")
                op_id = node.get("operator_ref")
                if not node_id:
                    issue("blocking", "workflow_node_id_missing", "stage '%s' has a node without node_id" % stage_id, stage_id=stage_id)
                    continue
                if node_id in node_by_id:
                    issue("blocking", "workflow_node_id_duplicate", "stage '%s' has duplicate node_id '%s'" % (stage_id, node_id), stage_id=stage_id, node_id=node_id)
                node_by_id[node_id] = node
                adjacency.setdefault(node_id, [])
                if not op_id:
                    issue("blocking", "workflow_node_operator_missing", "node '%s.%s' has no operator_ref" % (stage_id, node_id), stage_id=stage_id, node_id=node_id)
                elif op_id not in ops:
                    issue("blocking", "workflow_node_operator_unknown", "node '%s.%s' references unknown operator '%s'" % (stage_id, node_id, op_id), stage_id=stage_id, node_id=node_id, operator_id=op_id)
                else:
                    op = ops.get(op_id, {})
                    op_family = op.get("operator_family") or op.get("family") or ""
                    stage_family = stage.get("stage_family") or ""
                    if stage_family and op_family and stage_family != op_family:
                        issue(
                            "blocking",
                            "workflow_stage_family_mismatch",
                            "stage '%s' family '%s' does not match operator '%s' family '%s'" % (stage_id, stage_family, op_id, op_family),
                            stage_id=stage_id,
                            node_id=node_id,
                            operator_id=op_id,
                            stage_family=stage_family,
                            operator_family=op_family,
                        )

            for ref in sorted(stage_inputs):
                parts = _workflow_ref_parts(ref)
                if not parts or parts["node_id"] not in node_by_id:
                    issue("blocking", "workflow_stage_input_ref_invalid", "stage '%s' input ref '%s' must point to node_id.input_port" % (stage_id, ref), stage_id=stage_id, port_ref=ref)
                    continue
                op = ops.get(node_by_id[parts["node_id"]].get("operator_ref"), {})
                if parts["port_id"] not in _port_map(op.get("inputs", [])):
                    issue("blocking", "workflow_stage_input_port_missing", "stage '%s' input ref '%s' is not an operator input port" % (stage_id, ref), stage_id=stage_id, port_ref=ref)
            for ref in sorted(stage_outputs):
                parts = _workflow_ref_parts(ref)
                if not parts or parts["node_id"] not in node_by_id:
                    issue("blocking", "workflow_stage_output_ref_invalid", "stage '%s' output ref '%s' must point to node_id.output_port" % (stage_id, ref), stage_id=stage_id, port_ref=ref)
                    continue
                op = ops.get(node_by_id[parts["node_id"]].get("operator_ref"), {})
                if parts["port_id"] not in _port_map(op.get("outputs", [])):
                    issue("blocking", "workflow_stage_output_port_missing", "stage '%s' output ref '%s' is not an operator output port" % (stage_id, ref), stage_id=stage_id, port_ref=ref)

            incoming_ports = set(external_in_ports.get(stage_id, set()))
            incoming_nodes = set(external_in_nodes.get(stage_id, set()))
            outgoing_nodes = set(external_out_nodes.get(stage_id, set()))
            for idx, edge in enumerate(edges):
                internal_edge_count += 1
                edge_count += 1
                edge_id = _edge_key(edge, idx)
                fr, to = _edge_endpoint(edge, "from"), _edge_endpoint(edge, "to")
                src_node, dst_node = fr.get("node_id"), to.get("node_id")
                src_port, dst_port = fr.get("port_id"), to.get("port_id")
                if not src_node or not dst_node:
                    issue("blocking", "workflow_edge_node_missing", "stage '%s' edge #%d misses node_id" % (stage_id, idx), stage_id=stage_id, edge_id=edge_id)
                    continue
                if src_node not in node_by_id:
                    issue("blocking", "workflow_edge_source_node_missing", "edge '%s' source node '%s' not found in stage '%s'" % (edge_id, src_node, stage_id), edge_id=edge_id, node_id=src_node)
                    continue
                if dst_node not in node_by_id:
                    issue("blocking", "workflow_edge_target_node_missing", "edge '%s' target node '%s' not found in stage '%s'" % (edge_id, dst_node, stage_id), edge_id=edge_id, node_id=dst_node)
                    continue
                adjacency.setdefault(src_node, []).append(dst_node)
                outgoing_nodes.add(src_node)
                incoming_nodes.add(dst_node)
                incoming_ports.add("%s.%s" % (dst_node, dst_port))

                src_op = ops.get(node_by_id[src_node].get("operator_ref"), {})
                dst_op = ops.get(node_by_id[dst_node].get("operator_ref"), {})
                src_outputs = _port_map(src_op.get("outputs", []))
                dst_inputs = _port_map(dst_op.get("inputs", []))
                src_contract = _port_contract(src_outputs.get(src_port)).get("contract_id")
                dst_contract = _port_contract(dst_inputs.get(dst_port)).get("contract_id")
                src_contract_info = _port_contract(src_outputs.get(src_port))
                dst_contract_info = _port_contract(dst_inputs.get(dst_port))
                if src_port and src_port not in src_outputs:
                    issue("blocking", "workflow_edge_source_port_missing", "edge '%s' source port '%s' is not an output of '%s'" % (edge_id, src_port, src_node), edge_id=edge_id, port_id=src_port)
                if dst_port and dst_port not in dst_inputs:
                    issue("blocking", "workflow_edge_target_port_missing", "edge '%s' target port '%s' is not an input of '%s'" % (edge_id, dst_port, dst_node), edge_id=edge_id, port_id=dst_port)
                if src_contract and dst_contract and not _contracts_compatible(src_contract_info, dst_contract_info):
                    issue("blocking", "workflow_edge_contract_mismatch", "edge '%s' contract mismatch: %s -> %s" % (edge_id, src_contract, dst_contract), edge_id=edge_id, source_contract=src_contract, target_contract=dst_contract)
                if not src_contract or not dst_contract:
                    issue("warning", "workflow_edge_contract_incomplete", "edge '%s' has incomplete contract metadata; checked port existence only" % edge_id, edge_id=edge_id)

            visiting, visited = set(), set()

            def dfs(node_id):
                if node_id in visiting:
                    return True
                if node_id in visited:
                    return False
                visiting.add(node_id)
                for nxt in adjacency.get(node_id, []):
                    if dfs(nxt):
                        return True
                visiting.remove(node_id)
                visited.add(node_id)
                return False

            if any(dfs(nid) for nid in list(adjacency.keys())):
                issue("blocking", "workflow_stage_cycle", "stage '%s' contains a cycle; declare an explicit feedback policy before saving as runnable workflow" % stage_id, stage_id=stage_id)

            for node_id, node in node_by_id.items():
                op = ops.get(node.get("operator_ref"), {})
                if (len(nodes) > 1
                        and node_id not in incoming_nodes
                        and node_id not in outgoing_nodes
                        and (op.get("inputs") or [])
                        and (op.get("outputs") or [])):
                    issue("warning", "workflow_node_unconnected", "node '%s.%s' is not connected; keep it in the operator palette unless it is an intentional draft" % (stage_id, node_id), stage_id=stage_id, node_id=node_id)
                for port in op.get("inputs", []) or []:
                    if not port.get("required", False):
                        continue
                    key = "%s.%s" % (node_id, port.get("port_id"))
                    supplied_ports = incoming_ports | stage_inputs
                    if key not in supplied_ports and not _input_requirement_satisfied(op, node_id, port.get("port_id"), supplied_ports):
                        issue("warning", "workflow_required_input_not_supplied", "required input '%s.%s' is not connected; it must be provided by stage_inputs or an upstream edge" % (stage_id, key), stage_id=stage_id, port_ref=key)

    summary = {
        "workflow_id": workflow.get("workflow_id", ""),
        "stage_count": stage_count,
        "node_count": node_count,
        "edge_count": edge_count,
        "internal_edge_count": internal_edge_count,
        "stage_edge_count": stage_edge_count,
        "operator_count": len(ops),
    }
    return {"ok": not errors, "errors": errors, "warnings": warnings, "diagnostics": diagnostics, "summary": summary}


def _workflow_rows(workflow):
    phases, stages, nodes, edges = [], [], [], []
    if not isinstance(workflow, dict):
        return phases, stages, nodes, edges
    for phase in workflow.get("phases", []) or []:
        phase_id = phase.get("phase_id", "")
        phases.append({
            "phase_id": phase_id,
            "stage_count": len(phase.get("stages", []) or []),
        })
        for stage in phase.get("stages", []) or []:
            stage_id = stage.get("stage_id", "")
            subgraph = stage.get("subgraph", {}) or {}
            stage_nodes = subgraph.get("nodes", []) or []
            stage_edges = subgraph.get("edges", []) or []
            stages.append({
                "phase_id": phase_id,
                "stage_id": stage_id,
                "stage_family": stage.get("stage_family", ""),
                "node_count": len(stage_nodes),
                "edge_count": len(stage_edges),
                "operator_refs": stage.get("operator_refs", []),
            })
            for node in stage_nodes:
                nodes.append({
                    "phase_id": phase_id,
                    "stage_id": stage_id,
                    "node_id": node.get("node_id", ""),
                    "operator_ref": node.get("operator_ref", ""),
                    "activation_policy": node.get("activation_policy", {}),
                })
            for index, edge in enumerate(stage_edges):
                fr = _edge_endpoint(edge, "from")
                to = _edge_endpoint(edge, "to")
                edges.append({
                    "phase_id": phase_id,
                    "stage_id": stage_id,
                    "edge_id": _edge_key(edge, index),
                    "from_node_id": fr.get("node_id", ""),
                    "from_port_id": fr.get("port_id", ""),
                    "to_node_id": to.get("node_id", ""),
                    "to_port_id": to.get("port_id", ""),
                    "policy": edge.get("policy", {}),
                })
        for index, edge in enumerate(phase.get("stage_edges", []) or []):
            fr = _edge_endpoint(edge, "from")
            to = _edge_endpoint(edge, "to")
            edges.append({
                "phase_id": phase_id,
                "stage_id": "__stage_edge__",
                "edge_id": _edge_key(edge, index),
                "from_stage_id": fr.get("stage_id", ""),
                "from_node_id": fr.get("node_id", ""),
                "from_port_id": fr.get("port_id", ""),
                "to_stage_id": to.get("stage_id", ""),
                "to_node_id": to.get("node_id", ""),
                "to_port_id": to.get("port_id", ""),
                "policy": edge.get("policy", {}),
            })
    return phases, stages, nodes, edges


def workflow_studio(workflow_id, package_dir=None):
    raw = workflow_raw(workflow_id, package_dir=package_dir)
    if not raw.get("ok"):
        return raw
    workflow = raw.get("workflow", {})
    phases, stages, nodes, edges = _workflow_rows(workflow)
    return {
        "ok": True,
        "source": raw.get("source", ""),
        "path": raw.get("path", ""),
        "spec_file": raw.get("spec_file", ""),
        "workflow": workflow,
        "workflow_id": workflow.get("workflow_id", workflow_id),
        "phase": workflow.get("phase", ""),
        "description": workflow.get("description", ""),
        "clock": workflow.get("clock", {}),
        "solver_policy": workflow.get("solver_policy", {}),
        "scheduler_policy": workflow.get("scheduler_policy", {}),
        "branch_policy": workflow.get("branch_policy", workflow.get("branching_policy", {})),
        "checkpoint_policy": workflow.get("checkpoint_policy", {}),
        "phases": phases,
        "stages": stages,
        "nodes": nodes,
        "edges": edges,
        "validation": raw.get("validation", {}),
    }


def workflow_raw(workflow_id, draft_id="", package_dir=None):
    package_dir = package_dir or active_object_package()
    if draft_id:
        path = _find_workflow_draft(_safe_id(draft_id))
        doc = try_load_json(path) or None
        if doc is None:
            return {"ok": False, "message": "draft not found", "draft_id": draft_id}
        return {
            "ok": True,
            "source": "draft",
            "draft_id": draft_id,
            "path": path,
            "workflow": doc,
            "validation": validate_workflow_doc(doc, package_dir),
        }
    if not package_dir:
        return {"ok": False, "message": "尚未载入对象包，无法读取 workflow", "workflow_id": workflow_id}
    path, fn, doc = _workflow_file(workflow_id, package_dir)
    if doc is None:
        return {"ok": False, "message": "workflow not found", "workflow_id": workflow_id}
    return {
        "ok": True,
        "source": "object_package",
        "spec_file": fn,
        "path": path,
        "workflow": doc,
        "validation": validate_workflow_doc(doc, package_dir),
    }


def workflow_drafts():
    rows_by_id = {}
    for root in _workflow_draft_roots():
        try:
            os.makedirs(root, exist_ok=True)
            names = sorted(os.listdir(root))
        except OSError:
            continue
        for fn in names:
            if not fn.endswith(".json"):
                continue
            path = os.path.join(root, fn)
            doc = try_load_json(path) or {}
            rows_by_id.setdefault(fn[:-5], {
                "draft_id": fn[:-5],
                "workflow_id": doc.get("workflow_id", ""),
                "phase": doc.get("phase", ""),
                "updated_at": _dt.datetime.fromtimestamp(os.path.getmtime(path)).isoformat(timespec="seconds"),
                "path": path,
                "draft_root": root,
            })
    rows = list(rows_by_id.values())
    rows.sort(key=lambda x: x["updated_at"], reverse=True)
    return {"ok": True, "drafts": rows, "draft_root": DRAFT_ROOT, "fallback_draft_root": DRAFT_FALLBACK_ROOT}


def save_workflow_draft(payload):
    workflow = (payload or {}).get("workflow")
    if not isinstance(workflow, dict):
        return {"ok": False, "message": "payload.workflow must be an object"}
    object_draft_id, package_dir, error = ensure_editable_draft(payload or {})
    if error:
        return error
    validation = validate_workflow_doc(workflow, package_dir)
    workflow_id = workflow.get("workflow_id") or "workflow"
    draft_id = (payload or {}).get("draft_id") or "%s_%s" % (
        workflow_id,
        _dt.datetime.now().strftime("%Y%m%d_%H%M%S"),
    )
    draft_id = _safe_id(draft_id)
    path = None
    last_error = None
    for root in _workflow_draft_roots():
        try:
            path = _write_workflow_draft(root, draft_id, workflow)
            break
        except PermissionError as err:
            last_error = err
        except OSError as err:
            last_error = err
            if getattr(err, "errno", None) != 13:
                break
    if not path:
        return {"ok": False, "message": "failed to write workflow draft: %s" % last_error}
    object_draft_path = ""
    if object_draft_id and os.path.isdir(package_dir):
        wf_dir = os.path.join(package_dir, "workflows")
        os.makedirs(wf_dir, exist_ok=True)
        object_draft_path = os.path.join(wf_dir, _safe_id(workflow_id) + ".json")
        write_json_atomic(object_draft_path, workflow)
        register_workflow_in_object_manifest(package_dir, workflow_id, object_draft_path, workflow)
        touch_object_draft_manifest(object_draft_id, "workflow", workflow_id)
    return {
        "ok": True,
        "draft_id": draft_id,
        "workflow_id": workflow_id,
        "path": path,
        "object_draft_id": object_draft_id,
        "object_draft_path": object_draft_path,
        "object": object_summary(package_dir) if package_dir and os.path.isdir(package_dir) else {},
        "draft_root": os.path.dirname(path),
        "fallback_used": os.path.abspath(os.path.dirname(path)) != os.path.abspath(DRAFT_ROOT),
        "validation": validation,
    }


def register_workflow_in_object_manifest(package_dir, workflow_id, workflow_path, workflow):
    """Register a draft workflow in the object package manifest.

    The platform compiler resolves workflows through object/twin_object.json,
    so a workflow file alone is not enough to make a draft runnable.
    """
    if not package_dir or not workflow_id or not workflow_path:
        return False
    manifest_path = os.path.join(package_dir, "object", "twin_object.json")
    manifest = try_load_json(manifest_path) or {}
    if not manifest:
        return False
    entries = manifest.get("workflows")
    if not isinstance(entries, list):
        entries = []
        manifest["workflows"] = entries
    rel_path = os.path.relpath(workflow_path, os.path.dirname(manifest_path)).replace("\\", "/")
    entry = next((item for item in entries if isinstance(item, dict) and item.get("workflow_id") == workflow_id), None)
    if entry is None:
        entry = {"workflow_id": workflow_id}
        entries.append(entry)
    entry["path"] = rel_path
    if isinstance(workflow, dict):
        if workflow.get("phase"):
            entry["phase"] = workflow.get("phase")
        if workflow.get("description"):
            entry["description"] = workflow.get("description")
    write_json_atomic(manifest_path, manifest)
    return True


def object_draft_manifest_path(draft_id):
    return os.path.join(object_draft_dir(draft_id), "draft_manifest.json")


def read_object_draft_manifest(draft_id):
    return try_load_json(object_draft_manifest_path(draft_id)) or {}


def write_object_draft_manifest(draft_id, manifest):
    write_json_atomic(object_draft_manifest_path(draft_id), manifest)


def touch_object_draft_manifest(draft_id, change_kind="", target_id=""):
    if not draft_id:
        return
    manifest = read_object_draft_manifest(draft_id)
    manifest["updated_at_utc"] = _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"
    changes = manifest.setdefault("changes", [])
    if change_kind or target_id:
        changes.append({
            "time_utc": manifest["updated_at_utc"],
            "kind": change_kind,
            "target_id": target_id,
        })
    write_object_draft_manifest(draft_id, manifest)


def list_object_drafts():
    rows = []
    if OBJECT_PACKAGE:
        root = object_draft_dir("")
        if os.path.isdir(root):
            for name in sorted(os.listdir(root)):
                draft_id = _safe_id(name)
                manifest = read_object_draft_manifest(draft_id)
                pkg = object_draft_package_dir(draft_id)
                if os.path.isdir(pkg):
                    rows.append({
                        "draft_id": draft_id,
                        "status": manifest.get("status", "draft"),
                        "created_at_utc": manifest.get("created_at_utc", ""),
                        "updated_at_utc": manifest.get("updated_at_utc", ""),
                        "base_package": manifest.get("base_package", OBJECT_PACKAGE),
                        "package_dir": pkg,
                        "active": draft_id == ACTIVE_OBJECT_DRAFT_ID,
                        "change_count": len(manifest.get("changes", []) or []),
                    })
    rows.sort(key=lambda item: item.get("updated_at_utc") or item.get("created_at_utc"), reverse=True)
    return rows


def object_draft_status():
    active_pkg = active_object_package()
    return {
        "ok": True,
        "formal_package": OBJECT_PACKAGE,
        "active_draft_id": ACTIVE_OBJECT_DRAFT_ID,
        "active_package": active_pkg,
        "active_domain": package_domain(active_pkg),
        "draft_root": OBJECT_DRAFT_ROOT,
        "release_root": OBJECT_RELEASE_ROOT,
        "drafts": list_object_drafts(),
        "manifest": read_object_draft_manifest(ACTIVE_OBJECT_DRAFT_ID) if ACTIVE_OBJECT_DRAFT_ID else {},
    }


def create_object_draft(payload):
    global ACTIVE_OBJECT_DRAFT_ID
    if not OBJECT_PACKAGE:
        return {"ok": False, "message": "请先载入正式对象包"}
    validation = validate_object_package_dir(OBJECT_PACKAGE)
    if not validation.get("ok"):
        return {"ok": False, "message": "正式对象包校验未通过，不能创建草稿", "validation": validation}
    object_id = package_id_for(OBJECT_PACKAGE)
    draft_id = _safe_id((payload or {}).get("draft_id") or "%s_draft_%s" % (
        object_id,
        _dt.datetime.now().strftime("%Y%m%d_%H%M%S"),
    ))
    root = object_draft_dir(draft_id)
    pkg = object_draft_package_dir(draft_id)
    if os.path.exists(root) and not (payload or {}).get("overwrite"):
        return {"ok": False, "message": "草稿已存在", "draft_id": draft_id}
    copytree_clean(OBJECT_PACKAGE, pkg)
    now = _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"
    manifest = {
        "schema_version": "flightenv.webui.object_draft.v1",
        "draft_id": draft_id,
        "object_id": object_id,
        "status": "draft",
        "base_package": OBJECT_PACKAGE,
        "package_dir": pkg,
        "created_at_utc": now,
        "updated_at_utc": now,
        "created_by": "FlightEnv WebUI Phase 8",
        "purpose": (payload or {}).get("purpose", "platform_edit_closure"),
        "changes": [],
    }
    write_object_draft_manifest(draft_id, manifest)
    ACTIVE_OBJECT_DRAFT_ID = draft_id
    return {"ok": True, "message": "对象草稿已创建并激活", "draft": manifest, "status": object_draft_status(), "object": object_summary(pkg)}


def select_object_draft(payload):
    global ACTIVE_OBJECT_DRAFT_ID
    draft_id = _safe_id((payload or {}).get("draft_id") or "")
    if not draft_id:
        ACTIVE_OBJECT_DRAFT_ID = ""
        return {"ok": True, "message": "已切回正式对象包", "status": object_draft_status(), "object": object_summary()}
    pkg = object_draft_package_dir(draft_id)
    if not os.path.isdir(pkg):
        return {"ok": False, "message": "草稿对象包不存在", "draft_id": draft_id}
    ACTIVE_OBJECT_DRAFT_ID = draft_id
    touch_object_draft_manifest(draft_id, "select", draft_id)
    return {"ok": True, "message": "已激活对象草稿", "status": object_draft_status(), "object": object_summary(pkg)}


def discard_object_draft(payload):
    global ACTIVE_OBJECT_DRAFT_ID
    draft_id = _safe_id((payload or {}).get("draft_id") or ACTIVE_OBJECT_DRAFT_ID or "")
    if not draft_id:
        return {"ok": False, "message": "缺少 draft_id"}
    root = object_draft_dir(draft_id)
    if os.path.isdir(root):
        shutil.rmtree(root)
    if ACTIVE_OBJECT_DRAFT_ID == draft_id:
        ACTIVE_OBJECT_DRAFT_ID = ""
    return {"ok": True, "message": "对象草稿已删除", "draft_id": draft_id, "status": object_draft_status(), "object": object_summary()}


def ensure_editable_draft(payload=None):
    payload = payload or {}
    draft_id = _safe_id(payload.get("draft_id") or ACTIVE_OBJECT_DRAFT_ID or "")
    if draft_id and os.path.isdir(object_draft_package_dir(draft_id)):
        return draft_id, object_draft_package_dir(draft_id), None
    if payload.get("auto_create", True):
        created = create_object_draft({"purpose": "auto_created_for_edit"})
        if created.get("ok"):
            draft_id = created["draft"]["draft_id"]
            return draft_id, object_draft_package_dir(draft_id), None
        return "", "", created
    return "", "", {"ok": False, "message": "请先创建或选择对象草稿"}


def find_operator_file(package_dir, operator_id):
    op_dir = os.path.join(package_dir, "operators")
    if not os.path.isdir(op_dir):
        return "", {}
    for fn in sorted(os.listdir(op_dir)):
        if not fn.endswith(".atomic.json"):
            continue
        path = os.path.join(op_dir, fn)
        doc = try_load_json(path) or {}
        if doc.get("operator_id") == operator_id:
            return path, doc
    return "", {}


def _runtime_profile_doc(package_dir):
    return try_load_json(os.path.join(package_dir or "", "runtime", "platform_runtime_profile.json")) or {}


def _resolve_runtime_token(value, package_dir="", registry_path=""):
    text = str(value or "")
    registry_dir = os.path.dirname(os.path.abspath(registry_path)) if registry_path else ""
    return (
        text.replace("{workspace_root}", ROOT)
        .replace("{object_package}", os.path.abspath(package_dir or ""))
        .replace("{package_root}", os.path.abspath(package_dir or ""))
        .replace("{pdk_root}", os.path.join(ROOT, "flightenv-platform-pdk"))
        .replace("{registry_dir}", registry_dir)
        .replace("{python}", sys.executable)
    )


def _resolve_object_relative_path(package_dir, rel_or_abs):
    text = str(rel_or_abs or "").strip()
    if not text:
        return ""
    text = _resolve_runtime_token(text, package_dir)
    if re.match(r"^[a-zA-Z]+://", text):
        return ""
    return text if os.path.isabs(text) else os.path.abspath(os.path.join(package_dir or ROOT, text))


def _adapter_registry_candidates(package_dir):
    profile = _runtime_profile_doc(package_dir)
    candidates = []
    for rel in as_list(profile.get("adapter_registry_candidates")):
        path = _resolve_object_relative_path(package_dir, rel)
        if path:
            candidates.append(path)
    candidates.extend([
        os.path.join(package_dir or "", "adapters", "adapter_registry.json"),
        os.path.join(package_dir or "", "tools", "adapter_registries", "adapter_registry.json"),
        os.path.join(package_dir or "", "tools", "adapter_registries", "ballistic_adapters.local.json"),
    ])
    seen = set()
    out = []
    for path in candidates:
        full = os.path.abspath(path)
        if full not in seen:
            seen.add(full)
            out.append(full)
    return out


def _resolve_adapter_entry(package_dir, adapter_id, execution_kind=""):
    searched = []
    for registry_path in _adapter_registry_candidates(package_dir):
        searched.append(registry_path)
        doc = try_load_json(registry_path) or {}
        for adapter in as_list(doc.get("adapters")):
            aid = str(adapter.get("adapter_id") or "")
            kind = str(adapter.get("execution_kind") or "")
            matches = (
                aid == adapter_id
                or aid == "*"
                or (execution_kind and aid == "execution_kind:%s" % execution_kind)
                or (not adapter_id and kind and kind == execution_kind)
            )
            if not matches:
                continue
            entry = dict(adapter)
            for key in ("library", "path", "executable", "working_directory"):
                if key in entry:
                    entry[key] = _resolve_runtime_token(entry.get(key), package_dir, registry_path)
            command = entry.get("command")
            if isinstance(command, list):
                entry["command"] = [_resolve_runtime_token(item, package_dir, registry_path) for item in command]
            elif isinstance(command, str):
                entry["command"] = _resolve_runtime_token(command, package_dir, registry_path)
            entry["registry_path"] = registry_path
            return entry, searched
    return {}, searched


def _operator_test_vector_refs(package_dir, op):
    refs = []
    cases = []
    tv = op.get("test_vectors")
    if isinstance(tv, list):
        refs.extend(_strings(tv))
    elif isinstance(tv, dict):
        refs.extend(_strings(tv.get("refs")))
        refs.extend(_strings(tv.get("case_refs")))
        for case in as_list(tv.get("cases")):
            if isinstance(case, dict):
                case_refs = []
                for key in ("input_ref", "expected_ref", "tolerance_ref"):
                    if case.get(key):
                        refs.append(str(case.get(key)))
                        case_refs.append(str(case.get(key)))
                cases.append({
                    "case_id": case.get("case_id") or case.get("name") or case.get("kind") or "",
                    "kind": case.get("kind") or case.get("case_kind") or "",
                    "refs": case_refs,
                })
    if not refs:
        manifest = try_load_json(os.path.join(package_dir or "", "test_vectors", "operator_validation_pack_manifest.json")) or {}
        operator_id = str(op.get("operator_id") or op.get("id") or "")
        for item in as_list(manifest.get("operators")):
            if item.get("operator_id") == operator_id:
                refs.extend(_strings(item.get("case_refs")))
                cases.append({
                    "case_id": "operator_validation_pack",
                    "kind": ",".join(_strings(item.get("case_kinds"))),
                    "refs": _strings(item.get("case_refs")),
                })
                break
    refs = list(dict.fromkeys(refs))
    checks = []
    for ref in refs:
        resolved = _resolve_object_relative_path(package_dir, ref)
        checks.append({
            "ref": ref,
            "resolved_path": resolved,
            "exists": bool(resolved and os.path.isfile(resolved)),
            "status": "ok" if resolved and os.path.isfile(resolved) else "missing",
        })
    return {"refs": refs, "cases": cases, "checks": checks}


def _operator_trial_command(execution):
    if not isinstance(execution, dict):
        return None
    for key in ("trial_command", "test_command", "smoke_command"):
        value = execution.get(key)
        if value:
            return value
    return None


def _run_operator_trial_command(command_value, package_dir, operator_id, adapter_entry):
    if isinstance(command_value, list):
        args = [str(item) for item in command_value]
    elif isinstance(command_value, str):
        args = shlex.split(command_value, posix=False)
    else:
        return {"ok": False, "message": "试算命令必须是字符串或数组", "exit_code": None, "log_tail": ""}
    args = [
        _resolve_runtime_token(item, package_dir, adapter_entry.get("registry_path", ""))
        .replace("{operator_id}", operator_id)
        .replace("{adapter_id}", str(adapter_entry.get("adapter_id") or ""))
        for item in args
    ]
    if not args:
        return {"ok": False, "message": "试算命令为空", "exit_code": None, "log_tail": ""}
    work_dir = _resolve_runtime_token(adapter_entry.get("working_directory") or package_dir, package_dir, adapter_entry.get("registry_path", ""))
    timeout_s = int(adapter_entry.get("timeout_ms") or 30000) / 1000.0
    try:
        proc = subprocess.run(
            args,
            cwd=work_dir if os.path.isdir(work_dir) else package_dir,
            timeout=max(1.0, timeout_s),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            shell=False,
        )
        log = ((proc.stdout or "") + "\n" + (proc.stderr or "")).strip()
        return {
            "ok": proc.returncode == 0,
            "message": "试算命令执行通过" if proc.returncode == 0 else "试算命令执行失败",
            "exit_code": proc.returncode,
            "command": args,
            "working_directory": work_dir,
            "log_tail": log[-4000:],
        }
    except Exception as exc:
        return {
            "ok": False,
            "message": "试算命令执行异常：%s" % exc,
            "exit_code": None,
            "command": args,
            "working_directory": work_dir,
            "log_tail": "",
        }


def _apply_resource_patch(target, patch):
    for key, value in (patch or {}).items():
        if key in ("create", "group_id", "resource_id"):
            continue
        if value is None:
            target.pop(key, None)
        else:
            target[key] = value


def save_resource_draft(payload):
    draft_id, pkg, error = ensure_editable_draft(payload)
    if error:
        return error
    patch = {}
    resource_doc = (payload or {}).get("resource")
    if isinstance(resource_doc, dict):
        patch.update(resource_doc)
    if isinstance((payload or {}).get("patch"), dict):
        patch.update((payload or {}).get("patch") or {})
    resource_id = str((payload or {}).get("resource_id") or patch.get("resource_id") or "").strip()
    if not resource_id or not isinstance(patch, dict):
        return {"ok": False, "message": "缺少 resource_id 或 patch"}
    twin_path = os.path.join(pkg, "object", "twin_object.json")
    assets_path = os.path.join(pkg, "assets", "resources.json")
    twin = try_load_json(twin_path) or {}
    resources = twin.setdefault("resources", [])
    target = next((item for item in resources if _id(item, "resource_id", "id") == resource_id), None)
    if target is None and ((payload or {}).get("create") or patch.get("create")):
        target = {"resource_id": resource_id, "resource_type": patch.get("resource_type") or patch.get("type") or "resource"}
        resources.append(target)
    if target is None:
        return {"ok": False, "message": "资源不存在，暂不支持在 UI 中新建对象资源", "resource_id": resource_id}
    for key, value in patch.items():
        if key == "resource_id":
            continue
        if value is None:
            target.pop(key, None)
        else:
            target[key] = value
    target.pop("create", None)
    target.pop("group_id", None)
    group_id = patch.get("group_id")
    if group_id:
        assets = try_load_json(assets_path) or {"object_id": twin.get("object_id", ""), "asset_groups": []}
        groups = assets.setdefault("asset_groups", [])
        for group in groups:
            group["resources"] = [rid for rid in (group.get("resources", []) or []) if rid != resource_id]
        target_group = next((group for group in groups if group.get("group_id") == group_id), None)
        if target_group is None:
            target_group = {"group_id": group_id, "resources": []}
            groups.append(target_group)
        if resource_id not in target_group.setdefault("resources", []):
            target_group["resources"].append(resource_id)
        write_json_atomic(assets_path, assets)
    write_json_atomic(twin_path, twin)
    touch_object_draft_manifest(draft_id, "resource", resource_id)
    return {"ok": True, "message": "资源草稿已保存", "draft_id": draft_id, "resource": target, "object": object_summary(pkg)}


def delete_resource_draft(payload):
    draft_id, pkg, error = ensure_editable_draft(payload)
    if error:
        return error
    resource_id = str((payload or {}).get("resource_id") or "").strip()
    if not resource_id:
        return {"ok": False, "message": "缺少 resource_id"}
    twin_path = os.path.join(pkg, "object", "twin_object.json")
    assets_path = os.path.join(pkg, "assets", "resources.json")
    refs = scan_entity_references(pkg, resource_id, ["object/twin_object.json", "assets/resources.json"])
    if refs.get("count") and not bool((payload or {}).get("force")):
        return {
            "ok": False,
            "message": "资源仍被 operator/workflow 等对象包文件引用，未删除",
            "resource_id": resource_id,
            "references": refs,
        }
    twin = try_load_json(twin_path) or {}
    resources = twin.setdefault("resources", [])
    before = len(resources)
    twin["resources"] = [item for item in resources if _id(item, "resource_id", "id") != resource_id]
    assets = try_load_json(assets_path) or {"object_id": twin.get("object_id", ""), "asset_groups": []}
    for group in assets.setdefault("asset_groups", []):
        group["resources"] = [rid for rid in (group.get("resources", []) or []) if rid != resource_id]
    write_json_atomic(twin_path, twin)
    write_json_atomic(assets_path, assets)
    touch_object_draft_manifest(draft_id, "resource_delete", resource_id)
    return {
        "ok": True,
        "message": "资源已从对象草稿删除" if len(twin["resources"]) != before else "资源声明不存在，已清理资源组引用",
        "draft_id": draft_id,
        "resource_id": resource_id,
        "object": object_summary(pkg),
    }


def delete_workflow_draft(payload):
    draft_id, pkg, error = ensure_editable_draft(payload)
    if error:
        return error
    workflow_id = str((payload or {}).get("workflow_id") or "").strip()
    if not workflow_id:
        return {"ok": False, "message": "缺少 workflow_id"}
    wf_dir = os.path.abspath(os.path.join(pkg, "workflows"))
    path, _fn, _doc = _workflow_file(workflow_id, pkg)
    deleted_file = ""
    if path:
        abs_path = os.path.abspath(path)
        if abs_path.startswith(wf_dir + os.sep) and os.path.isfile(abs_path):
            os.remove(abs_path)
            deleted_file = abs_path
        else:
            return {"ok": False, "message": "workflow 文件不在对象草稿 workflows 目录内，拒绝删除", "path": abs_path}
    twin_path = os.path.join(pkg, "object", "twin_object.json")
    twin = try_load_json(twin_path) or {}
    entries = twin.get("workflows", [])
    if isinstance(entries, list):
        twin["workflows"] = [
            item for item in entries
            if not (isinstance(item, dict) and (item.get("workflow_id") or item.get("id")) == workflow_id)
        ]
        write_json_atomic(twin_path, twin)
    touch_object_draft_manifest(draft_id, "workflow_delete", workflow_id)
    return {
        "ok": True,
        "message": "workflow 已从对象草稿删除",
        "draft_id": draft_id,
        "workflow_id": workflow_id,
        "deleted_file": deleted_file,
        "object": object_summary(pkg),
    }


def save_operator_draft(payload):
    draft_id, pkg, error = ensure_editable_draft(payload)
    if error:
        return error
    operator_id = str((payload or {}).get("operator_id") or "").strip()
    if not operator_id:
        return {"ok": False, "message": "缺少 operator_id"}
    path, doc = find_operator_file(pkg, operator_id)
    if not path:
        return {"ok": False, "message": "算子声明不存在", "operator_id": operator_id}
    if isinstance((payload or {}).get("operator"), dict):
        doc = (payload or {}).get("operator")
    else:
        patch = (payload or {}).get("patch") or {}
        if "enabled" in patch:
            doc["enabled"] = bool(patch.get("enabled"))
        if "display_name" in patch:
            doc["display_name"] = str(patch.get("display_name") or "").strip()
        execution = doc.setdefault("execution", {})
        if patch.get("backend_kind") or patch.get("execution_kind"):
            execution["kind"] = patch.get("backend_kind") or patch.get("execution_kind")
        if "adapter_id" in patch:
            execution["adapter_id"] = patch.get("adapter_id")
        if "lifecycle" in patch:
            lifecycle = patch.get("lifecycle")
            if isinstance(lifecycle, str):
                lifecycle = [item.strip() for item in lifecycle.split(",") if item.strip()]
            execution["lifecycle"] = lifecycle if isinstance(lifecycle, list) else execution.get("lifecycle", [])
        display = doc.setdefault("display_descriptor", {})
        if "renderer_id" in patch:
            display["renderer_id"] = patch.get("renderer_id")
        if "fallback_renderer" in patch:
            display["fallback_renderer"] = patch.get("fallback_renderer")
        if "primary_outputs" in patch:
            outputs = patch.get("primary_outputs")
            if isinstance(outputs, str):
                outputs = [item.strip() for item in outputs.split(",") if item.strip()]
            display["primary_outputs"] = outputs if isinstance(outputs, list) else display.get("primary_outputs", [])
    validation = validate_operator_doc(doc, pkg, _rel_to_package(path, pkg))
    if not validation.get("ok"):
        return {"ok": False, "message": "operator spec 未通过 Phase3 release gate", "diagnostics": validation.get("diagnostics", [])}
    write_json_atomic(path, doc)
    touch_object_draft_manifest(draft_id, "operator", operator_id)
    return {"ok": True, "message": "算子草稿已保存", "draft_id": draft_id, "operator": doc, "object": object_summary(pkg)}


def save_run_profile_draft(payload):
    draft_id, pkg, error = ensure_editable_draft(payload)
    if error:
        return error
    profile = (payload or {}).get("profile")
    profile_id = str((payload or {}).get("profile_id") or (profile or {}).get("profile_id") or "").strip()
    if not profile_id:
        return {"ok": False, "message": "缺少 profile_id"}
    profiles = {item.get("profile_id"): item for item in _load_run_profiles(pkg)}
    base = dict(profiles.get(profile_id, {}))
    if isinstance(profile, dict):
        base.update(profile)
    patch = (payload or {}).get("patch") or {}
    if isinstance(patch, dict):
        base.update(patch)
    save_as = _safe_id((payload or {}).get("save_as") or profile_id)
    base = normalize_run_profile_doc(base, save_as)
    validation = validate_run_profile_doc(base, pkg)
    if not validation.get("ok"):
        return {"ok": False, "message": "run profile 未通过 Phase5 引用校验", "diagnostics": validation.get("diagnostics", [])}
    profile_dir = os.path.join(pkg, "run_profiles")
    os.makedirs(profile_dir, exist_ok=True)
    path = os.path.join(profile_dir, save_as + ".json")
    write_json_atomic(path, base)
    twin_path = os.path.join(pkg, "object", "twin_object.json")
    twin = try_load_json(twin_path) or {}
    entries = twin.setdefault("run_profiles", [])
    rel_path = "../run_profiles/%s.json" % save_as
    entry = next((item for item in entries if _id(item, "profile_id", "id") == save_as), None)
    if entry is None:
        entries.append({"profile_id": save_as, "path": rel_path})
    else:
        entry["path"] = rel_path
    write_json_atomic(twin_path, twin)
    touch_object_draft_manifest(draft_id, "run_profile", save_as)
    return {"ok": True, "message": "运行配置草稿已保存", "draft_id": draft_id, "profile": base, "object": object_summary(pkg)}


def dir_text_diff(left_dir, right_dir):
    files = set()
    for root in (left_dir, right_dir):
        for current_root, _dirs, names in os.walk(root):
            for name in names:
                if name.endswith((".json", ".md", ".ps1")):
                    files.add(os.path.relpath(os.path.join(current_root, name), root).replace("\\", "/"))
    rows = []
    for rel in sorted(files):
        lp = os.path.join(left_dir, rel)
        rp = os.path.join(right_dir, rel)
        left = []
        right = []
        if os.path.exists(lp):
            with open(lp, "r", encoding="utf-8-sig", errors="replace") as f:
                left = f.read().splitlines()
        if os.path.exists(rp):
            with open(rp, "r", encoding="utf-8-sig", errors="replace") as f:
                right = f.read().splitlines()
        if left != right:
            diff = list(difflib.unified_diff(left, right, fromfile="formal/" + rel, tofile="draft/" + rel, lineterm=""))
            rows.append({"file": rel, "line_count": len(diff), "diff": diff[:400]})
    return rows


def publish_object_draft(payload):
    draft_id = _safe_id((payload or {}).get("draft_id") or ACTIVE_OBJECT_DRAFT_ID or "")
    if not draft_id:
        return {"ok": False, "message": "缺少 draft_id"}
    draft_pkg = object_draft_package_dir(draft_id)
    if not os.path.isdir(draft_pkg):
        return {"ok": False, "message": "草稿对象包不存在", "draft_id": draft_id}
    validation = validate_object_package_dir(draft_pkg)
    if not validation.get("ok"):
        return {"ok": False, "message": "草稿对象包校验未通过，不能发布", "validation": validation}
    version_id = _safe_id((payload or {}).get("version_id") or "%s_%s" % (draft_id, _dt.datetime.now().strftime("%Y%m%d_%H%M%S")))
    release_dir = os.path.join(OBJECT_RELEASE_ROOT, package_id_for(draft_pkg), version_id)
    release_pkg = os.path.join(release_dir, "object_package")
    copytree_clean(draft_pkg, release_pkg)
    diff_rows = dir_text_diff(OBJECT_PACKAGE, draft_pkg) if OBJECT_PACKAGE else []
    report = {
        "schema_version": "flightenv.webui.object_release_report.v1",
        "version_id": version_id,
        "draft_id": draft_id,
        "formal_package": OBJECT_PACKAGE,
        "draft_package": draft_pkg,
        "release_package": release_pkg,
        "created_at_utc": _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z",
        "note": (payload or {}).get("note", ""),
        "validation": validation,
        "diff_count": len(diff_rows),
        "diff_files": [{"file": row["file"], "line_count": row["line_count"]} for row in diff_rows],
    }
    write_json_atomic(os.path.join(release_dir, "release_report.json"), report)
    write_json_atomic(os.path.join(release_dir, "object_diff.json"), {"diffs": diff_rows})
    zip_path = os.path.join(release_dir, "%s.zip" % version_id)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for current_root, _dirs, files in os.walk(release_pkg):
            for name in files:
                full = os.path.join(current_root, name)
                zf.write(full, os.path.relpath(full, release_pkg).replace("\\", "/"))
    manifest = read_object_draft_manifest(draft_id)
    manifest["status"] = "published"
    manifest["published_version_id"] = version_id
    manifest["published_release_dir"] = release_dir
    write_object_draft_manifest(draft_id, manifest)
    return {
        "ok": True,
        "message": "草稿已发布为版本包；正式对象包未被自动覆盖",
        "version_id": version_id,
        "release_dir": release_dir,
        "release_package": release_pkg,
        "report": report,
        "diff": {"diff_count": len(diff_rows), "files": report["diff_files"]},
        "bundle": zip_path,
        "status": object_draft_status(),
    }


def operator_preflight(payload):
    payload = payload or {}
    package_dir = package_for_payload(payload)
    if not package_dir:
        return {"ok": False, "message": "尚未载入对象包", "diagnostics": []}
    action = str(payload.get("action") or "preflight").strip() or "preflight"
    operator_id = str(payload.get("operator_id") or "").strip()
    path, op = find_operator_file(package_dir, operator_id)
    if not op:
        return {"ok": False, "message": "operator not found", "diagnostics": [{"severity": "blocking", "message": "未找到算子声明"}]}
    spec_validation = validate_operator_doc(op, package_dir, _rel_to_package(path, package_dir))
    spec_diagnostics = list(spec_validation.get("diagnostics", []))
    execution = op.get("execution", {}) if isinstance(op.get("execution"), dict) else {}
    execution_kind = str(execution.get("kind") or op.get("execution_kind") or "")
    adapter_id = str(execution.get("adapter_id") or "")
    lifecycle = execution.get("lifecycle", [])
    refs = _collect_resource_refs(op)
    object_payload = _load_object_payload(package_dir) or {}
    resources = {_id(res, "resource_id", "id"): res for res in as_list(object_payload.get("resources"))}
    resource_rows = {row.get("resource_id", ""): row for row in as_list((object_payload.get("resource_validation") or {}).get("resources"))}
    adapter_entry, searched_registries = _resolve_adapter_entry(package_dir, adapter_id, execution_kind)
    test_vectors = _operator_test_vector_refs(package_dir, op)
    sections = []

    def section_result(section_id, title, diagnostics, extra=None):
        blocking = any(item.get("severity") == "blocking" for item in diagnostics)
        sections.append({
            "section_id": section_id,
            "title": title,
            "ok": not blocking,
            "diagnostics": diagnostics,
            **(extra or {}),
        })

    section_result("spec", "算子声明", spec_diagnostics, {
        "operator_id": operator_id,
        "spec_file": _rel_to_package(path, package_dir),
    })

    adapter_diagnostics = []
    if not adapter_id:
        adapter_diagnostics.append({"severity": "blocking", "code": "adapter_id_missing", "message": "算子缺少 execution.adapter_id"})
    elif not adapter_entry:
        adapter_diagnostics.append({
            "severity": "blocking",
            "code": "adapter_registry_entry_missing",
            "message": "未在对象包 adapter registry 中找到该 adapter",
            "adapter_id": adapter_id,
        })
    else:
        library = str(adapter_entry.get("library") or adapter_entry.get("path") or adapter_entry.get("executable") or "")
        if library:
            if not os.path.exists(library):
                adapter_diagnostics.append({
                    "severity": "blocking",
                    "code": "adapter_library_missing",
                    "message": "adapter 声明的 DLL/可执行文件不存在",
                    "adapter_id": adapter_id,
                    "path": library,
                })
            else:
                adapter_diagnostics.append({
                    "severity": "info",
                    "code": "adapter_library_found",
                    "message": "adapter DLL/可执行文件路径存在",
                    "adapter_id": adapter_id,
                    "path": library,
                })
        else:
            adapter_diagnostics.append({
                "severity": "warning",
                "code": "adapter_library_not_declared",
                "message": "adapter registry 未声明 library/path/executable；只能完成声明级连接检查",
                "adapter_id": adapter_id,
            })
    section_result("connect", "后端连接 / DLL", adapter_diagnostics, {
        "adapter": adapter_entry,
        "adapter_registry_searched": searched_registries,
    })

    init_diagnostics = []
    if not lifecycle:
        init_diagnostics.append({"severity": "warning", "code": "lifecycle_missing", "message": "缺少 lifecycle 声明；Runtime 仍可按默认 prepare/execute 处理，但初始化阶段不够明确"})
    if not refs:
        init_diagnostics.append({"severity": "warning", "code": "resource_refs_empty", "message": "算子没有声明 resource_refs；如果算子确实无资源依赖可忽略"})
    for rid in refs:
        if rid not in resources:
            init_diagnostics.append({"severity": "blocking", "code": "operator_unknown_resource", "message": "算子引用了未声明资源", "resource_id": rid})
            continue
        row = resource_rows.get(rid, {})
        missing_checks = [item for item in as_list(row.get("path_checks")) if item.get("status") == "missing"]
        if missing_checks:
            for item in missing_checks:
                init_diagnostics.append({
                    "severity": "blocking",
                    "code": "operator_resource_path_missing",
                    "message": "初始化资源路径不存在",
                    "resource_id": rid,
                    "path": item.get("resolved_path") or item.get("value") or "",
                })
        else:
            init_diagnostics.append({
                "severity": "info",
                "code": "operator_resource_ready",
                "message": "资源声明可解析",
                "resource_id": rid,
            })
    section_result("initialize", "资源初始化", init_diagnostics, {
        "resource_refs": refs,
        "resources": [resources.get(rid, {"resource_id": rid}) for rid in refs],
    })

    trial_diagnostics = []
    missing_vectors = [item for item in as_list(test_vectors.get("checks")) if item.get("status") == "missing"]
    if not test_vectors.get("refs"):
        trial_diagnostics.append({"severity": "blocking", "code": "operator_test_vectors_missing", "message": "算子未声明测试向量，无法做试算验收"})
    elif missing_vectors:
        for item in missing_vectors:
            trial_diagnostics.append({
                "severity": "blocking",
                "code": "operator_test_vector_missing",
                "message": "测试向量文件不存在",
                "path": item.get("resolved_path") or item.get("ref") or "",
            })
    else:
        trial_diagnostics.append({
            "severity": "info",
            "code": "operator_test_vectors_ready",
            "message": "测试向量输入/期望文件齐备",
        })
    command_value = _operator_trial_command(execution)
    command_result = None
    if command_value and action == "trial":
        command_result = _run_operator_trial_command(command_value, package_dir, operator_id, adapter_entry or {})
        trial_diagnostics.append({
            "severity": "info" if command_result.get("ok") else "blocking",
            "code": "operator_trial_command",
            "message": command_result.get("message", ""),
            "exit_code": command_result.get("exit_code"),
        })
    elif command_value:
        trial_diagnostics.append({"severity": "info", "code": "operator_trial_command_available", "message": "已声明试算命令；点击试算按钮会执行"})
    else:
        trial_diagnostics.append({
            "severity": "blocking" if action == "trial" else "warning",
            "code": "operator_trial_command_missing",
            "message": "未声明 execution.trial_command/test_command/smoke_command；当前只能验证测试向量齐备，不能真实调用 adapter 试算",
        })
    section_result("trial", "试算 / 测试向量", trial_diagnostics, {
        "test_vectors": test_vectors,
        "trial_command_declared": bool(command_value),
        "trial_command_result": command_result,
    })

    diagnostics = []
    for item in sections:
        diagnostics.extend(item.get("diagnostics", []))
    ok = not any(item.get("severity") == "blocking" for item in diagnostics)
    return {
        "ok": ok,
        "action": action,
        "operator_id": operator_id,
        "display_name": op.get("display_name", ""),
        "backend": execution_kind,
        "adapter_id": adapter_id,
        "package_dir": package_dir,
        "package_domain": package_domain(package_dir),
        "draft_id": payload.get("draft_id") or ACTIVE_OBJECT_DRAFT_ID,
        "lifecycle": lifecycle,
        "resource_refs": refs,
        "adapter": adapter_entry,
        "sections": sections,
        "release_gate": {"ok": ok, "diagnostics": diagnostics},
        "diagnostics": diagnostics,
    }


def _compiled_workflow_dir(workflow_id):
    return os.path.join(ARTIFACTS, "platform-pdk", "compiled-workflows", _safe_id(workflow_id))


def _object_package_fingerprint(package_dir):
    package_dir = os.path.abspath(package_dir or "")
    if not package_dir or not os.path.isdir(package_dir):
        return {"sha256": "", "file_count": 0, "files": []}
    semantic_roots = ["object", "assets", "domain_schemas", "operators", "workflows", "run_profiles", "runtime", "manifests"]
    rows = []
    digest = hashlib.sha256()
    for rel_root in semantic_roots:
        root = os.path.join(package_dir, rel_root)
        if not os.path.isdir(root):
            continue
        for current_root, dirs, files in os.walk(root):
            dirs[:] = [d for d in dirs if d not in (".git", "__pycache__", ".vs", "build", "x64")]
            for name in sorted(files):
                if not name.endswith(".json"):
                    continue
                path = os.path.join(current_root, name)
                rel = _rel_to_package(path, package_dir)
                try:
                    with open(path, "rb") as f:
                        data = f.read()
                except OSError:
                    continue
                digest.update(rel.encode("utf-8"))
                digest.update(b"\0")
                digest.update(data)
                digest.update(b"\0")
                rows.append({"rel_path": rel, "sha256": hashlib.sha256(data).hexdigest()})
    rows.sort(key=lambda item: item.get("rel_path", ""))
    return {"sha256": digest.hexdigest(), "file_count": len(rows), "files": rows}


def _compile_job_meta(compiled_dir):
    return try_load_json(os.path.join(compiled_dir or "", COMPILE_JOB_META_FILE)) or {}


def _compile_job_meta_doc(package_dir, workflow_id, run_profile_id, run_id, command_exit_code, log_tail, plan):
    fingerprint = _object_package_fingerprint(package_dir)
    return {
        "schema_version": "flightenv.webui.compile_job.v1",
        "workflow_id": workflow_id,
        "run_profile_id": run_profile_id,
        "package_dir": package_dir,
        "package_domain": package_domain(package_dir),
        "package_fingerprint": fingerprint,
        "run_id": run_id,
        "compiled_at_utc": _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z",
        "command_exit_code": int(command_exit_code),
        "log_tail": log_tail,
        "standard_artifact_count": len(STANDARD_COMPILED_ARTIFACTS),
        "missing_standard_artifacts": plan.get("missing_standard_artifacts", []),
    }


def _compile_diagnostics_from_log(log_tail, package_dir="", workflow_id=""):
    diagnostics = []
    text = str(log_tail or "")

    def add(code, message, **extra):
        row = {
            "severity": "blocking",
            "code": code,
            "message": message,
            "workflow_id": workflow_id,
        }
        row.update({k: v for k, v in extra.items() if v not in (None, "")})
        diagnostics.append(row)

    for match in re.finditer(r"unknown operator(?:_ref)? ['\"]([^'\"]+)['\"]", text, re.IGNORECASE):
        add(
            "compile_unknown_operator_ref",
            "compile failed because a workflow node references an unknown operator",
            entity_kind="operator",
            entity_id=match.group(1),
            tree_node_id="operator:%s" % match.group(1),
        )
    for match in re.finditer(r"unknown resource referenced during compile: ([^\s\r\n]+)", text, re.IGNORECASE):
        add(
            "compile_unknown_resource_ref",
            "compile failed because an operator or workflow references an unknown resource",
            entity_kind="resource",
            entity_id=match.group(1).strip("'\""),
            tree_node_id="resource:%s" % match.group(1).strip("'\""),
        )
    for match in re.finditer(r"edge_binding_plan\.json: (.+)", text):
        add(
            "compile_edge_binding_error",
            "compile failed while freezing workflow edge bindings",
            entity_kind="workflow_edge",
            entity_id=match.group(1)[:160],
            tree_node_id="workflow:%s" % workflow_id,
        )
    for match in re.finditer(r"workflow(?: path)? not found|unknown workflow ['\"]([^'\"]+)['\"]", text, re.IGNORECASE):
        add(
            "compile_workflow_not_found",
            "compile failed because the workflow is missing",
            entity_kind="workflow",
            entity_id=match.group(1) if match.groups() else workflow_id,
            tree_node_id="workflow:%s" % (match.group(1) if match.groups() else workflow_id),
        )
    for match in re.finditer(r"run profile ['\"]([^'\"]+)['\"] does not allow workflow phase ['\"]([^'\"]+)['\"]", text, re.IGNORECASE):
        add(
            "compile_run_profile_phase_mismatch",
            "compile failed because the selected run profile does not allow this workflow phase",
            entity_kind="run_profile",
            entity_id=match.group(1),
            tree_node_id="run_profile:%s" % match.group(1),
            workflow_phase=match.group(2),
        )
    for match in re.finditer(r"unknown run profile ['\"]([^'\"]+)['\"]", text, re.IGNORECASE):
        add(
            "compile_unknown_run_profile",
            "compile failed because the run profile is missing",
            entity_kind="run_profile",
            entity_id=match.group(1),
            tree_node_id="run_profile:%s" % match.group(1),
        )
    if not diagnostics and text.strip():
        add(
            "compile_process_failed",
            "compile process failed; see log tail for details",
            entity_kind="compile_job",
            entity_id=workflow_id,
            tree_node_id="workflow:%s" % workflow_id,
        )
    return diagnostics


def _profile_allows_workflow(profile, workflow):
    phases = [str(item) for item in as_list((profile or {}).get("workflow_phases"))]
    return not phases or str((workflow or {}).get("phase", "")) in set(phases)


def _select_run_profile_for_workflow(workflow, profiles, preferred_id=""):
    by_id = {str(item.get("profile_id", "")): item for item in profiles if item.get("profile_id")}
    preferred_id = str(preferred_id or "").strip()
    if preferred_id and preferred_id in by_id and _profile_allows_workflow(by_id[preferred_id], workflow):
        return preferred_id
    for profile in profiles:
        profile_id = str(profile.get("profile_id", "")).strip()
        if profile_id and _profile_allows_workflow(profile, workflow):
            return profile_id
    return preferred_id if preferred_id in by_id else ""


def _compiled_plan_summary(compiled_dir):
    refs = []
    for fn in STANDARD_COMPILED_ARTIFACTS:
        path = os.path.join(compiled_dir, fn)
        doc = try_load_json(path) or {}
        summary = doc.get("summary", {}) if isinstance(doc, dict) else {}
        refs.append({
            "artifact_id": os.path.splitext(fn)[0],
            "file": fn,
            "path": path,
            "exists": os.path.exists(path),
            "summary": summary,
        })
    evidence = try_load_json(os.path.join(compiled_dir, "compile_evidence.json")) or {}
    execution = try_load_json(os.path.join(compiled_dir, "execution_plan.json")) or {}
    time_plan = try_load_json(os.path.join(compiled_dir, "time_plan.json")) or {}
    scheduler = try_load_json(os.path.join(compiled_dir, "scheduler_plan.json")) or {}
    data_plane = try_load_json(os.path.join(compiled_dir, "data_plane_plan.json")) or {}
    meta = _compile_job_meta(compiled_dir)
    package_dir = meta.get("package_dir") or active_object_package()
    current_fingerprint = _object_package_fingerprint(package_dir) if package_dir else {"sha256": "", "file_count": 0, "files": []}
    compiled_fingerprint = meta.get("package_fingerprint") or {}
    stale = bool(compiled_fingerprint.get("sha256") and current_fingerprint.get("sha256")
                 and compiled_fingerprint.get("sha256") != current_fingerprint.get("sha256"))
    missing_standard = [item.get("file") for item in refs if not item.get("exists")]
    existing_standard_count = len([item for item in refs if item.get("exists")])
    return {
        "compiled_dir": compiled_dir,
        "exists": os.path.isdir(compiled_dir),
        "workflow_id": execution.get("workflow_id", evidence.get("workflow_id", "")),
        "run_id": evidence.get("run_id", execution.get("run_id", "")),
        "status": evidence.get("status", ""),
        "plan_refs": refs,
        "standard_artifact_count": len(STANDARD_COMPILED_ARTIFACTS),
        "existing_standard_artifact_count": existing_standard_count,
        "missing_standard_artifacts": missing_standard,
        "compile_job": meta,
        "stale": stale,
        "stale_reason": "object package changed after compile" if stale else "",
        "package_fingerprint": compiled_fingerprint,
        "current_package_fingerprint": current_fingerprint,
        "summary": {
            "node_count": len(execution.get("nodes", []) or []),
            "time_node_count": len(time_plan.get("nodes", []) or []),
            "scheduler_node_count": len(scheduler.get("nodes", []) or []),
            "data_plane_node_count": len(data_plane.get("nodes", []) or []),
            "data_plane_port_count": (data_plane.get("summary", {}) or {}).get("port_count", ""),
            "resource_lock_count": (evidence.get("summary", {}) or {}).get("resource_lock_count", ""),
            "model_snapshot_count": (evidence.get("summary", {}) or {}).get("model_snapshot_count", ""),
            "standard_artifact_count": len(STANDARD_COMPILED_ARTIFACTS),
            "existing_standard_artifact_count": existing_standard_count,
            "missing_standard_artifact_count": len(missing_standard),
            "stale": stale,
            "compiled_at_utc": meta.get("compiled_at_utc", ""),
        },
    }


def _run_single_workflow_compile(package_dir, workflow_id, run_profile_id="", run_id="", timeout_s=180):
    out_dir = os.path.join(ARTIFACTS, "platform-pdk", "compiled-workflows")
    os.makedirs(out_dir, exist_ok=True)
    run_id = str(run_id or ("webui_compile_%s" % _dt.datetime.now().strftime("%Y%m%d_%H%M%S")))
    script = os.path.join(ROOT, "flightenv-platform-pdk", "tools", "compile_object_workflow.ps1")
    if not os.path.exists(script):
        return {"ok": False, "message": "cannot find workflow compile entry", "path": script}
    cmd = [
        ("power" + "sh" + "ell"),
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        script,
        "-Python",
        sys.executable,
        "-ObjectPackage",
        package_dir,
        "-Workflow",
        workflow_id,
        "-OutDir",
        out_dir,
        "-RunId",
        run_id,
    ]
    if run_profile_id:
        cmd.extend(["-RunProfile", run_profile_id])
    proc = subprocess.run(
        cmd,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        encoding="utf-8",
        errors="replace",
        check=False,
        timeout=int(timeout_s),
    )
    compiled_dir = _compiled_workflow_dir(workflow_id)
    summary = _compiled_plan_summary(compiled_dir)
    required_ok = all(item.get("exists") for item in summary.get("plan_refs", []))
    ok = proc.returncode == 0 and required_ok
    log_tail = "\n".join((proc.stdout or "").splitlines()[-100:])
    diagnostics = [] if ok else _compile_diagnostics_from_log(log_tail, package_dir, workflow_id)
    if ok:
        meta = _compile_job_meta_doc(package_dir, workflow_id, run_profile_id, run_id, proc.returncode, log_tail, summary)
        write_json_atomic(os.path.join(compiled_dir, COMPILE_JOB_META_FILE), meta)
        summary = _compiled_plan_summary(compiled_dir)
    return {
        "ok": ok,
        "message": "workflow compile completed" if ok else "workflow compile failed",
        "workflow_id": workflow_id,
        "run_profile_id": run_profile_id,
        "package_dir": package_dir,
        "package_domain": package_domain(package_dir),
        "draft_id": ACTIVE_OBJECT_DRAFT_ID,
        "run_id": run_id,
        "compiled_dir": compiled_dir,
        "command_exit_code": proc.returncode,
        "log_tail": log_tail,
        "plan": summary,
        "diagnostics": diagnostics,
    }


def compile_workflow_request(payload):
    payload = payload or {}
    package_dir = package_for_payload(payload)
    if not package_dir:
        return {"ok": False, "message": "尚未载入对象包，不能编译 workflow"}
    package_validation = validate_object_package_dir(package_dir)
    if not package_validation.get("ok"):
        return {
            "ok": False,
            "message": "对象包校验未通过，禁止编译",
            "package_dir": package_dir,
            "validation": package_validation,
            "diagnostics": package_validation.get("diagnostics", []),
        }
    workflow_id = str(payload.get("workflow_id", "")).strip()
    run_profile_id = str(payload.get("run_profile_id", "")).strip()
    if not workflow_id:
        return {"ok": False, "message": "缺少 workflow_id"}
    if not run_profile_id:
        return {"ok": False, "message": "缺少 run_profile_id"}
    profiles = {item.get("profile_id"): item for item in _load_run_profiles(package_dir)}
    if run_profile_id not in profiles:
        return {"ok": False, "message": "run profile 不存在或未由对象包声明", "run_profile_id": run_profile_id}
    raw = workflow_raw(workflow_id, package_dir=package_dir)
    if not raw.get("ok"):
        return raw
    validation = raw.get("validation", {})
    if not validation.get("ok"):
        return {"ok": False, "message": "workflow 校验未通过，不能编译", "validation": validation}

    run_id = str(payload.get("run_id") or ("webui_compile_%s" % _dt.datetime.now().strftime("%Y%m%d_%H%M%S")))
    result = _run_single_workflow_compile(
        package_dir,
        workflow_id,
        run_profile_id,
        run_id=run_id,
        timeout_s=payload.get("timeout_s", 180),
    )
    result["draft_id"] = payload.get("draft_id") or ACTIVE_OBJECT_DRAFT_ID
    return result


def compile_all_workflows_request(payload):
    payload = payload or {}
    package_dir = package_for_payload(payload)
    if not package_dir:
        return {"ok": False, "message": "尚未载入对象包，不能编译 workflows"}
    package_validation = validate_object_package_dir(package_dir)
    if not package_validation.get("ok"):
        return {
            "ok": False,
            "message": "对象包校验未通过，禁止编译全部 workflow",
            "package_dir": package_dir,
            "validation": package_validation,
            "diagnostics": package_validation.get("diagnostics", []),
        }
    workflows = as_list(object_summary(package_dir).get("workflows"))
    if not workflows:
        return {"ok": False, "message": "对象包没有可编译的 workflow", "package_dir": package_dir}
    profiles = _load_run_profiles(package_dir)
    preferred_profile_id = str(payload.get("run_profile_id", "")).strip()
    run_id_prefix = str(payload.get("run_id") or ("webui_compile_all_%s" % _dt.datetime.now().strftime("%Y%m%d_%H%M%S")))
    results = []
    for workflow in workflows:
        workflow_id = str(workflow.get("workflow_id") or workflow.get("id") or "").strip()
        if not workflow_id:
            continue
        raw = workflow_raw(workflow_id, package_dir=package_dir)
        workflow_doc = raw.get("workflow", workflow)
        profile_id = _select_run_profile_for_workflow(workflow_doc, profiles, preferred_profile_id)
        results.append(_run_single_workflow_compile(
            package_dir,
            workflow_id,
            profile_id,
            run_id="%s.%s" % (run_id_prefix, _safe_id(workflow_id)),
            timeout_s=payload.get("timeout_s", 180),
        ))
    failed = [row for row in results if not row.get("ok")]
    diagnostics = []
    for row in failed:
        diagnostics.extend(as_list(row.get("diagnostics")))
    return {
        "ok": not failed,
        "message": "全部 workflow 编译完成" if not failed else "部分 workflow 编译失败",
        "package_dir": package_dir,
        "package_domain": package_domain(package_dir),
        "draft_id": payload.get("draft_id") or ACTIVE_OBJECT_DRAFT_ID,
        "run_id": run_id_prefix,
        "compiled_count": len(results) - len(failed),
        "failed_count": len(failed),
        "results": results,
        "diagnostics": diagnostics,
    }


def run_preflight(payload):
    payload = payload or {}
    diagnostics = []
    package_dir = package_for_payload(payload)
    if not package_dir:
        diagnostics.append({"severity": "blocking", "code": "object_package_missing", "message": "尚未载入对象包"})
    workflow_id = str(payload.get("workflow_id", "")).strip()
    run_profile_id = str(payload.get("run_profile_id", "")).strip()
    if not workflow_id:
        diagnostics.append({"severity": "blocking", "code": "workflow_missing", "message": "缺少 workflow 选择"})
    if not run_profile_id:
        diagnostics.append({"severity": "blocking", "code": "run_profile_missing", "message": "缺少 run profile 选择"})

    profiles = {item.get("profile_id"): item for item in _load_run_profiles(package_dir)}
    if run_profile_id and run_profile_id not in profiles:
        diagnostics.append({"severity": "blocking", "code": "run_profile_unknown", "message": "run profile 未由对象包声明", "run_profile_id": run_profile_id})

    validation = validate_object_package_dir(package_dir) if package_dir else {"ok": False, "diagnostics": []}
    for item in validation.get("diagnostics", []) or []:
        diagnostics.append(item)
    resources = validate_resources(package_dir) if package_dir else {"ok": False, "diagnostics": []}
    for item in resources.get("diagnostics", []) or []:
        diagnostics.append(item)
    if workflow_id:
        raw = workflow_raw(workflow_id, package_dir=package_dir)
        wf_validation = raw.get("validation", {}) if raw.get("ok") else {"ok": False, "errors": [raw.get("message", "workflow not found")]}
        for err in wf_validation.get("errors", []) or []:
            diagnostics.append({"severity": "blocking", "code": "workflow_validation", "message": err})
        for warn in wf_validation.get("warnings", []) or []:
            diagnostics.append({"severity": "warning", "code": "workflow_validation", "message": warn})
    else:
        wf_validation = {"ok": False}

    compiled_dir = str(payload.get("compiled_dir", "")).strip() or (_compiled_workflow_dir(workflow_id) if workflow_id else "")
    required_plan_files = STANDARD_COMPILED_ARTIFACTS
    missing = [fn for fn in required_plan_files if not (compiled_dir and os.path.exists(os.path.join(compiled_dir, fn)))]
    for fn in missing:
        diagnostics.append({"severity": "blocking", "code": "compiled_plan_missing", "message": "缺少编译计划文件", "file": fn, "compiled_dir": compiled_dir})
    plan_summary = _compiled_plan_summary(compiled_dir) if compiled_dir else {}
    if plan_summary.get("stale") and not _bool_setting(payload.get("allow_stale_compiled_output", False), False):
        diagnostics.append({
            "severity": "blocking",
            "code": "compiled_output_stale",
            "message": "编译产物已过期：对象包在编译后发生变化，请重新编译或显式允许使用旧产物",
            "compiled_dir": compiled_dir,
            "workflow_id": workflow_id,
        })

    blocking = any(item.get("severity") == "blocking" for item in diagnostics)
    return {
        "ok": not blocking,
        "workflow_id": workflow_id,
        "run_profile_id": run_profile_id,
        "compiled_dir": compiled_dir,
        "package_dir": package_dir,
        "package_domain": package_domain(package_dir),
        "draft_id": payload.get("draft_id") or ACTIVE_OBJECT_DRAFT_ID,
        "validation": wf_validation,
        "plan": plan_summary,
        "diagnostics": diagnostics,
    }


# ---------------------------------------------------------------------------
# run trigger
# ---------------------------------------------------------------------------
def _script_candidates(script, package_dir=None):
    package_dir = package_dir or active_object_package()
    candidates = []
    if script:
        candidates.append(script)
    tools_dir = os.path.join(package_dir, "tools")
    discovered = []
    if os.path.isdir(tools_dir):
        for fn in sorted(os.listdir(tools_dir)):
            if not fn.lower().endswith(".ps1"):
                continue
            lower = fn.lower()
            path = os.path.join(tools_dir, fn)
            if "cpp_host" in lower:
                discovered.append((0, path))
            elif lower.startswith("run_") and "runtime" in lower:
                discovered.append((1, path))
            elif lower.startswith("run_") and "compiled" not in lower and "smoke" not in lower:
                discovered.append((2, path))
    candidates.extend(path for _, path in sorted(discovered, key=lambda item: item[0]))
    return candidates


def start_run(payload):
    global _LATEST_JOB_ID
    payload = payload or {}
    package_dir = package_for_payload(payload)
    if not package_dir:
        return {"ok": False, "message": "尚未载入并校验对象包，运行入口已禁用"}
    validation = validate_object_package_dir(package_dir)
    if not validation.get("ok"):
        return {
            "ok": False,
            "message": "对象包校验未通过，运行入口已禁用",
            "validation": validation,
        }
    workflow_id = str(payload.get("workflow_id", "")).strip()
    run_profile_id = str(payload.get("run_profile_id", "")).strip()
    if not workflow_id or not run_profile_id:
        return {
            "ok": False,
            "message": "运行前必须显式选择 workflow 和 run profile",
            "workflow_id": workflow_id,
            "run_profile_id": run_profile_id,
        }
    profile_launch = _with_workflow_branch_defaults(
        _profile_runtime_launch(_run_profile_by_id(run_profile_id, package_dir)),
        workflow_id,
        package_dir,
    )
    preflight = run_preflight(payload)
    if not preflight.get("ok"):
        return {
            "ok": False,
            "message": "preflight 未通过，不能进入 Runtime prepare/run",
            "preflight": preflight,
        }
    script = payload.get("script")
    ps = next((c for c in _script_candidates(script, package_dir) if c and os.path.exists(c)), None)
    if ps is None:
        return {"ok": False, "message": "没有找到主链运行脚本"}

    action = payload.get("action", "run")
    run_id = payload.get("run_id") or ("webui_%s_%s" % (
        "prepare" if action == "prepare" else "run",
        _dt.datetime.now().strftime("%Y%m%d_%H%M%S"),
    ))
    run_rel = os.path.join("platform-runtime", "mainline-runs", run_id).replace("\\", "/")
    run_dir = run_rel_to_dir(run_rel)
    os.makedirs(run_dir, exist_ok=True)
    job_id = "job_%d" % int(time.time() * 1000)
    log_path = os.path.join(HERE, "%s.log" % job_id)
    online_frames = _int_setting(_payload_or_profile(payload, profile_launch, "online_frames", 50), 50, 1)
    prediction_every = _int_setting(_payload_or_profile(payload, profile_launch, "prediction_every_frames", 30), 30, 1)
    future_max = _int_setting(_payload_or_profile(payload, profile_launch, "future_max_iterations", 0), 0, 0)
    branch_chunk = _int_setting(_payload_or_profile(payload, profile_launch, "branch_chunk_iterations", 1), 1, 1)
    skip_build = _bool_setting(payload.get("skip_build", True), True)
    replay_clock = _bool_setting(_payload_or_profile(payload, profile_launch, "replay_by_platform_clock", True), True)
    external_stream = str(
        payload.get("external_observation_stream")
        or payload.get("external_input_stream")
        or profile_launch.get("external_observation_stream")
        or ""
    ).strip()
    external_stream = resolve_package_file(external_stream, package_dir)
    runtime_launch = {
        "online_frames": online_frames,
        "prediction_every_frames": prediction_every,
        "future_max_iterations": future_max,
        "branch_chunk_iterations": branch_chunk,
        "replay_by_platform_clock": replay_clock,
        "external_observation_stream": external_stream,
        "branch_enabled": _bool_setting(_payload_or_profile(payload, profile_launch, "branch_enabled", False), False),
        "branch_target_workflow_id": str(_payload_or_profile(payload, profile_launch, "branch_target_workflow_id", "") or "").strip(),
        "branch_trigger_kind": str(_payload_or_profile(payload, profile_launch, "branch_trigger_kind", "every_n_frames") or "every_n_frames"),
        "branch_every_n_frames": _int_setting(_payload_or_profile(payload, profile_launch, "branch_every_n_frames", prediction_every), prediction_every, 1),
        "branch_max_concurrent": _int_setting(_payload_or_profile(payload, profile_launch, "branch_max_concurrent", 1), 1, 1),
        "branch_seed_policy": str(_payload_or_profile(payload, profile_launch, "branch_seed_policy", "latest_checkpoint") or "latest_checkpoint"),
        "branch_cancel_policy": str(_payload_or_profile(payload, profile_launch, "branch_cancel_policy", "never") or "never"),
    }
    backend = "object_package_runtime_bridge"
    package_domain_value = package_domain(package_dir)
    ensure_runtime_run_package(
        run_dir,
        run_rel,
        payload,
        run_id,
        workflow_id,
        run_profile_id,
        "accepted",
        runtime_launch=runtime_launch,
        backend=backend,
        package_dir=package_dir,
        compiled_dir=payload.get("compiled_dir", ""),
        job_id=job_id,
        message="runtime command accepted",
    )
    append_runtime_event(
        run_dir,
        "runtime_submit_accepted",
        "Runtime job accepted by WebUI bridge",
        run_id=run_id,
        job_id=job_id,
        workflow_id=workflow_id,
        run_profile_id=run_profile_id,
    )

    if action == "run" and external_stream and not os.path.exists(external_stream):
        now_utc = utc_now_iso()
        waiting_event = {
            "time_utc": now_utc,
            "severity": "info",
            "event": "waiting_external_input",
            "message": "等待外部输入源写入数据，RuntimeHost 尚未启动模型计算",
            "external_observation_stream": external_stream,
            "run_id": run_id,
        }
        write_json_atomic(os.path.join(run_dir, "mainline_progress.json"), {
            "schema_version": "flightenv.webui.runtime_progress.v1",
            "status": "waiting_external_input",
            "stage": "waiting_external_input",
            "run_id": run_id,
            "workflow_id": workflow_id,
            "run_profile_id": run_profile_id,
            "clock": {"tick_index": 0, "run_time_s": 0.0, "source_time_s": 0.0},
            "inputs": {"external_observation_stream": external_stream},
            "total_progress_percent": 0,
            "message": waiting_event["message"],
            "updated_at_utc": now_utc,
        })
        append_runtime_event(
            run_dir,
            "waiting_external_input",
            waiting_event["message"],
            external_observation_stream=external_stream,
            run_id=run_id,
            job_id=job_id,
            workflow_id=workflow_id,
            run_profile_id=run_profile_id,
        )
        ensure_runtime_run_package(
            run_dir,
            run_rel,
            payload,
            run_id,
            workflow_id,
            run_profile_id,
            "waiting_external_input",
            runtime_launch=runtime_launch,
            backend=backend,
            package_dir=package_dir,
            compiled_dir=payload.get("compiled_dir", ""),
            job_id=job_id,
            message=waiting_event["message"],
        )
        append_command_record(action, payload, {
            "ok": True,
            "run_id": run_id,
            "run": run_rel,
            "job_id": job_id,
            "message": waiting_event["message"],
            "runtime_launch": runtime_launch,
        }, run_rel=run_rel, job_id=job_id, status="waiting_external_input")
        update_runtime_run_package_status(
            run_rel,
            "waiting_external_input",
            waiting_event["message"],
            "",
            job_id=job_id,
            payload=payload,
            result={"run_id": run_id, "runtime_launch": runtime_launch},
        )
        with _JOBS_LOCK:
            _JOBS[job_id] = {
                "proc": None,
                "log": log_path,
                "script": ps,
                "cmd": [],
                "started": time.time(),
                "run_id": run_id,
                "run": run_rel,
                "action": action,
                "workflow_id": workflow_id,
                "run_profile_id": run_profile_id,
                "compiled_dir": payload.get("compiled_dir", ""),
                "backend": backend,
                "package_dir": package_dir,
                "package_domain": package_domain_value,
                "external_observation_stream": external_stream,
                "runtime_launch": runtime_launch,
                "branching_policy": profile_launch.get("branching_policy", {}),
                "waiting_external_input": True,
                "status": "waiting_external_input",
                "exit_code": None,
            }
            _LATEST_JOB_ID = job_id
        return {
            "ok": True,
            "job_id": job_id,
            "run_id": run_id,
            "run": run_rel,
            "action": action,
            "script": os.path.basename(ps),
            "log": log_path,
            "pid": "",
            "backend": backend,
            "package_dir": package_dir,
            "package_domain": package_domain_value,
            "runtime_launch": runtime_launch,
            "status": "waiting_external_input",
            "message": waiting_event["message"],
        }

    cmd = [
        ("power" + "sh" + "ell"), "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", ps,
        "-RunIdPrefix", run_id,
        "-OnlineWorkflowId", workflow_id,
        "-FutureWorkflowId", runtime_launch.get("branch_target_workflow_id") or workflow_id,
        "-OnlineFrames", str(online_frames),
        "-PredictionEveryFrames", str(prediction_every),
        "-MaxConcurrentBranches", str(runtime_launch.get("branch_max_concurrent") or 1),
        "-FutureMaxIterations", str(future_max),
        "-BranchChunkIterations", str(branch_chunk),
    ]
    if skip_build:
        cmd.append("-SkipBuild")
    if replay_clock:
        cmd.append("-ReplayByPlatformClock")
    if external_stream:
        cmd.extend(["-ExternalObservationStream", external_stream])
    if action == "prepare":
        cmd.extend(["-PrepareOnly", "-PreflightAdapters"])
    elif payload.get("preflight_adapters"):
        cmd.append("-PreflightAdapters")
    cmd.extend(["-PdkRoot", os.path.join(ROOT, "flightenv-platform-pdk")])
    if package_domain_value != "object_package":
        cmd.extend(["-WorkspaceRootOverride", ROOT])

    append_command_record(action, payload, {
        "ok": None,
        "run_id": run_id,
        "run": run_rel,
        "job_id": job_id,
        "message": "command accepted",
        "runtime_launch": runtime_launch,
    }, run_rel=run_rel, job_id=job_id, status="accepted")
    log = open(log_path, "w", encoding="utf-8")
    proc = subprocess.Popen(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    with _JOBS_LOCK:
        _JOBS[job_id] = {
            "proc": proc,
            "log": log_path,
            "script": ps,
            "cmd": cmd,
            "started": time.time(),
            "run_id": run_id,
            "run": run_rel,
            "action": action,
            "workflow_id": workflow_id,
            "run_profile_id": run_profile_id,
            "compiled_dir": payload.get("compiled_dir", ""),
            "backend": backend,
            "package_dir": package_dir,
            "package_domain": package_domain_value,
            "external_observation_stream": external_stream,
            "runtime_launch": runtime_launch,
            "branching_policy": profile_launch.get("branching_policy", {}),
        }
        _LATEST_JOB_ID = job_id
    append_command_record(action, payload, {
        "ok": True,
        "run_id": run_id,
        "run": run_rel,
        "job_id": job_id,
        "pid": proc.pid,
        "message": "process launched",
    }, run_rel=run_rel, job_id=job_id, status="launched")
    update_runtime_run_package_status(
        run_rel,
        "preparing" if action == "prepare" else "running",
        "process launched",
        "runtime_process_launched",
        job_id=job_id,
        payload=payload,
        result={"run_id": run_id, "runtime_launch": runtime_launch},
    )
    return {
        "ok": True,
        "job_id": job_id,
        "run_id": run_id,
        "run": run_rel,
        "action": action,
        "script": os.path.basename(ps),
        "log": log_path,
        "pid": proc.pid,
        "backend": backend,
        "package_dir": package_dir,
        "package_domain": package_domain_value,
        "runtime_launch": runtime_launch,
    }


def run_status(job_id):
    with _JOBS_LOCK:
        if not job_id:
            job_id = _LATEST_JOB_ID
        job = _JOBS.get(job_id)
    if not job:
        return {"ok": True, "message": "no webui job", "running": False, "status": "idle"}
    proc = job.get("proc")
    if proc is None:
        code = job.get("exit_code", None)
        running = job.get("status", "") not in ("stopped", "failed", "completed")
    else:
        code = proc.poll()
        running = code is None
    tail = ""
    try:
        with open(job["log"], "r", encoding="utf-8", errors="ignore") as f:
            tail = "".join(f.readlines()[-80:])
    except Exception:
        pass
    return {
        "ok": True,
        "job_id": job_id,
        "run_id": job.get("run_id", ""),
        "run": job.get("run", ""),
        "action": job.get("action", ""),
        "backend": job.get("backend", ""),
        "runtime_launch": job.get("runtime_launch", {}),
        "branching_policy": job.get("branching_policy", {}),
        "running": running,
        "exit_code": code,
        "elapsed_s": round(time.time() - job.get("started", time.time()), 1),
        "log_tail": tail,
    }


def stop_run(job_id, payload=None):
    payload = payload or {}
    with _JOBS_LOCK:
        if not job_id:
            job_id = _LATEST_JOB_ID
        job = _JOBS.get(job_id)
    if not job:
        result = {"ok": True, "message": "no webui job", "running": False}
        append_command_record("stop", payload, result, run_rel=payload.get("run", ""), job_id=job_id, status="no_active_job")
        return result
    proc = job.get("proc")
    if proc is None:
        with _JOBS_LOCK:
            if job_id in _JOBS:
                _JOBS[job_id]["status"] = "stopped"
                _JOBS[job_id]["waiting_external_input"] = False
                _JOBS[job_id]["exit_code"] = 0
        run_rel = job.get("run", "")
        run_dir = run_rel_to_dir(run_rel)
        if run_dir:
            now_utc = utc_now_iso()
            progress = try_load_json(os.path.join(run_dir, "mainline_progress.json")) or {}
            if isinstance(progress, dict):
                progress["status"] = "stopped"
                progress["stage"] = "stopped"
                progress["updated_at_utc"] = now_utc
                progress["message"] = "用户停止等待外部输入"
                write_json_atomic(os.path.join(run_dir, "mainline_progress.json"), progress)
            events_path = os.path.join(run_dir, "runtime_events.json")
            events = normalize_runtime_events(try_load_json(events_path) or [])
            events.append({
                "time_utc": now_utc,
                "severity": "info",
                "event": "stopped_waiting_external_input",
                "message": "用户停止等待外部输入",
                "run_id": job.get("run_id", ""),
            })
            write_json_atomic(events_path, events)
        result = {"ok": True, "message": "stopped waiting for external input", "running": False, "run": run_rel, "run_id": job.get("run_id", ""), "job_id": job_id}
        append_command_record("stop", payload, result, run_rel=run_rel, job_id=job_id, status="stopped")
        update_runtime_run_package_status(
            run_rel,
            "stopped",
            "stopped waiting for external input",
            "runtime_stop_requested",
            job_id=job_id,
            payload=payload,
            result=result,
        )
        return result
    if proc.poll() is not None:
        result = {"ok": True, "message": "job already finished", "exit_code": proc.returncode, "run": job.get("run", ""), "run_id": job.get("run_id", ""), "job_id": job_id}
        append_command_record("stop", payload, result, run_rel=job.get("run", ""), job_id=job_id, status="already_finished")
        update_runtime_run_package_status(
            job.get("run", ""),
            "completed" if proc.returncode == 0 else "failed",
            "job already finished",
            "runtime_stop_checked_finished",
            job_id=job_id,
            payload=payload,
            result=result,
        )
        return result
    try:
        subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    except Exception:
        proc.terminate()
    for _ in range(30):
        if proc.poll() is not None:
            break
        time.sleep(0.1)
    if proc.poll() is None:
        try:
            proc.terminate()
            proc.wait(timeout=2)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass
    stopped = proc.poll() is not None
    if stopped:
        with _JOBS_LOCK:
            if job_id in _JOBS:
                _JOBS[job_id]["status"] = "stopped"
    result = {
        "ok": True,
        "message": "stopped" if stopped else "stop requested",
        "job_id": job_id,
        "run": job.get("run", ""),
        "run_id": job.get("run_id", ""),
        "pid": proc.pid,
        "exit_code": proc.poll(),
    }
    append_command_record("stop", payload, result, run_rel=job.get("run", ""), job_id=job_id, status="stopped" if stopped else "requested")
    update_runtime_run_package_status(
        job.get("run", ""),
        "stopped" if stopped else "stop_requested",
        "stopped" if stopped else "stop requested",
        "runtime_stop_requested",
        job_id=job_id,
        payload=payload,
        result=result,
    )
    return result


def runtime_soft_command(action, payload):
    payload = payload or {}
    job_id = payload.get("job_id", "")
    job = job_snapshot(job_id)
    run_rel = payload.get("run") or job.get("run", "")
    if action == "checkpoint":
        run_dir = run_rel_to_dir(run_rel)
        checkpoint_dir = os.path.join(run_dir, "webui_checkpoints") if run_dir else os.path.join(ARTIFACTS, "webui-runtime-commands", "webui_checkpoints")
        os.makedirs(checkpoint_dir, exist_ok=True)
        checkpoint_path = os.path.join(checkpoint_dir, "checkpoint_%d.json" % int(time.time() * 1000))
        data = {
            "schema_version": "flightenv.webui.runtime_checkpoint_request.v1",
            "generated_at_utc": _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z",
            "job": job,
            "payload": {
                "workflow_id": payload.get("workflow_id", ""),
                "run_profile_id": payload.get("run_profile_id", ""),
                "compiled_dir": payload.get("compiled_dir", ""),
            },
        }
        write_json_atomic(checkpoint_path, data)
        result = {"ok": True, "message": "checkpoint request recorded", "checkpoint": checkpoint_path, "run": run_rel, "job_id": job.get("job_id", job_id)}
        append_command_record(action, payload, result, run_rel=run_rel, job_id=job.get("job_id", job_id), status="recorded")
        with _JOBS_LOCK:
            if job.get("job_id", job_id) in _JOBS:
                _JOBS[job.get("job_id", job_id)]["status"] = "checkpoint_requested"
        update_runtime_run_package_status(
            run_rel,
            "checkpoint_requested",
            "checkpoint request recorded",
            "runtime_checkpoint_requested",
            job_id=job.get("job_id", job_id),
            payload=payload,
            result=result,
        )
        return result
    job_key = job.get("job_id", job_id)
    soft_status = "paused" if action == "pause" else ("resume_requested" if action == "resume" else "%s_requested" % action)
    result = {
        "ok": True,
        "message": "%s request recorded; current runtime bridge has no direct host-level %s primitive" % (action, action),
        "run": run_rel,
        "job_id": job_key,
        "supported_by_bridge": False,
    }
    with _JOBS_LOCK:
        if job_key in _JOBS:
            _JOBS[job_key]["status"] = soft_status
    append_command_record(action, payload, result, run_rel=run_rel, job_id=job_key, status="recorded")
    update_runtime_run_package_status(
        run_rel,
        soft_status,
        result["message"],
        "runtime_%s_requested" % action,
        job_id=job_key,
        payload=payload,
        result=result,
    )
    return result


def branch_control_path(run_dir):
    return os.path.join(run_dir, "branch_control_records.json") if run_dir else os.path.join(ARTIFACTS, "webui-runtime-commands", "branch_control_records.json")


def append_branch_control_record(run_dir, record):
    doc = try_load_json(branch_control_path(run_dir)) or {
        "schema_version": "flightenv.webui.branch_control_records.v1",
        "records": [],
    }
    if not isinstance(doc.get("records"), list):
        doc["records"] = []
    doc["records"].append(record)
    doc["updated_at_utc"] = record.get("generated_at_utc", "")
    write_json_atomic(branch_control_path(run_dir), doc)
    return doc


def branch_command(action, payload):
    payload = payload or {}
    run_rel = payload.get("run", "")
    branch_id = str(payload.get("branch_id") or payload.get("branch") or "").strip()
    run_dir = run_rel_to_dir(run_rel)
    if not run_dir or not os.path.isdir(run_dir):
        result = {"ok": False, "message": "缺少有效 run，无法记录分支控制命令", "run": run_rel, "branch_id": branch_id}
        append_command_record("branch_%s" % action, payload, result, run_rel=run_rel, job_id=payload.get("job_id", ""), status="failed")
        return result
    if not branch_id:
        result = {"ok": False, "message": "缺少 branch_id，无法记录分支控制命令", "run": run_rel}
        append_command_record("branch_%s" % action, payload, result, run_rel=run_rel, job_id=payload.get("job_id", ""), status="failed")
        return result

    reg_path = os.path.join(run_dir, "branch_registry.json")
    reg = try_load_json(reg_path) or {"branches": []}
    branches = reg.get("branches", []) if isinstance(reg.get("branches", []), list) else []
    target = next((branch for branch in branches if branch.get("branch_id") == branch_id), None)
    if target is None:
        result = {"ok": False, "message": "当前 run 中没有找到分支", "run": run_rel, "branch_id": branch_id}
        append_command_record("branch_%s" % action, payload, result, run_rel=run_rel, job_id=payload.get("job_id", ""), status="failed")
        return result

    now_utc = _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"
    old_status = target.get("status", "")
    if action == "stop":
        new_status = "stopped"
        event_name = "branch_stop_requested"
        message = "用户请求停止分支"
    else:
        new_status = "resume_requested"
        event_name = "branch_resume_requested"
        message = "用户请求继续分支"
    target["status"] = new_status
    target["stop_reason"] = payload.get("reason", "") if action == "stop" else target.get("stop_reason", "")
    target["updated_at_utc"] = now_utc
    target["control_state"] = {
        "last_action": action,
        "old_status": old_status,
        "new_status": new_status,
        "generated_at_utc": now_utc,
        "source": "webui",
    }
    write_json_atomic(reg_path, reg)

    record = {
        "record_id": "branch_%s_%d" % (_safe_id(action), int(time.time() * 1000)),
        "action": action,
        "branch_id": branch_id,
        "old_status": old_status,
        "new_status": new_status,
        "reason": payload.get("reason", ""),
        "run": run_rel,
        "generated_at_utc": now_utc,
    }
    append_branch_control_record(run_dir, record)

    events_path = os.path.join(run_dir, "runtime_events.json")
    events = normalize_runtime_events(try_load_json(events_path) or [])
    events.append({
        "time_utc": now_utc,
        "severity": "info",
        "event": event_name,
        "message": message,
        "branch_id": branch_id,
        "old_status": old_status,
        "new_status": new_status,
    })
    write_json_atomic(events_path, events)

    result = {
        "ok": True,
        "message": message,
        "run": run_rel,
        "branch_id": branch_id,
        "old_status": old_status,
        "new_status": new_status,
        "record": record,
    }
    append_command_record("branch_%s" % action, payload, result, run_rel=run_rel, job_id=payload.get("job_id", ""), status="recorded")
    return result


def runtime_status_view(job_id="", run_rel=""):
    job = job_snapshot(job_id)
    if not job and run_rel:
        job = job_for_run(run_rel, "")
    run_dir = run_rel_to_dir(run_rel or job.get("run", ""))
    info = runtime_info(run_dir) if run_dir and os.path.isdir(run_dir) else {
        "ok": True,
        "status": job.get("status", "idle") if job else "idle",
        "process": {
            "job_id": job.get("job_id", "") if job else "",
            "pid": job.get("pid", "") if job else "",
            "running": job.get("running", False) if job else False,
            "exit_code": job.get("exit_code", None) if job else None,
            "elapsed_s": job.get("elapsed_s", "") if job else "",
            "script": os.path.basename(job.get("script", "")) if job else "",
        },
        "backend": job.get("backend", "") if job else "",
        "run_id": job.get("run_id", "") if job else "",
        "run": job.get("run", "") if job else "",
        "command_records": read_command_records(run_dir),
        "runtime_events": [],
        "adapter_sessions": [],
        "operator_initialization": summarize_initialization({}, job.get("compiled_dir", "") if job else ""),
        "blocking": runtime_blocking_state(run_dir, job, []),
    }
    info["job"] = job
    info["job_id"] = job.get("job_id", info.get("process", {}).get("job_id", ""))
    info["running"] = bool(job.get("running", info.get("process", {}).get("running", False)))
    return info


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    server_version = "FlightEnvWebUI/0.2"

    def _send_json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path):
        if not os.path.isfile(path):
            self.send_error(404, "not found")
            return
        ext = os.path.splitext(path)[1].lower()
        ctype = {
            ".html": "text/html; charset=utf-8",
            ".js": "text/javascript; charset=utf-8",
            ".jsx": "text/babel; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".json": "application/json; charset=utf-8",
            ".png": "image/png",
            ".svg": "image/svg+xml",
        }.get(ext, "application/octet-stream")
        with open(path, "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, fmt, *args):
        sys.stderr.write("[webui] " + (fmt % args) + "\n")

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        qs = urllib.parse.parse_qs(parsed.query)

        def arg(name, default=None):
            v = qs.get(name, [default])
            return v[0] if v else default

        try:
            if path == "/api/health":
                return self._send_json({
                    "ok": True,
                    "root": ROOT,
                    "object_package": OBJECT_PACKAGE,
                    "object_loaded": bool(OBJECT_PACKAGE),
                })
            if path == "/api/workspace":
                return self._send_json(workspace_view())
            if path == "/api/settings":
                return self._send_json(settings_view())
            if path == "/api/object":
                return self._send_json(object_summary())
            if path == "/api/object-project":
                return self._send_json(object_model_tree({}))
            if path == "/api/object-project/tree":
                return self._send_json(object_model_tree({}))
            if path == "/api/object-project/entity":
                return self._send_json(object_entity_read({
                    "kind": arg("kind", ""),
                    "entity_kind": arg("entity_kind", ""),
                    "entity_id": arg("entity_id", arg("id", "")),
                    "id": arg("id", ""),
                    "rel_path": arg("rel_path", ""),
                    "draft_id": arg("draft_id", ""),
                }))
            if path == "/api/object-geometry":
                return self._send_json(object_geometry({
                    "component_id": arg("component_id", ""),
                    "resource_id": arg("resource_id", ""),
                    "max_triangles": arg("max_triangles", "15000"),
                    "package_dir": arg("package_dir", ""),
                    "draft_id": arg("draft_id", ""),
                }))
            if path == "/api/draft/status":
                return self._send_json(object_draft_status())
            if path == "/api/object/packages":
                return self._send_json(list_object_packages())
            if path == "/api/resources/validate":
                return self._send_json(validate_resources())
            if path == "/api/runs":
                return self._send_json({"ok": True, "runs": list_runs(), "artifacts_root": ARTIFACTS})
            if path == "/api/snapshot":
                return self._send_json(snapshot_for_run(run_dir_abs(arg("run"))))
            if path == "/api/geometry":
                return self._send_json(geometry(
                    run_dir_abs(arg("run")),
                    arg("subject", ""),
                    arg("layout_ref", arg("layout", "")),
                    arg("mesh_ref", arg("mesh", "")),
                    arg("node_count", ""),
                    str(arg("visual_surface", "")).lower() in ("1", "true", "yes", "on"),
                ))
            if path == "/api/timeline":
                return self._send_json(timeline(run_dir_abs(arg("run"))))
            if path == "/api/dataplane":
                return self._send_json(dataplane_fields(run_dir_abs(arg("run"))))
            if path == "/api/runtime":
                return self._send_json(runtime_info(run_dir_abs(arg("run"))))
            if path == "/api/evidence":
                return self._send_json(evidence_view(run_dir_abs(arg("run"))))
            if path == "/api/diagnostics":
                return self._send_json(diagnostics_view(run_dir_abs(arg("run"))))
            if path == "/api/runtime/status":
                return self._send_json(runtime_status_view(arg("job", ""), arg("run", "")))
            if path == "/api/field":
                step = arg("step")
                step = int(step) if step not in (None, "") else None
                return self._send_json(field_values(
                    run_dir_abs(arg("run")),
                    arg("node"),
                    arg("port", ""),
                    step,
                    arg("branch", ""),
                    arg("artifact_id", ""),
                ))
            if path == "/api/workflow":
                return self._send_json(workflow_dag(arg("id", "")))
            if path == "/api/workflow/studio":
                return self._send_json(workflow_studio(arg("id", "")))
            if path == "/api/workflow/raw":
                return self._send_json(workflow_raw(arg("id", ""), arg("draft", "")))
            if path == "/api/workflow/drafts":
                return self._send_json(workflow_drafts())
            if path == "/api/run-config":
                return self._send_json({
                    "ok": True,
                    "object_loaded": bool(OBJECT_PACKAGE),
                    "workflows": (object_summary().get("workflows", []) if OBJECT_PACKAGE else []),
                    "run_profiles": _load_run_profiles(),
                    "settings": settings_view().get("settings", []),
                })
            if path == "/api/run/status":
                return self._send_json(run_status(arg("job", "")))
            if path == "/favicon.ico":
                self.send_response(204)
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                return

            rel = path.lstrip("/") or "index.html"
            safe = os.path.normpath(os.path.join(HERE, rel))
            if not safe.startswith(HERE):
                return self.send_error(403, "forbidden")
            return self._send_file(safe)
        except Exception as exc:  # noqa: BLE001
            return self._send_json({"ok": False, "error": str(exc)}, code=500)

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        try:
            length = int(self.headers.get("Content-Length", 0))
            payload = json.loads(self.rfile.read(length) or b"{}") if length else {}
            if parsed.path == "/api/run":
                return self._send_json(start_run(payload))
            if parsed.path == "/api/run/stop":
                return self._send_json(stop_run(payload.get("job_id", ""), payload))
            if parsed.path == "/api/runtime/prepare":
                payload["action"] = "prepare"
                return self._send_json(start_run(payload))
            if parsed.path == "/api/runtime/start":
                payload["action"] = "run"
                return self._send_json(start_run(payload))
            if parsed.path == "/api/runtime/stop":
                return self._send_json(stop_run(payload.get("job_id", ""), payload))
            if parsed.path == "/api/runtime/pause":
                return self._send_json(runtime_soft_command("pause", payload))
            if parsed.path == "/api/runtime/resume":
                return self._send_json(runtime_soft_command("resume", payload))
            if parsed.path == "/api/runtime/checkpoint":
                return self._send_json(runtime_soft_command("checkpoint", payload))
            if parsed.path == "/api/branch/stop":
                return self._send_json(branch_command("stop", payload))
            if parsed.path == "/api/branch/resume":
                return self._send_json(branch_command("resume", payload))
            if parsed.path == "/api/object/select":
                return self._send_json(select_object_package(payload))
            if parsed.path == "/api/object/unload":
                return self._send_json(unload_object_package())
            if parsed.path == "/api/object/validate":
                return self._send_json(validate_active_object_package(payload))
            if parsed.path == "/api/object/rescan":
                return self._send_json(list_object_packages())
            if parsed.path == "/api/object-project/create":
                return self._send_json(create_object_project(payload))
            if parsed.path == "/api/object-project/state":
                return self._send_json(save_object_project_state(payload))
            if parsed.path == "/api/object-project/save":
                return self._send_json(object_entity_save(payload))
            if parsed.path == "/api/object-project/delete":
                return self._send_json(object_entity_delete(payload))
            if parsed.path == "/api/object-project/validate":
                return self._send_json(validate_active_object_package(payload))
            if parsed.path == "/api/object-project/compile":
                return self._send_json(compile_workflow_request(payload))
            if parsed.path == "/api/object-project/compile-all":
                return self._send_json(compile_all_workflows_request(payload))
            if parsed.path == "/api/object-project/preflight":
                return self._send_json(run_preflight(payload))
            if parsed.path == "/api/object-project/run":
                payload["action"] = payload.get("action") or "run"
                return self._send_json(start_run(payload))
            if parsed.path == "/api/draft/create":
                return self._send_json(create_object_draft(payload))
            if parsed.path == "/api/draft/select":
                return self._send_json(select_object_draft(payload))
            if parsed.path == "/api/draft/discard":
                return self._send_json(discard_object_draft(payload))
            if parsed.path == "/api/draft/publish":
                return self._send_json(publish_object_draft(payload))
            if parsed.path == "/api/resource/draft":
                return self._send_json(save_resource_draft(payload))
            if parsed.path == "/api/resource/delete":
                return self._send_json(delete_resource_draft(payload))
            if parsed.path == "/api/operator/draft":
                return self._send_json(save_operator_draft(payload))
            if parsed.path == "/api/run-profile/draft":
                return self._send_json(save_run_profile_draft(payload))
            if parsed.path == "/api/resources/validate":
                return self._send_json(validate_resources())
            if parsed.path == "/api/operators/preflight":
                return self._send_json(operator_preflight(payload))
            if parsed.path == "/api/workflow/validate":
                if payload.get("workflow_id") and not payload.get("workflow"):
                    return self._send_json(workflow_raw(payload.get("workflow_id", "")).get("validation", {}))
                return self._send_json(validate_workflow_doc(payload.get("workflow")))
            if parsed.path == "/api/workflow/compile":
                return self._send_json(compile_workflow_request(payload))
            if parsed.path == "/api/workflow/compile-all":
                return self._send_json(compile_all_workflows_request(payload))
            if parsed.path == "/api/run/preflight":
                return self._send_json(run_preflight(payload))
            if parsed.path == "/api/workflow/draft":
                return self._send_json(save_workflow_draft(payload))
            if parsed.path == "/api/workflow/delete":
                return self._send_json(delete_workflow_draft(payload))
            if parsed.path == "/api/evidence/export":
                return self._send_json(export_evidence_bundle(run_dir_abs(payload.get("run", ""))))
            return self.send_error(404, "not found")
        except Exception as exc:  # noqa: BLE001
            return self._send_json({"ok": False, "error": str(exc)}, code=500)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()


def main():
    # PORT(预览/部署注入) 优先，其次 FLIGHTENV_WEBUI_PORT，最后默认 8787。
    port = int(os.environ.get("PORT") or os.environ.get("FLIGHTENV_WEBUI_PORT") or "8787")
    httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print("FlightEnv Web Twin Workbench")
    print("  workspace root:", ROOT)
    print("  object package :", OBJECT_PACKAGE)
    print("  serving        :", HERE)
    print("  open           : http://127.0.0.1:%d/" % port)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("stopping...")
        httpd.shutdown()


if __name__ == "__main__":
    main()
