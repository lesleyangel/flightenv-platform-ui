/* ===================== 配置与数据源 Config ===================== */
function PageConfig({ go }) {
  const D = window.FE;
  const [runMode, setRunMode] = useState("标准在线流程");
  const [dataSource, setDataSource] = useState("database");
  const [retrain, setRetrain] = useState(false);
  const [archive, setArchive] = useState(true);
  const [frameStorage, setFrameStorage] = useState("both");

  const Section = ({ title, sub, children }) => (
    <Panel title={title} sub={sub} className="" style={{ marginBottom: 12 }}>{children}</Panel>
  );
  const Field = ({ label, hint, control }) => (
    <div className="row between" style={{ padding: "9px 0", borderBottom: "1px dashed var(--line)" }}>
      <div><div style={{ fontSize: 12.5, fontWeight: 600 }}>{label}</div>{hint && <div className="tiny muted" style={{ marginTop: 1 }}>{hint}</div>}</div>
      <div>{control}</div>
    </div>
  );

  return (
    <div className="page-pad">
      <div className="page-head">
        <div><h1>配置与数据源</h1><p>以工程语言配置一次运行，而不是平铺 JSON 字段编辑器</p></div>
        <div className="row gap8">
          <button className="btn sm"><Icon name="shield" size={13} />Preflight 校验</button>
          <button className="btn sm primary"><Icon name="play" size={13} />应用并启动</button>
        </div>
      </div>

      <div className="grid" style={{ gridTemplateColumns: "1fr 1fr", gap: 12, alignItems: "start" }}>
        <div>
          <Section title="运行概览">
            <KV left rows={[
              ["object_id", <span className="mono tiny">{D.ctx.object_id}</span>, 1],
              ["phase", <span className="mono tiny">{D.ctx.phase}</span>, 1],
              ["graph 模板", <span className="mono tiny">{D.ctx.graph_template}</span>, 1],
              ["run_id", <span className="mono tiny">{D.ctx.run_id}</span>, 1],
            ]} />
          </Section>

          <Section title="运行模式与诊断">
            <Field label="运行模式" hint="决定是否需要观测、是否预测、是否回放"
              control={<select className="btn sm" value={runMode} onChange={e => setRunMode(e.target.value)} style={{ minWidth: 160 }}>
                {["标准在线流程", "无观测只预测", "训练诊断", "回放验证"].map(o => <option key={o}>{o}</option>)}
              </select>} />
            <Field label="先重训模型" hint="在线加载前是否重新训练 POD/BPNN（默认否）"
              control={<Toggle on={retrain} onClick={() => setRetrain(r => !r)} />} />
            <Field label="诊断级别" hint="preflight + runtime"
              control={<Seg options={["info", "warn", "error"]} value="warn" onChange={() => { }} />} />
          </Section>

          <Section title="数据源与同步">
            <Field label="数据源" hint="database / replay / sensor"
              control={<Seg options={["database", "replay", "sensor"]} value={dataSource} onChange={setDataSource} />} />
            <Field label="数据库路径" hint="task_field DB（只读引用，不入平台库）"
              control={<span className="mono tiny" style={{ color: "var(--ink-2)" }}>{D.config.db_path}</span>} />
            <Field label="最大输入年龄" hint="max_input_age · 超时输出 stale"
              control={<span className="num">200 ms</span>} />
          </Section>
        </div>

        <div>
          <Section title="模型来源">
            <Field label="catalog" hint="platform-catalog.json"
              control={<span className="mono tiny">_local_artifacts/platform/</span>} />
            <Field label="模型选择口径" hint="object_id + type + phase + priority + enabled"
              control={<span className="badge plain tiny">按 catalog binding</span>} />
            <Field label="trajectory 适配" hint="FlightEnvTrajectoryCli.exe"
              control={<span className="chip">cli_exe</span>} />
          </Section>

          <Section title="模型细节">
            <KV left rows={[
              ["trajectory", <span className="mono tiny">traj.biconic.cli.v3</span>, 1],
              ["field_prediction", <span className="mono tiny">field.pkst.pod_bpnn.v2</span>, 1],
              ["damage", <span className="mono tiny">damage.creep_fatigue.v1</span>, 1],
              ["life", <span className="mono tiny">life.rul_baseline.v1</span>, 1],
            ]} />
            <div className="tiny muted" style={{ marginTop: 8 }}>点击模型在 <a style={{ color: "var(--acc)", cursor: "pointer" }} onClick={() => go("models")}>模型资产</a> 查看端口与资源锁。</div>
          </Section>

          <Section title="在线归档">
            <Field label="启用 archive" hint="异步落盘，不阻塞实时链路"
              control={<Toggle on={archive} onClick={() => setArchive(a => !a)} />} />
            <Field label="frame_storage" hint="binary / jsonl / both"
              control={<Seg options={["binary", "jsonl", "both"]} value={frameStorage} onChange={setFrameStorage} />} />
            <Field label="输出目录预览" control={<span></span>} />
            <div className="pre" style={{ background: "var(--panel-2)", border: "1px solid var(--line)", borderRadius: 6, padding: 9, marginTop: 4, fontSize: 10.5, whiteSpace: "pre-wrap", lineHeight: 1.6 }}>
{`${D.config.output_dir}
  run_manifest.json
  graph_snapshot.json   operator_snapshot.json
  resource_lock.json    model_snapshot.json
  graph_outputs.json    workflow_timeline.json
  frames/  ${frameStorage === "both" ? "(binary + jsonl)" : "(" + frameStorage + ")"}`}
            </div>
          </Section>
        </div>
      </div>
    </div>
  );
}
Object.assign(window, { PageConfig });
