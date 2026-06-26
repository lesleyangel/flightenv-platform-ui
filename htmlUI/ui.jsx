/* ===================================================================
   Shared UI primitives + icon set. Exposes to window.
   =================================================================== */
const { useState, useEffect, useRef, useMemo, useCallback } = React;

/* ---------- icons (16px stroke) ---------- */
const IconPaths = {
  gauge: "M12 13a2 2 0 100-4 2 2 0 000 4zM12 9V5M4.9 19a8 8 0 1114.2 0M13.4 10.6l3.1-3.1",
  activity: "M3 12h4l2 6 4-14 2 8h6",
  cube: "M12 2l8 4.5v9L12 20l-8-4.5v-9L12 2zM12 2v9m0 0l8-4.5M12 11L4 6.5",
  layers: "M12 3l8 4-8 4-8-4 8-4zM4 12l8 4 8-4M4 17l8 4 8-4",
  graph: "M5 6a2 2 0 100-4 2 2 0 000 4zM5 22a2 2 0 100-4 2 2 0 000 4zM19 14a2 2 0 100-4 2 2 0 000 4zM5 6v12M7 5h7a3 3 0 013 3v3M7 19h7a3 3 0 003-3",
  replay: "M3 12a9 9 0 109-9 9 9 0 00-6.4 2.6L3 8M3 4v4h4M10 9l5 3-5 3V9z",
  ledger: "M5 3h11l3 3v15H5zM9 8h7M9 12h7M9 16h4",
  sliders: "M4 8h10M18 8h2M4 16h2M10 16h10M14 6v4M8 14v4",
  shield: "M12 3l7 3v5c0 4.5-3 7.5-7 9-4-1.5-7-4.5-7-9V6l7-3zM9.5 11.5l1.8 1.8 3.2-3.6",
  play: "M6 4l14 8-14 8V4z",
  pause: "M7 4h4v16H7zM15 4h4v16h-4z",
  step_f: "M5 4l10 8-10 8V4zM18 4v16",
  step_b: "M19 4L9 12l10 8V4zM6 4v16",
  chevR: "M9 6l6 6-6 6",
  chevD: "M6 9l6 6 6-6",
  file: "M6 2h8l4 4v16H6zM14 2v4h4",
  lock: "M6 11h12v9H6zM8 11V8a4 4 0 018 0v3",
  dot: "M12 12m-3 0a3 3 0 106 0 3 3 0 10-6 0",
  sensor: "M12 12m-2 0a2 2 0 104 0 2 2 0 10-4 0M8.5 8.5a5 5 0 000 7M15.5 8.5a5 5 0 010 7M6 6a8 8 0 000 12M18 6a8 8 0 010 12",
  warn: "M12 3l9 16H3l9-16zM12 9v5M12 17h.01",
  check: "M4 12l5 5L20 6",
  x: "M5 5l14 14M19 5L5 19",
  download: "M12 4v11m0 0l4-4m-4 4l-4-4M5 20h14",
  clock: "M12 21a9 9 0 100-18 9 9 0 000 18zM12 7v5l3 2",
  search: "M11 11m-7 0a7 7 0 1014 0 7 7 0 10-14 0M21 21l-4-4",
  filter: "M3 5h18l-7 8v5l-4 2v-7L3 5z",
  refresh: "M20 11a8 8 0 10-2.3 6M20 5v6h-6",
  bolt: "M13 2L4 14h6l-1 8 9-12h-6l1-8z",
  link: "M9 15l6-6M10 6l1-1a4 4 0 016 6l-1 1M14 18l-1 1a4 4 0 01-6-6l1-1",
  db: "M12 3c4.4 0 8 1.3 8 3s-3.6 3-8 3-8-1.3-8-3 3.6-3 8-3zM4 6v12c0 1.7 3.6 3 8 3s8-1.3 8-3V6M4 12c0 1.7 3.6 3 8 3s8-1.3 8-3",
  target: "M12 21a9 9 0 100-18 9 9 0 000 18zM12 16a4 4 0 100-8 4 4 0 000 8zM12 12h.01",
  thermo: "M12 14V5a2 2 0 10-4 0v9a4 4 0 104 4 4 4 0 00-4-4",
  route: "M6 19a2 2 0 100-4 2 2 0 000 4zM18 9a2 2 0 100-4 2 2 0 000 4zM8 17h6a4 4 0 000-8H10a4 4 0 010-8",
};
function Icon({ name, size = 16, className = "", style }) {
  const d = IconPaths[name] || IconPaths.dot;
  return (
    <svg className={className} width={size} height={size} viewBox="0 0 24 24" fill="none"
      stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round" style={style}>
      {d.split("M").filter(Boolean).map((seg, i) => <path key={i} d={"M" + seg} />)}
    </svg>
  );
}

