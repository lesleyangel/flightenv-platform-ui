/* ===================== 对象画像 Object Profile ===================== */
function PageObject({ go }) {
  const D = window.FE;
  const [sel, setSel] = useState("shell");
  const node = D.objectTree.find(n => n.id === sel) || D.objectTree[0];
  const regionMap = { vehicle: null, shell: null, nosecap: "nose_cap", tps: "tps_windward", structure: "structure", shoulder: "shoulder", mount: null };
  const subjToField = { P: "pressure_P", K: "heatflux_K", T: "temperature", S: "stress", L: "life" };
  // ensure FIELD_DEFS has the aero subjects
  const fieldKeyFor = (subj) => ({ P: "pressure", K: "heatflux", T: "temperature", S: "stress" }[subj]);

  const applicableFieldKeys = node.fields.map(f => fieldKeyFor(f.subject));
  const [field, setField] = useState(null);
  const curField = (field && applicableFieldKeys.includes(field)) ? field : (applicableFieldKeys[0] || "temperature");

  const domainTone = { aero: { c: "var(--acc)", soft: "var(--acc-soft)", ink: "var(--acc-ink)", label: "气动外表面域" },
    struct: { c: "#6f5fb3", soft: "#eae6f6", ink: "#574a86", label: "内部结构域" },
    meta: { c: "var(--ink-3)", soft: "var(--unk-soft)", ink: "var(--ink-2)", label: "元数据" },
    整机: {} };
  const dom = domainTone[node.domain] || { c: "var(--ink-3)", soft: "var(--unk-soft)", ink: "var(--ink-2)", label: "整机" };
  const nodeSensors = D.sensors.filter(s => node.sensors.includes(s.ch));

  return (
    <div className="page-pad">
      <div className="page-head">
        <div><h1>对象画像</h1><p>对象决定方法：不同对象的可用物理场、传感器、损伤模型各不相同 · 平台按对象匹配算子与模型</p></div>
        <span className="chip">geometry · mesh://rv_biconic_full.v4</span>
      </div>

      <div className="grid" style={{ gridTemplateColumns: "264px 1fr 312px", gap: 12, alignItems: "start" }}>
        {/* tree */}
        <Panel title="对象树" icon="cube" sub="按物理域分组" bodyClass="flush scroll" bodyStyle={{ maxHeight: 580 }}>
          <div style={{ padding: 7 }} className="tree">
            {D.objectTree.map(n => {
              const t = domainTone[n.domain] || {};
              return (
                <div key={n.id} className={`tnode ${sel === n.id ? "sel" : ""}`} style={{ paddingLeft: 8 + n.depth * 15 }} onClick={() => { setSel(n.id); setField(null); }}>
                  {n.depth < 2 && n.type !== "nodeset" ? <Icon name="chevD" size={13} className="tw" /> : <span className="tw"></span>}
                  <span className="tdot" style={{ background: { ok: "var(--ok)", warn: "var(--warn)", fail: "var(--fail)" }[n.status], borderRadius: n.domain === "struct" ? "50%" : 2 }}></span>
                  <span style={{ flex: 1, minWidth: 0, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>{n.name}</span>
                  {n.domain !== "整机" && n.domain !== "meta" && n.fields.length > 0 &&
                    <span className="row gap6">{n.fields.map(f => <span key={f.subject} className="mono" style={{ fontSize: 9, fontWeight: 700, color: t.ink, background: t.soft, borderRadius: 3, padding: "0 3px" }}>{f.subject}</span>)}</span>}
                </div>
              );
            })}
          </div>
          <div style={{ padding: "8px 11px", borderTop: "1px solid var(--line)" }} className="tiny muted">
            <div className="row gap8"><span className="row gap6"><span className="tdot" style={{ background: "var(--acc)", width: 7, height: 7, borderRadius: 2 }}></span>气动域 P/K/T</span>
            <span className="row gap6"><span className="tdot" style={{ background: "#6f5fb3", width: 7, height: 7, borderRadius: "50%" }}></span>结构域 S/T</span></div>
          </div>
        </Panel>

        {/* center: geometry + applicable methods */}
        <div className="col gap12">
          <Panel title={`${node.name} · 几何视图`} icon="cube" sub={node.meta.geometry_ref}
            right={applicableFieldKeys.length > 0
              ? <div className="ftabs">{node.fields.map(f => {
                  const fk = fieldKeyFor(f.subject); const d = FIELD_DEFS[fk];
                  return <div key={f.subject} className={`ftab ${curField === fk ? "on" : ""}`} onClick={() => setField(fk)}>
                    <span className="sw" style={{ background: cmapCss(d.cmap, 0.8) }}></span>{f.subject}·{d.label}</div>;
                })}</div>
              : <span className="tiny muted">无物理场</span>} bodyClass="flush">
            <div style={{ height: 300, padding: "8px 8px 0" }}>
              <MeshView field={curField} frame={0.9} region={regionMap[sel]} showSensors={true} animateKey={sel + curField} />
            </div>
            <div style={{ padding: "4px 12px 12px" }}>
              <div className="row between tiny muted" style={{ marginBottom: 6 }}>
                <span className="mono">□ 传感器 marker · 虚线框 = 选中区域</span>
                <span className="mono">{node.sensors.length} sensors · {node.fields.length} 场 · {node.damage ? 1 : 0} 损伤模型</span>
              </div>
              <ColorBar field={curField} />
            </div>
          </Panel>

          {/* applicable methods matrix */}
          <Panel title="本对象适用方法" icon="link" sub="对象 → 可用场 / 损伤 / 寿命模型（按 appliesTo 匹配）">
            <div style={{ background: dom.soft, border: "1px solid var(--line)", borderRadius: 7, padding: "8px 11px", marginBottom: 10 }}>
              <div className="row gap8" style={{ marginBottom: 3 }}>
                <span className="badge" style={{ background: "#fff", color: dom.ink, borderColor: "var(--line-2)" }}><span className="bdot" style={{ background: dom.c }}></span>{dom.label}</span>
                <span className="mono tiny muted">{node.meta.object_type}</span>
              </div>
              <div className="tiny" style={{ color: "var(--ink-2)", lineHeight: 1.55 }}>{node.character}</div>
            </div>

            {node.fields.length === 0
              ? <div className="tiny muted" style={{ textAlign: "center", padding: "8px 0" }}>该对象不承载物理场（聚合/测点节点）</div>
              : (
              <table className="tbl">
                <thead><tr><th>物理场</th><th>port</th><th>场模型 model_id</th><th>传感观测</th><th>状态</th></tr></thead>
                <tbody>
                  {node.fields.map((f, i) => {
                    const obsSensors = nodeSensors.filter(s => s.observes.includes(f.subject));
                    return (
                      <tr key={i} onClick={() => go("models")}>
                        <td><span className="row gap6"><span className="sw" style={{ width: 10, height: 10, borderRadius: 2, background: cmapCss(FIELD_DEFS[fieldKeyFor(f.subject)].cmap, 0.8), display: "inline-block" }}></span><b>{f.subject} · {f.label}</b></span></td>
                        <td className="mono tiny muted">{f.port}</td>
                        <td className="mono tiny t-strong">{f.model}</td>
                        <td className="tiny muted">{obsSensors.length ? obsSensors.map(s => s.ch).join(" · ") : "—"}</td>
                        <td><SDot status={f.status} /></td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            )}

            {node.damage && (
              <div className="grid" style={{ gridTemplateColumns: "1fr 1fr", gap: 10, marginTop: 10 }}>
                <div style={{ border: "1px solid var(--line)", borderRadius: 7, padding: 10 }}>
                  <div className="tiny muted" style={{ fontWeight: 600, marginBottom: 4 }}>损伤模型 · 按对象选型</div>
                  <div className="mono tiny t-strong">{node.damage.model}</div>
                  <div className="row gap6 wrap" style={{ marginTop: 5 }}>
                    <span className="badge plain tiny">{node.damage.kind}</span>
                    <span className="chip tiny">driver: {node.damage.driver}</span>
                  </div>
                </div>
                <div style={{ border: "1px solid var(--line)", borderRadius: 7, padding: 10 }}>
                  <div className="tiny muted" style={{ fontWeight: 600, marginBottom: 4 }}>寿命模型</div>
                  <div className="mono tiny t-strong">{node.life.model}</div>
                  <div className="row gap6" style={{ marginTop: 5 }}>
                    <button className="btn sm" onClick={() => go("health")}><Icon name="ledger" size={12} />健康账本</button>
                  </div>
                </div>
              </div>
            )}
          </Panel>
        </div>

        {/* right: object detail + sensors */}
        <div className="col gap12">
          <Panel title="对象详情" icon="cube" sub={node.meta.object_type}>
            <KV left rows={[
              ["object_id", <span className="mono tiny">{node.meta.object_id}</span>, 1],
              ["物理域", <span className="tiny">{node.meta.domain}</span>, 0],
              ["geometry_ref", <span className="mono tiny">{node.meta.geometry_ref}</span>, 1],
              ["material_ref", <span className="mono tiny">{node.meta.material_ref}</span>, 1],
            ]} />
          </Panel>

          <Panel title="绑定传感器" icon="sensor" sub={`${nodeSensors.length} 通道 · 按区域`} bodyClass="flush scroll" bodyStyle={{ maxHeight: 230 }}>
            {nodeSensors.length === 0
              ? <div className="tiny muted" style={{ textAlign: "center", padding: "12px 0" }}>该对象无直接绑定测点</div>
              : (
              <table className="tbl">
                <thead><tr><th>通道</th><th>类型</th><th>观测</th><th>速率</th><th>状态</th></tr></thead>
                <tbody>
                  {nodeSensors.map((s, i) => (
                    <tr key={i} style={{ cursor: "default" }}>
                      <td className="mono tiny t-strong">{s.ch}</td>
                      <td className="tiny">{s.kind === "thermocouple" ? "热电偶" : s.kind === "strain_gauge" ? "应变片" : s.kind === "pressure" ? "压力" : s.kind === "imu" ? "IMU" : s.kind}</td>
                      <td><span className="chip tiny">{s.observes}</span></td>
                      <td className="num tiny">{s.rate}Hz</td>
                      <td><SDot status={s.status} /></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </Panel>

          <Panel title="匹配的模型" icon="layers" sub="appliesTo 命中本对象" bodyClass="flush" right={<button className="btn sm" onClick={() => go("models")}>全部</button>}>
            {D.models.filter(m => m.appliesTo && m.appliesTo.includes(sel)).slice(0, 7).map((m, i) => (
              <div className="evrow" key={i} onClick={() => go("models")}>
                <SDot status={m.validation} />
                <div className="col" style={{ minWidth: 0 }}>
                  <span className="mono tiny t-strong">{m.model_id}</span>
                  <span className="mono muted" style={{ fontSize: 10 }}>{m.model_type}</span>
                </div>
                <span className="chip tiny" style={{ marginLeft: "auto" }}>{m.version}</span>
              </div>
            ))}
            {D.models.filter(m => m.appliesTo && m.appliesTo.includes(sel)).length === 0 &&
              <div className="tiny muted" style={{ textAlign: "center", padding: "12px 0" }}>无对象级匹配（整机级算子见在线运行）</div>}
          </Panel>
        </div>
      </div>
    </div>
  );
}
Object.assign(window, { PageObject });
