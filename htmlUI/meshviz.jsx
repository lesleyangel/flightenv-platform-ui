/* ===================================================================
   MeshView — programmatic reentry-vehicle cross-section mesh, node-
   colored by physical field. Looks like an engineering mesh, not a
   random heatmap. Canvas 2D, DPR-aware, hover-to-read.
   =================================================================== */

/* vehicle silhouette: control points [xNorm(0 nose →1 base), rNorm half-height] */
const RV_PROFILE = [
  [0.000, 0.030], [0.018, 0.085], [0.045, 0.140], [0.090, 0.205],
  [0.160, 0.285], [0.260, 0.360], [0.400, 0.440], [0.560, 0.510],
  [0.730, 0.575], [0.880, 0.630], [0.960, 0.665], [1.000, 0.690],
];
function profileR(xn) {
  const p = RV_PROFILE;
  for (let i = 0; i < p.length - 1; i++) {
    if (xn >= p[i][0] && xn <= p[i + 1][0]) {
      const f = (xn - p[i][0]) / (p[i + 1][0] - p[i][0] || 1);
      const ff = f * f * (3 - 2 * f); // smoothstep
      return p[i][1] + (p[i + 1][1] - p[i][1]) * ff;
    }
  }
  return p[p.length - 1][1];
}
function hashNoise(a, b) { const s = Math.sin(a * 127.1 + b * 311.7) * 43758.5453; return s - Math.floor(s); }

/* field value at (xn 0..1, yn -1..1 windward<0), returns normalized 0..1 */
function fieldVal(field, xn, yn, frame) {
  const wind = (1 - yn) / 2;            // 1 at windward (bottom), 0 leeward
  const nose = Math.exp(-xn / 0.16);    // strong near nose
  const surf = Math.abs(yn);            // near outer surface
  const ramp = 0.55 + 0.45 * frame;     // reentry heating builds with frame
  const nz = (hashNoise(xn * 9, yn * 9) - 0.5) * 0.07;
  let v;
  if (field === "temperature") {
    const edge = Math.exp(-Math.abs(xn - 0.18) / 0.06) * 0.35;       // leading-edge shoulder
    v = (0.16 + 0.74 * nose * (0.45 + 0.55 * wind) + edge * wind) * ramp;
    v += 0.10 * wind * (1 - xn);
  } else if (field === "heatflux") {
    // heat flux: very concentrated at stagnation nose + windward leading edge
    const stag = Math.exp(-xn / 0.10) * 0.95;
    const edge = Math.exp(-Math.abs(xn - 0.18) / 0.05) * 0.5 * wind;
    v = (0.08 + (stag * (0.4 + 0.6 * wind)) + edge) * (0.6 + 0.4 * ramp);
  } else if (field === "pressure") {
    // pressure: high on windward stagnation, smooth, drops leeward & aft
    const stag = Math.exp(-xn / 0.22) * 0.85;
    v = (0.12 + stag * (0.25 + 0.75 * wind) + 0.18 * wind * (1 - xn * 0.5));
  } else if (field === "stress") {
    const shoulder = Math.exp(-Math.abs(xn - 0.21) / 0.05) * 0.8;
    const flare = Math.exp(-Math.abs(xn - 0.95) / 0.05) * 0.6;
    v = (0.12 + (shoulder + flare) * (0.5 + 0.5 * surf) + 0.18 * surf) * ramp;
  } else if (field === "damage") {
    const noseHot = Math.exp(-xn / 0.12) * 0.85;
    const shoulderHot = Math.exp(-Math.abs(xn - 0.21) / 0.04) * 0.7 * wind;
    v = (noseHot * (0.4 + 0.6 * wind) + shoulderHot) * (0.7 + 0.3 * frame);
  } else { // life = 1 - damage-ish
    const noseHot = Math.exp(-xn / 0.12) * 0.85;
    const shoulderHot = Math.exp(-Math.abs(xn - 0.21) / 0.04) * 0.7 * wind;
    const dmg = (noseHot * (0.4 + 0.6 * wind) + shoulderHot) * (0.7 + 0.3 * frame);
    v = 1 - Math.min(1, dmg);
  }
  return Math.max(0, Math.min(1, v + nz));
}

/* sensor markers in (xn, yn) */
const RV_SENSORS = [
  { id: "TC-01", xn: 0.02, yn: -0.2 },
  { id: "TC-02", xn: 0.14, yn: -0.85 },
  { id: "TC-05", xn: 0.45, yn: -0.92 },
  { id: "SG-03", xn: 0.21, yn: 0.9 },
  { id: "PT-02", xn: 0.30, yn: -0.95 },
  { id: "SG-07", xn: 0.72, yn: 0.55 },
];
const REGION_SPAN = {
  nose_cap:   [0.0, 0.12], tps_windward: [0.12, 0.6], shoulder: [0.15, 0.28], structure: [0.4, 1.0],
  nose: [0.0, 0.12], tps: [0.12, 0.6],
};

