/* ===================== 回放实验 Replay ===================== */
function PageReplay({ go, arg }) {
  const D = window.FE;
  const [runId, setRunId] = useState((arg && arg.run) || "run_20260610_142210");
  const [frame, setFrame] = useState(0.42);    // 0..1
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(1);
  const [field, setField] = useState("temperature");
  const scrubRef = useRef(null);
  const dragRef = useRef(false);

  useEffect(() => {
    if (!playing) return;
    const iv = setInterval(() => setFrame(f => (f >= 1 ? 0 : Math.min(1, f + 0.012 * speed))), 60);
    return () => clearInterval(iv);
  }, [playing, speed]);

  const run = D.runs.find(r => r.run_id === runId) || D.runs[0];
  const totalFrames = run.frames;
  const curFrame = Math.round(frame * (totalFrames - 1));
  const tRel = (frame * 4).toFixed(2);
  const cursorX = frame * 4;

  // interpolate state at frame
  const idx = Math.min(D.series.altitude.length - 1, Math.round(frame * (D.series.altitude.length - 1)));
  const cur = {
    alt: D.series.altitude[idx].y.toFixed(2),
    mach: D.series.mach[idx].y.toFixed(2),
    aoa: D.series.aoa[idx].y.toFixed(2),
    heat: D.series.heat[idx].y.toFixed(2),
  };
  const dmg = (0.18 + 0.44 * frame).toFixed(3);
  const rul = Math.round(540 - 254 * frame);
  const frameFrac = 0.55 + 0.45 * frame;

  function setFromEvent(e) {
    const r = scrubRef.current.getBoundingClientRect();
    const x = (e.clientX - r.left) / r.width;
    setFrame(Math.max(0, Math.min(1, x)));
  }
  useEffect(() => {
    function mv(e) { if (dragRef.current) setFromEvent(e); }
    function up() { dragRef.current = false; }
    window.addEventListener("mousemove", mv); window.addEventListener("mouseup", up);
    return () => { window.removeEventListener("mousemove", mv); window.removeEventListener("mouseup", up); };
  }, []);

  return (
    <div className="page-pad">
      <div className="page-head">
        <div><h1>回放实验</h1><p>打开历史 run package，按帧查看状态 / 轨迹 / 场 / 损伤 / 寿命与证据，并比较模型版本</p></div>
        <span className="chip">mode: Replay</span>
      </div>

      <div className="grid" style={{ gridTemplateColumns: "248px 1fr 290px", gap: 12, alignItems: "start" }}>
        {/* run catalog */}
        <Panel title="Run Catalog" icon="replay" sub={`${D.runs.length} runs`} bodyClass="flush scroll" bodyStyle={{ maxHeight: 560 }}>
          {D.runs.map(r => (
            <div key={r.run_id} onClick={() => { setRunId(r.run_id); setFrame(0.42); }}
              style={{ padding: "9px 11px", borderBottom: "1px solid var(--line)", cursor: "pointer", background: r.run_id === runId ? "var(--acc-soft)" : "transparent" }}>
              <div className="row between">
                <span className="mono tiny t-strong" style={{ fontWeight: 700, color: r.run_id === runId ? "var(--acc-ink)" : "var(--ink)" }}>{r.run_id}</span>
                <Badge status={r.status} />
              </div>
              <div className="row between tiny muted" style={{ marginTop: 3 }}>
                <span>{r.phase} · {r.started}</span><span className="num">{r.frames} 帧</span>
              </div>
              <div className="mono tiny muted" style={{ marginTop: 3, fontSize: 10 }}>{r.models}</div>
            </div>
          ))}
        </Panel>

        {/* center: scrubber + frame */}
        <div className="col gap12">
          <Panel title={`当前帧 · ${run.run_id}`} icon="clock"
            sub={`frame ${curFrame} / ${totalFrames} · t_rel ${tRel}s`}
            right={<FieldTabs value={field} onChange={setField} />} bodyClass="flush">
            <div style={{ height: 264, padding: "6px 8px 0" }}>
              <MeshView field={field} frame={frameFrac} animateKey={Math.round(frame * 100)} />
            </div>
            <div style={{ padding: "2px 12px 10px" }}><ColorBar field={field} /></div>

            {/* scrubber */}
            <div style={{ padding: "0 12px 12px" }}>
              <div className="scrub" ref={scrubRef}
                onMouseDown={(e) => { dragRef.current = true; setFromEvent(e); }}>
                <div className="scrub-fill" style={{ width: (frame * 100) + "%" }}></div>
                <div className="scrub-ticks">
                  {Array.from({ length: 20 }).map((_, i) => <div key={i} style={{ flex: 1, borderRight: "1px solid var(--line)" }}></div>)}
                </div>
                <div className="scrub-cursor" style={{ left: (frame * 100) + "%" }}></div>
              </div>
              <div className="row between" style={{ marginTop: 8 }}>
                <div className="row gap6">
                  <button className="btn sm icon" onClick={() => setFrame(f => Math.max(0, f - 1 / totalFrames))}><Icon name="step_b" size={13} /></button>
                  {playing
                    ? <button className="btn sm" onClick={() => setPlaying(false)}><Icon name="pause" size={13} />暂停</button>
                    : <button className="btn sm primary" onClick={() => setPlaying(true)}><Icon name="play" size={13} />播放</button>}
                  <button className="btn sm icon" onClick={() => setFrame(f => Math.min(1, f + 1 / totalFrames))}><Icon name="step_f" size={13} /></button>
                  <button className="btn sm icon" onClick={() => setSpeed(s => s >= 4 ? 0.5 : s * 2)}><span className="mono tiny" style={{ fontWeight: 700 }}>{speed}×</span></button>
                </div>
                <span className="mono tiny muted">frame_storage: both · binary + jsonl</span>
              </div>
            </div>
          </Panel>

          {/* experiment compare */}
          <Panel title="实验对比" icon="layers" sub="input_run 相同 · model_package A / B 差异">
            <div className="grid" style={{ gridTemplateColumns: "1fr 1fr", gap: 10 }}>
              {[
                { tag: "A", run: "field v2 · pod_bpnn", dmg: 0.62, rul: 286, risk: "中", c: "var(--acc)" },
                { tag: "B", run: "field v1 · legacy_ST", dmg: 0.58, rul: 332, risk: "中", c: "#6f5fb3" },
              ].map(p => (
                <div key={p.tag} style={{ border: "1px solid var(--line)", borderRadius: 7, padding: 10 }}>
                  <div className="row between" style={{ marginBottom: 6 }}>
                    <span className="badge plain"><span className="bdot" style={{ background: p.c }}></span>包 {p.tag}</span>
                    <span className="mono tiny muted">{p.run}</span>
                  </div>
                  <KV left rows={[
                    ["max damage", <span className="num">{p.dmg}</span>, 1],
                    ["RUL", <span className="num">{p.rul}s</span>, 1],
                    ["风险等级", p.risk, 0],
                  ]} />
                </div>
              ))}
            </div>
            <div className="row gap12 tiny" style={{ marginTop: 10, paddingTop: 8, borderTop: "1px dashed var(--line)" }}>
              <span className="muted">Δ damage <b className="num" style={{ color: "var(--ink)" }}>+0.04</b></span>
              <span className="muted">Δ RUL <b className="num" style={{ color: "var(--warn)" }}>−46s</b></span>
              <span className="muted">风险等级变化 <b style={{ color: "var(--ink)" }}>无</b></span>
            </div>
          </Panel>
        </div>

        {/* right: current frame summary */}
        <div className="col gap12">
          <Panel title="状态 / 轨迹" icon="target" sub={`t_rel ${tRel}s`}>
            <KV left rows={[
              ["高度", <span className="num">{cur.alt} km</span>, 1],
              ["马赫数", <span className="num">{cur.mach} Ma</span>, 1],
              ["攻角", <span className="num">{cur.aoa} °</span>, 1],
              ["驻点热流", <span className="num">{cur.heat} MW/m²</span>, 1],
            ]} />
            <div style={{ marginTop: 8 }}>
              <div className="tiny muted">时间 — 高度（游标=当前帧）</div>
              <LineChart width={266} height={78} cursorX={cursorX}
                series={[{ points: D.series.altitude, color: "var(--acc)", fill: "var(--acc-soft)" }]}
                yDomain={[28, 44]} xDomain={[0, 4]} />
            </div>
          </Panel>

          <Panel title="损伤 / 寿命" icon="shield" sub="damage.forecast · life.assessment">
            <KV left rows={[
              ["累计损伤", <span className="num">{dmg}</span>, 1],
              ["RUL", <span className="num">{rul}s</span>, 1],
              ["首超破坏", <span className="num">312s</span>, 1],
              ["置信度", <Badge status="warn">medium</Badge>, 0],
            ]} />
            <div style={{ marginTop: 6 }}><Meter value={parseFloat(dmg)} color={cmapCss("damage", parseFloat(dmg))} /></div>
          </Panel>

          <Panel title="Evidence" icon="file" sub="本帧可追溯" bodyClass="flush">
            {D.evidence.slice(0, 5).map((e, i) => (
              <div className="evrow" key={i}><Icon name="file" size={14} className="ev-ico" /><span className="ev-name tiny">{e.name}</span><span className="ev-size">{e.size}</span></div>
            ))}
          </Panel>
        </div>
      </div>
    </div>
  );
}
Object.assign(window, { PageReplay });
