const FIELD_CACHE = new Map();

function fieldApiUrl(run, branch, port, step, artifactId = "") {
  const query = new URLSearchParams();
  if (run) query.set("run", run);
  if (artifactId) {
    query.set("artifact_id", artifactId);
  } else {
    if (branch) query.set("branch", branch);
    if (port) query.set("port", port);
    if (step !== undefined && step !== null && step !== "") query.set("step", String(step));
  }
  return `/api/field?${query.toString()}`;
}

function geometryApiUrl(run, field) {
  const query = new URLSearchParams();
  const geometry = (field && field.geometry_query) || {};
  const layoutRef = geometry.layout_ref || (field && field.layout_ref);
  const meshRef = geometry.mesh_ref || (field && field.mesh_ref);
  const nodeCount = geometry.node_count || (field && field.node_count);
  if (run) query.set("run", run);
  if (layoutRef) query.set("layout_ref", layoutRef);
  if (meshRef) query.set("mesh_ref", meshRef);
  if (nodeCount !== undefined && nodeCount !== null && nodeCount !== "") query.set("node_count", String(nodeCount));
  query.set("visual_surface", "1");
  return `/api/geometry?${query.toString()}`;
}

function geometryCountText(value) {
  const n = Number(value);
  if (!Number.isFinite(n)) return "-";
  return Math.round(n).toLocaleString("zh-CN");
}

function scaleValue(value, min, max) {
  if (!Number.isFinite(value)) return 0;
  if (!Number.isFinite(min) || !Number.isFinite(max) || max <= min) return 0.5;
  return Math.max(0, Math.min(1, (value - min) / (max - min)));
}

function colorRamp(t) {
  const stops = [
    [0.00, [28, 53, 128]],
    [0.18, [24, 128, 205]],
    [0.38, [54, 190, 108]],
    [0.58, [238, 224, 64]],
    [0.78, [236, 116, 38]],
    [1.00, [180, 24, 30]],
  ];
  for (let i = 0; i < stops.length - 1; i += 1) {
    const [a, ca] = stops[i];
    const [b, cb] = stops[i + 1];
    if (t >= a && t <= b) {
      const u = (t - a) / (b - a);
      return [
        (ca[0] + (cb[0] - ca[0]) * u) / 255,
        (ca[1] + (cb[1] - ca[1]) * u) / 255,
        (ca[2] + (cb[2] - ca[2]) * u) / 255,
      ];
    }
  }
  const c = stops[stops.length - 1][1];
  return [c[0] / 255, c[1] / 255, c[2] / 255];
}

function normalizeGeometry(positions) {
  const count = positions.length / 3;
  if (!count) return { radius: 1, positions };
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  for (let i = 0; i < positions.length; i += 3) {
    const x = Number(positions[i]);
    const y = Number(positions[i + 1]);
    const z = Number(positions[i + 2]);
    minX = Math.min(minX, x); minY = Math.min(minY, y); minZ = Math.min(minZ, z);
    maxX = Math.max(maxX, x); maxY = Math.max(maxY, y); maxZ = Math.max(maxZ, z);
  }
  const center = [(minX + maxX) / 2, (minY + maxY) / 2, (minZ + maxZ) / 2];
  let radius = 0;
  const normalized = new Float32Array(positions.length);
  for (let i = 0; i < positions.length; i += 3) {
    const x = Number(positions[i]) - center[0];
    const y = Number(positions[i + 1]) - center[1];
    const z = Number(positions[i + 2]) - center[2];
    radius = Math.max(radius, Math.hypot(x, y, z));
    normalized[i] = x;
    normalized[i + 1] = y;
    normalized[i + 2] = z;
  }
  return { radius: radius || 1, positions: normalized };
}