function MeshView({ field = "temperature", frame = 1, region = null, showSensors = true, showGrid = true, animateKey }) {
  const wrapRef = useRef(null);
  const canRef = useRef(null);
  const [size, setSize] = useState({ w: 600, h: 320 });
  const [hover, setHover] = useState(null);
  const def = FIELD_DEFS[field] || FIELD_DEFS.temperature;

  useEffect(() => {
    const el = wrapRef.current; if (!el) return;
    const ro = new ResizeObserver(() => {
      const r = el.getBoundingClientRect();
      setSize({ w: Math.max(280, r.width), h: Math.max(200, r.height) });
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const draw = useCallback(() => {
    const cv = canRef.current; if (!cv) return;
    const dpr = window.devicePixelRatio || 1;
    const W = size.w, H = size.h;
    cv.width = W * dpr; cv.height = H * dpr; cv.style.width = W + "px"; cv.style.height = H + "px";
    const ctx = cv.getContext("2d"); ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);

    // layout
    const padL = 28, padR = 18, padT = 26, padB = 30;
    const vehLen = W - padL - padR;
    const maxR = Math.min((H - padT - padB) / 2, vehLen * 0.34);
    const midY = padT + (H - padT - padB) / 2;
    const x0 = padL;

    const nx = 38, ny = 18;
    const px = (xn) => x0 + xn * vehLen;
    const py = (xn, yn) => midY + yn * profileR(xn) * maxR * 2;

    const span = region ? REGION_SPAN[region] : null;
    const inRegion = (xn) => !span || (xn >= span[0] && xn <= span[1]);

    // draw quads
    for (let i = 0; i < nx; i++) {
      const xa = i / nx, xb = (i + 1) / nx;
      for (let j = 0; j < ny; j++) {
        const ya = -1 + (2 * j) / ny, yb = -1 + (2 * (j + 1)) / ny;
        const xc = (xa + xb) / 2, yc = (ya + yb) / 2;
        const v = fieldVal(field, xc, yc, frame);
        const c = cmap(def.cmap, v);
        const dim = !inRegion(xc);
        ctx.beginPath();
        ctx.moveTo(px(xa), py(xa, ya));
        ctx.lineTo(px(xb), py(xb, ya));
        ctx.lineTo(px(xb), py(xb, yb));
        ctx.lineTo(px(xa), py(xa, yb));
        ctx.closePath();
        if (dim) {
          const g = Math.round(0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]);
          ctx.fillStyle = `rgba(${g},${g},${g},0.28)`;
        } else {
          ctx.fillStyle = `rgb(${c[0]},${c[1]},${c[2]})`;
        }
        ctx.fill();
        if (showGrid) { ctx.strokeStyle = "rgba(18,24,34,0.10)"; ctx.lineWidth = 0.5; ctx.stroke(); }
      }
    }

    // silhouette outline
    ctx.beginPath();
    for (let i = 0; i <= nx; i++) { const xn = i / nx; const x = px(xn), y = py(xn, -1); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
    for (let i = nx; i >= 0; i--) { const xn = i / nx; ctx.lineTo(px(xn), py(xn, 1)); }
    ctx.closePath();
    ctx.strokeStyle = "rgba(18,24,34,0.55)"; ctx.lineWidth = 1.2; ctx.stroke();

    // centerline (axis of symmetry)
    ctx.beginPath(); ctx.moveTo(px(0), midY); ctx.lineTo(px(1), midY);
    ctx.strokeStyle = "rgba(18,24,34,0.18)"; ctx.lineWidth = 0.8; ctx.setLineDash([4, 4]); ctx.stroke(); ctx.setLineDash([]);

    // region outline
    if (span) {
      ctx.beginPath();
      for (let i = 0; i <= nx; i++) { const xn = span[0] + (span[1] - span[0]) * i / nx; const x = px(xn), y = py(xn, -1); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
      for (let i = nx; i >= 0; i--) { const xn = span[0] + (span[1] - span[0]) * i / nx; ctx.lineTo(px(xn), py(xn, 1)); }
      ctx.closePath();
      ctx.strokeStyle = "rgba(14,138,156,0.95)"; ctx.lineWidth = 2; ctx.setLineDash([5, 3]); ctx.stroke(); ctx.setLineDash([]);
    }

    // flow direction arrow + windward label
    ctx.fillStyle = "rgba(18,24,34,0.4)"; ctx.font = "10px 'JetBrains Mono', monospace";
    ctx.fillText("→ 来流 / windward", px(0.02), padT - 10);

    // sensors
    if (showSensors) {
      RV_SENSORS.forEach(s => {
        const x = px(s.xn), y = py(s.xn, s.yn);
        ctx.beginPath(); ctx.rect(x - 4, y - 4, 8, 8);
        ctx.fillStyle = "#fff"; ctx.fill();
        ctx.strokeStyle = "#13202c"; ctx.lineWidth = 1.3; ctx.stroke();
        ctx.beginPath(); ctx.moveTo(x - 2, y); ctx.lineTo(x + 2, y); ctx.moveTo(x, y - 2); ctx.lineTo(x, y + 2);
        ctx.strokeStyle = "#13202c"; ctx.lineWidth = 1; ctx.stroke();
        ctx.fillStyle = "rgba(18,24,34,0.7)"; ctx.font = "9px 'JetBrains Mono', monospace";
        ctx.fillText(s.id, x + 7, y - 6);
      });
    }

    // hover marker
    if (hover) {
      const x = px(hover.xn), y = py(hover.xn, hover.yn);
      ctx.beginPath(); ctx.arc(x, y, 5, 0, Math.PI * 2);
      ctx.strokeStyle = "#0e8a9c"; ctx.lineWidth = 2; ctx.stroke();
      ctx.beginPath(); ctx.arc(x, y, 2, 0, Math.PI * 2); ctx.fillStyle = "#0e8a9c"; ctx.fill();
    }
  }, [size, field, frame, region, showSensors, showGrid, hover]);

  useEffect(() => { draw(); }, [draw, animateKey]);

  function onMove(e) {
    const cv = canRef.current; const r = cv.getBoundingClientRect();
    const mx = e.clientX - r.left, my = e.clientY - r.top;
    const W = size.w, H = size.h, padL = 28, padR = 18, padT = 26, padB = 30;
    const vehLen = W - padL - padR, maxR = Math.min((H - padT - padB) / 2, vehLen * 0.34), midY = padT + (H - padT - padB) / 2;
    const xn = Math.max(0, Math.min(1, (mx - padL) / vehLen));
    const rr = profileR(xn) * maxR * 2;
    const yn = (my - midY) / (rr || 1);
    if (Math.abs(yn) <= 1.02 && xn >= 0 && xn <= 1) {
      const v = fieldVal(field, xn, Math.max(-1, Math.min(1, yn)), frame);
      const real = def.min + (def.max - def.min) * v;
      setHover({ xn, yn: Math.max(-1, Math.min(1, yn)), val: real, mx, my });
    } else setHover(null);
  }

  const def2 = def;
  return (
    <div ref={wrapRef} style={{ position: "relative", width: "100%", height: "100%" }}>
      <canvas ref={canRef} onMouseMove={onMove} onMouseLeave={() => setHover(null)} style={{ display: "block", cursor: "crosshair" }} />
      {hover && (
        <div style={{
          position: "absolute", left: Math.min(hover.mx + 12, size.w - 120), top: Math.max(6, hover.my - 36),
          background: "rgba(20,28,40,0.92)", color: "#fff", fontSize: 11, padding: "4px 8px", borderRadius: 5,
          fontFamily: "var(--mono)", pointerEvents: "none", whiteSpace: "nowrap", boxShadow: "var(--sh-pop)",
        }}>
          node · {def2.short} = {def2.unit === "" ? hover.val.toFixed(3) : hover.val.toFixed(0)}{def2.unit}
        </div>
      )}
    </div>
  );
}

/* colorbar legend strip */
function ColorBar({ field, selectedVal }) {
  const def = FIELD_DEFS[field] || FIELD_DEFS.temperature;
  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 5 }}>
      <div className="cbar" style={{ background: cbarGradient(def.cmap), width: "100%" }}></div>
      <div className="cbar-row between" style={{ display: "flex", justifyContent: "space-between" }}>
        <span>{def.min}{def.unit}</span>
        <span style={{ color: "var(--ink-2)" }}>{def.label} {def.port ? `· ${def.port}` : ""}</span>
        <span>{def.max}{def.unit}</span>
      </div>
    </div>
  );
}

/* field selector tabs */
function FieldTabs({ value, onChange, fields }) {
  const list = fields || ["temperature", "stress", "damage", "life"];
  return (
    <div className="ftabs">
      {list.map(f => {
        const d = FIELD_DEFS[f];
        return (
          <div key={f} className={`ftab ${value === f ? "on" : ""}`} onClick={() => onChange(f)}>
            <span className="sw" style={{ background: cmapCss(d.cmap, 0.8) }}></span>{d.label}
          </div>
        );
      })}
    </div>
  );
}

Object.assign(window, { MeshView, ColorBar, FieldTabs, RV_SENSORS, fieldVal, FIELD_DEFS });
