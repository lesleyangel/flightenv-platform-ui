/* ===================== 健康账本 Health Ledger ===================== */
function PageHealth({ go }) {
  const D = window.FE;
  const [regId, setRegId] = useState("nose_cap");
  const [field, setField] = useState("life");
  const reg = D.regions.find(r => r.id === regId) || D.regions[0];
  const regionMap = { nose_cap: "nose_cap", tps_windward: "tps_windward", shoulder: "shoulder", structure: "structure" };

  const dmgColor = (d) => cmapCss("damage", d);
  const confBadge = { high: "ok", medium: "warn", low: "fail" };

  return (
    <div className="page-pad">
      <div className="page-head">
        <div><h1>健康账本</h1><p>对象/区域的长期累计损伤、剩余寿命与首超破坏时间 · 每条结论可追溯到 run / 模型 / 证据</p></div>
        <span className="chip">HealthLedger · {D.ctx.object_id}</span>
      </div>

      <div className="grid" style={{ gridTemplateColumns: "264px 1fr 300px", gap: 12, alignItems: "start" }}>
        {/* region list */}
        <Panel title="对象 / 区域" icon="cube" sub="累计损伤 0–1" bodyClass="flush scroll" bodyStyle={{ maxHeight: 560 }}>
          {D.regions.map(r => (
            <div key={r.id} onClick={() => { setRegId(r.id); }}
              style={{ padding: "10px 11px", borderBottom: "1px solid var(--line)", cursor: "pointer", background: r.id === regId ? "var(--acc-soft)" : "transparent" }}>
              <div className="row between">
                <span style={{ fontWeight: 700, fontSize: 12.5, color: r.id === regId ? "var(--acc-ink)" : "var(--ink)" }}>{r.name}</span>
                <span className="num tiny" style={{ fontWeight: 700, color: dmgColor(r.damage) }}>{r.damage.toFixed(2)}</span>
              </div>
              <div className="mono tiny muted" style={{ marginTop: 2, fontSize: 10 }}>{r.object}</div>
              <div style={{ marginTop: 6 }}><Meter value={r.damage} color={dmgColor(r.damage)} height={6} /></div>
              <div className="row between tiny muted" style={{ marginTop: 4 }}>
                <span>RUL {r.rul_s}s</span>
                <Badge status={confBadge[r.conf]} className="tiny">{r.conf}</Badge>
              </div>
            </div>
          ))}
        </Panel>

        {/* center: life field + trends */}
        <div className="col gap12">
          <Panel title={`${reg.name} · 物理场`} icon="cube"
            sub={`${reg.material} · 绑定 mesh://rv_biconic_full.v4`}
            right={<FieldTabs value={field} onChange={setField} fields={["life", "damage", "temperature", "stress"]} />} bodyClass="flush">
            <div style={{ height: 244, padding: "6px 8px 0" }}>
              <MeshView field={field} frame={0.85} region={regionMap[regId]} animateKey={regId + field} />
            </div>
            <div style={{ padding: "2px 12px 12px" }}><ColorBar field={field} /></div>
          </Panel>

          <div className="grid" style={{ gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Panel title="Damage over runs" icon="activity" sub="累计损伤趋势">
              <LineChart width={300} height={120}
                series={[{ points: reg.trendDamage.map((y, i) => ({ x: i, y })), color: cmapCss("damage", 0.7), fill: "var(--fail-soft)" }]}
                yDomain={[0, 1]} xDomain={[0, reg.trendDamage.length - 1]} xLabel="run 序" />
              <div className="row between tiny muted" style={{ marginTop: 2 }}>
                <span>首 run {reg.trendDamage[0].toFixed(2)}</span>
                <span className="num" style={{ color: dmgColor(reg.damage), fontWeight: 700 }}>当前 {reg.damage.toFixed(2)}{reg.saturated && " · saturated"}</span>
              </div>
            </Panel>
            <Panel title="RUL over runs" icon="clock" sub="剩余寿命趋势">
              <LineChart width={300} height={120}
                series={[{ points: reg.trendRul.map((y, i) => ({ x: i, y })), color: "var(--acc)", fill: "var(--acc-soft)" }]}
                yDomain={[0, Math.max(...reg.trendRul) * 1.1]} xDomain={[0, reg.trendRul.length - 1]} xLabel="run 序" />
              <div className="row between tiny muted" style={{ marginTop: 2 }}>
                <span>首 run {reg.trendRul[0]}s</span>
                <span className="num" style={{ color: "var(--acc-ink)", fontWeight: 700 }}>当前 {reg.rul_s}s</span>
              </div>
            </Panel>
          </div>

          {/* increments */}
          <Panel title="损伤增量来源" icon="layers" sub="run / frame range / model / evidence" bodyClass="flush">
            <table className="tbl">
              <thead><tr><th>run_id</th><th>frame range</th><th>model_id</th><th>evidence</th><th className="t-r">Δ damage</th></tr></thead>
              <tbody>
                {reg.increments.map((inc, i) => (
                  <tr key={i} onClick={() => go("replay", { run: inc.run })}>
                    <td className="mono tiny t-strong">{inc.run}</td>
                    <td className="mono tiny">{inc.frames}</td>
                    <td className="mono tiny muted">{inc.model}</td>
                    <td><span className="chip">{inc.src}</span></td>
                    <td className="num t-r" style={{ color: dmgColor(0.7), fontWeight: 700 }}>+{inc.d.toFixed(2)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </Panel>
        </div>

        {/* right: region detail */}
        <div className="col gap12">
          <Panel title="当前评估" icon="shield" sub={reg.object}
            right={<Badge status={confBadge[reg.conf]}>{reg.conf}</Badge>}>
            <div style={{ textAlign: "center", padding: "6px 0 10px" }}>
              <div className="num" style={{ fontSize: 34, fontWeight: 800, color: dmgColor(reg.damage), lineHeight: 1 }}>{reg.damage.toFixed(2)}</div>
              <div className="tiny muted" style={{ marginTop: 2 }}>累计损伤 · {reg.saturated ? "已饱和" : "未饱和"} (0–1)</div>
            </div>
            <KV left rows={[
              ["RUL 剩余寿命", <span className="num">{reg.rul_s}s</span>, 1],
              ["首超破坏", reg.first_exc ? <span className="num">{reg.first_exc}s</span> : <span className="muted">unknown</span>, 1],
              ["超限类型", <span className="tiny">{reg.first_exc_kind}</span>, 0],
              ["适用范围", <span className="tiny">{reg.envelope}</span>, 0],
              ["材料", <span className="tiny">{reg.material}</span>, 0],
            ]} />
            {reg.conf === "low" && (
              <div style={{ background: "var(--fail-soft)", border: "1px solid #f0c4c4", borderRadius: 6, padding: "7px 9px", marginTop: 8 }}>
                <div className="tiny" style={{ color: "#9c2b2b", lineHeight: 1.5 }}><b>低置信度</b>：{reg.envelope}，RUL 仅作预警，不作放行结论。</div>
              </div>
            )}
          </Panel>

          <Panel title="维护修正" icon="sliders" sub="manual correction">
            {reg.corrections.length === 0
              ? <div className="tiny muted" style={{ textAlign: "center", padding: "10px 0" }}>无维护修正记录</div>
              : reg.corrections.map((c, i) => (
                <div key={i} style={{ borderLeft: "2px solid var(--acc)", paddingLeft: 10, marginBottom: 8 }}>
                  <div className="row between"><span className="mono tiny t-strong">{c.date}</span><span className="num tiny"><span className="muted">{c.from}</span> → <b>{c.to}</b></span></div>
                  <div className="tiny muted" style={{ marginTop: 3, lineHeight: 1.5 }}>{c.reason}</div>
                  <div className="tiny muted" style={{ marginTop: 2 }}>操作者：{c.by}</div>
                </div>
              ))}
          </Panel>
        </div>
      </div>
    </div>
  );
}
Object.assign(window, { PageHealth });