function buildRenderableGeometry(geometry) {
  const positions = Array.isArray(geometry && geometry.positions) ? geometry.positions : [];
  const indices = Array.isArray(geometry && geometry.indices) ? geometry.indices : [];
  const sourceNodeIds = Array.isArray(geometry && geometry.node_ids) ? geometry.node_ids : [];
  const sourceVertexCount = Math.floor(positions.length / 3);

  if (!indices.length) {
    return {
      positions,
      indices: [],
      node_ids: sourceNodeIds.length === sourceVertexCount
        ? sourceNodeIds
        : Array.from({ length: sourceVertexCount }, (_, i) => i + 1),
      render_mode: "source_points",
      source_vertex_count: sourceVertexCount,
    };
  }

  // Qt VTK uses a face-expanded surface: every triangle inserts its three
  // vertices and stores the original node index in cachedFaceIds.  Repeating
  // vertices here keeps WebUI color placement identical to the Qt renderer.
  const expandedPositions = [];
  const expandedNodeIds = [];
  indices.forEach((rawIndex) => {
    const index = Math.trunc(Number(rawIndex));
    if (!Number.isFinite(index) || index < 0 || index >= sourceVertexCount) return;
    const base = index * 3;
    expandedPositions.push(Number(positions[base]), Number(positions[base + 1]), Number(positions[base + 2]));
    expandedNodeIds.push(sourceNodeIds.length === sourceVertexCount ? sourceNodeIds[index] : index + 1);
  });
  return {
    positions: new Float32Array(expandedPositions),
    indices: [],
    node_ids: expandedNodeIds,
    render_mode: "qt_face_expanded",
    source_vertex_count: sourceVertexCount,
  };
}

function geometryNodeIndexMap(geometry, vertexCount) {
  const ids = Array.isArray(geometry && geometry.node_ids) ? geometry.node_ids : [];
  if (ids.length !== vertexCount) return null;
  const map = new Map();
  ids.forEach((rawId, index) => {
    const id = Number(rawId);
    if (!Number.isFinite(id)) return;
    const key = String(Math.trunc(id));
    if (!map.has(key)) map.set(key, index);
  });
  return map.size ? map : null;
}

function geometrySignature(geometry, vertexCount) {
  const positionLength = geometry && geometry.positions && Number.isFinite(Number(geometry.positions.length))
    ? Number(geometry.positions.length)
    : 0;
  return [
    (geometry && geometry.mesh_ref) || "",
    (geometry && geometry.layout_ref) || "",
    (geometry && geometry.render_mode) || "",
    (geometry && geometry.mesh_file) || "",
    String(vertexCount || 0),
    String(positionLength),
    String(Array.isArray(geometry && geometry.indices) ? geometry.indices.length : 0),
    String(Array.isArray(geometry && geometry.node_ids) ? geometry.node_ids.length : 0),
  ].join("|");
}

function disposeMesh(mesh) {
  if (!mesh) return;
  if (mesh.geometry) mesh.geometry.dispose();
  if (mesh.material) mesh.material.dispose();
}

function buildColorAttribute(field, geometry, vertexCount) {
  const values = Array.isArray(field && field.values) ? field.values : [];
  const ids = Array.isArray(field && field.node_ids) ? field.node_ids : [];
  const stats = (typeof fieldStats === "function")
    ? fieldStats(field)
    : Object.assign({}, (field && field.stats) || {}, { min: field && field.min, max: field && field.max });
  const min = Number(stats.min);
  const max = Number(stats.max);
  const colors = new Float32Array(vertexCount * 3);
  colors.fill(0.45);

  if (!values.length) return colors;
  if (ids.length === values.length) {
    const geometryNodeIds = Array.isArray(geometry && geometry.node_ids) ? geometry.node_ids : [];
    const valuesByNodeId = new Map();
    ids.forEach((rawId, i) => {
      const id = Number(rawId);
      if (Number.isFinite(id)) valuesByNodeId.set(String(Math.trunc(id)), Number(values[i]));
    });
    if (geometryNodeIds.length === vertexCount) {
      geometryNodeIds.forEach((rawId, index) => {
        const id = Number(rawId);
        let value = Number.isFinite(id) ? valuesByNodeId.get(String(Math.trunc(id))) : undefined;
        if (value === undefined && Number.isFinite(id)) {
          value = values[Math.trunc(id) - 1];
        }
        if (value === undefined) return;
        const color = colorRamp(scaleValue(Number(value), min, max));
        colors[index * 3] = color[0];
        colors[index * 3 + 1] = color[1];
        colors[index * 3 + 2] = color[2];
      });
      return colors;
    }

    values.forEach((value, i) => {
      const id = Number(ids[i]);
      const index = Math.trunc(id) - 1;
      if (!Number.isFinite(index) || index < 0 || index >= vertexCount) return;
      const color = colorRamp(scaleValue(Number(value), min, max));
      colors[index * 3] = color[0];
      colors[index * 3 + 1] = color[1];
      colors[index * 3 + 2] = color[2];
    });
    return colors;
  }

  const limit = Math.min(values.length, vertexCount);
  for (let i = 0; i < limit; i += 1) {
    const color = colorRamp(scaleValue(Number(values[i]), min, max));
    colors[i * 3] = color[0];
    colors[i * 3 + 1] = color[1];
    colors[i * 3 + 2] = color[2];
  }
  return colors;
}