/* ---------- status helpers ---------- */
const STATUS = {
  ok:        { cls: "ok",   label: "正常",   sdot: "ok" },
  running:   { cls: "run",  label: "运行中", sdot: "run" },
  run:       { cls: "run",  label: "运行中", sdot: "run" },
  warn:      { cls: "warn", label: "降级",   sdot: "warn" },
  degraded:  { cls: "warn", label: "降级",   sdot: "warn" },
  fail:      { cls: "fail", label: "失败",   sdot: "fail" },
  failed:    { cls: "fail", label: "失败",   sdot: "fail" },
  unk:       { cls: "unk",  label: "未知",   sdot: "unk" },
  unknown:   { cls: "unk",  label: "未知",   sdot: "unk" },
  idle:      { cls: "unk",  label: "空闲",   sdot: "unk" },
  disabled:  { cls: "unk",  label: "停用",   sdot: "unk" },
  replaying: { cls: "replaying", label: "回放中", sdot: "acc" },
};
function st(s) { return STATUS[s] || STATUS.unk; }

function Badge({ status, children, className = "" }) {
  const s = st(status);
  return <span className={`badge ${s.cls} ${className}`}><span className="bdot" style={{ background: "currentColor" }}></span>{children || s.label}</span>;
}
function SDot({ status, ring }) { const s = st(status); return <span className={`sdot ${s.sdot} ${ring ? "ring" : ""}`}></span>; }

/* ---------- panel ---------- */
function Panel({ title, sub, icon, right, children, className = "", bodyClass = "", style, bodyStyle }) {
  return (
    <div className={`panel ${className}`} style={style}>
      {(title || right) && (
        <div className="panel-h">
          {title && <h3>{icon && <Icon name={icon} size={14} className="ico" />}{title}{sub && <span className="sub">{sub}</span>}</h3>}
          {right && <><div className="spacer"></div>{right}</>}
        </div>
      )}
      <div className={`panel-b ${bodyClass}`} style={bodyStyle}>{children}</div>
    </div>
  );
}

/* ---------- key-value ---------- */
function KV({ rows, lined = true, left = false, className = "" }) {
  return (
    <div className={`kv ${lined ? "lined" : ""} ${left ? "left" : ""} ${className}`}>
      {rows.map((r, i) => (
        <React.Fragment key={i}>
          <div className="k">{r[0]}</div>
          <div className={`v ${r[2] ? "mono" : ""}`}>{r[1]}</div>
        </React.Fragment>
      ))}
    </div>
  );
}

/* ---------- meter ---------- */
function Meter({ value, max = 1, color, height = 7 }) {
  const pct = Math.max(0, Math.min(1, value / max)) * 100;
  return <div className="meter" style={{ height }}><i style={{ width: pct + "%", background: color || "var(--acc)" }}></i></div>;
}

/* ---------- toggle ---------- */
function Toggle({ on, onClick, disabled }) {
  return <button className={`tgl ${on ? "on" : ""}`} disabled={disabled} onClick={onClick} aria-pressed={on}></button>;
}

