/* ===================== App shell: nav + context bar + routing ===================== */
const NAV = [
  { sec: "运行" },
  { id: "overview", name: "总览", icon: "gauge" },
  { id: "online", name: "在线运行", icon: "activity", badge: "live" },
  { sec: "对象与模型" },
  { id: "object", name: "对象画像", icon: "cube" },
  { id: "models", name: "模型资产", icon: "layers", badge: "6" },
  { id: "graph", name: "算子编排", icon: "graph" },
  { sec: "证据与健康" },
  { id: "replay", name: "回放实验", icon: "replay" },
  { id: "health", name: "健康账本", icon: "ledger" },
  { sec: "配置与诊断" },
  { id: "config", name: "配置与数据源", icon: "sliders" },
  { id: "diagnostics", name: "诊断报告", icon: "shield" },
];

const PAGES = {
  overview: PageOverview, online: PageOnline, object: PageObject, models: PageModels,
  graph: PageGraph, replay: PageReplay, health: PageHealth, config: PageConfig, diagnostics: PageDiagnostics,
};

function ContextBar() {
  const D = window.FE;
  const s = st(D.ctx.status);
  const fields = [
    ["对象", <span>{D.ctx.object_name} <span className="muted mono tiny">/ {D.ctx.object_id}</span></span>, false],
    ["阶段", D.ctx.phase, false],
    ["模式", D.ctx.mode, false],
    ["run", D.ctx.run_id, true],
    ["graph", D.ctx.graph_template, true],
    ["archive", <Badge status={D.ctx.archive === "enabled" ? "ok" : "unk"}>{D.ctx.archive}</Badge>, false],
  ];
  return (
    <div className="ctx">
      <div className="ctx-l">
        {fields.map((f, i) => (
          <div className="ctx-field" key={i}>
            <div className="k">{f[0]}</div>
            <div className={`v ${f[2] ? "mono" : ""}`}>{f[1]}</div>
          </div>
        ))}
      </div>
      <div className="ctx-spacer"></div>
      <div className="ctx-r">
        <span className={`runpill ${D.ctx.status}`}><span className="dot"></span>{s.label}</span>
      </div>
    </div>
  );
}

function App() {
  const [page, setPage] = useState("overview");
  const [arg, setArg] = useState(null);
  const go = useCallback((p, payload) => { setPage(p); setArg(payload || null); document.querySelector(".page")?.scrollTo(0, 0); }, []);
  const Cur = PAGES[page] || PageOverview;

  return (
    <div className="app">
      {/* nav */}
      <div className="nav">
        <div className="nav-brand">
          <div className="nav-mark">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
              <path d="M3 17l5-1 6-9 3 1-4 9 5 1M5 21h14" />
            </svg>
          </div>
          <div className="nav-brand-tx">
            <b>FlightEnv</b><span>Twin Workbench</span>
          </div>
        </div>
        <div className="nav-scroll">
          {NAV.map((n, i) => n.sec
            ? <div className="nav-sec" key={i}>{n.sec}</div>
            : <div key={n.id} className={`nav-item ${page === n.id ? "active" : ""}`} onClick={() => go(n.id)}>
                <Icon name={n.icon} size={16} className="ni-ico" />
                <span>{n.name}</span>
                {n.badge && <span className="ni-badge">{n.badge}</span>}
              </div>
          )}
        </div>
        <div className="nav-foot">
          <span>contracts v0.9</span>
          <span className="mono">SDK read-only</span>
        </div>
      </div>

      {/* main */}
      <div className="main">
        <ContextBar />
        <div className="page">
          <Cur go={go} arg={arg} />
        </div>
      </div>
    </div>
  );
}

ReactDOM.createRoot(document.getElementById("root")).render(<App />);
