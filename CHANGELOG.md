# Changelog

## Unreleased

- Add `EnvTwinWorkbench`, a standalone Qt workbench that reskins the htmlUI prototype with all nine pages (overview/online/object/models/graph/replay/health/config/diagnostics) wired to real platform data: ROS2 live prediction + VTK fields, GraphRunner evidence, and the platform catalog via node-sdk readers.
- Add orchestration blueprints to the workbench graph page: coupled (方案A) and decoupled (方案B) state-transition chains, field-reconstruction + structural-damage/ablation accumulation, per-sensor observation equations + particle filter, and shell/structure failure QoI — each node annotated with spec operator_type, dot-namespaced ports, and backing-model bearing checks against the catalog (gaps marked, not faked).
- Drive the GraphRuntimeController UI with explicit multi-frame controls for prediction samples, replay frame count, prediction interval, and frame delay; the page now reads `workflow_timeline.json` during startup so DB replay progress is visible before the first heavy model evidence finishes.
- Extend graph evidence UI boundary smoke to verify result DTO ports are consumed through the public node-sdk reader.
- Split low-risk UI helpers and project-config save assembly out of `EnvPredictorUI.cpp`.
- Document that QTest requires a configured Qt 6 environment; static boundary checks remain the local fallback when Qt 6 is unavailable.

## 0.1.0-pilot.1

- Seed initial controller UI source, resources, QTest entry points, and build helper from the FlightEnvPredictor subrepo pilot freeze.

