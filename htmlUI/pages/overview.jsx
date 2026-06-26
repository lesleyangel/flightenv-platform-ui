/* ===================== 总览 Overview ===================== */
function PageOverview({ go }) {
  const D = window.FE;
  const stCls = { ok: "s-ok", warn: "s-warn", fail: "s-fail", unk: "s-unk", run: "s-run" };
  const arrow = <span className="stage-arrow"><Icon name="chevR" size={16} /></span>;

  return (
    <div className="page-pad">
      <div className="page-head">
        <div><h1>总览</h1><p>这套数字孪生当前处于什么状态 · {D.ctx.object_name} · {D.ctx.phase}</p></div>
        <div className="row gap8">
          <button className="btn sm" onClick={() => go("diagnostics")}><Icon name="shield" size={14} />诊断报告</button>
          <button className="btn sm primary" onClick={() => go("online")}><Icon name="activity" size={14} />进入在线运行</button>
        </div>
      </div>

      {/* object state summary */}
      <div className="grid" style={{ gridTemplateColumns: "1.5fr 1fr", marginBottom: 12 }}>
        <Panel title="对象状态摘要" icon="cube" sub="state.posterior" right={<Badge status="ok">{D.state.state_source}</Badge>}>
          <div className="grid" style={{ gridTemplateColumns: "repeat(4,1fr)", gap: 10 }}>
            {[
              ["高度", D.state.altitude_km, "km"],
              ["马赫数", D.state.mach, "Ma"],
              ["攻角", D.state.aoa_deg, "°"],
              ["速度", D.state.velocity_ms, "m/s"],
              ["动压", D.state.dynamic_pressure_kpa, "kPa"],
              ["驻点热流", D.state.heat_flux_mw, "MW/m²"],
              ["状态帧", D.state.frame_id, ""],
              ["新鲜度", D.state.freshness_ms, "ms"],
            ].map((m, i) => (
              <div key={i} style={{ padding: "8px 0" }}>
                <div className="tiny muted" style={{ marginBottom: 2 }}>{m[0]}</div>
                <div className="num" style={{ fontSize: 19, fontWeight: 700, color: "var(--ink)", lineHeight: 1.1 }}>
                  {m[1]}{m[2] && <span style={{ fontSize: 11, color: "var(--ink-3)", fontWeight: 500, marginLeft: 3 }}>{m[2]}</span>}
                </div>
              </div>
            ))}
          </div>
          <div className="row between tiny muted" style={{ marginTop: 6, borderTop: "1px dashed var(--line)", paddingTop: 8 }}>
            <span className="mono">{D.state.timestamp}</span>
            <span>来源：在线 PF 融合 · {D.ctx.graph_template}</span>
          </div>
        </Panel>

        <Panel title="风险摘要" icon="warn" sub="当前 horizon 4.0s">
          <div className="kv lined left" style={{ fontSize: 12 }}>
            <div className="k">最大温度</div><div className="v num">{D.risk.max_temp_k} K <span className="tiny muted">· {D.risk.max_temp_loc}</span></div>
            <div className="k">最大应力</div><div className="v num">{D.risk.max_stress_mpa} MPa <span className="tiny muted">· {D.risk.max_stress_loc}</span></div>
            <div className="k">最大损伤</div><div className="v num">{D.risk.max_damage} <span className="tiny muted">· {D.risk.max_damage_loc}</span></div>
            <div className="k">RUL 剩余寿命</div><div className="v num">{D.risk.rul_s}s <Badge status="warn" className="tiny">置信 {D.risk.rul_conf}</Badge></div>
            <div className="k">首超破坏</div><div className="v num">{D.risk.first_exceedance_s}s <span className="tiny muted">· {D.risk.first_exceedance_kind}</span></div>
          </div>
        </Panel>
      </div>

      {/* run pipeline */}
      <Panel title="运行链路状态" icon="route" sub="Sensor → Filter → State → Trajectory → Field → Damage → Life"
        right={<span className="tiny muted">弹道 / 多场 / 损伤 / 寿命为可选物理子图</span>} className="" style={{ marginBottom: 12 }}>
        <div className="stage-row">
          {D.stages.map((s, i) => (
            <React.Fragment key={s.key}>
              <div className={`stage ${stCls[s.status]}`} style={{ flex: 1 }}>
                <div className="st-top"><SDot status={s.status} /><span className="nm">{s.name}</span><span className="ty">{s.type}</span></div>
                <div className="st-val">{s.val}{s.unit && <small> {s.unit}</small>}</div>
                <div className="st-sub">{s.sub}</div>
              </div>
              {i < D.stages.length - 1 && arrow}
            </React.Fragment>
          ))}
        </div>
      </Panel>

      <div className="grid" style={{ gridTemplateColumns: "1fr 1.4fr", gap: 12 }}>
        {/* model snapshot */}
        <Panel title="当前模型快照" icon="layers" sub="model_snapshot.json" right={<button className="btn sm" onClick={() => go("models")}>模型资产</button>} bodyClass="flush">
          <table className="tbl">
            <thead><tr><th>类型</th><th>model_id</th><th>版本</th><th>执行</th><th>状态</th></tr></thead>
            <tbody>
              {D.modelSnapshot.map((m, i) => (
                <tr key={i} onClick={() => go("models")}>
                  <td className="t-strong">{m.type}</td>
                  <td className="mono tiny">{m.model_id}</td>
                  <td className="mono">{m.version}</td>
                  <td><span className="chip">{m.runtime}</span></td>
                  <td><Badge status={m.status} /></td>
                </tr>
              ))}
            </tbody>
          </table>
        </Panel>

        {/* recent runs */}
        <Panel title="最近运行" icon="replay" sub="点击进入回放" right={<button className="btn sm" onClick={() => go("replay")}>全部回放</button>} bodyClass="flush">
          <table className="tbl">
            <thead><tr><th>run_id</th><th>阶段</th><th>模板</th><th>帧数</th><th>状态</th></tr></thead>
            <tbody>
              {D.runs.slice(0, 6).map((r, i) => (
                <tr key={i} onClick={() => go("replay", { run: r.run_id })}>
                  <td className="mono tiny t-strong">{r.run_id}</td>
                  <td>{r.phase}</td>
                  <td className="mono tiny muted">{r.template}</td>
                  <td className="num t-r">{r.frames}</td>
                  <td><Badge status={r.status} /></td>
                </tr>
              ))}
            </tbody>
          </table>
        </Panel>
      </div>
    </div>
  );
}
Object.assign(window, { PageOverview });
