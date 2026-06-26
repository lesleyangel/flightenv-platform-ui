/* ===================================================================
   Two-level orchestration model.  window.FE.orchestration
   STATE currency = POD coefficients of 4 fields + trajectory.

   Level-1 framework (固化):
     state_transition ─┐
                       ├→ filter(滤波融合) ─┬→ qoi
     observation ──────┘                    └→ state_transition(预测) → qoi(预测)
   · state_transition(预测) 与 state_transition 是同一套配置
   · 预测额外配置：预测频率 + 停止条件（落点 / 多弹道 / 结构破坏）

   Level-2 (动态算子编排):
     state_transition = 顺序耦合链 traj→{predP,predK}→predST
   =================================================================== */
(function () {
  const W = 158, H = 46;

  const stateVector = [
    { sym: "x_traj", name: "弹道状态向量", dim: 6, port: "trajectory", domain: "traj" },
    { sym: "α_P", name: "压力场 POD 系数", dim: 32, port: "coeff.P", domain: "aero" },
    { sym: "α_K", name: "热流场 POD 系数", dim: 24, port: "coeff.K", domain: "aero" },
    { sym: "α_S", name: "应力场 POD 系数", dim: 28, port: "coeff.S", domain: "struct" },
    { sym: "α_T", name: "温度场 POD 系数", dim: 36, port: "coeff.T", domain: "aero" },
  ];

  const level1 = [
    { id: "state_transition", name: "状态转移方程", en: "state_transition", ops: 4, online: true,
      io: "STATE(t) → STATE(t+1)", kind: "transition",
      desc: "顺序耦合链，不是单一方程：弹道单步 → predP / predK（用上一时刻 ST）→ predST（用本步 P/K 闭合）。所有输出均为 t+1。在线与预测共用同一套配置。" },
    { id: "observation", name: "观测方程", en: "observation_equation", ops: 4, online: true,
      io: "STATE → observation.predicted", kind: "observe",
      desc: "POD 基仅在传感测点处还原场值（不还原全场），再过传感器模型得到预测观测。" },
    { id: "filter", name: "滤波融合", en: "filter_fusion", ops: 5, online: true,
      io: "状态转移 + 观测 融合 → STATE.posterior", kind: "filter",
      desc: "融合核心：predict 复用『状态转移』、update 复用『观测』，在 POD 系数空间上把预测与实测观测融合为后验。" },
    { id: "qoi", name: "QoI 方程", en: "qoi_equation", ops: 7, online: true,
      io: "STATE → 场 / 损伤 / 失效 / 寿命", kind: "qoi",
      desc: "把系数还原成全场并计算 QoI。在线在滤波融合后跑一次；预测中每个预测步跑一次。QoI 输出不回写状态。" },
    { id: "forecast", name: "预测", en: "forecast_loop", ops: 3, online: false,
      io: "迭代（状态转移 + QoI）→ 停止条件", kind: "forecast",
      desc: "预测 = 状态转移（同一配置）+ QoI 迭代。额外配置：预测频率、停止条件（弹道到落点 / 多条弹道 / 结构破坏为止）。" },
  ];

  // ---- Level-1 fixed framework wiring (for the top strip) ----
  const framework = {
    nodes: {
      state_transition: { x: 16, y: 12 }, observation: { x: 16, y: 96 },
      filter: { x: 226, y: 54 }, qoi: { x: 440, y: 12 }, forecast: { x: 440, y: 96 },
    },
    edges: [
      { from: "state_transition", to: "filter" },
      { from: "observation", to: "filter" },
      { from: "filter", to: "qoi", label: "posterior" },
      { from: "filter", to: "forecast", label: "→ 状态转移(预测)" },
    ],
    fwW: 620, fwH: 168,
  };

  const forecastConfig = {
    frequency: "每 0.5 s · 10 帧",
    horizon: "至停止条件",
    stopOptions: [
      { id: "impact", label: "弹道到落点 impact", sub: "高度 = 0 终止", on: true },
      { id: "multi", label: "多条弹道 Monte-Carlo", sub: "N=64 采样落点散布", on: false },
      { id: "failure", label: "结构破坏为止", sub: "首次失效判据触发即停", on: false },
    ],
  };

  // ---- Level-2 subgraphs ----
  const subgraphs = {
    // ============ STATE TRANSITION (sequential coupled chain) ============
    state_transition: {
      w: 900, h: 312, inX: 150, outX: 772,
      inLabel: "STATE(t) · 当前系数 + 弹道", outLabel: "STATE(t+1)",
      inPorts: ["trajectory", "coeff.P", "coeff.K", "coeff.S/T"],
      outPorts: ["trajectory'", "coeff.P'", "coeff.K'", "coeff.S'/T'"],
      note: "顺序链：弹道先推进到 t+1；predP/predK 用 t+1 弹道 + 上一时刻 ST；predST 再用本步 P'/K' 闭合。可动态配置参与的场。",
      nodes: [
        { id: "traj_step", label: "trajectory.step", op: "弹道单步推进", exec: "cli_exe", x: 190, y: 130, optional: false, status: "ok",
          inputs: ["trajectory(t)", "control.u"], outputs: ["trajectory'(t+1)"], operator_id: "op.traj.step.biconic.v3", deps: [],
          assets: [{ role: "trajectory_package", ref: "catalog://pkg.traj.biconic.v3" }],
          note: "链首：积分一个 dt 得到下一时刻弹道，供后续场系数推进使用。" },
        { id: "predP", label: "predP", op: "压力系数推进", exec: "in_process", x: 410, y: 44, optional: true, status: "ok", subject: "P",
          inputs: ["trajectory'", "coeff.S/T(t)"], outputs: ["coeff.P'"], operator_id: "op.coeff.predP.pod_bpnn.v2", deps: ["traj_step"],
          assets: [{ role: "pod_basis", ref: "asset://pod/P_basis.npz" }, { role: "bpnn", ref: "asset://bpnn/predP.onnx" }],
          note: "输入 = t+1 弹道 + 上一时刻 ST 系数，输出 t+1 压力系数。" },
        { id: "predK", label: "predK", op: "热流系数推进", exec: "in_process", x: 410, y: 216, optional: true, status: "degraded", subject: "K",
          inputs: ["trajectory'", "coeff.S/T(t)"], outputs: ["coeff.K'"], operator_id: "op.coeff.predK.pod_bpnn.v2", deps: ["traj_step"],
          assets: [{ role: "pod_basis", ref: "asset://pod/K_basis.npz" }, { role: "bpnn", ref: "asset://bpnn/predK.onnx" }],
          reason: "POD-BPNN 系数推进外插超出适用包络 (M>15)，输出 degraded。" },
        { id: "predST", label: "predST", op: "应力+温度系数推进", exec: "in_process", x: 600, y: 130, optional: true, status: "ok", subject: "S/T",
          inputs: ["trajectory'", "coeff.P'", "coeff.K'"], outputs: ["coeff.S'", "coeff.T'"], operator_id: "op.coeff.predST.pod_bpnn.v2", deps: ["traj_step", "predP", "predK"],
          assets: [{ role: "pod_basis", ref: "asset://pod/ST_basis.npz" }, { role: "bpnn", ref: "asset://bpnn/predST.onnx" }],
          note: "链尾：用本步压力/热流闭合，联合推进应力与温度系数（S、T 热-力耦合，一并求解）。" },
      ],
      edges: [
        { from: "IN", to: "traj_step", label: "traj(t)" },
        { from: "traj_step", to: "predP", label: "traj'" }, { from: "traj_step", to: "predK", label: "traj'" }, { from: "traj_step", to: "predST" },
        { from: "IN", to: "predP", label: "ST(t)" }, { from: "IN", to: "predK" },
        { from: "predP", to: "predST", label: "P'" }, { from: "predK", to: "predST", label: "K'" },
        { from: "traj_step", to: "OUT" }, { from: "predP", to: "OUT" }, { from: "predK", to: "OUT" }, { from: "predST", to: "OUT" },
      ],
    },

    // ============ OBSERVATION ============
    observation: {
      w: 720, h: 286, inX: 150, outX: 560,
      inLabel: "STATE · 系数", outLabel: "observation.predicted",
      inPorts: ["coeff.T", "coeff.S", "coeff.P"],
      outPorts: ["obs.predicted"],
      note: "POD 基只在测点节点上做局部还原（系数→测点值），再过传感器模型；与实测观测的融合在滤波块完成。",
      nodes: [
        { id: "obs_T", label: "pod.sample.T@TC", op: "测点温度还原", exec: "in_process", x: 210, y: 26, optional: false, status: "ok",
          inputs: ["coeff.T"], outputs: ["pred.T@TC"], operator_id: "op.pod.sample.T.v1", deps: [],
          assets: [{ role: "pod_basis_sensor", ref: "asset://pod/T_basis@sensors.npz" }],
          note: "POD 基在热电偶节点处把 α_T 还原成温度，不还原全场，省算力。" },
        { id: "obs_S", label: "pod.sample.S@SG", op: "测点应变还原", exec: "in_process", x: 210, y: 96, optional: false, status: "ok",
          inputs: ["coeff.S"], outputs: ["pred.S@SG"], operator_id: "op.pod.sample.S.v1", deps: [],
          assets: [{ role: "pod_basis_sensor", ref: "asset://pod/S_basis@sensors.npz" }] },
        { id: "obs_P", label: "pod.sample.P@PT", op: "测点压力还原", exec: "in_process", x: 210, y: 166, optional: false, status: "ok",
          inputs: ["coeff.P"], outputs: ["pred.P@PT"], operator_id: "op.pod.sample.P.v1", deps: [],
          assets: [{ role: "pod_basis_sensor", ref: "asset://pod/P_basis@sensors.npz" }] },
        { id: "obs_model", label: "sensor.model", op: "传感器观测模型", exec: "in_process", x: 430, y: 96, optional: false, status: "ok",
          inputs: ["pred.T@TC", "pred.S@SG", "pred.P@PT"], outputs: ["observation.predicted"], operator_id: "op.obs.sensor_model.v2", deps: ["obs_T", "obs_S", "obs_P"],
          assets: [{ role: "sensor_cfg", ref: "catalog://obs.sensor_set.v2" }],
          note: "加偏置/噪声模型，组装成与实测同构的预测观测向量。" },
      ],
      edges: [
        { from: "IN", to: "obs_T" }, { from: "IN", to: "obs_S" }, { from: "IN", to: "obs_P" },
        { from: "obs_T", to: "obs_model" }, { from: "obs_S", to: "obs_model" }, { from: "obs_P", to: "obs_model" },
        { from: "obs_model", to: "OUT" },
      ],
    },

    // ============ FILTER (滤波融合) ============
    filter: {
      w: 880, h: 286, inX: 150, outX: 840,
      inLabel: "STATE.posterior(t-1) · observation.measured", outLabel: "STATE.posterior · diagnostics",
      inPorts: ["STATE.posterior(t-1)", "observation.measured"],
      outPorts: ["STATE.posterior", "filter.diagnostics"],
      note: "粒子滤波融合：predict 复用『状态转移』、update 复用『观测』，把预测与实测在 POD 系数空间融合。",
      nodes: [
        { id: "pf_predict", label: "pf.predict", op: "粒子预测", exec: "in_process", x: 160, y: 118, optional: false, status: "ok", reuse: "state_transition",
          inputs: ["STATE.posterior(t-1)"], outputs: ["particles.predicted"], operator_id: "reuse://state_transition", deps: [],
          note: "对每个粒子调用『状态转移方程』块，推进系数 + 弹道。" },
        { id: "pf_observe", label: "pf.observe", op: "粒子观测", exec: "in_process", x: 335, y: 118, optional: false, status: "ok", reuse: "observation",
          inputs: ["particles.predicted"], outputs: ["obs.predicted"], operator_id: "reuse://observation", deps: ["pf_predict"],
          note: "对每个粒子调用『观测方程』块，生成预测观测。" },
        { id: "pf_weight", label: "pf.fuse", op: "观测融合 / 权重", exec: "in_process", x: 510, y: 58, optional: false, status: "ok",
          inputs: ["obs.predicted", "observation.measured"], outputs: ["weights"], operator_id: "op.pf.fuse.gauss.v1", deps: ["pf_observe"],
          note: "预测观测与实测观测融合，按似然给粒子加权。" },
        { id: "pf_resample", label: "pf.resample", op: "残差重采样", exec: "in_process", x: 510, y: 168, optional: true, status: "ok",
          inputs: ["weights"], outputs: ["particles.resampled"], operator_id: "op.pf.resample.residual.v1", deps: ["pf_weight"],
          note: "ESS 低于阈值 0.5 时触发，可换重采样策略。" },
        { id: "pf_post", label: "pf.posterior", op: "后验估计", exec: "in_process", x: 685, y: 118, optional: false, status: "ok",
          inputs: ["particles.resampled", "weights"], outputs: ["STATE.posterior", "filter.diagnostics"], operator_id: "op.pf.posterior.v1", deps: ["pf_weight"] },
      ],
      edges: [
        { from: "IN", to: "pf_predict" }, { from: "pf_predict", to: "pf_observe" }, { from: "pf_observe", to: "pf_weight" },
        { from: "IN", to: "pf_weight", label: "measured" }, { from: "pf_weight", to: "pf_resample" }, { from: "pf_resample", to: "pf_post" },
        { from: "pf_weight", to: "pf_post" }, { from: "pf_post", to: "OUT" },
      ],
    },

    // ============ QOI (standalone block) ============
    qoi: {
      w: 820, h: 300, inX: 150, outX: 700,
      inLabel: "STATE · 系数", outLabel: "QoI 输出（不回写状态）",
      inPorts: ["coeff.T", "coeff.S", "coeff.K", "coeff.P"],
      outPorts: ["field.forecast", "damage.forecast", "failure.assessment", "life.assessment"],
      note: "POD 系数→全场，再算损伤/失效/寿命。在线滤波后跑一次；预测每步跑一次。损伤/失效/寿命可逐算子开关。",
      nodes: [
        { id: "qoi_recT", label: "pod.reconstruct.T", op: "POD 还原温度全场", exec: "in_process", x: 200, y: 22, optional: false, status: "ok", subject: "T",
          inputs: ["coeff.T"], outputs: ["field.T.forecast"], operator_id: "op.pod.reconstruct.T.v2", deps: [],
          assets: [{ role: "pod_basis", ref: "asset://pod/T_basis.npz" }, { role: "mesh", ref: "mesh://rv_biconic_full.v4" }],
          note: "系数→全场，用于输出/损伤/可视化，不回到状态。" },
        { id: "qoi_recS", label: "pod.reconstruct.S", op: "POD 还原应力全场", exec: "in_process", x: 200, y: 88, optional: false, status: "ok", subject: "S",
          inputs: ["coeff.S"], outputs: ["field.S.forecast"], operator_id: "op.pod.reconstruct.S.v2", deps: [],
          assets: [{ role: "pod_basis", ref: "asset://pod/S_basis.npz" }] },
        { id: "qoi_recK", label: "pod.reconstruct.K", op: "POD 还原热流全场", exec: "in_process", x: 200, y: 154, optional: true, status: "degraded", subject: "K",
          inputs: ["coeff.K"], outputs: ["field.K.forecast"], operator_id: "op.pod.reconstruct.K.v2", deps: [],
          assets: [{ role: "pod_basis", ref: "asset://pod/K_basis.npz" }],
          reason: "α_K 来自 degraded 的系数推进，还原场继承 degraded。" },
        { id: "qoi_recP", label: "pod.reconstruct.P", op: "POD 还原压力全场", exec: "in_process", x: 200, y: 220, optional: true, status: "ok", subject: "P",
          inputs: ["coeff.P"], outputs: ["field.P.forecast"], operator_id: "op.pod.reconstruct.P.v2", deps: [],
          assets: [{ role: "pod_basis", ref: "asset://pod/P_basis.npz" }] },
        { id: "qoi_damage", label: "damage_accumulation", op: "损伤累计", exec: "in_process", x: 400, y: 88, optional: true, status: "ok",
          inputs: ["field.T.forecast", "field.S.forecast"], outputs: ["damage.forecast"], operator_id: "op.damage.creep_fatigue.v1", deps: ["qoi_recT", "qoi_recS"],
          assets: [{ role: "material_db", ref: "db://material_creep.sqlite" }],
          note: "初始损伤取自健康账本基准，沿 dt 累计，裁剪到 [0,1]。" },
        { id: "qoi_failure", label: "failure_criterion", op: "失效判据", exec: "in_process", x: 400, y: 180, optional: true, status: "ok",
          inputs: ["damage.forecast", "field.T.forecast", "field.S.forecast"], outputs: ["failure.assessment"], operator_id: "op.failure.threshold.v1", deps: ["qoi_damage"],
          assets: [{ role: "limits", ref: "catalog://limits.tps.v1" }],
          note: "也是预测『结构破坏为止』停止条件的触发源。" },
        { id: "qoi_life", label: "life_assessment", op: "寿命评估", exec: "in_process", x: 560, y: 134, optional: true, status: "warn",
          inputs: ["damage.forecast", "failure.assessment"], outputs: ["life.assessment"], operator_id: "op.life.rul_baseline.v1", deps: ["qoi_damage", "qoi_failure"],
          assets: [{ role: "life_baseline", ref: "catalog://life.cert_baseline.v1" }],
          reason: "当前 M 接近适用包络边缘，RUL confidence=medium。" },
      ],
      edges: [
        { from: "IN", to: "qoi_recT" }, { from: "IN", to: "qoi_recS" }, { from: "IN", to: "qoi_recK" }, { from: "IN", to: "qoi_recP" },
        { from: "qoi_recT", to: "qoi_damage" }, { from: "qoi_recS", to: "qoi_damage" },
        { from: "qoi_damage", to: "qoi_failure" }, { from: "qoi_damage", to: "qoi_life" }, { from: "qoi_failure", to: "qoi_life" },
        { from: "qoi_recK", to: "OUT" }, { from: "qoi_recP", to: "OUT" }, { from: "qoi_damage", to: "OUT" }, { from: "qoi_life", to: "OUT" },
      ],
    },

    // ============ FORECAST (预测: reuse state_transition + qoi, loop + stop) ============
    forecast: {
      w: 720, h: 300, inX: 150, outX: 580, hasConfig: true,
      inLabel: "STATE.posterior · 起点", outLabel: "预测输出（不回写状态）",
      inPorts: ["STATE.posterior"],
      outPorts: ["forecast.series", "field.forecast", "damage.forecast", "impact / 破坏"],
      note: "迭代：状态转移（同一配置）推进系数 → QoI（同一配置）算场/损伤/寿命 → 停止条件判定，未停则按预测频率回到状态转移。",
      nodes: [
        { id: "fc_st", label: "state_transition", op: "状态转移 · 同一配置", exec: "in_process", x: 200, y: 60, optional: false, status: "ok", reuse: "state_transition",
          inputs: ["STATE(t+k)"], outputs: ["STATE(t+k+1)"], operator_id: "reuse://state_transition", deps: [],
          note: "复用在线『状态转移』块（同一套配置），逐步推进 POD 系数与弹道。" },
        { id: "fc_qoi", label: "qoi", op: "QoI · 同一配置", exec: "in_process", x: 400, y: 60, optional: false, status: "ok", reuse: "qoi",
          inputs: ["STATE(t+k+1)"], outputs: ["field.forecast", "damage.forecast", "life.assessment"], operator_id: "reuse://qoi", deps: ["fc_st"],
          note: "复用『QoI』块，每个预测步算一次场/损伤/失效/寿命。输出不回写状态。" },
        { id: "fc_stop", label: "stop_condition", op: "停止条件", exec: "in_process", x: 200, y: 180, optional: false, status: "ok",
          inputs: ["STATE(t+k+1)", "failure.assessment"], outputs: ["impact / 破坏", "loop"], operator_id: "op.forecast.stop.v1", deps: ["fc_st"],
          note: "可配：弹道到落点 / 多条弹道 / 结构破坏为止。满足即终止，否则按预测频率回到状态转移。" },
      ],
      edges: [
        { from: "IN", to: "fc_st" },
        { from: "fc_st", to: "fc_qoi", label: "STATE(t+k+1)" },
        { from: "fc_st", to: "fc_stop" },
        { from: "fc_stop", to: "fc_st", kind: "fb", label: "按预测频率 · loop" },
        { from: "fc_qoi", to: "OUT" },
        { from: "fc_stop", to: "OUT", label: "终止" },
      ],
    },
  };

  window.FE.orchestration = { stateVector, level1, framework, forecastConfig, subgraphs, W, H };
})();