/* ---------- segmented ---------- */
function Seg({ options, value, onChange }) {
  return (
    <div className="seg">
      {options.map(o => {
        const val = typeof o === "string" ? o : o.value;
        const lab = typeof o === "string" ? o : o.label;
        return <button key={val} className={value === val ? "on" : ""} onClick={() => onChange(val)}>{lab}</button>;
      })}
    </div>
  );
}

/* ---------- field colormaps ---------- */
// scientific-ish colormaps returning [r,g,b] for t in [0,1]
const COLORMAPS = {
  inferno: [[0,0,4],[40,11,84],[101,21,110],[159,42,99],[212,72,66],[245,125,21],[250,193,39],[252,255,164]],
  viridis: [[68,1,84],[64,67,135],[41,120,142],[34,167,132],[121,209,81],[253,231,37]],
  damage:  [[230,238,234],[140,200,170],[210,196,90],[214,140,40],[200,60,50],[150,24,28]],
  life:    [[150,24,28],[214,120,40],[223,196,90],[150,200,120],[40,160,90],[20,120,70]],
  stress:  [[235,239,245],[120,170,220],[90,150,120],[220,200,90],[210,90,60],[150,28,30]],
  pressure:[[247,251,255],[198,219,239],[107,174,214],[49,130,189],[8,81,156],[8,48,107]],
  heatflux:[[8,29,88],[34,94,168],[29,145,192],[120,198,121],[247,202,60],[240,120,30],[200,30,30]],
};
function cmap(name, t) {
  const stops = COLORMAPS[name] || COLORMAPS.inferno;
  t = Math.max(0, Math.min(1, t));
  const x = t * (stops.length - 1);
  const i = Math.floor(x), f = x - i;
  const a = stops[i], b = stops[Math.min(i + 1, stops.length - 1)];
  return [Math.round(a[0] + (b[0] - a[0]) * f), Math.round(a[1] + (b[1] - a[1]) * f), Math.round(a[2] + (b[2] - a[2]) * f)];
}
function cmapCss(name, t) { const c = cmap(name, t); return `rgb(${c[0]},${c[1]},${c[2]})`; }
function cbarGradient(name) {
  const stops = COLORMAPS[name] || COLORMAPS.inferno;
  return `linear-gradient(to right, ${stops.map((c, i) => `rgb(${c[0]},${c[1]},${c[2]}) ${(i / (stops.length - 1) * 100).toFixed(0)}%`).join(", ")})`;
}

const FIELD_DEFS = {
  temperature: { label: "Temperature", short: "T", unit: "K",   cmap: "inferno", min: 320,  max: 1750, port: "field.T.forecast" },
  heatflux:    { label: "Heat Flux",   short: "K", unit: "MW/m\u00b2", cmap: "heatflux", min: 0, max: 4.2, port: "field.K.forecast" },
  pressure:    { label: "Pressure",    short: "P", unit: "kPa", cmap: "pressure", min: 0,   max: 62,   port: "field.P.forecast" },
  stress:      { label: "Stress",      short: "S", unit: "MPa", cmap: "stress",  min: 0,    max: 460,  port: "field.S.forecast" },
  damage:      { label: "Damage",      short: "D", unit: "",    cmap: "damage",  min: 0,    max: 1,    port: "damage.forecast" },
  life:        { label: "Remaining Life", short: "L", unit: "%", cmap: "life",   min: 0,    max: 100,  port: "life.field" },
};

