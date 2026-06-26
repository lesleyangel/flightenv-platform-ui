/* ===================== 算子编排 Graph (two-level orchestration) ===================== */
function PageGraph({ go }) {
  const D = window.FE;
  const O = D.orchestration;
  const NW = O.W, NH = O.H;

  const [block, setBlock] = useState("state_transition");
  const sg = O.subgraphs[block];
  const byId = Object.fromEntries(sg.nodes.map(n => [n.id, n]));
  const optIds = sg.nodes.filter(n => n.optional).map(n => n.id);

  // per-block enabled maps
  const [enabledAll, setEnabledAll] = useState(() => {
    const m = {};
    Object.entries(O.subgraphs).forEach(([bid, g]) => { m[bid] = Object.fromEntries(g.nodes.filter(n => n.optional).map(n => [n.id, true])); });
    return m;
  });
  const enabled = enabledAll[block];
  const setOp = (id, v) => setEnabledAll(s => ({ ...s, [block]: { ...s[block], [id]: v } }));

  const [sel, setSel] = useState(sg.nodes[0].id);
  useEffect(() => { setSel(O.subgraphs[block].nodes[0].id); }, [block]);
  // guard: during the render right after a block switch, `sel` may still point at the
  // previous block's node (effect hasn't run yet) — fall back to the first node.
  const selId = byId[sel] ? sel : sg.nodes[0].id;

  // cascade eval within block
  function evalNode(n) {
    if (n.optional && !enabled[n.id]) return { status: "disabled", reason: "该算子在本块实例中被关闭（optional）· 不参与编排" };
    for (const d of (n.deps || [])) {
      const dn = byId[d]; if (!dn) continue;
      const ds = evalNode(dn);
      if (ds.status === "disabled" || ds.status === "missing") return { status: "missing", reason: "上游算子 " + dn.label + " 不可用 → 本算子输出 missing/unknown" };
    }
    return { status: n.status, reason: n.reason || "" };
  }
  const statusMap = Object.fromEntries(sg.nodes.map(n => [n.id, evalNode(n)]));
  const stColor = { ok: "var(--ok)", warn: "var(--warn)", degraded: "var(--warn)", fail: "var(--fail)", missing: "var(--fail)", disabled: "var(--unk)", unk: "var(--unk)" };
  const stBadge = { ok: "ok", warn: "warn", degraded: "warn", fail: "fail", missing: "fail", disabled: "unk", unk: "unk" };

  // geometry
  const portY = (sgr, list, i) => {
    const top = (sgr.h - list.length * 26) / 2;
    return top + i * 26 + 13;
  };
  function nodeBox(n) { return { x: n.x, y: n.y, w: n.w || NW, h: n.h || NH, cx: n.x + (n.w || NW) / 2, cy: n.y + (n.h || NH) / 2 }; }
  function edgeGeom(e) {
    let a, b;
    if (e.from === "IN") { const nb = nodeBox(byId[e.to]); a = { x: sg.inX, y: nb.cy }; b = { x: nb.x, y: nb.cy }; return { a, b, mode: "h" }; }
    if (e.to === "OUT") { const na = nodeBox(byId[e.from]); a = { x: na.x + na.w, y: na.cy }; b = { x: sg.outX, y: na.cy }; return { a, b, mode: "h" }; }
    const na = nodeBox(byId[e.from]), nb = nodeBox(byId[e.to]);
    if (e.kind === "fb") return { a: { x: na.x, y: na.cy }, b: { x: nb.x, y: nb.cy }, mode: "fb" };
    if (e.kind === "v") return { a: { x: na.cx, y: na.y + na.h }, b: { x: nb.cx, y: nb.y }, mode: "v" };
    if (nb.x >= na.x + na.w - 4) return { a: { x: na.x + na.w, y: na.cy }, b: { x: nb.x, y: nb.cy }, mode: "h" };
    return { a: { x: na.cx, y: na.y + na.h }, b: { x: nb.cx, y: nb.y }, mode: "v" };
  }
  function edgePath(g) {
    const { a, b, mode } = g;
    if (mode === "fb") { const my = Math.min(a.y, b.y) - 26; return `M${a.x},${a.y} C${a.x - 40},${my} ${b.x + 40},${my} ${b.x},${b.y}`; }
    if (mode === "v") { const my = (a.y + b.y) / 2; return `M${a.x},${a.y} C${a.x},${my} ${b.x},${my} ${b.x},${b.y}`; }
    const mx = (a.x + b.x) / 2; return `M${a.x},${a.y} C${mx},${a.y} ${mx},${b.y} ${b.x},${b.y}`;
  }
  const isHot = (e) => e.from === selId || e.to === selId;

  const selNode = byId[selId];
  const selSt = statusMap[selId];
  const curL1 = O.level1.find(l => l.id === block);

  // framework strip geometry
  const FW = 660, FH = 134;
  const fwBox = (id) => { const p = O.framework.nodes[id]; return { x: p.x, y: p.y, w: 150, h: 52, cx: p.x + 75, cy: p.y + 26 }; };
  const fwEdges = [
    ["state_transition", "filter"], ["observation", "filter"], ["filter", "forecast"],
  ];

  return (
    <div className="page-pad">
      <div className="page-head">
        <div><h1>算子编排</h1><p>两级编排：一级框架（状态转移 / 观测 / 滤波 / 预测）固化，二级是每个方程内部的算子排布 · 状态在 POD 系数空间传递</p></div>
        <div className="row gap8"><span className="chip acc">state_prediction_qoi_graph.v1</span><span className="chip">graph v{D.ctx.graph_version}</span></div>
      </div>

      {/* ===== Level-1 fixed framework strip ===== */}
      <Panel title="一级框架 · 固化" icon="route" sub="点击进入任一方程的内部算子编排"
        right={<span className="tiny muted">STATE 在 POD 系数空间流动：predict/update/forecast 都传系数，不传场</span>}
        style={{ marginBottom: 12 }} bodyClass="flush">
        <div className="row" style={{ alignItems: "stretch", gap: 0 }}>
          {/* state vector */}
          <div style={{ width: 232, flex: "none", borderRight: "1px solid var(--line)", padding: "10px 12px" }}>
            <div className="tiny muted" style={{ fontWeight: 700, marginBottom: 6 }}>STATE 向量 · 状态载体</div>
            <div className="col gap6">
              {O.stateVector.map(s => (
                <div key={s.sym} className="row between" style={{ fontSize: 11 }}>
                  <span className="row gap6"><span className="mono" style={{ fontWeight: 700, color: s.domain === "struct" ? "#574a86" : s.domain === "traj" ? "var(--ink)" : "var(--acc-ink)" }}>{s.sym}</span><span className="muted">{s.name}</span></span>
                  <span className="mono tiny muted">{s.port}·{s.dim}</span>
                </div>
              ))}
            </div>
            <div className="tiny muted" style={{ marginTop: 8, paddingTop: 7, borderTop: "1px dashed var(--line)", lineHeight: 1.5 }}>
              POD 系数 = 场的降维表示。全场还原只在观测（测点）与 QoI（输出）发生。
            </div>
          </div>
          {/* framework svg */}
          <div style={{ flex: 1, overflowX: "auto", padding: "8px 12px" }}>
            <svg width={FW} height={FH} style={{ display: "block" }}>
              <defs>
                <marker id="fwarr" markerWidth="8" markerHeight="8" refX="6" refY="3" orient="auto"><path d="M0,0 L6,3 L0,6" fill="none" stroke="var(--line-3)" strokeWidth="1.2" /></marker>
              </defs>
              {fwEdges.map(([f, t], i) => {
                const a = fwBox(f), b = fwBox(t);
                const x1 = a.x + a.w, y1 = a.cy, x2 = b.x, y2 = b.cy, mx = (x1 + x2) / 2;
                return <path key={i} d={`M${x1},${y1} C${mx},${y1} ${mx},${y2} ${x2},${y2}`} fill="none" stroke="var(--line-3)" strokeWidth="1.4" markerEnd="url(#fwarr)" />;
              })}
              {/* posterior label */}
              <text x={fwBox("filter").x + fwBox("filter").w + 8} y={fwBox("filter").cy - 6} fontSize="9" fontFamily="'JetBrains Mono', monospace" fill="var(--ink-3)">posterior</text>
              {/* forecast → outputs */}
              <text x={fwBox("forecast").x + fwBox("forecast").w + 8} y={fwBox("forecast").cy + 4} fontSize="9" fontFamily="'JetBrains Mono', monospace" fill="var(--ink-3)">落点/场/损伤/寿命 →</text>
              {O.level1.map(l => {
                const bx = fwBox(l.id); const on = block === l.id;
                return (
                  <g key={l.id} transform={`translate(${bx.x},${bx.y})`} style={{ cursor: "pointer" }} onClick={() => setBlock(l.id)}>
                    <rect width={bx.w} height={bx.h} rx="9" fill={on ? "var(--acc-soft)" : "var(--panel)"} stroke={on ? "var(--acc)" : "var(--line-2)"} strokeWidth={on ? 2 : 1.2}
                      style={{ filter: on ? "drop-shadow(0 3px 8px rgba(14,138,156,.16))" : "drop-shadow(0 1px 2px rgba(20,28,40,.05))" }} />
                    <text x="12" y="20" fontSize="12" fontWeight="700" fill={on ? "var(--acc-ink)" : "var(--ink)"}>{l.name}</text>
                    <text x="12" y="35" fontSize="8.5" fontFamily="'JetBrains Mono', monospace" fill="var(--ink-3)">{l.en}</text>
                    <text x="12" y="46" fontSize="8.5" fill="var(--ink-4)">{l.ops} 算子 · {l.online ? "在线" : "迭代"}</text>
                  </g>
                );
              })}
            </svg>
          </div>
        </div>
      </Panel>

      <div className="grid" style={{ gridTemplateColumns: "240px 1fr 290px", gap: 12, alignItems: "start" }}>
        {/* LEFT: current block operators */}
        <Panel title="本块算子 · 可编排" icon="layers" sub={curL1.name} bodyClass="flush">
          <div style={{ padding: "9px 11px", background: "var(--panel-2)", borderBottom: "1px solid var(--line)" }}>
            <div className="mono tiny" style={{ color: "var(--acc-ink)", fontWeight: 700 }}>{curL1.io}</div>
            <div className="tiny muted" style={{ marginTop: 3, lineHeight: 1.5 }}>{curL1.desc}</div>
          </div>
          {sg.nodes.map(n => {
            const es = statusMap[n.id];
            return (
              <div key={n.id} onClick={() => setSel(n.id)} style={{ padding: "8px 11px", borderBottom: "1px solid var(--line)", cursor: "pointer", background: selId === n.id ? "var(--acc-soft)" : "transparent" }}>
                <div className="row between">
                  <span className="row gap6" style={{ minWidth: 0 }}><SDot status={stBadge[es.status]} /><b className="mono" style={{ fontSize: 11 }}>{n.label}</b></span>
                  {n.optional
                    ? <Toggle on={enabled[n.id]} onClick={(ev) => { ev.stopPropagation(); setOp(n.id, !enabled[n.id]); }} />
                    : (n.reuse ? <span className="badge acc tiny">固化复用</span> : <span className="badge ghost tiny">必选</span>)}
                </div>
                <div className="tiny muted" style={{ marginTop: 2 }}>{n.op} · {n.exec}</div>
              </div>
            );
          })}
          <div className="tiny muted" style={{ padding: "8px 11px", lineHeight: 1.5 }}>{sg.note}</div>
        </Panel>

        {/* CENTER: level-2 canvas */}
        <Panel title={`${curL1.name} · 内部算子图`} icon="graph" sub={curL1.en}
          right={<span className="row gap12 tiny muted">
            <span className="row gap6"><span style={{ width: 14, height: 0, borderTop: "1.5px solid var(--line-3)" }}></span>系数/端口</span>
            <span className="row gap6"><span style={{ width: 14, height: 0, borderTop: "1.5px dashed #6f5fb3" }}></span>迭代回环</span>
          </span>} bodyClass="flush">
          <div className="scrollx" style={{ overflow: "auto", maxHeight: 472 }}>
            <svg width={sg.w} height={sg.h} style={{ display: "block", background: "var(--panel-2)" }}>
              <defs>
                <pattern id="gdot2" width="22" height="22" patternUnits="userSpaceOnUse"><circle cx="1" cy="1" r="1" fill="var(--line-2)" /></pattern>
                <marker id="ar" markerWidth="9" markerHeight="9" refX="6.5" refY="3" orient="auto"><path d="M0,0 L6,3 L0,6" fill="none" stroke="var(--line-3)" strokeWidth="1.2" /></marker>
                <marker id="arA" markerWidth="9" markerHeight="9" refX="6.5" refY="3" orient="auto"><path d="M0,0 L6,3 L0,6" fill="none" stroke="var(--acc)" strokeWidth="1.4" /></marker>
                <marker id="arF" markerWidth="9" markerHeight="9" refX="6.5" refY="3" orient="auto"><path d="M0,0 L6,3 L0,6" fill="none" stroke="#6f5fb3" strokeWidth="1.3" /></marker>
              </defs>
              <rect width={sg.w} height={sg.h} fill="url(#gdot2)" />

              {/* clusters */}
              {(sg.clusters || []).map(c => (
                <g key={c.id}>
                  <rect x={c.x} y={c.y} width={c.w} height={c.h} rx="10"
                    fill={c.tone === "acc" ? "rgba(14,138,156,0.05)" : "rgba(176,125,9,0.05)"}
                    stroke={c.tone === "acc" ? "var(--acc-soft-2)" : "#ecd8a0"} strokeWidth="1.4" strokeDasharray="6 4" />
                  <text x={c.x + 12} y={c.y + 16} fontSize="10" fontFamily="'JetBrains Mono', monospace" fill={c.tone === "acc" ? "var(--acc-ink)" : "#7a5806"} fontWeight="600">{c.label}</text>
                </g>
              ))}

              {/* in/out rails */}
              <line x1={sg.inX} y1="24" x2={sg.inX} y2={sg.h - 16} stroke="var(--line-2)" strokeWidth="1.5" strokeDasharray="3 3" />
              <line x1={sg.outX} y1="24" x2={sg.outX} y2={sg.h - 16} stroke="var(--line-2)" strokeWidth="1.5" strokeDasharray="3 3" />
              <text x={sg.inX - 6} y="18" fontSize="9.5" fontFamily="'JetBrains Mono', monospace" fill="var(--ink-3)" textAnchor="end">{sg.inLabel} ▸</text>
              <text x={sg.outX + 6} y="18" fontSize="9.5" fontFamily="'JetBrains Mono', monospace" fill="var(--ink-3)">▸ {sg.outLabel}</text>
              {sg.inPorts.map((p, i) => (
                <text key={"in" + i} x={sg.inX - 8} y={portY(sg, sg.inPorts, i) + 3} fontSize="9.5" fontFamily="'JetBrains Mono', monospace" fill="var(--ink-2)" textAnchor="end">{p}</text>
              ))}
              {sg.outPorts.map((p, i) => (
                <text key={"out" + i} x={sg.outX + 8} y={portY(sg, sg.outPorts, i) + 3} fontSize="9.5" fontFamily="'JetBrains Mono', monospace" fill={p.includes("impact") || p.includes("forecast") ? "var(--acc-ink)" : "var(--ink-2)"}>{p}</text>
              ))}

              {/* edges */}
              {sg.edges.map((e, i) => {
                const g = edgeGeom(e); const hot = isHot(e); const fb = e.kind === "fb";
                const aOff = e.from !== "IN" && e.from !== "OUT" && statusMap[e.from] && (statusMap[e.from].status === "disabled" || statusMap[e.from].status === "missing");
                const col = fb ? "#6f5fb3" : hot ? "var(--acc)" : (aOff ? "var(--line-2)" : "var(--line-3)");
                const mk = fb ? "url(#arF)" : hot ? "url(#arA)" : "url(#ar)";
                return <g key={i}>
                  <path d={edgePath(g)} fill="none" stroke={col} strokeWidth={hot ? 2 : 1.3} strokeDasharray={fb ? "5 3" : (aOff ? "4 4" : "none")} markerEnd={mk} />
                  {e.label && <text x={(g.a.x + g.b.x) / 2} y={(g.a.y + g.b.y) / 2 - 4} fontSize="8.5" fontFamily="'JetBrains Mono', monospace" fill={fb ? "#6f5fb3" : "var(--ink-3)"} textAnchor="middle">{e.label}</text>}
                </g>;
              })}

              {/* nodes */}
              {sg.nodes.map(n => {
                const b = nodeBox(n); const es = statusMap[n.id]; const isSel = n.id === selId;
                const disabled = es.status === "disabled" || es.status === "missing";
                return (
                  <g key={n.id} transform={`translate(${b.x},${b.y})`} style={{ cursor: "pointer" }} onClick={() => setSel(n.id)}>
                    <rect width={b.w} height={b.h} rx="8"
                      fill={disabled ? "var(--panel-3)" : (n.reuse ? "var(--acc-soft)" : "var(--panel)")}
                      stroke={isSel ? "var(--acc)" : (n.reuse ? "var(--acc-soft-2)" : "var(--line-2)")} strokeWidth={isSel ? 2 : 1.2}
                      strokeDasharray={disabled ? "5 3" : (n.reuse ? "4 3" : "none")}
                      style={{ filter: isSel ? "drop-shadow(0 3px 8px rgba(14,138,156,.18))" : "drop-shadow(0 1px 2px rgba(20,28,40,.05))" }} />
                    <circle cx="13" cy={b.h / 2} r="4" fill={stColor[es.status]} />
                    <text x="24" y={b.h / 2 - 3} fontSize="11" fontWeight="700" fill={disabled ? "var(--ink-3)" : "var(--ink)"} fontFamily="'JetBrains Mono', monospace">{n.label}</text>
                    <text x="24" y={b.h / 2 + 11} fontSize="9" fill="var(--ink-3)">{n.op}{n.subject ? " · " + n.subject : ""}</text>
                    {n.reuse && <text x={b.w - 8} y="14" fontSize="8" fill="var(--acc-ink)" textAnchor="end" fontFamily="'JetBrains Mono', monospace">复用</text>}
                    <circle cx="0" cy={b.h / 2} r="3" fill="#fff" stroke="var(--line-3)" strokeWidth="1.2" />
                    <circle cx={b.w} cy={b.h / 2} r="3" fill="#fff" stroke="var(--line-3)" strokeWidth="1.2" />
                  </g>
                );
              })}
            </svg>
          </div>
        </Panel>

        {/* RIGHT: node detail */}
        <Panel title="算子详情" icon="cube" sub={selNode.op} bodyClass="scroll" bodyStyle={{ maxHeight: 472 }}>
          <div className="col gap12">
            <div>
              <div className="row between"><span className="mono" style={{ fontWeight: 700, fontSize: 12.5 }}>{selNode.label}</span><Badge status={stBadge[selSt.status]}>{selSt.status}</Badge></div>
              <div className="mono tiny muted" style={{ marginTop: 3 }}>{selNode.operator_id}</div>
            </div>
            {selNode.reuse && <div style={{ background: "var(--acc-soft)", border: "1px solid var(--acc-soft-2)", borderRadius: 6, padding: "7px 9px" }}>
              <div className="tiny" style={{ color: "var(--acc-ink)", fontWeight: 700, marginBottom: 2 }}>固化复用 · {O.level1.find(l => l.id === selNode.reuse).name}</div>
              <div className="tiny" style={{ color: "var(--ink-2)", lineHeight: 1.5 }}>{selNode.note}</div>
            </div>}
            {!selNode.reuse && selNode.note && <div style={{ background: "var(--panel-2)", border: "1px solid var(--line)", borderRadius: 6, padding: "7px 9px" }}>
              <div className="tiny" style={{ color: "var(--ink-2)", lineHeight: 1.55 }}>{selNode.note}</div>
            </div>}
            <KV left rows={[
              ["execution.kind", <span className="chip">{selNode.exec}</span>, 0],
              ["optional", selNode.optional ? <Badge status="warn">可开关</Badge> : <Badge status="ok">必选</Badge>, 0],
              ...(selNode.subject ? [["field subject", <span className="mono tiny">{selNode.subject}</span>, 1]] : []),
            ]} />
            <div>
              <div className="tiny muted" style={{ marginBottom: 4, fontWeight: 600 }}>inputs</div>
              <div className="col gap6">{selNode.inputs.map((p, i) => <span key={i} className="chip" style={{ width: "fit-content" }}>{p}</span>)}</div>
            </div>
            <div>
              <div className="tiny muted" style={{ marginBottom: 4, fontWeight: 600 }}>outputs</div>
              <div className="col gap6">{selNode.outputs.map((p, i) => <span key={i} className="chip acc" style={{ width: "fit-content" }}>{p}</span>)}</div>
            </div>
            {selNode.assets && selNode.assets.length > 0 && (
              <div>
                <div className="tiny muted" style={{ marginBottom: 4, fontWeight: 600 }}>assets · 含 POD 基</div>
                <div className="col gap6">{selNode.assets.map((a, i) => (
                  <div key={i} className="row gap6" style={{ fontSize: 11 }}><Icon name="lock" size={12} style={{ color: a.role.includes("pod") ? "var(--acc)" : "var(--ink-3)" }} /><span className="muted">{a.role}</span><span className="mono tiny" style={{ marginLeft: "auto", color: "var(--ink-2)" }}>{a.ref}</span></div>
                ))}</div>
              </div>
            )}
            {selSt.reason && (
              <div style={{ background: selSt.status === "disabled" ? "var(--unk-soft)" : selSt.status === "missing" || selSt.status === "fail" ? "var(--fail-soft)" : "var(--warn-soft)",
                border: "1px solid " + (selSt.status === "disabled" ? "var(--line-2)" : selSt.status === "missing" ? "#f0c4c4" : "#ecd8a0"), borderRadius: 6, padding: "8px 10px" }}>
                <div className="tiny" style={{ fontWeight: 700, marginBottom: 2, color: "var(--ink-2)" }}>status / reason</div>
                <div className="tiny" style={{ color: "var(--ink-2)", lineHeight: 1.5 }}>{selSt.reason}</div>
              </div>
            )}
          </div>
        </Panel>
      </div>

      {/* evidence */}
      <Panel title="Evidence 文件" icon="file" sub="两级编排快照都写入证据链" right={<button className="btn sm" onClick={() => go("replay")}><Icon name="replay" size={13} />回放此 run</button>}
        style={{ marginTop: 12 }} bodyClass="flush">
        <div style={{ display: "grid", gridTemplateColumns: "repeat(4,1fr)" }}>
          {D.evidence.map((e, i) => (
            <div className="evrow" key={i} style={{ borderRight: (i % 4 !== 3) ? "1px solid var(--line)" : "none" }}>
              <Icon name="file" size={15} className="ev-ico" />
              <div className="col" style={{ minWidth: 0 }}><span className="ev-name" style={{ fontSize: 11 }}>{e.name}</span><span className="tiny muted" style={{ fontSize: 9.5 }}>{e.desc}</span></div>
              <span className="ev-size">{e.size}</span><SDot status={e.status} />
            </div>
          ))}
        </div>
      </Panel>
    </div>
  );
}
Object.assign(window, { PageGraph });
