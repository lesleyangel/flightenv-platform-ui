/* ===================== 模型资产 Model Assets ===================== */
function PageModels({ go }) {
  const D = window.FE;
  const [sel, setSel] = useState("filter.simple_pf.v1");
  const [typeFilter, setTypeFilter] = useState("all");
  const [objFilter, setObjFilter] = useState("all");
  const m = D.models.find(x => x.model_id === sel) || D.models[0];
  const types = ["all", "dynamics", "observation", "filter", "qoi", "trajectory", "field_prediction", "damage", "failure", "life", "mapper"];
  const typeLabel = { all: "全部", dynamics: "状态转移", observation: "观测", filter: "滤波", qoi: "QoI", trajectory: "弹道", field_prediction: "多场", damage: "损伤", failure: "失效", life: "寿命", mapper: "mapper" };
  const objOpts = [{ id: "all", name: "全部对象" }, { id: "vehicle", name: "整机" }, ...D.objectTree.filter(o => o.domain === "aero" || o.domain === "struct").map(o => ({ id: o.id, name: o.name }))];
  const objName = (id) => (D.objectTree.find(o => o.id === id) || {}).name || (id === "vehicle" ? "整机" : id);
  let list = typeFilter === "all" ? D.models : D.models.filter(x => x.model_type === typeFilter);
  if (objFilter !== "all") list = list.filter(x => x.appliesTo && x.appliesTo.includes(objFilter));

  return (
    <div className="page-pad">
      <div className="page-head">
        <div><h1>模型资产</h1><p>按对象匹配算子与模型 · 不同对象的可用模型不同 · model_id / version / checksum / 适用包络 / appliesTo</p></div>
        <div className="row gap6 wrap" style={{ maxWidth: 560, justifyContent: "flex-end" }}>
          {types.map(t => <button key={t} className={`btn sm ${typeFilter === t ? "active" : ""}`} onClick={() => setTypeFilter(t)}>{typeLabel[t]}</button>)}
        </div>
      </div>

      <div className="grid" style={{ gridTemplateColumns: "1fr 320px", gap: 12, alignItems: "start" }}>
        <Panel title="模型资产表" icon="layers" sub={`${list.length} 个模型`}
          right={<div className="row gap6"><span className="tiny muted">按对象</span>
            <select className="btn sm" value={objFilter} onChange={e => setObjFilter(e.target.value)} style={{ minWidth: 120 }}>
              {objOpts.map(o => <option key={o.id} value={o.id}>{o.name}</option>)}
            </select></div>}
          bodyClass="flush scroll" bodyStyle={{ maxHeight: 580 }}>
          <table className="tbl">
            <thead><tr>
              <th>model_id</th><th>type</th><th>runtime</th><th>ver</th><th>适用对象 appliesTo</th><th>校验</th><th>启用</th>
            </tr></thead>
            <tbody>
              {list.map(x => (
                <tr key={x.model_id} className={sel === x.model_id ? "sel" : ""} onClick={() => setSel(x.model_id)}>
                  <td className="mono tiny t-strong">{x.model_id}</td>
                  <td className="tiny">{x.model_type}</td>
                  <td><span className="chip tiny">{x.runtime_type}</span></td>
                  <td className="mono tiny">{x.version}</td>
                  <td><span className="row gap6 wrap">{(x.appliesTo || []).map(o => <span key={o} className="badge plain tiny">{objName(o)}</span>)}</span></td>
                  <td><Badge status={x.validation} /></td>
                  <td>{x.enabled ? <SDot status="ok" /> : <SDot status="unk" />}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <div className="tiny muted" style={{ padding: "8px 12px", borderTop: "1px solid var(--line)" }}>
            对象 + type + phase 决定候选模型，priority 仅用于同类多候选的默认选型，不代表 DAG 执行顺序。
          </div>
        </Panel>

        {/* detail */}
        <Panel title="模型详情" icon="cube" sub={m.model_type} bodyClass="scroll" bodyStyle={{ maxHeight: 580 }}>
          <div className="col gap12">
            <div>
              <div className="row between"><span className="mono t-strong" style={{ fontWeight: 700, fontSize: 12.5 }}>{m.model_id}</span><Badge status={m.validation} /></div>
              <div className="row gap6" style={{ marginTop: 5 }}>
                <span className="chip">{m.runtime_type}</span>
                <span className="chip">v{m.version}</span>
                {m.enabled ? <span className="badge ok tiny">enabled</span> : <span className="badge unk tiny">disabled</span>}
              </div>
            </div>

            <KV left rows={[
              ["artifact_ref", <span className="mono tiny">{m.artifact_ref}</span>, 1],
              ["checksum", <span className="mono tiny">{m.checksum}</span>, 1],
              ["applicable_envelope", <span className="tiny">{m.envelope}</span>, 0],
              ["priority", <span className="num">{m.priority}</span>, 1],
            ]} />

            <div>
              <div className="tiny muted" style={{ marginBottom: 5, fontWeight: 600 }}>适用对象 · appliesTo</div>
              <div className="row gap6 wrap">
                {(m.appliesTo || []).map(o => (
                  <span key={o} className="badge acc tiny" style={{ cursor: "pointer" }} onClick={() => go("object")}>{objName(o)}</span>
                ))}
              </div>
              {m.subject && <div className="tiny muted" style={{ marginTop: 5 }}>场主题 subject: <b style={{ color: "var(--ink-2)" }}>{m.subject}</b></div>}
            </div>

            <div>
              <div className="tiny muted" style={{ marginBottom: 5, fontWeight: 600 }}>输入端口</div>
              <div className="col gap6">{m.inputs.map((p, i) => <span key={i} className="chip" style={{ width: "fit-content" }}>{p}</span>)}</div>
            </div>
            <div>
              <div className="tiny muted" style={{ marginBottom: 5, fontWeight: 600 }}>输出端口</div>
              <div className="col gap6">{m.outputs.map((p, i) => <span key={i} className="chip acc" style={{ width: "fit-content" }}>{p}</span>)}</div>
            </div>

            <div>
              <div className="tiny muted" style={{ marginBottom: 5, fontWeight: 600 }}>使用资产 · resource lock</div>
              <div style={{ border: "1px solid var(--line)", borderRadius: 6, overflow: "hidden" }}>
                {m.assets.map((a, i) => (
                  <div key={i} className="row gap8" style={{ padding: "6px 9px", borderBottom: i < m.assets.length - 1 ? "1px solid var(--line)" : "none", fontSize: 11 }}>
                    <Icon name="lock" size={12} style={{ color: "var(--acc)" }} />
                    <span className="muted" style={{ width: 90, flex: "none" }}>{a.role}</span>
                    <span className="mono tiny" style={{ marginLeft: "auto", color: "var(--ink-2)", textAlign: "right" }}>{a.ref}</span>
                  </div>
                ))}
              </div>
            </div>

            <div className="row gap8">
              <button className="btn sm"><Icon name="file" size={13} />校验报告</button>
              <button className="btn sm" onClick={() => go("graph")}><Icon name="graph" size={13} />在 graph 查看</button>
            </div>

            <div>
              <div className="tiny muted" style={{ marginBottom: 5, fontWeight: 600 }}>最近使用此模型的 run</div>
              <div className="col gap6">
                {m.runsUsing.map((r, i) => (
                  <div key={i} className="row gap6" style={{ fontSize: 11, cursor: "pointer" }} onClick={() => go("replay", { run: r })}>
                    <Icon name="replay" size={12} style={{ color: "var(--ink-3)" }} /><span className="mono tiny">{r}</span>
                  </div>
                ))}
              </div>
            </div>
          </div>
        </Panel>
      </div>
    </div>
  );
}
Object.assign(window, { PageModels });
