/* ===================== 诊断报告 Diagnostics ===================== */
function PageDiagnostics({ go }) {
  const D = window.FE;
  const [scen, setScen] = useState("degraded"); // ok | degraded | failed
  const runtime = D.diagRuntime[scen];
  const overall = { ok: "ok", degraded: "warn", failed: "fail" }[scen];
  const sevBadge = { info: "ok", warn: "warn", error: "fail" };

  // preflight adapts a bit to scenario
  const preflight = D.diagPreflight.map(p => {
    if (scen === "ok" && p.status === "warn") return { ...p, status: "ok", detail: "contract 版本一致" };
    if (scen === "failed" && p.check === "checksum 匹配") return { ...p, status: "fail", detail: "field.pkst POD 基 checksum 不匹配（期望 1ad7… 实得 1ad9…）" };
    return p;
  });

  return (
    <div className="page-pad">
      <div className="page-head">
        <div><h1>诊断报告</h1><p>解释为什么可信 / 为什么失败 · preflight + runtime · 每条含 severity / source / reason / action</p></div>
        <div className="row gap8">
          <span className="tiny muted">示例状态</span>
          <div className="seg">
            <button className={scen === "ok" ? "on" : ""} onClick={() => setScen("ok")}>ok</button>
            <button className={scen === "degraded" ? "on" : ""} onClick={() => setScen("degraded")}>degraded</button>
            <button className={scen === "failed" ? "on" : ""} onClick={() => setScen("failed")}>failed</button>
          </div>
        </div>
      </div>

      {/* banner */}
      <div style={{
        display: "flex", alignItems: "center", gap: 12, padding: "12px 16px", marginBottom: 12, borderRadius: 9,
        background: overall === "ok" ? "var(--ok-soft)" : overall === "warn" ? "var(--warn-soft)" : "var(--fail-soft)",
        border: "1px solid " + (overall === "ok" ? "#c3e6d1" : overall === "warn" ? "#ecd8a0" : "#f0c4c4"),
      }}>
        <div style={{ width: 36, height: 36, borderRadius: 9, display: "grid", placeItems: "center", flex: "none",
          background: "#fff", color: overall === "ok" ? "var(--ok)" : overall === "warn" ? "var(--warn)" : "var(--fail)" }}>
          <Icon name={overall === "ok" ? "check" : overall === "warn" ? "warn" : "x"} size={20} />
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ fontWeight: 700, fontSize: 14 }}>
            {scen === "ok" ? "运行可信 · 所有算子输出 ok" : scen === "degraded" ? "运行降级 · 部分输出 degraded，结果可用但受限" : "运行失败 · 关键算子失败，部分输出 unknown"}
          </div>
          <div className="tiny muted" style={{ marginTop: 1 }}>{D.ctx.run_id} · {D.ctx.graph_template} · graph v{D.ctx.graph_version}</div>
        </div>
        <Badge status={overall}>{scen}</Badge>
      </div>

      <div className="grid" style={{ gridTemplateColumns: "1fr 1.3fr", gap: 12, alignItems: "start" }}>
        {/* preflight */}
        <Panel title="Preflight 检查" icon="shield" sub="启动前校验" bodyClass="flush">
          {preflight.map((p, i) => (
            <div key={i} className="row gap8" style={{ padding: "9px 12px", borderBottom: i < preflight.length - 1 ? "1px solid var(--line)" : "none" }}>
              <div style={{ marginTop: 1 }}><SDot status={p.status === "fail" ? "fail" : p.status} /></div>
              <div style={{ flex: 1, minWidth: 0 }}>
                <div className="row between"><span style={{ fontSize: 12.5, fontWeight: 600 }}>{p.check}</span><Badge status={p.status === "fail" ? "fail" : p.status} /></div>
                <div className="tiny muted" style={{ marginTop: 2, lineHeight: 1.5 }}>{p.detail}</div>
              </div>
            </div>
          ))}
        </Panel>

        {/* runtime diagnostics */}
        <Panel title="Runtime 诊断" icon="activity" sub={`${runtime.length} 条`} bodyClass="flush scroll" bodyStyle={{ maxHeight: 420 }}>
          {runtime.map((r, i) => (
            <div key={i} style={{ padding: "10px 12px", borderBottom: i < runtime.length - 1 ? "1px solid var(--line)" : "none" }}>
              <div className="row gap8" style={{ marginBottom: 5 }}>
                <Badge status={sevBadge[r.severity]}>{r.severity}</Badge>
                <span className="mono tiny" style={{ color: "var(--ink)", fontWeight: 700 }}>{r.source}</span>
              </div>
              <div style={{ fontSize: 12, color: "var(--ink-2)", lineHeight: 1.5 }}><b className="muted" style={{ fontWeight: 600 }}>reason · </b>{r.reason}</div>
              <div className="row gap6" style={{ marginTop: 5, alignItems: "flex-start" }}>
                <Icon name="bolt" size={13} style={{ color: "var(--acc)", marginTop: 1, flex: "none" }} />
                <span className="tiny" style={{ color: "var(--acc-ink)", lineHeight: 1.5 }}>{r.action}</span>
              </div>
            </div>
          ))}
        </Panel>
      </div>

      {/* report outputs */}
      <Panel title="报告输出" icon="download" sub="可导出文件" style={{ marginTop: 12 }} bodyClass="flush">
        <div style={{ display: "grid", gridTemplateColumns: "repeat(4,1fr)" }}>
          {[
            { name: "validation_report.json", size: "7.2 KB", st: overall },
            { name: "graph_run_evidence.json", size: "11.0 KB", st: "ok" },
            { name: "dashboard.csv", size: "240 KB", st: "ok" },
            { name: "run_summary.md", size: "3.4 KB", st: "ok" },
          ].map((f, i) => (
            <div className="evrow" key={i} style={{ borderRight: (i % 4 !== 3) ? "1px solid var(--line)" : "none" }}>
              <Icon name="file" size={15} className="ev-ico" />
              <span className="ev-name tiny">{f.name}</span>
              <span className="ev-size">{f.size}</span>
              <SDot status={f.st} />
            </div>
          ))}
        </div>
      </Panel>
    </div>
  );
}
Object.assign(window, { PageDiagnostics });
