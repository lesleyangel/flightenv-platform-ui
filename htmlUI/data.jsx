/* ===================================================================
   FlightEnv mock data — naming kept close to real platform contracts.
   Exposed on window.FE
   =================================================================== */
(function () {

  // ---------- run context (top context bar) ----------
  const ctx = {
    object_id: "flightenv_object",
    object_name: "Reentry Vehicle",
    phase: "reentry",
    mode: "Online",            // Online / Replay / Validation
    run_id: "run_20260610_153000",
    graph_template: "state_prediction_qoi_graph.v1",
    graph_version: "1.4.2",
    archive: "enabled",
    status: "running",         // idle / running / degraded / failed / replaying
  };

  // ---------- object current state ----------
  const state = {
    altitude_km: 41.82,
    mach: 12.4,
    aoa_deg: 11.3,
    velocity_ms: 3892,
    dynamic_pressure_kpa: 48.6,
    heat_flux_mw: 3.41,
    timestamp: "2026-06-10 15:31:07.420",
    frame_id: "sf_0041982",
    state_source: "online_pf · state.posterior",
    freshness_ms: 38,
  };

  // ---------- run pipeline stages ----------
  const stages = [
    { key: "sensor",     name: "Sensor",     type: "input",                status: "ok",   val: "50",   unit: "Hz",  sub: "12 通道 · 新鲜" },
    { key: "filter",     name: "Filter",     type: "filter_algorithm",     status: "ok",   val: "ESS 0.71", unit: "", sub: "PF · 2048 粒子" },
    { key: "state",      name: "State",      type: "state.posterior",      status: "ok",   val: "41.8", unit: "km",  sub: "残差 0.94σ" },
    { key: "trajectory", name: "Trajectory", type: "trajectory.forecast",  status: "ok",   val: "80",   unit: "步",  sub: "horizon 4.0s" },
    { key: "field",      name: "Field",      type: "field.forecast",       status: "warn", val: "3/4",  unit: "场",  sub: "field.K degraded" },
    { key: "damage",     name: "Damage",     type: "damage.forecast",      status: "ok",   val: "0.62", unit: "",    sub: "nose cap" },
    { key: "life",       name: "Life",       type: "life.assessment",      status: "warn", val: "low",  unit: "conf", sub: "适用包络边缘" },
  ];

  // ---------- risk summary ----------
  const risk = {
    max_temp_k: 1684,
    max_temp_loc: "nose cap · 驻点",
    max_stress_mpa: 412,
    max_stress_loc: "前缘肩部",
    max_damage: 0.62,
    max_damage_loc: "nose cap",
    rul_s: 286,
    rul_conf: "medium",
    first_exceedance_s: 312,
    first_exceedance_kind: "温度超限 1750K",
  };

  // ---------- current model snapshot ----------
  const modelSnapshot = [
    { type: "trajectory",       model_id: "traj.biconic.cli.v3",     version: "3.2.0", status: "ok",   runtime: "cli_exe" },
    { type: "field_prediction", model_id: "field.pkst.pod_bpnn.v2",  version: "2.4.1", status: "warn", runtime: "in_process" },
    { type: "damage",           model_id: "damage.creep_fatigue.v1", version: "1.1.0", status: "ok",   runtime: "in_process" },
    { type: "life",             model_id: "life.rul_baseline.v1",    version: "1.0.3", status: "warn", runtime: "in_process" },
  ];

  // ---------- recent runs ----------
  const runs = [
    { run_id: "run_20260610_153000", object: "flightenv_object", phase: "reentry",  status: "running",  started: "15:30:00", frames: 1982, models: "traj v3 · field v2 · dmg v1 · life v1", template: "state_prediction_qoi_graph.v1" },
    { run_id: "run_20260610_142210", object: "flightenv_object", phase: "reentry",  status: "ok",       started: "14:22:10", frames: 3204, models: "traj v3 · field v2 · dmg v1 · life v1", template: "state_prediction_qoi_graph.v1" },
    { run_id: "run_20260610_111845", object: "flightenv_object", phase: "ascent",   status: "degraded", started: "11:18:45", frames: 2870, models: "traj v3 · field v2 · dmg v1", template: "trajectory_only_forecast" },
    { run_id: "run_20260609_201530", object: "flightenv_object", phase: "reentry",  status: "ok",       started: "昨 20:15", frames: 4102, models: "traj v3 · field v1 · dmg v1 · life v1", template: "structural_health_forecast" },
    { run_id: "run_20260609_154012", object: "flightenv_object", phase: "reentry",  status: "failed",   started: "昨 15:40", frames: 412,  models: "traj v3 · field v2", template: "state_prediction_qoi_graph.v1" },
    { run_id: "run_20260609_092200", object: "flightenv_object", phase: "cruise",   status: "ok",       started: "昨 09:22", frames: 5510, models: "traj v3 · field v2 · dmg v1 · life v1", template: "online_bayes_filter_graph.v1" },
  ];

  // ---------- online: filter diagnostics ----------
  const filter = {
    source_rate_hz: 50,
    latest_frame: "sf_0041982",
    freshness_ms: 38,
    particles: 2048,
    ess: 0.71,             // normalized 0..1
    ess_n: 1454,
    residual_sigma: 0.94,
    resample_threshold: 0.5,
    resamples: 37,
    diag_status: "ok",
    seed: 4815162342,
  };

  // ---------- online: forecast meta ----------
  const forecast = {
    pred_run: "fc_0214",
    horizon_s: 4.0,
    dt_s: 0.05,
    steps: 80,
    duration_ms: 142,
    triggers: 214,
    last_trigger_frame: "sf_0041960",
  };

  // ---------- time series for charts (online + replay) ----------
  // generate plausible reentry-ish curves
  function gen(n, f) { const a = []; for (let i = 0; i < n; i++) a.push(f(i, i / (n - 1))); return a; }
  const N = 80;
  const series = {
    altitude: gen(N, (i, t) => ({ x: t * 4, y: 41.8 - 9.5 * t + 0.6 * Math.sin(t * 6) })),
    mach:     gen(N, (i, t) => ({ x: t * 4, y: 12.4 - 3.1 * t - 0.2 * Math.sin(t * 5) })),
    aoa:      gen(N, (i, t) => ({ x: t * 4, y: 11.3 + 1.8 * Math.sin(t * 3.2) - 0.6 * t })),
    heat:     gen(N, (i, t) => ({ x: t * 4, y: 3.41 + 1.2 * Math.sin(t * 2.3) * (1 - t) + 0.5 * t })),
  };
  // future trajectory table (sample of steps)
  const trajTable = [0, 10, 20, 30, 40, 50, 60, 70, 79].map(i => {
    const t = i / (N - 1);
    return {
      step: i,
      t_rel: (t * 4).toFixed(2),
      alt: series.altitude[i].y.toFixed(2),
      mach: series.mach[i].y.toFixed(2),
      aoa: series.aoa[i].y.toFixed(2),
      q: (48.6 - 18 * t + 4 * Math.sin(t * 5)).toFixed(1),
    };
  });

  // ---------- workflow timeline (operator-level, online) ----------
  const timeline = [
    { op: "state_transition",     kind: "in_process", dur_ms: 0.8,  bytes: "12.4 KB", status: "ok",   port: "state.predicted" },
    { op: "observation_equation", kind: "in_process", dur_ms: 0.5,  bytes: "3.1 KB",  status: "ok",   port: "observation.predicted" },
    { op: "filter_algorithm",     kind: "in_process", dur_ms: 6.2,  bytes: "41.0 KB", status: "ok",   port: "state.posterior" },
    { op: "qoi_equation",         kind: "in_process", dur_ms: 1.1,  bytes: "8.8 KB",  status: "ok",   port: "qoi.series" },
    { op: "trajectory_projection",kind: "cli_exe",    dur_ms: 88.0, bytes: "210 KB",  status: "ok",   port: "trajectory.forecast" },
    { op: "field.P.predict",      kind: "in_process", dur_ms: 14.5, bytes: "1.2 MB",  status: "ok",   port: "field.P.forecast" },
    { op: "field.K.predict",      kind: "in_process", dur_ms: 12.0, bytes: "—",       status: "warn", port: "field.K.forecast" },
    { op: "field.S.predict",      kind: "in_process", dur_ms: 15.1, bytes: "1.1 MB",  status: "ok",   port: "field.S.forecast" },
    { op: "field.T.predict",      kind: "in_process", dur_ms: 15.4, bytes: "1.3 MB",  status: "ok",   port: "field.T.forecast" },
    { op: "field.merge",          kind: "in_process", dur_ms: 2.2,  bytes: "—",       status: "warn", port: "field.forecast" },
    { op: "damage_accumulation",  kind: "in_process", dur_ms: 4.0,  bytes: "96 KB",   status: "ok",   port: "damage.forecast" },
    { op: "failure_criterion",    kind: "in_process", dur_ms: 0.9,  bytes: "2.0 KB",  status: "ok",   port: "failure.assessment" },
    { op: "life_assessment",      kind: "in_process", dur_ms: 3.1,  bytes: "5.4 KB",  status: "warn", port: "life.assessment" },
  ];

  // ---------- object tree (each object carries its OWN applicable physics) ----------
  // field subjects: P 压力 · K 热流 · T 温度 · S 应力 · L 寿命
  const objectTree = [
    { id: "vehicle", name: "Reentry Vehicle", type: "vehicle", depth: 0, status: "ok",
      meta: { object_id: "flightenv_object", object_type: "vehicle", domain: "整机", geometry_ref: "mesh://rv_biconic_full.v4", material_ref: "mat://multilayer_tps.v2" },
      character: "整机聚合：气动外表面 + 内部承力结构两类物理域，各自方法不同。",
      fields: [], sensors: [], damage: null, life: null },

    // ===== 气动外表面域：热流 / 压力 / 温度 · 热电偶 + 压力点 · 烧蚀/热防护损伤 =====
    { id: "shell", name: "Outer Aero Shell", type: "shell", depth: 1, status: "ok", domain: "aero",
      meta: { object_id: "obj.aero_shell", object_type: "aero_surface", domain: "气动外表面", geometry_ref: "mesh://rv_shell.v4", material_ref: "mat://c_sic.v1" },
      character: "迎流外表面，受气动加热与气动压力。可用场=压力 P / 热流 K / 温度 T；不解算结构应力场。",
      fields: [
        { subject: "P", label: "压力场", port: "field.P.forecast", model: "field.P.pod_bpnn.v2", status: "ok" },
        { subject: "K", label: "热流密度", port: "field.K.forecast", model: "field.K.pod_bpnn.v2", status: "warn" },
        { subject: "T", label: "温度场", port: "field.T.forecast", model: "field.T.pod_bpnn.v2", status: "ok" },
      ],
      sensors: ["TC-01", "TC-02", "TC-05", "PT-02", "PT-04"],
      damage: { model: "damage.creep_fatigue.v1", kind: "热致蠕变/烧蚀", driver: "field.T + field.K" },
      life: { model: "life.rul_baseline.v1" } },
    { id: "nosecap", name: "Nose Cap · 驻点防热", type: "region", depth: 2, status: "warn", domain: "aero",
      meta: { object_id: "obj.nose_cap", object_type: "thermal_region", domain: "气动驻点", geometry_ref: "mesh://rv_nose.v4", material_ref: "mat://c_c_carbon.v3" },
      character: "驻点热环境最严酷。可用场=热流 K / 温度 T / 压力 P；损伤由温度+热流驱动。",
      fields: [
        { subject: "K", label: "热流密度", port: "field.K.forecast", model: "field.K.pod_bpnn.v2", status: "warn" },
        { subject: "T", label: "温度场", port: "field.T.forecast", model: "field.T.pod_bpnn.v2", status: "ok" },
        { subject: "P", label: "压力场", port: "field.P.forecast", model: "field.P.pod_bpnn.v2", status: "ok" },
      ],
      sensors: ["TC-01", "TC-02", "PT-02"],
      damage: { model: "damage.creep_fatigue.v1", kind: "热致蠕变", driver: "field.T" },
      life: { model: "life.rul_baseline.v1" } },
    { id: "tps", name: "TPS Windward · 迎风热防护", type: "region", depth: 2, status: "warn", domain: "aero",
      meta: { object_id: "obj.tps_windward", object_type: "thermal_region", domain: "气动迎风", geometry_ref: "mesh://rv_tps_wind.v4", material_ref: "mat://ablative_tps.v2" },
      character: "迎风大面积烧蚀防热。可用场=热流 K / 温度 T；烧蚀型损伤模型。",
      fields: [
        { subject: "K", label: "热流密度", port: "field.K.forecast", model: "field.K.pod_bpnn.v2", status: "warn" },
        { subject: "T", label: "温度场", port: "field.T.forecast", model: "field.T.pod_bpnn.v2", status: "ok" },
      ],
      sensors: ["TC-05", "PT-04"],
      damage: { model: "damage.creep_fatigue.v1", kind: "烧蚀退移", driver: "field.K + field.T" },
      life: { model: "life.rul_baseline.v1" } },

    // ===== 内部承力结构域：应力 / 温度 · 应变片 + IMU · 结构疲劳损伤 =====
    { id: "structure", name: "Primary Structure", type: "structure", depth: 1, status: "ok", domain: "struct",
      meta: { object_id: "obj.primary_struct", object_type: "load_structure", domain: "内部承力结构", geometry_ref: "mesh://rv_struct.v4", material_ref: "mat://ti_alloy.v1" },
      character: "内部承力框/梁，受热-力耦合载荷。可用场=应力 S / 温度 T（经热→结构映射）；无外表面热流/压力场。结构疲劳损伤。",
      fields: [
        { subject: "S", label: "应力场", port: "field.S.forecast", model: "field.S.pod_bpnn.v2", status: "ok" },
        { subject: "T", label: "温度场", port: "field.T.forecast", model: "mapper.field_to_struct.v1", status: "ok" },
      ],
      sensors: ["SG-07", "SG-09", "IMU-1"],
      damage: { model: "damage.fatigue_only.v0", kind: "结构疲劳", driver: "field.S" },
      life: { model: "life.rul_baseline.v1" } },
    { id: "shoulder", name: "Leading-edge Shoulder", type: "region", depth: 2, status: "warn", domain: "struct",
      meta: { object_id: "obj.shoulder", object_type: "load_structure", domain: "前缘肩部·热力耦合", geometry_ref: "mesh://rv_shoulder.v4", material_ref: "mat://c_sic.v1" },
      character: "前缘肩部热-力耦合最强。可用场=应力 S / 温度 T / 热流 K；结构疲劳为主、含热致。",
      fields: [
        { subject: "S", label: "应力场", port: "field.S.forecast", model: "field.S.pod_bpnn.v2", status: "ok" },
        { subject: "T", label: "温度场", port: "field.T.forecast", model: "field.T.pod_bpnn.v2", status: "ok" },
        { subject: "K", label: "热流密度", port: "field.K.forecast", model: "field.K.pod_bpnn.v2", status: "warn" },
      ],
      sensors: ["SG-03", "TC-02"],
      damage: { model: "damage.creep_fatigue.v1", kind: "热-力耦合疲劳", driver: "field.S + field.T" },
      life: { model: "life.rul_baseline.v1" } },

    { id: "mount", name: "Sensor Node Set", type: "nodeset", depth: 1, status: "ok", domain: "meta",
      meta: { object_id: "obj.sensor_mount", object_type: "node_set", domain: "测点集合", geometry_ref: "nodes://rv_sensor_set.v2", material_ref: "—" },
      character: "传感器测点节点集，不承载物理场，仅用于观测方程与场采样对齐。",
      fields: [], sensors: ["TC-01", "TC-02", "TC-05", "SG-03", "SG-07", "SG-09", "PT-02", "PT-04", "IMU-1"], damage: null, life: null },
  ];

  // sensor bindings (each maps to a region + the field subject it observes)
  const sensors = [
    { ch: "TC-01", kind: "thermocouple", domain: "aero",   region: "nosecap",   loc: "nose cap 驻点", unit: "K",   rate: 50,  observes: "T/K", status: "ok" },
    { ch: "TC-02", kind: "thermocouple", domain: "aero",   region: "shoulder",  loc: "nose 肩部",     unit: "K",   rate: 50,  observes: "T",   status: "ok" },
    { ch: "TC-05", kind: "thermocouple", domain: "aero",   region: "tps",       loc: "TPS 迎风",      unit: "K",   rate: 50,  observes: "T",   status: "ok" },
    { ch: "PT-02", kind: "pressure",     domain: "aero",   region: "nosecap",   loc: "迎风驻点",      unit: "kPa", rate: 100, observes: "P",   status: "ok" },
    { ch: "PT-04", kind: "pressure",     domain: "aero",   region: "tps",       loc: "迎风大面积",    unit: "kPa", rate: 100, observes: "P",   status: "ok" },
    { ch: "SG-03", kind: "strain_gauge", domain: "struct", region: "shoulder",  loc: "前缘肩部",      unit: "με",  rate: 100, observes: "S",   status: "ok" },
    { ch: "SG-07", kind: "strain_gauge", domain: "struct", region: "structure", loc: "主结构框",      unit: "με",  rate: 100, observes: "S",   status: "warn" },
    { ch: "SG-09", kind: "strain_gauge", domain: "struct", region: "structure", loc: "承力梁根部",    unit: "με",  rate: 100, observes: "S",   status: "ok" },
    { ch: "IMU-1", kind: "imu",          domain: "struct", region: "structure", loc: "质心",          unit: "—",   rate: 200, observes: "state", status: "ok" },
  ];

  // ---------- model assets (comprehensive: one family per operator type) ----------
  const models = [
    // — dynamics (state_transition) —
    { model_id: "dyn.runtime_engine.v2", model_type: "dynamics", runtime_type: "in_process", version: "2.3.0",
      artifact_ref: "in_process://RuntimeEngineV2", checksum: "sha256:aa10…9f31", envelope: "再入 3-DoF · M 4–20",
      validation: "ok", enabled: true, priority: 10, appliesTo: ["vehicle"], op_type: "state_transition",
      inputs: ["state.posterior(t-1)", "control.u"], outputs: ["state.predicted"],
      assets: [{ role: "dynamics_core", ref: "in_process://RuntimeEngineV2::transition" }], runsUsing: ["run_20260610_153000", "run_20260610_142210"] },
    { model_id: "dyn.6dof.dll.v1", model_type: "dynamics", runtime_type: "dll", version: "1.0.0",
      artifact_ref: "pkg://dynamics/sixdof_v1.dll", checksum: "sha256:b2c0…44de", envelope: "6-DoF · 含姿态",
      validation: "ok", enabled: false, priority: 20, appliesTo: ["vehicle"], op_type: "state_transition",
      inputs: ["state.posterior(t-1)", "control.u"], outputs: ["state.predicted"],
      assets: [{ role: "aero_db", ref: "db://aero_6dof.sqlite" }], runsUsing: [] },
    // — observation —
    { model_id: "obs.runtime_engine.v2", model_type: "observation", runtime_type: "in_process", version: "2.3.0",
      artifact_ref: "in_process://RuntimeEngineV2", checksum: "sha256:aa10…9f31", envelope: "TC/SG/PT/IMU",
      validation: "ok", enabled: true, priority: 10, appliesTo: ["vehicle"], op_type: "observation_equation",
      inputs: ["state.predicted"], outputs: ["observation.predicted"],
      assets: [{ role: "sensor_model", ref: "in_process://RuntimeEngineV2::observe" }], runsUsing: ["run_20260610_153000"] },
    { model_id: "obs.radar_los.v1", model_type: "observation", runtime_type: "in_process", version: "1.1.0",
      artifact_ref: "pkg://obs/radar_los_v1.zip", checksum: "sha256:7711…0a2b", envelope: "外测雷达 LOS",
      validation: "ok", enabled: false, priority: 20, appliesTo: ["vehicle"], op_type: "observation_equation",
      inputs: ["state.predicted"], outputs: ["observation.predicted"],
      assets: [{ role: "radar_cfg", ref: "catalog://obs.radar.site_a" }], runsUsing: [] },
    // — filter —
    { model_id: "filter.simple_pf.v1", model_type: "filter", runtime_type: "in_process", version: "1.4.0",
      artifact_ref: "in_process://SimplePF", checksum: "sha256:3c9a…71b0", envelope: "2048 粒子 · 残差重采样",
      validation: "ok", enabled: true, priority: 10, appliesTo: ["vehicle"], op_type: "filter_algorithm",
      inputs: ["state.predicted", "observation.predicted", "observation.measured"], outputs: ["state.posterior", "filter.diagnostics"],
      assets: [{ role: "transition_op", ref: "op.dyn.runtime_engine.v2" }, { role: "observation_op", ref: "op.obs.runtime_engine.v2" }], runsUsing: ["run_20260610_153000", "run_20260610_142210"] },
    { model_id: "filter.ukf.v1", model_type: "filter", runtime_type: "in_process", version: "1.0.2",
      artifact_ref: "pkg://filter/ukf_v1.zip", checksum: "sha256:55b1…cd03", envelope: "弱非线性 · 低维",
      validation: "ok", enabled: false, priority: 20, appliesTo: ["vehicle"], op_type: "filter_algorithm",
      inputs: ["state.predicted", "observation.predicted", "observation.measured"], outputs: ["state.posterior", "filter.diagnostics"],
      assets: [{ role: "transition_op", ref: "op.dyn.runtime_engine.v2" }], runsUsing: [] },
    // — qoi —
    { model_id: "qoi.reentry.v1", model_type: "qoi", runtime_type: "in_process", version: "1.2.0",
      artifact_ref: "catalog://qoi.reentry.v1", checksum: "sha256:18ef…2240", envelope: "热流/动压/攻角/落点",
      validation: "ok", enabled: true, priority: 10, appliesTo: ["vehicle"], op_type: "qoi_equation",
      inputs: ["state.future"], outputs: ["qoi.series"],
      assets: [{ role: "qoi_def", ref: "catalog://qoi.reentry.v1" }], runsUsing: ["run_20260610_153000"] },
    // — trajectory —
    { model_id: "traj.biconic.cli.v3", model_type: "trajectory", runtime_type: "cli_exe", version: "3.2.0",
      artifact_ref: "pkg://trajectory/biconic_v3.zip", checksum: "sha256:9f2c…b41a", envelope: "M 6–20 · H 0–80km · 至落点",
      validation: "ok", enabled: true, priority: 10, appliesTo: ["vehicle"], op_type: "trajectory_projection",
      inputs: ["state.posterior", "control.profile"], outputs: ["trajectory.forecast", "state.future", "impact.point"],
      assets: [{ role: "trajectory_package", ref: "catalog://pkg.traj.biconic.v3" }, { role: "control_profile", ref: "catalog://ctrl.reentry.nominal" }], runsUsing: ["run_20260610_153000", "run_20260610_142210"] },
    { model_id: "traj.3dof.inproc.v2", model_type: "trajectory", runtime_type: "in_process", version: "2.1.0",
      artifact_ref: "pkg://trajectory/threedof_v2.zip", checksum: "sha256:c071…aa92", envelope: "M 4–18 · 快速近似",
      validation: "ok", enabled: false, priority: 20, appliesTo: ["vehicle"], op_type: "trajectory_projection",
      inputs: ["state.posterior"], outputs: ["trajectory.forecast", "state.future", "impact.point"],
      assets: [{ role: "atmos_model", ref: "catalog://atmos.us76" }], runsUsing: ["run_20260609_092200"] },
    { model_id: "traj.coupled_planner.v1", model_type: "trajectory", runtime_type: "in_process", version: "0.9.0",
      artifact_ref: "pkg://trajectory/coupled_planner_v1.zip", checksum: "sha256:dd31…6610", envelope: "离线耦合规划 · 实验",
      validation: "warn", enabled: false, priority: 30, appliesTo: ["vehicle"], op_type: "trajectory_projection",
      inputs: ["state.posterior", "aero.provider"], outputs: ["trajectory.forecast", "coupling.diagnostics"],
      assets: [{ role: "aero_provider", ref: "catalog://aero.provider.rom" }], runsUsing: [] },
    // — field_prediction P/K/S/T —
    { model_id: "field.P.pod_bpnn.v2", model_type: "field_prediction", runtime_type: "in_process", version: "2.4.1",
      artifact_ref: "pkg://field/pod_bpnn_P_v2.zip", checksum: "sha256:1ad7…77ce", envelope: "M 8–16 · AoA 5–14°", subject: "P · 压力场",
      validation: "ok", enabled: true, priority: 10, appliesTo: ["shell","nosecap","tps"], op_type: "field_reconstruction",
      inputs: ["trajectory.forecast", "state.future"], outputs: ["field.P.forecast"],
      assets: [{ role: "pod_basis", ref: "catalog://asset.pod.P" }, { role: "bpnn_network", ref: "catalog://asset.bpnn.predP" }, { role: "mesh", ref: "mesh://rv_biconic_full.v4" }], runsUsing: ["run_20260610_153000"] },
    { model_id: "field.K.pod_bpnn.v2", model_type: "field_prediction", runtime_type: "in_process", version: "2.4.1",
      artifact_ref: "pkg://field/pod_bpnn_K_v2.zip", checksum: "sha256:1ad8…88cf", envelope: "M 8–15 · 当前外插", subject: "K · 热流密度",
      validation: "warn", enabled: true, priority: 10, appliesTo: ["shell","nosecap","tps","shoulder"], op_type: "field_reconstruction",
      inputs: ["trajectory.forecast", "state.future"], outputs: ["field.K.forecast"],
      assets: [{ role: "pod_basis", ref: "catalog://asset.pod.K" }, { role: "bpnn_network", ref: "catalog://asset.bpnn.predK" }], runsUsing: ["run_20260610_153000"] },
    { model_id: "field.S.pod_bpnn.v2", model_type: "field_prediction", runtime_type: "in_process", version: "2.4.1",
      artifact_ref: "pkg://field/pod_bpnn_S_v2.zip", checksum: "sha256:1ad9…99d0", envelope: "M 6–16", subject: "S · 应力场",
      validation: "ok", enabled: true, priority: 10, appliesTo: ["structure","shoulder"], op_type: "field_reconstruction",
      inputs: ["trajectory.forecast", "state.future"], outputs: ["field.S.forecast"],
      assets: [{ role: "pod_basis", ref: "catalog://asset.pod.S" }, { role: "bpnn_network", ref: "catalog://asset.bpnn.predS" }], runsUsing: ["run_20260610_153000"] },
    { model_id: "field.T.pod_bpnn.v2", model_type: "field_prediction", runtime_type: "in_process", version: "2.4.1",
      artifact_ref: "pkg://field/pod_bpnn_T_v2.zip", checksum: "sha256:1ae0…aae1", envelope: "M 6–16", subject: "T · 温度场",
      validation: "ok", enabled: true, priority: 10, appliesTo: ["shell","nosecap","tps","structure","shoulder"], op_type: "field_reconstruction",
      inputs: ["trajectory.forecast", "state.future"], outputs: ["field.T.forecast"],
      assets: [{ role: "pod_basis", ref: "catalog://asset.pod.T" }, { role: "bpnn_network", ref: "catalog://asset.bpnn.predT" }], runsUsing: ["run_20260610_153000"] },
    { model_id: "field.pkst.legacy_st.v1", model_type: "field_prediction", runtime_type: "dll", version: "1.6.0",
      artifact_ref: "pkg://field/legacy_predST_v1.dll", checksum: "sha256:c4e0…2b9d", envelope: "M 6–18 · S/T 耦合", subject: "S+T · 耦合 legacy",
      validation: "ok", enabled: false, priority: 30, appliesTo: ["structure","shoulder"], op_type: "field_reconstruction",
      inputs: ["state.future"], outputs: ["field.S.forecast", "field.T.forecast"],
      assets: [{ role: "legacy_db", ref: "db://task_field_legacy.sqlite" }, { role: "pod_basis", ref: "catalog://asset.pod.ST" }], runsUsing: ["run_20260609_201530"] },
    // — damage —
    { model_id: "damage.creep_fatigue.v1", model_type: "damage", runtime_type: "in_process", version: "1.1.0",
      artifact_ref: "pkg://damage/creep_fatigue_v1.zip", checksum: "sha256:7b18…ee31", envelope: "T < 1900K",
      validation: "ok", enabled: true, priority: 10, appliesTo: ["nosecap","tps","shoulder","shell"], op_type: "damage_accumulation",
      inputs: ["damage.initial", "field.forecast"], outputs: ["damage.forecast"],
      assets: [{ role: "material_db", ref: "db://material_creep.sqlite" }, { role: "damage_baseline", ref: "catalog://health.nose_cap.baseline" }], runsUsing: ["run_20260610_153000"] },
    { model_id: "damage.fatigue_only.v0", model_type: "damage", runtime_type: "in_process", version: "0.9.0",
      artifact_ref: "pkg://damage/fatigue_only_v0.zip", checksum: "sha256:9012…ab3c", envelope: "仅疲劳 · 不含蠕变",
      validation: "warn", enabled: false, priority: 20, appliesTo: ["structure"], op_type: "damage_accumulation",
      inputs: ["damage.initial", "field.S.forecast"], outputs: ["damage.forecast"],
      assets: [{ role: "sn_curve", ref: "catalog://material.sn_curve.v1" }], runsUsing: [] },
    // — failure —
    { model_id: "failure.threshold.v1", model_type: "failure", runtime_type: "in_process", version: "1.0.0",
      artifact_ref: "catalog://limits.tps.v1", checksum: "sha256:2f9d…aa10", envelope: "温度/应力/损伤阈值",
      validation: "ok", enabled: true, priority: 10, appliesTo: ["nosecap","tps","shoulder","structure"], op_type: "failure_criterion",
      inputs: ["damage.forecast", "field.T.forecast", "field.S.forecast"], outputs: ["failure.assessment"],
      assets: [{ role: "limits", ref: "catalog://limits.tps.v1" }], runsUsing: ["run_20260610_153000"] },
    // — life —
    { model_id: "life.rul_baseline.v1", model_type: "life", runtime_type: "in_process", version: "1.0.3",
      artifact_ref: "pkg://life/rul_baseline_v1.zip", checksum: "sha256:55aa…0c2f", envelope: "鉴定试验外推 ±15%",
      validation: "warn", enabled: true, priority: 10, appliesTo: ["nosecap","tps","shoulder","structure"], op_type: "life_assessment",
      inputs: ["damage.forecast", "failure.assessment"], outputs: ["life.assessment", "life.field"],
      assets: [{ role: "life_baseline", ref: "catalog://life.cert_baseline.v1" }, { role: "mission_profile", ref: "catalog://mission.reentry.nominal" }], runsUsing: ["run_20260610_153000"] },
    // — mapper —
    { model_id: "mapper.field_to_struct.v1", model_type: "mapper", runtime_type: "in_process", version: "1.0.0",
      artifact_ref: "pkg://mapper/field_struct_v1.zip", checksum: "sha256:2f9d…aa10", envelope: "mesh v4 · 热→结构投影",
      validation: "ok", enabled: true, priority: 10, appliesTo: ["structure"], op_type: "field_reconstruction",
      inputs: ["field.T.forecast"], outputs: ["field.S.forecast"],
      assets: [{ role: "mesh", ref: "mesh://rv_biconic_full.v4" }, { role: "projection", ref: "catalog://map.thermal_struct.v1" }], runsUsing: ["run_20260609_201530"] },
  ];

  // ---------- operator catalog (what operators exist + how many implementations) ----------
  const operatorCatalog = [
    { op_type: "state_transition",      label: "状态转移方程", node: "state_transition",   impls: 2, active: "dyn.runtime_engine.v2", optional: false, core: true },
    { op_type: "observation_equation",  label: "观测方程",     node: "observation_equation", impls: 2, active: "obs.runtime_engine.v2", optional: false, core: true },
    { op_type: "filter_algorithm",      label: "滤波算法",     node: "filter_algorithm",  impls: 2, active: "filter.simple_pf.v1", optional: false, core: true },
    { op_type: "qoi_equation",          label: "QoI 方程",     node: "qoi_equation",      impls: 1, active: "qoi.reentry.v1", optional: false, core: false },
    { op_type: "trajectory_projection", label: "弹道投影 → 落点", node: "trajectory_projection", impls: 3, active: "traj.biconic.cli.v3", optional: true, core: false },
    { op_type: "field_reconstruction",  label: "多场重构 P/K/S/T", nodes: ["field_P","field_K","field_S","field_T"], impls: 6, active: "field.*.pod_bpnn.v2", optional: true, core: false },
    { op_type: "field_merge",           label: "场聚合 merge", node: "field_merge",       impls: 1, optional: true, core: false },
    { op_type: "damage_accumulation",   label: "损伤累计",     node: "damage_accumulation", impls: 2, active: "damage.creep_fatigue.v1", optional: true, core: false },
    { op_type: "failure_criterion",     label: "失效判据",     node: "failure_criterion", impls: 1, optional: true, core: false },
    { op_type: "life_assessment",       label: "寿命评估",     node: "life_assessment",   impls: 1, optional: true, core: false },
  ];

  // ---------- operator graph (explicit px layout · correct filter fan-in · iterate to impact) ----------
  const graph = {
    template: "state_prediction_qoi_graph.v1",
    templates: [
      { id: "online_bayes_filter_graph.v1", name: "在线贝叶斯滤波", nodes: 3 },
      { id: "state_prediction_qoi_graph.v1", name: "状态预测 + QoI", nodes: 14, active: true },
      { id: "trajectory_only_forecast", name: "仅弹道预测", nodes: 5 },
      { id: "structural_health_forecast", name: "结构健康预测", nodes: 14 },
    ],
    groups: { physics: { id: "structural_health", label: "可选物理子图 · structural_health", enabled: true } },
    clusters: [
      { id: "filter",   label: "在线滤波 · filter 同时调用 state_transition + observation_equation（predict → update 循环）", x: 18, y: 26, w: 392, h: 174, tone: "acc" },
      { id: "forecast", label: "未来预测 · 从 state.posterior 迭代弹道直到落点 impact", x: 18, y: 232, w: 566, h: 84, tone: "plain" },
      { id: "physics",  label: "可选物理子图 · structural_health（可逐算子动态开关）", x: 18, y: 338, w: 846, h: 198, tone: "warn", group: true },
    ],
    nodes: [
      { id: "state_transition", label: "state_transition", type: "state_transition", optional: false, x: 44, y: 52, status: "ok",
        operator_id: "op.dyn.runtime_engine.v2", op_role: "propagate", exec: "in_process",
        inputs: ["state.posterior(t-1)", "control.u"], outputs: ["state.predicted"],
        assets: [{ role: "dynamics_core", ref: "in_process://RuntimeEngineV2::transition" }],
        reason: "", note: "被 filter 在每个粒子上调用做状态推进（predict）" },
      { id: "observation_equation", label: "observation_equation", type: "observation_equation", optional: false, x: 44, y: 142, status: "ok",
        operator_id: "op.obs.runtime_engine.v2", op_role: "observe", exec: "in_process",
        inputs: ["state.predicted"], outputs: ["observation.predicted"],
        assets: [{ role: "sensor_model", ref: "in_process://RuntimeEngineV2::observe" }],
        reason: "", note: "被 filter 调用，从预测状态生成预测观测，与实测对比" },
      { id: "filter_algorithm", label: "filter_algorithm", type: "filter_algorithm", optional: false, x: 236, y: 97, status: "ok",
        operator_id: "op.filter.simple_pf.v1", op_role: "bayes_update", exec: "in_process",
        inputs: ["state.predicted", "observation.predicted", "observation.measured"], outputs: ["state.posterior", "filter.diagnostics"],
        assets: [{ role: "transition_op", ref: "op.dyn.runtime_engine.v2" }, { role: "observation_op", ref: "op.obs.runtime_engine.v2" }],
        reason: "", note: "滤波是编排核心：同时使用 state_transition（predict）与 observation_equation（update），不是三者顺序串联" },
      { id: "qoi_equation", label: "qoi_equation", type: "qoi_equation", optional: false, x: 44, y: 256, status: "ok",
        operator_id: "op.qoi.reentry.v1", op_role: "qoi", exec: "in_process",
        inputs: ["state.future"], outputs: ["qoi.series"],
        assets: [{ role: "qoi_def", ref: "catalog://qoi.reentry.v1" }], reason: "", note: "沿未来弹道计算热流/动压/攻角/落点等 QoI" },
      { id: "trajectory_projection", label: "trajectory_projection", type: "trajectory_projection", optional: true, x: 236, y: 256, status: "ok",
        operator_id: "op.traj.biconic.cli.v3", op_role: "trajectory", exec: "cli_exe",
        inputs: ["state.posterior"], outputs: ["trajectory.forecast", "state.future", "impact.point"],
        assets: [{ role: "trajectory_package", ref: "catalog://pkg.traj.biconic.v3" }],
        reason: "", note: "从当前 state.posterior 出发，按 dt 迭代积分弹道，直到 altitude=0（落点 impact）" },
      { id: "impact", label: "落点 impact", type: "terminal", optional: false, x: 470, y: 261, status: "ok", w: 116, h: 38,
        operator_id: "impact.point", op_role: "terminal", exec: "—",
        inputs: ["trajectory.forecast"], outputs: ["impact.point"],
        assets: [], reason: "", note: "弹道迭代终止条件：高度=0 落点，输出落点经纬度/时间/速度" },
      { id: "field_P", label: "field.P.predict", type: "field_reconstruction", optional: true, subject: "P", x: 40, y: 360, status: "ok",
        operator_id: "op.field.P.pod_bpnn.v2", op_role: "field:P", exec: "in_process",
        inputs: ["trajectory.forecast", "state.future"], outputs: ["field.P.forecast"],
        assets: [{ role: "pod_basis", ref: "catalog://asset.pod.P" }, { role: "bpnn_network", ref: "catalog://asset.bpnn.predP" }], reason: "" },
      { id: "field_K", label: "field.K.predict", type: "field_reconstruction", optional: true, subject: "K", x: 212, y: 360, status: "degraded",
        operator_id: "op.field.K.pod_bpnn.v2", op_role: "field:K", exec: "in_process",
        inputs: ["trajectory.forecast", "state.future"], outputs: ["field.K.forecast"],
        assets: [{ role: "pod_basis", ref: "catalog://asset.pod.K" }, { role: "bpnn_network", ref: "catalog://asset.bpnn.predK" }],
        reason: "POD 基外插超出适用包络 (M>15)，输出 degraded" },
      { id: "field_S", label: "field.S.predict", type: "field_reconstruction", optional: true, subject: "S", x: 40, y: 432, status: "ok",
        operator_id: "op.field.S.pod_bpnn.v2", op_role: "field:S", exec: "in_process",
        inputs: ["trajectory.forecast", "state.future"], outputs: ["field.S.forecast"],
        assets: [{ role: "pod_basis", ref: "catalog://asset.pod.S" }], reason: "" },
      { id: "field_T", label: "field.T.predict", type: "field_reconstruction", optional: true, subject: "T", x: 212, y: 432, status: "ok",
        operator_id: "op.field.T.pod_bpnn.v2", op_role: "field:T", exec: "in_process",
        inputs: ["trajectory.forecast", "state.future"], outputs: ["field.T.forecast"],
        assets: [{ role: "pod_basis", ref: "catalog://asset.pod.T" }], reason: "" },
      { id: "field_merge", label: "field.merge", type: "field_merge", optional: true, x: 392, y: 396, status: "degraded",
        operator_id: "op.field.merge.v1", op_role: "merge", exec: "in_process",
        inputs: ["field.P.forecast", "field.K.forecast", "field.S.forecast", "field.T.forecast"], outputs: ["field.forecast"],
        assets: [], reason: "subject=K 为 degraded，merge 输出 degraded 并列出缺失/降级 subject", note: "仅做容器聚合 + 布局/时间一致性检查，不做物理耦合融合" },
      { id: "damage_accumulation", label: "damage_accumulation", type: "damage_accumulation", optional: true, x: 560, y: 396, status: "ok",
        operator_id: "op.damage.creep_fatigue.v1", op_role: "accumulate", exec: "in_process",
        inputs: ["damage.initial", "field.forecast"], outputs: ["damage.forecast"],
        assets: [{ role: "material_db", ref: "db://material_creep.sqlite" }, { role: "damage_baseline", ref: "catalog://health.nose_cap.baseline" }],
        reason: "", note: "初始损伤来自健康账本基准，不从 0 开始；沿 dt 积分裁剪到 [0,1]" },
      { id: "failure_criterion", label: "failure_criterion", type: "failure_criterion", optional: true, x: 712, y: 360, status: "ok",
        operator_id: "op.failure.threshold.v1", op_role: "limit", exec: "in_process",
        inputs: ["damage.forecast", "field.T.forecast", "field.S.forecast"], outputs: ["failure.assessment"],
        assets: [{ role: "limits", ref: "catalog://limits.tps.v1" }], reason: "" },
      { id: "life_assessment", label: "life_assessment", type: "life_assessment", optional: true, x: 712, y: 452, status: "warn",
        operator_id: "op.life.rul_baseline.v1", op_role: "rul", exec: "in_process",
        inputs: ["damage.forecast", "failure.assessment"], outputs: ["life.assessment", "life.field"],
        assets: [{ role: "life_baseline", ref: "catalog://life.cert_baseline.v1" }],
        reason: "当前 M 接近适用包络边缘，RUL confidence=medium" },
    ],
    edges: [
      { from: "state_transition", to: "observation_equation" },
      { from: "state_transition", to: "filter_algorithm" },
      { from: "observation_equation", to: "filter_algorithm" },
      { from: "filter_algorithm", to: "state_transition", kind: "fb", label: "t → t+1" },
      { from: "filter_algorithm", to: "trajectory_projection", kind: "v", label: "state.posterior" },
      { from: "trajectory_projection", to: "qoi_equation", label: "state.future" },
      { from: "trajectory_projection", to: "impact", label: "× N 步 → 落点" },
      { from: "trajectory_projection", to: "field_P", kind: "v" },
      { from: "trajectory_projection", to: "field_K", kind: "v" },
      { from: "trajectory_projection", to: "field_S", kind: "v" },
      { from: "trajectory_projection", to: "field_T", kind: "v" },
      { from: "field_P", to: "field_merge" },
      { from: "field_K", to: "field_merge" },
      { from: "field_S", to: "field_merge" },
      { from: "field_T", to: "field_merge" },
      { from: "field_merge", to: "damage_accumulation" },
      { from: "damage_accumulation", to: "failure_criterion" },
      { from: "damage_accumulation", to: "life_assessment" },
      { from: "failure_criterion", to: "life_assessment" },
    ],
    canW: 884, canH: 552,
  };

  const evidence = [
    { name: "run_manifest.json",      size: "4.1 KB",  status: "ok",   desc: "SDK/UI 第一读取入口" },
    { name: "graph_snapshot.json",    size: "18.7 KB", status: "ok",   desc: "本次 graph 模板与节点关系" },
    { name: "operator_snapshot.json", size: "62.3 KB", status: "ok",   desc: "每算子端口/执行形态/资产" },
    { name: "resource_lock.json",     size: "9.8 KB",  status: "ok",   desc: "锁定的 DB/POD/BPNN/mesh/traj" },
    { name: "model_snapshot.json",    size: "5.2 KB",  status: "ok",   desc: "每类模型选中的 model_id/version" },
    { name: "graph_outputs.json",     size: "1.4 MB",  status: "warn", desc: "端口输出 · field.K degraded" },
    { name: "graph_run_evidence.json",size: "11.0 KB", status: "ok",   desc: "run 级状态汇总" },
    { name: "workflow_timeline.json", size: "28.4 KB", status: "ok",   desc: "帧级执行与吞吐记录" },
  ];

  // resource lock entries
  const resourceLock = [
    { role: "trajectory_package", ref: "pkg://trajectory/biconic_v3.zip", checksum: "9f2c…b41a", schema: "v3" },
    { role: "task_field_db",      ref: "db://task_field_2026q2.sqlite",   checksum: "a30c…71ff", schema: "v7" },
    { role: "pod_basis.PKST",     ref: "asset://pod/pkst_basis.npz",      checksum: "1ad7…77ce", schema: "v2" },
    { role: "bpnn_network.PKST",  ref: "asset://bpnn/pkst_net.onnx",      checksum: "be41…3a2c", schema: "v2" },
    { role: "mesh",               ref: "mesh://rv_biconic_full.v4",       checksum: "77de…9012", schema: "v4" },
    { role: "material_db",        ref: "db://material_creep.sqlite",      checksum: "7b18…ee31", schema: "v3" },
    { role: "life_baseline",      ref: "catalog://life.cert_baseline.v1", checksum: "55aa…0c2f", schema: "v1" },
  ];

  // ---------- health ledger ----------
  const regions = [
    { id: "nose_cap", name: "Nose Cap", object: "obj.nose_cap", damage: 0.62, saturated: false, rul_s: 286, conf: "medium",
      first_exc: 312, first_exc_kind: "温度超限 1750K", envelope: "M 6–18 · 适用",
      field: "life", material: "C/C carbon · v3",
      increments: [
        { run: "run_20260610_153000", frames: "0–1982", model: "damage.creep_fatigue.v1", d: 0.04, src: "field.T.forecast" },
        { run: "run_20260610_142210", frames: "0–3204", model: "damage.creep_fatigue.v1", d: 0.07, src: "field.T.forecast" },
        { run: "run_20260609_201530", frames: "0–4102", model: "damage.creep_fatigue.v1", d: 0.06, src: "field.T.forecast" },
      ],
      corrections: [ { date: "2026-05-28", from: 0.51, to: 0.45, by: "工程师 · 李", reason: "目视检查 + 超声未发现裂纹，回退累计值" } ],
      trendDamage: [0.31,0.36,0.40,0.45,0.49,0.51,0.55,0.62],
      trendRul: [520,470,440,410,360,330,310,286] },
    { id: "tps_windward", name: "TPS Windward", object: "obj.tps_windward", damage: 0.41, saturated: false, rul_s: 540, conf: "high",
      first_exc: null, first_exc_kind: "—", envelope: "M 6–18 · 适用",
      field: "damage", material: "Ablative TPS · v2",
      increments: [
        { run: "run_20260610_153000", frames: "0–1982", model: "damage.creep_fatigue.v1", d: 0.03, src: "field.T.forecast" },
        { run: "run_20260610_142210", frames: "0–3204", model: "damage.creep_fatigue.v1", d: 0.05, src: "field.T.forecast" },
      ],
      corrections: [],
      trendDamage: [0.18,0.21,0.25,0.28,0.31,0.34,0.38,0.41],
      trendRul: [820,780,720,690,640,600,570,540] },
    { id: "shoulder", name: "Leading-edge Shoulder", object: "obj.shoulder", damage: 0.88, saturated: false, rul_s: 64, conf: "low",
      first_exc: 58, first_exc_kind: "应力超限 430MPa", envelope: "AoA>12° · 边缘",
      field: "stress", material: "C/SiC · v1",
      increments: [
        { run: "run_20260610_153000", frames: "0–1982", model: "damage.creep_fatigue.v1", d: 0.09, src: "field.S.forecast" },
        { run: "run_20260610_142210", frames: "0–3204", model: "damage.creep_fatigue.v1", d: 0.11, src: "field.S.forecast" },
      ],
      corrections: [],
      trendDamage: [0.42,0.51,0.58,0.66,0.71,0.78,0.83,0.88],
      trendRul: [310,250,200,160,130,98,80,64] },
    { id: "structure", name: "Primary Structure", object: "obj.primary_struct", damage: 0.12, saturated: false, rul_s: 2100, conf: "high",
      first_exc: null, first_exc_kind: "—", envelope: "全包络 · 适用",
      field: "stress", material: "Ti alloy · v1",
      increments: [
        { run: "run_20260610_153000", frames: "0–1982", model: "damage.creep_fatigue.v1", d: 0.01, src: "field.S.forecast" },
      ],
      corrections: [],
      trendDamage: [0.05,0.06,0.07,0.08,0.09,0.10,0.11,0.12],
      trendRul: [3200,3000,2800,2600,2480,2300,2200,2100] },
  ];

  // ---------- diagnostics ----------
  const diagPreflight = [
    { check: "catalog 存在", status: "ok",   detail: "platform-catalog.json · 已加载 4 模型 / 7 资产" },
    { check: "model binding 完整", status: "ok", detail: "trajectory/field/damage/life 均绑定" },
    { check: "artifact 存在", status: "ok",   detail: "7/7 artifact 可解析" },
    { check: "checksum 匹配", status: "ok",   detail: "7/7 checksum 通过" },
    { check: "端口 contract 匹配", status: "warn", detail: "field.K.forecast contract 版本 v2，下游声明 v2.1 — 兼容降级" },
    { check: "单位匹配", status: "ok",        detail: "K / MPa / kPa / με 一致" },
  ];
  const diagRuntime = {
    ok: [
      { severity: "info", source: "filter_algorithm", reason: "ESS 0.71 高于重采样阈值 0.5", action: "无需处理" },
      { severity: "info", source: "trajectory_projection", reason: "CLI 退出码 0 · 88ms", action: "无需处理" },
    ],
    degraded: [
      { severity: "warn", source: "field.K.predict", reason: "输入 M=15.2 超出 POD 适用包络 (8–15)，外插", action: "切换 field.K 高马赫模型或缩小 horizon" },
      { severity: "warn", source: "field.merge", reason: "subject=K 为 degraded，merge 输出 degraded", action: "结果可用但 K 场不计入风险判定" },
      { severity: "warn", source: "life_assessment", reason: "RUL 置信度 medium，处于适用包络边缘", action: "采集更多本包络飞行数据再确认" },
    ],
    failed: [
      { severity: "error", source: "trajectory_projection", reason: "CLI 退出码 3 · nonzero_exit · 输入状态 NaN", action: "检查 state.posterior 协方差是否发散" },
      { severity: "error", source: "field.merge", reason: "missing_input · field.T.forecast 上游失败", action: "修复 field.T 后重跑 forecast 子图" },
      { severity: "error", source: "life_assessment", reason: "disabled · 上游 damage.forecast missing，输出 unknown", action: "本帧不写 RUL，标记 unknown/reason" },
    ],
  };

  // ---------- config ----------
  const config = {
    run_mode: "标准在线流程",
    data_source: "database",
    db_path: "_local_artifacts/db/task_field_2026q2.sqlite",
    retrain: false,
    archive: true,
    frame_storage: "both",
    output_dir: "_local_artifacts/online-runs/run_20260610_153000/",
  };

  window.FE = {
    ctx, state, stages, risk, modelSnapshot, runs, filter, forecast, series, trajTable,
    timeline, objectTree, sensors, models, operatorCatalog, graph, evidence, resourceLock, regions,
    diagPreflight, diagRuntime, config,
  };
})();
