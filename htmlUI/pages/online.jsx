/* ===================== 在线运行 Online ===================== */
function PageOnline({ go }) {
  const D = window.FE;
  const [playing, setPlaying] = useState(true);
  const [mode, setMode] = useState("online");   // online | replay
  const [speed, setSpeed] = useState(1);
  const [tick, setTick] = useState(0);
  const [field, setField] = useState("temperature");
  const [predFlash, setPredFlash] = useState(false);

  useEffect(() => {
    if (!playing) return;
    const iv = setInterval(() => setTick(t => t + 1), 420 / speed);
    return () => clearInterval(iv);
  }, [playing, speed]);

  // prediction trigger every 14 ticks
  useEffect(() => {
    if (playing && tick > 0 && tick % 14 === 0) {
      setPredFlash(true);
      const to = setTimeout(() => setPredFlash(false), 600);
      return () => clearTimeout(to);
    }
  }, [tick, playing]);

  const frameNo = 41982 + tick * (mode === "replay" ? 1 : 3);
  const ess = 0.71 + 0.055 * Math.sin(tick * 0.31);
  const resid = 0.94 + 0.18 * Math.sin(tick * 0.47 + 1);
  const fresh = mode === "replay" ? 0 : Math.round(34 + 14 * Math.abs(Math.sin(tick * 0.6)));
  const predCount = D.forecast.triggers + Math.floor(tick / 14);
  const predDur = 142 + Math.round(20 * Math.sin(tick * 0.5));

  // ---- live state advances along reentry profile; forecast re-projects from current state to impact (落点) ----
  const mp = (tick % 224) / 224;                       // mission progress 0..1 (loops)
  const curAlt = 62 * Math.pow(1 - mp, 1.25);          // km, → 0 at impact
  const curMach = 1.8 + 17 * Math.pow(1 - mp, 0.9);
  const curAoA = 11 + 2.4 * Math.sin(mp * 6);
  const curQ = Math.max(8, 12 + 44 * Math.sin(mp * Math.PI));
  const timeToImpact = Math.max(2.5, 96 * (1 - mp));   // s remaining to 落点
  const NF = 42;
  const altS = [], machS = [], aoaS = [];
  for (let i = 0; i < NF; i++) {
    const fr = i / (NF - 1), tau = fr * timeToImpact;
    altS.push({ x: tau, y: curAlt * Math.pow(1 - fr, 1.18) });
    machS.push({ x: tau, y: 1.8 + (curMach - 1.8) * Math.pow(1 - fr, 0.8) });
    aoaS.push({ x: tau, y: curAoA + 1.4 * Math.sin((mp + fr * 0.6) * 6) - 0.6 * fr });
  }
  const predTable = [0, 8, 16, 24, 32, NF - 1].map(i => {
    const fr = i / (NF - 1);
    return { step: i, t_rel: (fr * timeToImpact).toFixed(2), alt: altS[i].y.toFixed(2), mach: machS[i].y.toFixed(2),
      aoa: aoaS[i].y.toFixed(2), q: (curQ * Math.pow(1 - fr, 0.9)).toFixed(1), impact: i === NF - 1 };
  });
  const frameFrac = 0.5 + 0.5 * Math.sin(mp * Math.PI);  // heating peaks mid-reentry

  const fdef = FIELD_DEFS[field];
  const stCls = { ok: "s-ok", warn: "s-warn", fail: "s-fail", unk: "s-unk", run: "s-run" };

  return (
    <div className="page-pad">
      <div className="page-head">
        <div><h1>在线运行</h1><p>实时滤波状态 → 未来预测 → 场 / 损伤 / 寿命 · 在线帧每帧推进，预测按自身频率触发</p></div>
        <div className="row gap8">
          <div className="seg">
            <button className={mode === "online" ? "on" : ""} onClick={() => setMode("online")}>Online</button>
            <button className={mode === "replay" ? "on" : ""} onClick={() => setMode("replay")}>Replay</button>
          </div>
          <button className="btn icon" onClick={() => setSpeed(s => s >= 4 ? 1 : s * 2)} title="倍速"><span className="mono tiny" style={{ fontWeight: 700 }}>{speed}×</span></button>
          {playing
            ? <button className="btn" onClick={() => setPlaying(false)}><Icon name="pause" size={14} />暂停</button>
            : <button className="btn primary" onClick={() => setPlaying(true)}><Icon name="play" size={14} />开始</button>}
        </div>
      </div>

      <div className="grid" style={{ gridTemplateColumns: "270px 1fr 330px", gap: 12, alignItems: "start" }}>
        {/* LEFT: input + filter */}
        <div className="col gap12">
          <Panel title="实时输入" icon="sensor" sub={mode === "replay" ? "replay 帧" : "database / sensor"}>
            <KV left rows={[
              ["数据源帧率", <span className="num">{D.filter.source_rate_hz} Hz</span>, 1],
              ["最新状态帧", <span className="num">sf_{String(frameNo).padStart(7, "0")}</span>, 1],
              ["状态新鲜度", <span className="num" style={{ color: fresh > 80 ? "var(--warn)" : "var(--ink)" }}>{fresh} ms</span>, 1],
              ["当前高度", <span className="num">{curAlt.toFixed(2)} km</span>, 1],
              ["当前马赫", <span className="num">{curMach.toFixed(2)} Ma</span>, 1],
            ]} />
            <div className="tiny muted" style={{ marginTop: 6, paddingTop: 6, borderTop: "1px dashed var(--line)" }}>
              state.posterior(t_k) 每帧推进 · 距落点约 <b className="num" style={{ color: "var(--ink)" }}>{timeToImpact.toFixed(0)}s</b>
            </div>
          </Panel>

          <Panel title="滤波状态" icon="filter" sub="filter.diagnostics" right={<Badge status="ok">PF</Badge>}>
            <div className="col gap8">
              <div>
                <div className="row between tiny muted"><span>ESS（有效样本比）</span><span className="num" style={{ color: "var(--ink)", fontWeight: 700 }}>{ess.toFixed(3)}</span></div>
                <div style={{ marginTop: 4 }}><Meter value={ess} color={ess < D.filter.resample_threshold ? "var(--warn)" : "var(--ok)"} /></div>
                <div className="tiny muted" style={{ marginTop: 2 }}>阈值 {D.filter.resample_threshold} · {Math.round(ess * D.filter.particles)} / {D.filter.particles} 粒子</div>
              </div>
              <KV left rows={[
                ["粒子数", <span className="num">{D.filter.particles}</span>, 1],
                ["残差", <span className="num">{resid.toFixed(2)} σ</span>, 1],
                ["重采样次数", <span className="num">{D.filter.resamples + Math.floor(tick / 11)}</span>, 1],
                ["random_seed", <span className="num tiny">{D.filter.seed}</span>, 1],
                ["诊断", <Badge status="ok" />, 0],
              ]} />
            </div>
          </Panel>

          <Panel title="预测触发" icon="bolt" sub="forecast scheduler" right={predFlash ? <Badge status="run">触发</Badge> : <Badge status="ok" className="tiny">就绪</Badge>}>
            <KV left rows={[
              ["当前预测 run", <span className="num tiny">fc_{String(214 + Math.floor(tick / 14)).padStart(4, "0")}</span>, 1],
              ["触发次数", <span className="num">{predCount}</span>, 1],
              ["horizon", <span className="num">{D.forecast.horizon_s}s · {D.forecast.steps} 步</span>, 1],
              ["dt", <span className="num">{D.forecast.dt_s}s</span>, 1],
              ["上次耗时", <span className="num">{predDur} ms</span>, 1],
            ]} />
          </Panel>
        </div>

        {/* CENTER: vehicle field view */}
        <Panel title="飞行器场显示" icon="cube" sub={`mesh://rv_biconic_full.v4 · ${fdef.port}`}
          right={<FieldTabs value={field} onChange={setField} />} bodyClass="flush">
          <div style={{ padding: "8px 12px 0" }}>
            <div className="row between tiny muted">
              <span className="mono">{fdef.label} · 38×18 结构网格 · 沿未来预测步推进到落点</span>
              <span className="mono">colormap: {fdef.cmap} · 悬停读节点值</span>
            </div>
          </div>
          <div style={{ height: 320, padding: "4px 8px 0" }}>
            <MeshView field={field} frame={frameFrac} animateKey={tick} />
          </div>
          <div style={{ padding: "4px 12px 12px" }}>
            <ColorBar field={field} />
          </div>
        </Panel>

        {/* RIGHT: prediction results */}
        <div className="col gap12">
          <Panel title="未来轨迹 → 落点" icon="route" sub={`fc_${String(214 + Math.floor(tick / 14)).padStart(4, "0")} · 从当前状态起`} bodyClass="">
            <div className="col gap8">
              <div>
                <div className="tiny muted row between"><span>时间 — 高度 (km)</span><span className="mono" style={{ color: "var(--acc)" }}>落点 t+{timeToImpact.toFixed(0)}s · 0km</span></div>
                <LineChart width={306} height={92}
                  series={[{ points: altS, color: "var(--acc)", fill: "var(--acc-soft)" }]}
                  yDomain={[0, 66]} xDomain={[0, timeToImpact]} />
              </div>
              <div>
                <div className="tiny muted">时间 — 马赫数 / 攻角</div>
                <LineChart width={306} height={84}
                  series={[
                    { points: machS, color: "#b07d09", width: 1.6 },
                    { points: aoaS, color: "#6f5fb3", width: 1.6 },
                  ]}
                  yDomain={[0, 20]} xDomain={[0, timeToImpact]} />
                <div className="row gap12 tiny muted" style={{ marginTop: 2 }}>
                  <span className="row gap6"><span style={{ width: 9, height: 2, background: "#b07d09" }}></span>Mach</span>
                  <span className="row gap6"><span style={{ width: 9, height: 2, background: "#6f5fb3" }}></span>AoA</span>
                  <span style={{ marginLeft: "auto" }}>horizon = 距落点 {timeToImpact.toFixed(0)}s</span>
                </div>
              </div>
            </div>
          </Panel>

          <Panel title="未来轨迹表" icon="target" sub="trajectory.forecast · 迭代至 impact" bodyClass="flush scroll" bodyStyle={{ maxHeight: 196 }}>
            <table className="tbl">
              <thead><tr><th>step</th><th className="t-r">t_rel</th><th className="t-r">alt</th><th className="t-r">Ma</th><th className="t-r">AoA</th></tr></thead>
              <tbody>
                {predTable.map((r, i) => (
                  <tr key={i} style={{ cursor: "default", background: r.impact ? "var(--acc-soft)" : "transparent" }}>
                    <td className="mono">{r.impact ? "落点" : r.step}</td><td className="num t-r">{r.t_rel}</td>
                    <td className="num t-r" style={r.impact ? { color: "var(--acc-ink)", fontWeight: 700 } : null}>{r.alt}</td><td className="num t-r">{r.mach}</td><td className="num t-r">{r.aoa}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </Panel>
        </div>
      </div>

      {/* BOTTOM: workflow timeline */}
      <Panel title="Workflow Timeline" icon="activity" sub="workflow_timeline.json · 每算子耗时 / 输出字节 / 状态"
        right={<div className="row gap12 tiny muted">
          <span>在线帧 <b className="num" style={{ color: "var(--ink)" }}>{(1982 + tick * 3).toLocaleString()}</b></span>
          <span>预测触发 <b className="num" style={{ color: "var(--ink)" }}>{predCount}</b></span>
          <span>每次 <b className="num" style={{ color: "var(--ink)" }}>{D.forecast.steps}</b> 步</span>
        </div>}
        style={{ marginTop: 12 }} bodyClass="flush scroll">
        <table className="tbl">
          <thead><tr><th>operator</th><th>execution.kind</th><th>output port</th><th className="t-r">耗时</th><th className="t-r">输出字节</th><th>状态</th></tr></thead>
          <tbody>
            {D.timeline.map((t, i) => (
              <tr key={i} style={{ cursor: "default" }}>
                <td className="mono t-strong">{t.op}</td>
                <td><span className="chip">{t.kind}</span></td>
                <td className="mono tiny muted">{t.port}</td>
                <td className="num t-r">{t.dur_ms.toFixed(1)} ms</td>
                <td className="num t-r">{t.bytes}</td>
                <td><Badge status={t.status} /></td>
              </tr>
            ))}
          </tbody>
        </table>
      </Panel>
    </div>
  );
}
Object.assign(window, { PageOnline });