function Legend({ field }) {
  const stats = (typeof fieldStats === "function")
    ? fieldStats(field)
    : ((field && field.stats) || {});
  return (
    <div className="field-legend">
      <span>{fmtNumber(stats.max)}</span>
      <div />
      <span>{fmtNumber(stats.min)}</span>
    </div>
  );
}

function Field3D({ run, branch, port, step, artifactId = "", height = 480 }) {
  const hostRef = React.useRef(null);
  const rendererRef = React.useRef(null);
  const meshRef = React.useRef(null);
  const meshSignatureRef = React.useRef("");
  const frameRef = React.useRef(0);
  const sceneRef = React.useRef(null);
  const abortRef = React.useRef(null);
  const [reloadToken, setReloadToken] = React.useState(0);
  const [state, setState] = React.useState({
    loading: false,
    error: "",
    field: null,
    geometry: null,
    diagnostics: [],
  });

  React.useEffect(() => {
    if (!run || !port) {
      setState({
        loading: false,
        error: "请选择 run、分支和输出端口。",
        field: null,
        geometry: null,
        diagnostics: [],
      });
      return;
    }

    let cancelled = false;
    const controller = new AbortController();
    abortRef.current = controller;
    async function load() {
      setState((old) => ({
        ...old,
        loading: true,
        error: "",
        // 已经有场在显示时（逐帧播放/实时跟随的重新拉取）保留旧诊断，避免底部信息条闪烁。
        diagnostics: old.field ? old.diagnostics : ["读取 field artifact", "等待 mesh/layout 对账"],
      }));
      try {
        const fieldRes = await fetch(fieldApiUrl(run, branch, port, step, artifactId), {
          cache: "no-store",
          signal: controller.signal,
        });
        const field = await fieldRes.json();
        if (!fieldRes.ok || field.ok === false) throw new Error(field.error || "field artifact 读取失败");
        if (!Array.isArray(field.values) || !field.values.length) {
          throw new Error("field artifact 缺少 values，拒绝绘制默认假图");
        }

        const fieldNodeCount = Number(field.node_count);
        if (!Number.isFinite(fieldNodeCount) || fieldNodeCount <= 0) {
          throw new Error(`field artifact 缺少有效 node_count：${field.node_count || "-"}`);
        }

        const geoKey = [run, field.layout_ref || "", field.mesh_ref || "", String(fieldNodeCount)].join("|");
        let geometry = FIELD_CACHE.get(geoKey);
        if (!geometry) {
          const geoRes = await fetch(geometryApiUrl(run, field), {
            cache: "no-store",
            signal: controller.signal,
          });
          geometry = await geoRes.json();
          if (!geoRes.ok || geometry.ok === false) throw new Error(geometry.error || "geometry 读取失败");
          FIELD_CACHE.set(geoKey, geometry);
        }

        const meshNodeCount = Number(geometry.node_count);
        if (!Number.isFinite(meshNodeCount) || meshNodeCount <= 0) {
          throw new Error(`geometry 缺少有效 node_count：${geometry.node_count || "-"}`);
        }
        if (meshNodeCount !== fieldNodeCount) {
          throw new Error(`节点数不一致，拒绝渲染：field.node_count=${fieldNodeCount}，mesh.node_count=${meshNodeCount}`);
        }
        if (!Array.isArray(geometry.positions) || !geometry.positions.length) {
          throw new Error("geometry 缺少 positions，拒绝绘制默认假图");
        }

        const hasFieldNodeIds = Array.isArray(field.node_ids) && field.node_ids.length === field.values.length;
        const hasGeometryNodeIds = Array.isArray(geometry.node_ids) && geometry.node_ids.length === meshNodeCount;
        const nodeMapMode = hasFieldNodeIds && hasGeometryNodeIds
          ? "field_node_ids -> geometry_node_ids"
          : (hasFieldNodeIds ? "field_node_ids -> sequential_geometry" : "value_order -> geometry_order");
        const diagnostics = [
          `artifact=${field.artifact_id || field.artifact_uri || "-"}`,
          `mesh=${field.mesh_ref || geometry.mesh_ref || "-"}`,
          `layout=${field.layout_ref || geometry.layout_ref || "-"}`,
          `mesh_file=${geometry.mesh_file || "-"}`,
          `node_map=${nodeMapMode}`,
          `node_count=${fieldNodeCount}`,
          `values=${field.values.length}`,
        ];
        if (!cancelled) setState({ loading: false, error: "", field, geometry, diagnostics });
      } catch (err) {
        if (cancelled || err.name === "AbortError") return;
        if (!cancelled) {
          setState({
            loading: false,
            error: err.message || String(err),
            field: null,
            geometry: null,
            diagnostics: [],
          });
        }
      }
    }
    load();
    return () => {
      cancelled = true;
      controller.abort();
    };
  }, [run, branch, port, step, artifactId, reloadToken]);

  React.useEffect(() => {
    const host = hostRef.current;
    if (!host) return undefined;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0xf5f8fb);
    const camera = new THREE.PerspectiveCamera(45, 1, 0.1, 10000);
    camera.position.set(0, -4.2, 2.4);
    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    renderer.setClearColor(0xf5f8fb, 1);
    host.appendChild(renderer.domElement);

    const light = new THREE.DirectionalLight(0xffffff, 1.0);
    light.position.set(3, -4, 5);
    scene.add(light);
    scene.add(new THREE.AmbientLight(0xffffff, 0.72));

    const grid = new THREE.GridHelper(3.6, 8, 0xb8c6d6, 0xdce5ef);
    grid.rotation.x = Math.PI / 2;
    scene.add(grid);
    scene.add(new THREE.AxesHelper(1.2));

    sceneRef.current = scene;
    rendererRef.current = renderer;

    const pointer = { dragging: false, x: 0, y: 0, theta: -0.9, phi: 0.58, distance: 4.8 };
    function updateCamera() {
      const x = pointer.distance * Math.cos(pointer.phi) * Math.sin(pointer.theta);
      const y = pointer.distance * Math.cos(pointer.phi) * Math.cos(pointer.theta);
      const z = pointer.distance * Math.sin(pointer.phi);
      camera.position.set(x, y, z);
      camera.lookAt(0, 0, 0);
    }
    function resize() {
      const rect = host.getBoundingClientRect();
      const w = Math.max(320, rect.width || 320);
      const h = Math.max(260, rect.height || height);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      renderer.setSize(w, h, false);
    }
    function onDown(ev) {
      pointer.dragging = true;
      pointer.x = ev.clientX;
      pointer.y = ev.clientY;
      renderer.domElement.setPointerCapture(ev.pointerId);
    }
    function onMove(ev) {
      if (!pointer.dragging) return;
      const dx = ev.clientX - pointer.x;
      const dy = ev.clientY - pointer.y;
      pointer.x = ev.clientX;
      pointer.y = ev.clientY;
      pointer.theta -= dx * 0.008;
      pointer.phi = Math.max(-1.2, Math.min(1.2, pointer.phi + dy * 0.006));
      updateCamera();
    }
    function onUp(ev) {
      pointer.dragging = false;
      try { renderer.domElement.releasePointerCapture(ev.pointerId); } catch (_) {}
    }
    function onWheel(ev) {
      ev.preventDefault();
      pointer.distance = Math.max(1.2, Math.min(12, pointer.distance * (ev.deltaY > 0 ? 1.08 : 0.92)));
      updateCamera();
    }
    function animate() {
      frameRef.current = requestAnimationFrame(animate);
      const mesh = meshRef.current;
      if (mesh && !pointer.dragging) mesh.rotation.z += 0.0015;
      renderer.render(scene, camera);
    }

    renderer.domElement.addEventListener("pointerdown", onDown);
    renderer.domElement.addEventListener("pointermove", onMove);
    renderer.domElement.addEventListener("pointerup", onUp);
    renderer.domElement.addEventListener("pointercancel", onUp);
    renderer.domElement.addEventListener("wheel", onWheel, { passive: false });
    window.addEventListener("resize", resize);

    updateCamera();
    resize();
    animate();

    return () => {
      cancelAnimationFrame(frameRef.current);
      window.removeEventListener("resize", resize);
      renderer.domElement.removeEventListener("pointerdown", onDown);
      renderer.domElement.removeEventListener("pointermove", onMove);
      renderer.domElement.removeEventListener("pointerup", onUp);
      renderer.domElement.removeEventListener("pointercancel", onUp);
      renderer.domElement.removeEventListener("wheel", onWheel);
      if (meshRef.current) {
        disposeMesh(meshRef.current);
        meshRef.current = null;
        meshSignatureRef.current = "";
      }
      renderer.dispose();
      renderer.domElement.remove();
    };
  }, []);

  React.useEffect(() => {
    const scene = sceneRef.current;
    if (!scene || !state.field || !state.geometry) return;

    const renderGeometry = buildRenderableGeometry(state.geometry);
    const normalized = normalizeGeometry(renderGeometry.positions || []);
    const vertexCount = normalized.positions.length / 3;
    const geometryForColor = { ...state.geometry, ...renderGeometry };
    const signature = geometrySignature(geometryForColor, vertexCount);
    const colors = buildColorAttribute(state.field, geometryForColor, vertexCount);

    if (meshRef.current && meshSignatureRef.current === signature) {
      const colorAttribute = meshRef.current.geometry.getAttribute("color");
      if (colorAttribute && colorAttribute.array && colorAttribute.array.length === colors.length) {
        colorAttribute.array.set(colors);
        colorAttribute.needsUpdate = true;
      } else {
        meshRef.current.geometry.setAttribute("color", new THREE.BufferAttribute(colors, 3));
      }
      return;
    }

    if (meshRef.current) {
      scene.remove(meshRef.current);
      disposeMesh(meshRef.current);
      meshRef.current = null;
      meshSignatureRef.current = "";
    }

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.BufferAttribute(normalized.positions, 3));
    geometry.setAttribute("color", new THREE.BufferAttribute(colors, 3));
    if (Array.isArray(renderGeometry.indices) && renderGeometry.indices.length) {
      geometry.setIndex(renderGeometry.indices);
    }
    geometry.computeVertexNormals();

    const material = new THREE.MeshStandardMaterial({
      vertexColors: true,
      metalness: 0.02,
      roughness: 0.72,
      side: THREE.DoubleSide,
    });
    const mesh = new THREE.Mesh(geometry, material);
    const scale = 1.75 / (normalized.radius || 1);
    mesh.scale.setScalar(scale);
    meshRef.current = mesh;
    meshSignatureRef.current = signature;
    scene.add(mesh);
  }, [state.field, state.geometry]);

  const stats = (typeof fieldStats === "function")
    ? fieldStats(state.field)
    : ((state.field && state.field.stats) || {});
  const fillHeight = height === "100%" || height === "auto";
  return (
    <div className="field3d-card" style={fillHeight ? { minHeight: 320 } : { minHeight: height }}>
      <div className="field3d-top">
        <div>
          <strong>{portDisplay(port)}</strong>
          <span>{branch || "-"} / step {step || 0}</span>
        </div>
        <div className="field3d-stats">
          <span>min {fmtNumber(stats.min)}</span>
          <span>max {fmtNumber(stats.max)}</span>
          <span>mean {fmtNumber(stats.mean)}</span>
        </div>
      </div>
      <div className="field3d-stage" ref={hostRef} style={fillHeight ? undefined : { height }} />
      {state.field && !state.error && <Legend field={state.field} />}
      {state.diagnostics.length > 0 && (
        <div className="field3d-diagnostics">
          {state.diagnostics.map((item) => <span key={item}>{item}</span>)}
        </div>
      )}
      {state.loading && !state.field && (
        <div className="field3d-overlay">
          <span>正在读取真实场 artifact 并校验 mesh/layout...</span>
          <button type="button" onClick={() => abortRef.current && abortRef.current.abort()}>取消</button>
        </div>
      )}
      {state.loading && state.field && (
        <div className="field3d-updating">更新中…</div>
      )}
      {state.error && (
        <div className="field3d-overlay error">
          <span>{state.error}</span>
          <button type="button" onClick={() => setReloadToken((value) => value + 1)}>重试</button>
        </div>
      )}
    </div>
  );
}