/* ---------- SVG line chart ---------- */
function LineChart({ series, width, height, yDomain, xDomain, xLabel, yLabel, cursorX, grid = true, pad }) {
  const P = pad || { t: 10, r: 12, b: 22, l: 38 };
  const W = width, H = height;
  const allX = series.flatMap(s => s.points.map(p => p.x));
  const allY = series.flatMap(s => s.points.map(p => p.y));
  const xd = xDomain || [Math.min(...allX), Math.max(...allX)];
  const yd = yDomain || [Math.min(...allY), Math.max(...allY)];
  const sx = x => P.l + ((x - xd[0]) / (xd[1] - xd[0] || 1)) * (W - P.l - P.r);
  const sy = y => H - P.b - ((y - yd[0]) / (yd[1] - yd[0] || 1)) * (H - P.t - P.b);
  const xticks = 5, yticks = 4;
  return (
    <svg width={W} height={H} style={{ display: "block" }}>
      {grid && Array.from({ length: yticks + 1 }).map((_, i) => {
        const v = yd[0] + (yd[1] - yd[0]) * (i / yticks);
        const y = sy(v);
        return <g key={"y" + i}>
          <line x1={P.l} y1={y} x2={W - P.r} y2={y} stroke="var(--line)" strokeWidth="1" />
          <text x={P.l - 5} y={y + 3} textAnchor="end" fontSize="9" fill="var(--ink-4)" fontFamily="var(--mono)">{Math.abs(v) >= 100 ? v.toFixed(0) : v.toFixed(1)}</text>
        </g>;
      })}
      {grid && Array.from({ length: xticks + 1 }).map((_, i) => {
        const v = xd[0] + (xd[1] - xd[0]) * (i / xticks);
        const x = sx(v);
        return <text key={"x" + i} x={x} y={H - P.b + 13} textAnchor="middle" fontSize="9" fill="var(--ink-4)" fontFamily="var(--mono)">{v.toFixed(1)}</text>;
      })}
      {cursorX != null && cursorX >= xd[0] && cursorX <= xd[1] && (
        <line x1={sx(cursorX)} y1={P.t} x2={sx(cursorX)} y2={H - P.b} stroke="var(--acc)" strokeWidth="1.5" strokeDasharray="3 2" />
      )}
      {series.map((s, si) => {
        const dd = s.points.map((p, i) => `${i ? "L" : "M"}${sx(p.x).toFixed(1)},${sy(p.y).toFixed(1)}`).join(" ");
        return <g key={si}>
          {s.fill && <path d={`${dd} L${sx(s.points[s.points.length - 1].x)},${H - P.b} L${sx(s.points[0].x)},${H - P.b} Z`} fill={s.fill} opacity="0.5" />}
          <path d={dd} fill="none" stroke={s.color} strokeWidth={s.width || 1.8} strokeLinejoin="round" />
        </g>;
      })}
      {xLabel && <text x={(W + P.l) / 2} y={H - 2} textAnchor="middle" fontSize="9" fill="var(--ink-3)">{xLabel}</text>}
      {yLabel && <text x={10} y={P.t + 2} fontSize="9" fill="var(--ink-3)">{yLabel}</text>}
    </svg>
  );
}

/* ---------- sparkline ---------- */
function Spark({ data, width = 90, height = 26, color = "var(--acc)", fill }) {
  const mn = Math.min(...data), mx = Math.max(...data);
  const sx = i => (i / (data.length - 1)) * width;
  const sy = v => height - 2 - ((v - mn) / (mx - mn || 1)) * (height - 4);
  const d = data.map((v, i) => `${i ? "L" : "M"}${sx(i).toFixed(1)},${sy(v).toFixed(1)}`).join(" ");
  return <svg width={width} height={height} style={{ display: "block" }}>
    {fill && <path d={`${d} L${width},${height} L0,${height} Z`} fill={fill} opacity="0.4" />}
    <path d={d} fill="none" stroke={color} strokeWidth="1.6" strokeLinejoin="round" strokeLinecap="round" />
  </svg>;
}

Object.assign(window, {
  useState, useEffect, useRef, useMemo, useCallback,
  Icon, IconPaths, STATUS, st, Badge, SDot, Panel, KV, Meter, Toggle, Seg,
  COLORMAPS, cmap, cmapCss, cbarGradient, FIELD_DEFS, LineChart, Spark,
});