function ObjectGeometry3D({ resource, componentId = "", packageDir = "", height = 360, title = "真实几何" }) {
  const hostRef = React.useRef(null);
  const rendererRef = React.useRef(null);
  const meshRef = React.useRef(null);
  const wireRef = React.useRef(null);
  const frameRef = React.useRef(0);
  const sceneRef = React.useRef(null);
  const resourceId = resource && (resource.resource_id || resource.id || resource.uri || resource.path);
  const [state, setState] = React.useState({ loading: false, error: "", geometry: null });

  React.useEffect(() => {
    if (!resourceId) {
      setState({ loading: false, error: "当前对象未绑定可渲染的 mesh 资源。", geometry: null });
      return undefined;
    }
    const controller = new AbortController();
    const query = new URLSearchParams({
      component_id: componentId || "",
      resource_id: resourceId,
      max_triangles: "60000",
    });
    if (packageDir) query.set("package_dir", packageDir);
    setState({ loading: true, error: "", geometry: null });
    fetch(`/api/object-geometry?${query.toString()}`, { cache: "no-store", signal: controller.signal })
      .then((res) => res.json().then((data) => ({ res, data })))
      .then(({ res, data }) => {
        if (!res.ok || !data || data.ok === false) throw new Error((data && (data.message || data.error)) || "geometry 读取失败");
        if (!Array.isArray(data.positions) || !data.positions.length) throw new Error("geometry 缺少 positions，拒绝绘制空图");
        setState({ loading: false, error: "", geometry: data });
      })
      .catch((err) => {
        if (err && err.name === "AbortError") return;
        setState({ loading: false, error: err.message || String(err), geometry: null });
      });
    return () => controller.abort();
  }, [resourceId, componentId, packageDir]);

  React.useEffect(() => {
    const host = hostRef.current;
    if (!host) return undefined;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0xf7fbfd);
    const camera = new THREE.PerspectiveCamera(45, 1, 0.1, 10000);
    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    renderer.setClearColor(0xf7fbfd, 1);
    host.appendChild(renderer.domElement);

    scene.add(new THREE.HemisphereLight(0xffffff, 0xc8d8e8, 1.15));
    const keyLight = new THREE.DirectionalLight(0xffffff, 1.2);
    keyLight.position.set(4, 5, 3);
    scene.add(keyLight);
    const fillLight = new THREE.DirectionalLight(0xcdefff, 0.45);
    fillLight.position.set(-3, 2, -2);
    scene.add(fillLight);

    sceneRef.current = scene;
    rendererRef.current = renderer;

    const pointer = { dragging: false, x: 0, y: 0, theta: -0.62, phi: 0.48, distance: 5.0 };
    function updateCamera() {
      const x = pointer.distance * Math.cos(pointer.phi) * Math.sin(pointer.theta);
      const y = pointer.distance * Math.cos(pointer.phi) * Math.cos(pointer.theta);
      const z = pointer.distance * Math.sin(pointer.phi);
      camera.position.set(x, y, z);
      camera.lookAt(0, 0, 0);
    }
    function resize() {
      const rect = host.getBoundingClientRect();
      const w = Math.max(320, rect.width || 320);
      const h = Math.max(260, rect.height || height);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      renderer.setSize(w, h, false);
    }
    function onDown(ev) {
      pointer.dragging = true;
      pointer.x = ev.clientX;
      pointer.y = ev.clientY;
      renderer.domElement.setPointerCapture(ev.pointerId);
    }
    function onMove(ev) {
      if (!pointer.dragging) return;
      const dx = ev.clientX - pointer.x;
      const dy = ev.clientY - pointer.y;
      pointer.x = ev.clientX;
      pointer.y = ev.clientY;
      pointer.theta -= dx * 0.008;
      pointer.phi = Math.max(-1.2, Math.min(1.2, pointer.phi + dy * 0.006));
      updateCamera();
    }
    function onUp(ev) {
      pointer.dragging = false;
      try { renderer.domElement.releasePointerCapture(ev.pointerId); } catch (_) {}
    }
    function onWheel(ev) {
      ev.preventDefault();
      pointer.distance = Math.max(1.2, Math.min(12, pointer.distance * (ev.deltaY > 0 ? 1.08 : 0.92)));
      updateCamera();
    }
    function animate() {
      frameRef.current = requestAnimationFrame(animate);
      const mesh = meshRef.current;
      if (mesh && !pointer.dragging) {
        mesh.rotation.z += 0.0015;
      }
      renderer.render(scene, camera);
    }

    renderer.domElement.addEventListener("pointerdown", onDown);
    renderer.domElement.addEventListener("pointermove", onMove);
    renderer.domElement.addEventListener("pointerup", onUp);
    renderer.domElement.addEventListener("pointercancel", onUp);
    renderer.domElement.addEventListener("wheel", onWheel, { passive: false });
    window.addEventListener("resize", resize);
    updateCamera();
    resize();
    animate();

    return () => {
      cancelAnimationFrame(frameRef.current);
      window.removeEventListener("resize", resize);
      renderer.domElement.removeEventListener("pointerdown", onDown);
      renderer.domElement.removeEventListener("pointermove", onMove);
      renderer.domElement.removeEventListener("pointerup", onUp);
      renderer.domElement.removeEventListener("pointercancel", onUp);
      renderer.domElement.removeEventListener("wheel", onWheel);
      [meshRef.current, wireRef.current].forEach((item) => {
        if (!item) return;
        item.geometry.dispose();
        item.material.dispose();
      });
      meshRef.current = null;
      wireRef.current = null;
      renderer.dispose();
      renderer.domElement.remove();
    };
  }, []);

  React.useEffect(() => {
    const scene = sceneRef.current;
    if (!scene || !state.geometry) return;
    [meshRef.current, wireRef.current].forEach((item) => {
      if (!item) return;
      scene.remove(item);
      item.geometry.dispose();
      item.material.dispose();
    });
    meshRef.current = null;
    wireRef.current = null;

    const normalized = normalizeGeometry(state.geometry.positions || []);
    const merged = mergeGeometryVertices(normalized.positions, state.geometry.indices);
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.BufferAttribute(merged.positions, 3));
    const zeroColor = colorRamp(0);
    const colors = new Float32Array((merged.positions.length / 3) * 3);
    for (let i = 0; i < colors.length; i += 3) {
      colors[i] = zeroColor[0];
      colors[i + 1] = zeroColor[1];
      colors[i + 2] = zeroColor[2];
    }
    geometry.setAttribute("color", new THREE.BufferAttribute(colors, 3));
    if (merged.indices.length) {
      geometry.setIndex(merged.indices);
    }
    geometry.computeVertexNormals();
    const material = new THREE.MeshStandardMaterial({
      vertexColors: true,
      metalness: 0.02,
      roughness: 0.72,
      side: THREE.DoubleSide,
    });
    const mesh = new THREE.Mesh(geometry, material);
    mesh.scale.setScalar(1.68 / (normalized.radius || 1));
    meshRef.current = mesh;
    scene.add(mesh);

  }, [state.geometry]);

  const stats = state.geometry || {};
  const displayedNodeCount = Number(stats.node_count) || (Array.isArray(stats.positions) ? Math.floor(stats.positions.length / 3) : 0);
  const displayedTriangleCount = Number(stats.triangle_count)
    || (Array.isArray(stats.indices) && stats.indices.length ? Math.floor(stats.indices.length / 3) : 0);
  return (
    <div className="field3d-card object-geometry-card" style={{ minHeight: height }}>
      <div className="field3d-top">
        <div>
          <strong>{title}</strong>
          <span>{resourceId || "-"} / {componentId || "-"}</span>
        </div>
        <div className="field3d-stats">
          <span>nodes {geometryCountText(displayedNodeCount)}</span>
          <span>triangles {geometryCountText(displayedTriangleCount)}</span>
          <span>{stats.decimated ? "decimated" : "full"}</span>
        </div>
      </div>
      <div className="field3d-stage" ref={hostRef} style={{ height }} />
      {(state.loading || state.error) && (
        <div className={state.error ? "field3d-overlay error" : "field3d-overlay"}>
          <strong>{state.error ? "真实几何读取失败" : "正在读取真实几何"}</strong>
          <span>{state.error || "从对象包 mesh 资源加载 positions / indices。"}</span>
        </div>
      )}
      {state.geometry && (
        <div className="field3d-diagnostics">
          <span>mesh={state.geometry.mesh_ref || resourceId || "-"}</span>
          <span>source={state.geometry.source_file_name || "-"}</span>
        </div>
      )}
    </div>
  );
}

window.Field3D = Field3D;
window.ObjectGeometry3D = ObjectGeometry3D;
