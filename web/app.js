"use strict";

const CLASES_NOMBRES = [
  "Ritmo Sinusal Normal",
  "Arritmia Detectada",
  "Infarto de Miocardio (MI)",
  "Historial Post-MI"
];

const MANUAL_PRESETS = {
  normal:    { rr: 820,  pr: 162, qrs: 91,  qt: 405, bpm: 73 },
  arritmia:  { rr: 430,  pr: 130, qrs: 80,  qt: 350, bpm: 140 },
  infarto:   { rr: 1180, pr: 180, qrs: 92,  qt: 438, bpm: 51 },
  historial: { rr: 680,  pr: 152, qrs: 87,  qt: 385, bpm: 88 }
};

const state = {
  activePanel: "room",
  selectedFile: null,
  selectedFileDataUrl: null,
  cases: [],
  caseFilter: "all",
  correctTargetIndex: null,
  correctCallback: null,
  audioEnabled: false,
  audioCtx: null,
  ecgMode: "idle", // "idle" | 0 | 1 | 2 | 3
  ecgBpm: 72
};

/* ===================================================
   UTILIDADES UI & FECHA
   =================================================== */
function todayDDMMYYYY() {
  const d = new Date();
  const dd = String(d.getDate()).padStart(2, "0");
  const mm = String(d.getMonth() + 1).padStart(2, "0");
  const yyyy = d.getFullYear();
  return `${dd}/${mm}/${yyyy}`;
}

function showToast(msg, isError = false) {
  const container = document.getElementById("toast-container");
  const el = document.createElement("div");
  el.className = `toast${isError ? " error" : ""}`;
  el.textContent = msg;
  container.appendChild(el);
  setTimeout(() => el.remove(), 3600);
}

async function apiJson(url, options = {}) {
  let res;
  try {
    res = await fetch(url, options);
  } catch (_) {
    throw new Error("Sin conexion con el servidor C. Ejecuta output\\SINA-VISUAL.exe para iniciar el motor.");
  }
  const data = await res.json();
  if (!res.ok || data.ok === false) {
    throw new Error(data.error || `Error HTTP ${res.status}`);
  }
  return data;
}

function escapeHtml(str) {
  return String(str ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/* ===================================================
   NAVEGACION DE SALA Y PANELES
   =================================================== */
function openPanel(panelName) {
  state.activePanel = panelName;
  const workspace = document.getElementById("panel-workspace");
  const panels = document.querySelectorAll(".clinical-panel");

  panels.forEach((p) => p.classList.add("hidden"));

  if (panelName === "room") {
    workspace.classList.add("hidden");
    document.body.classList.remove("panel-open");
  } else {
    const target = document.getElementById(`panel-${panelName}`);
    if (target) {
      workspace.classList.remove("hidden");
      target.classList.remove("hidden");
      document.body.classList.add("panel-open");
    }
    if (panelName === "cases") {
      refreshCasesAndStats();
    } else if (panelName === "neural") {
      refreshStatus();
      refreshTrainingLogs();
    }
  }

  document.querySelectorAll(".dock-btn").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.dockPanel === panelName);
  });
}

/* ===================================================
   MONITOR ECG HOLOGRAFICO EN TIEMPO REAL
   =================================================== */
function playBeep(freq = 760, dur = 0.14) {
  if (!state.audioEnabled) return;
  try {
    if (!state.audioCtx) {
      state.audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    }
    const ctx = state.audioCtx;
    if (ctx.state === "suspended") ctx.resume();
    const now = ctx.currentTime;
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.type = "sine";
    osc.frequency.value = freq;
    gain.gain.setValueAtTime(0.0005, now);
    gain.gain.linearRampToValueAtTime(0.035, now + 0.015);
    gain.gain.exponentialRampToValueAtTime(0.0005, now + dur);
    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.start(now);
    osc.stop(now + dur);
  } catch (_) {
    /* Ignorar si navegador bloquea audio */
  }
}

function setTelemetryDiagnosis(tipoDiagnostico, bpmOverride = null) {
  state.ecgMode = tipoDiagnostico;
  const labelEl = document.getElementById("holo-state-label");
  const bpmEl = document.getElementById("holo-bpm-badge");

  if (tipoDiagnostico === 0) {
    state.ecgBpm = bpmOverride || 72;
    document.body.dataset.alert = "normal";
    labelEl.textContent = "TELEMETRIA UCI · RITMO SINUSAL";
  } else if (tipoDiagnostico === 1) {
    state.ecgBpm = bpmOverride || 136;
    document.body.dataset.alert = "warning";
    labelEl.textContent = "ALERTA · ARRITMIA DETECTADA";
  } else if (tipoDiagnostico === 2) {
    state.ecgBpm = bpmOverride || 54;
    document.body.dataset.alert = "critical";
    labelEl.textContent = "CRITICO · INFARTO DE MIOCARDIO";
  } else if (tipoDiagnostico === 3) {
    state.ecgBpm = bpmOverride || 84;
    document.body.dataset.alert = "warning";
    labelEl.textContent = "CONTROL · HISTORIAL POST-MI";
  } else {
    state.ecgBpm = 72;
    document.body.dataset.alert = "idle";
    labelEl.textContent = "TELEMETRIA UCI · EN ESPERA";
  }
  bpmEl.textContent = `${Math.round(state.ecgBpm)} BPM`;
}

function startEcgOscilloscope() {
  const canvas = document.getElementById("scene-ecg-canvas");
  const ctx = canvas.getContext("2d");
  const W = canvas.width;
  const H = canvas.height;
  const buffer = new Array(W).fill(H * 0.56);
  let cursorFloat = 0;
  let cursor = 0;
  let phase = 0;
  let lastBeatCycle = -1;
  let lastTime = performance.now();
  let lastBeepTime = 0;

  function ecgSample(p, mode) {
    // Onda P-QRS-T sintetizada en fase [0..1)
    const gauss = (x, mu, sigma) => Math.exp(-Math.pow((x - mu) / sigma, 2));
    let y = 0;
    y += 0.14 * gauss(p, 0.16, 0.035);  // P
    y -= 0.12 * gauss(p, 0.27, 0.012);  // Q
    y += 0.92 * gauss(p, 0.30, 0.014);  // R
    y -= 0.26 * gauss(p, 0.33, 0.014);  // S

    if (mode === 2) {
      // Elevacion del segmento ST tipica de infarto
      y += 0.30 * gauss(p, 0.42, 0.075);
      y += 0.32 * gauss(p, 0.54, 0.06);
    } else if (mode === 3) {
      // Onda T invertida (Post-MI)
      y -= 0.22 * gauss(p, 0.52, 0.055);
    } else {
      y += 0.24 * gauss(p, 0.52, 0.055); // T normal
    }

    if (mode === 1) {
      // Ruido / irregularidad auricular
      y += (Math.sin(p * 47.0) * 0.05);
    }
    return y;
  }

  function frame(now) {
    const dt = Math.min(0.05, Math.max(0.001, (now - lastTime) / 1000.0));
    lastTime = now;

    // Ritmo lento, calmado e independiente de los Hz del monitor (60Hz/144Hz)
    const bps = Math.max(0.5, state.ecgBpm / 60.0);
    const tempoScale = 0.35;
    const pxPerSec = 72;

    let stepPhase = bps * tempoScale * dt;
    if (state.ecgMode === 1) {
      stepPhase *= 0.85 + 0.25 * Math.sin(now * 0.003);
    }
    phase += stepPhase;

    const cycle = Math.floor(phase);
    const localPhase = phase - cycle;

    if (localPhase > 0.27 && localPhase < 0.38 && cycle !== lastBeatCycle) {
      lastBeatCycle = cycle;
      if (now - lastBeepTime >= 900) {
        lastBeepTime = now;
        const freq = state.ecgMode === 2 ? 520 : state.ecgMode === 1 ? 820 : 720;
        playBeep(freq, 0.14);
      }
    }

    const prevCursor = Math.floor(cursorFloat) % W;
    cursorFloat = (cursorFloat + pxPerSec * dt) % W;
    const nextCursor = Math.floor(cursorFloat) % W;
    const steps = (nextCursor - prevCursor + W) % W;

    for (let s = 1; s <= steps; s++) {
      cursor = (prevCursor + s) % W;
      const frac = s / steps;
      const interpPhase = (phase - stepPhase * (1 - frac)) % 1;
      const amp = ecgSample(interpPhase < 0 ? interpPhase + 1 : interpPhase, state.ecgMode);
      buffer[cursor] = H * 0.62 - amp * (H * 0.44);
    }

    ctx.fillStyle = "#030b14";
    ctx.fillRect(0, 0, W, H);

    // Rejilla medica sutil
    ctx.strokeStyle = "rgba(56, 224, 240, 0.08)";
    ctx.lineWidth = 1;
    for (let x = 0; x < W; x += 24) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, H);
      ctx.stroke();
    }
    for (let y = 0; y < H; y += 22) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(W, y);
      ctx.stroke();
    }

    const strokeColor =
      state.ecgMode === 2 ? "#ef4444" :
      state.ecgMode === 1 ? "#f59e0b" :
      state.ecgMode === 3 ? "#fbbf24" : "#22c55e";

    ctx.strokeStyle = strokeColor;
    ctx.shadowColor = strokeColor;
    ctx.shadowBlur = 8;
    ctx.lineWidth = 2;

    ctx.beginPath();
    let started = false;
    for (let x = 0; x < W; x++) {
      // Gap de barrido delante del cursor
      const dist = (x - cursor + W) % W;
      if (dist < 10) {
        started = false;
        continue;
      }
      if (!started) {
        ctx.moveTo(x, buffer[x]);
        started = true;
      } else {
        ctx.lineTo(x, buffer[x]);
      }
    }
    ctx.stroke();
    ctx.shadowBlur = 0;

    // Punto luminoso actual
    const curY = buffer[(cursor - 1 + W) % W];
    ctx.fillStyle = "#ffffff";
    ctx.beginPath();
    ctx.arc(cursor, curY, 3, 0, Math.PI * 2);
    ctx.fill();

    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
}

/* ===================================================
   CONVERSOR CLIENT-SIDE PNG/JPG -> BMP 24-BIT
   =================================================== */
async function fileTo24BitBmpBuffer(file, forceConvert = false) {
  const ext = file.name.toLowerCase();
  if (ext.endsWith(".bmp") && !forceConvert) {
    const raw = await file.arrayBuffer();
    if (raw.byteLength >= 54) {
      const view = new DataView(raw);
      const sig = view.getUint16(0, true);
      const bpp = view.getUint16(28, true);
      const comp = view.getUint32(30, true);
      if (sig === 0x4D42 && comp === 0 && (bpp === 24 || bpp === 32)) {
        return raw;
      }
    }
  }

  // Cargar imagen en canvas y codificar BMP 24-bit sin compresion
  const imgUrl = URL.createObjectURL(file);
  try {
    const img = await new Promise((resolve, reject) => {
      const i = new Image();
      i.onload = () => resolve(i);
      i.onerror = () => reject(new Error("No se pudo leer la imagen seleccionada"));
      i.src = imgUrl;
    });

    const maxDim = 2500;
    let w = img.naturalWidth || img.width;
    let h = img.naturalHeight || img.height;
    if (w > maxDim || h > maxDim) {
      const scale = Math.min(maxDim / w, maxDim / h);
      w = Math.max(1, Math.round(w * scale));
      h = Math.max(1, Math.round(h * scale));
    }

    const canvas = document.createElement("canvas");
    canvas.width = w;
    canvas.height = h;
    const ctx = canvas.getContext("2d");
    ctx.drawImage(img, 0, 0, w, h);
    const rgba = ctx.getImageData(0, 0, w, h).data;

    const rowStride = Math.floor((w * 3 + 3) / 4) * 4;
    const pixelDataSize = rowStride * h;
    const fileSize = 54 + pixelDataSize;
    const buf = new ArrayBuffer(fileSize);
    const view = new DataView(buf);
    const bytes = new Uint8Array(buf);

    // BITMAPFILEHEADER (14 bytes)
    view.setUint16(0, 0x4D42, true);
    view.setUint32(2, fileSize, true);
    view.setUint32(10, 54, true);
    // BITMAPINFOHEADER (40 bytes)
    view.setUint32(14, 40, true);
    view.setInt32(18, w, true);
    view.setInt32(22, h, true);
    view.setUint16(26, 1, true);
    view.setUint16(28, 24, true);
    view.setUint32(34, pixelDataSize, true);

    for (let y = 0; y < h; y++) {
      const srcY = h - 1 - y; // Bottom-up BMP
      const dstRow = 54 + y * rowStride;
      for (let x = 0; x < w; x++) {
        const sIdx = (srcY * w + x) * 4;
        const dIdx = dstRow + x * 3;
        bytes[dIdx]     = rgba[sIdx + 2]; // B
        bytes[dIdx + 1] = rgba[sIdx + 1]; // G
        bytes[dIdx + 2] = rgba[sIdx];     // R
      }
    }
    return buf;
  } finally {
    URL.revokeObjectURL(imgUrl);
  }
}

/* ===================================================
   RENDERIZADO DE RESULTADOS Y VERIFICACION INLINE
   =================================================== */
function renderDiagnosisHtml(containerEl, data, onVerified) {
  const res = data.resultado;
  const probs = res.probabilidades || [0, 0, 0, 0];

  const probRows = CLASES_NOMBRES.map((nombre, idx) => {
    const pct = Math.max(0, Math.min(100, probs[idx] * 100));
    return `
      <div class="prob-row">
        <span>${escapeHtml(nombre)}</span>
        <div class="prob-track"><div class="prob-fill" style="width:${pct.toFixed(1)}%"></div></div>
        <strong>${pct.toFixed(1)}%</strong>
      </div>
    `;
  }).join("");

  containerEl.innerHTML = `
    <div class="diag-banner urg-${res.nivel_urgencia}">
      <div class="diag-title-row">
        <span class="diag-title">${escapeHtml(res.descripcion)}</span>
        <span class="diag-conf">Confianza: ${(res.confianza * 100).toFixed(1)}% · Urgencia ${escapeHtml(res.urgencia_texto)}</span>
      </div>
      <p class="diag-rec">${escapeHtml(res.recomendacion)}</p>
    </div>

    <div class="prob-list">${probRows}</div>

    <div class="verify-bar">
      <button type="button" class="btn-secondary" data-verif="confirm">Confirmar Correcto</button>
      <button type="button" class="btn-chip" data-verif="correct">Corregir Diagnostico</button>
      <button type="button" class="btn-chip" data-verif="discard">Descartar (Ruido)</button>
      <span class="verify-done-msg hidden" style="color:#86efac;font-size:12px;align-self:center;"></span>
    </div>
  `;

  const doneMsg = containerEl.querySelector(".verify-done-msg");
  const buttons = containerEl.querySelectorAll("[data-verif]");

  const lockVerified = (text) => {
    buttons.forEach((b) => (b.disabled = true));
    doneMsg.textContent = text;
    doneMsg.classList.remove("hidden");
    refreshStatus();
    if (onVerified) onVerified();
  };

  containerEl.querySelector('[data-verif="confirm"]').addEventListener("click", async () => {
    await verifyCaseApi(data.case_index, 1, 0, 0, res.tipo_diagnostico);
    lockVerified("Caso confirmado en archivo clinico.");
  });

  containerEl.querySelector('[data-verif="discard"]').addEventListener("click", async () => {
    await verifyCaseApi(data.case_index, 1, 1, 0, res.tipo_diagnostico);
    lockVerified("Caso marcado como descartado.");
  });

  containerEl.querySelector('[data-verif="correct"]').addEventListener("click", () => {
    openCorrectionModal(data.case_index, res.tipo_diagnostico, (nuevoTipo) => {
      lockVerified(`Corregido a: ${CLASES_NOMBRES[nuevoTipo]}`);
    });
  });
}

function draw32x32Matrix(pixels) {
  const canvas = document.getElementById("bmp-matrix-canvas");
  const ctx = canvas.getContext("2d");
  const off = document.createElement("canvas");
  off.width = 32;
  off.height = 32;
  const octx = off.getContext("2d");
  const imgData = octx.createImageData(32, 32);

  let maxVal = 1;
  for (let i = 0; i < 1024; i++) {
    if ((pixels[i] ?? 0) > maxVal) maxVal = pixels[i];
  }
  const gain = Math.min(4.0, 255.0 / Math.max(64, maxVal));

  for (let i = 0; i < 1024; i++) {
    const v = Math.min(255, Math.round((pixels[i] ?? 0) * gain));
    const p = i * 4;
    imgData.data[p]     = Math.round(v * 0.45);
    imgData.data[p + 1] = Math.min(255, Math.round(v * 0.95));
    imgData.data[p + 2] = v;
    imgData.data[p + 3] = 255;
  }
  octx.putImageData(imgData, 0, 0);

  ctx.imageSmoothingEnabled = false;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.drawImage(off, 0, 0, canvas.width, canvas.height);
}

/* ===================================================
   ESTADO GLOBAL, MUESTRAS Y CASOS
   =================================================== */
async function refreshStatus() {
  try {
    const st = await apiJson("/api/status");
    const pillBmp = document.getElementById("pill-bmp");
    const pillMan = document.getElementById("pill-manual");

    pillBmp.classList.toggle("ready", st.red_bmp.entrenada);
    pillMan.classList.toggle("ready", st.red_manual.entrenada);

    const accBmp = st.red_bmp.precision_final != null ? ` · ${st.red_bmp.precision_final.toFixed(0)}%` : "";
    const accMan = st.red_manual.precision_final != null ? ` · ${st.red_manual.precision_final.toFixed(0)}%` : "";

    document.getElementById("status-bmp-text").textContent =
      st.red_bmp.entrenada ? `Lista${accBmp}` : "Sin entrenar";
    document.getElementById("status-manual-text").textContent =
      st.red_manual.entrenada ? `Lista${accMan}` : "Sin entrenar";
    document.getElementById("status-cases-count").textContent = st.estadisticas.total;

    document.getElementById("info-net-bmp").innerHTML = `
      <div><strong>Topologia:</strong> ${st.red_bmp.topologia.join(" → ")}</div>
      <div><strong>Activacion:</strong> PReLU (α=0.01) / Sigmoide</div>
      <div><strong>Tasa / Epocas:</strong> ${st.red_bmp.tasa} / ${st.red_bmp.epocas}</div>
      <div><strong>Precision entreno:</strong> ${st.red_bmp.precision_final != null ? st.red_bmp.precision_final.toFixed(1) + "%" : "N/D"}</div>
      <div><strong>Loss final:</strong> ${st.red_bmp.error_final != null ? st.red_bmp.error_final.toFixed(4) : "N/D"}</div>
    `;
    document.getElementById("info-net-manual").innerHTML = `
      <div><strong>Topologia:</strong> ${st.red_manual.topologia.join(" → ")}</div>
      <div><strong>Activacion:</strong> PReLU (α=0.01) / Sigmoide</div>
      <div><strong>Tasa / Epocas:</strong> ${st.red_manual.tasa} / ${st.red_manual.epocas}</div>
      <div><strong>Precision entreno:</strong> ${st.red_manual.precision_final != null ? st.red_manual.precision_final.toFixed(1) + "%" : "N/D"}</div>
      <div><strong>Loss final:</strong> ${st.red_manual.error_final != null ? st.red_manual.error_final.toFixed(4) : "N/D"}</div>
    `;
  } catch (err) {
    console.error(err);
  }
}

async function loadSamples() {
  try {
    const data = await apiJson("/api/samples");
    const box = document.getElementById("samples-list");
    box.innerHTML = "";
    if (!data.samples.length) {
      box.innerHTML = `<span style="color:var(--text-dim)">Sin muestras en /samples</span>`;
      return;
    }
    data.samples.forEach((name) => {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "btn-chip";
      btn.textContent = name;
      btn.addEventListener("click", () => runSampleBmpAnalysis(name));
      box.appendChild(btn);
    });
  } catch (err) {
    console.error(err);
  }
}

function getPatientFormValues(prefix) {
  return {
    nombre: document.getElementById(`${prefix}-nombre`).value.trim() || "Paciente Demo",
    cedula: document.getElementById(`${prefix}-cedula`).value.trim() || "30123456",
    medico: document.getElementById(`${prefix}-medico`).value.trim() || "Dr. Neira",
    fecha: document.getElementById(`${prefix}-fecha`).value.trim() || todayDDMMYYYY()
  };
}

async function runSampleBmpAnalysis(sampleName) {
  try {
    const pac = getPatientFormValues("bmp");
    const data = await apiJson("/api/analyze/sample", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ sample: sampleName, ...pac })
    });

    document.getElementById("bmp-orig-preview").src = `/samples/${encodeURIComponent(sampleName)}`;
    document.getElementById("bmp-result-empty").classList.add("hidden");
    document.getElementById("bmp-result-content").classList.remove("hidden");
    draw32x32Matrix(data.preview_32x32);
    renderDiagnosisHtml(document.getElementById("bmp-diagnosis-box"), data);
    setTelemetryDiagnosis(data.resultado.tipo_diagnostico);
    refreshStatus();
    showToast(`Muestra ${sampleName} analizada (${data.ancho_orig}x${data.alto_orig})`);
  } catch (err) {
    showToast(err.message, true);
  }
}

async function verifyCaseApi(index, verificado, descartado, corregido, tipo_correcto) {
  await apiJson("/api/cases/verify", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ index, verificado, descartado, corregido, tipo_correcto })
  });
  showToast("Estado del caso actualizado");
}

function openCorrectionModal(caseIndex, currentTipo, callback) {
  state.correctTargetIndex = caseIndex;
  state.correctCallback = callback;
  document.getElementById("correct-select").value = String(currentTipo);
  document.getElementById("correct-modal-sub").textContent = `Caso #${caseIndex + 1} · Seleccione el diagnostico definitivo:`;
  document.getElementById("correct-modal").classList.remove("hidden");
}

async function refreshCasesAndStats() {
  try {
    const [casesRes, statsRes] = await Promise.all([
      apiJson("/api/cases"),
      apiJson("/api/stats")
    ]);
    state.cases = casesRes.cases;
    renderStatsKpis(statsRes);
    renderCasesTable();
  } catch (err) {
    showToast(err.message, true);
  }
}

function renderStatsKpis(st) {
  document.getElementById("cases-kpi-grid").innerHTML = `
    <div class="kpi-card"><div class="kpi-label">Total</div><div class="kpi-val">${st.total}</div></div>
    <div class="kpi-card"><div class="kpi-label">Verificados</div><div class="kpi-val" style="color:#86efac">${st.verificados}</div></div>
    <div class="kpi-card"><div class="kpi-label">Pendientes</div><div class="kpi-val" style="color:#fcd34d">${st.pendientes}</div></div>
    <div class="kpi-card"><div class="kpi-label">Corregidos</div><div class="kpi-val" style="color:var(--cyan)">${st.corregidos}</div></div>
    <div class="kpi-card"><div class="kpi-label">Descartados</div><div class="kpi-val" style="color:#fca5a5">${st.descartados}</div></div>
  `;

  const total = Math.max(1, st.total);
  document.getElementById("stats-bars").innerHTML = CLASES_NOMBRES.map((nom, i) => {
    const cnt = st.por_tipo[i] || 0;
    const pct = (cnt / total) * 100;
    return `
      <div class="prob-row">
        <span>${escapeHtml(nom)}</span>
        <div class="prob-track"><div class="prob-fill" style="width:${pct.toFixed(1)}%"></div></div>
        <strong>${cnt} (${pct.toFixed(0)}%)</strong>
      </div>
    `;
  }).join("");
}

function renderCasesTable() {
  const tbody = document.getElementById("cases-tbody");
  const filtered = state.cases.filter((c) => {
    if (state.caseFilter === "pending") return !c.verificado && !c.descartado;
    if (state.caseFilter === "verified") return c.verificado && !c.descartado;
    if (state.caseFilter === "discarded") return c.descartado;
    return true;
  });

  if (!filtered.length) {
    tbody.innerHTML = `<tr><td colspan="7" style="text-align:center;color:var(--text-dim);padding:18px">No hay casos en esta vista.</td></tr>`;
    return;
  }

  tbody.innerHTML = filtered.map((c) => {
    let statusTag = `<span class="tag-status tag-pending">Pendiente</span>`;
    if (c.descartado) statusTag = `<span class="tag-status tag-discarded">Descartado</span>`;
    else if (c.diagnostico_corregido) statusTag = `<span class="tag-status tag-corrected">Corregido</span>`;
    else if (c.verificado) statusTag = `<span class="tag-status tag-verified">Verificado</span>`;

    return `
      <tr>
        <td>${escapeHtml(c.nombre)}</td>
        <td>${escapeHtml(c.cedula)}</td>
        <td>${c.usa_bmp ? "BMP" : "Manual"}</td>
        <td>${escapeHtml(c.descripcion_final)}</td>
        <td>${(c.confianza * 100).toFixed(1)}%</td>
        <td>${statusTag}</td>
        <td>
          <button class="action-mini" data-row-act="confirm" data-idx="${c.index}" title="Confirmar">✓</button>
          <button class="action-mini" data-row-act="correct" data-idx="${c.index}" data-tipo="${c.tipo_diagnostico}" title="Corregir">✎</button>
          <button class="action-mini" data-row-act="discard" data-idx="${c.index}" title="Descartar">✕</button>
          <button class="action-mini" data-row-act="report" data-ced="${escapeHtml(c.cedula)}" title="Ver Reporte">📄</button>
          <button class="action-mini action-mini-danger" data-row-act="delete" data-idx="${c.index}" data-nombre="${escapeHtml(c.nombre)}" title="Borrar caso">🗑</button>
        </td>
      </tr>
    `;
  }).join("");
}

async function fetchAndShowReport(cedula) {
  if (!cedula) return;
  try {
    const data = await apiJson(`/api/report?cedula=${encodeURIComponent(cedula)}`);
    document.getElementById("report-search-cedula").value = cedula;
    document.getElementById("report-viewer").textContent = data.reporte;
  } catch (err) {
    document.getElementById("report-viewer").textContent = err.message;
  }
}

/* ===================================================
   GRAFICOS DE ENTRENAMIENTO EN CANVAS
   =================================================== */
async function refreshTrainingLogs() {
  try {
    const data = await apiJson("/api/training-log");
    drawTrainingChart("chart-bmp", data.bmp);
    drawTrainingChart("chart-manual", data.manual);
  } catch (err) {
    console.error(err);
  }
}

function drawTrainingChart(canvasId, series) {
  const canvas = document.getElementById(canvasId);
  const ctx = canvas.getContext("2d");
  const W = canvas.width;
  const H = canvas.height;

  ctx.fillStyle = "#040b14";
  ctx.fillRect(0, 0, W, H);

  if (!series || series.length === 0) {
    ctx.fillStyle = "#92adc9";
    ctx.font = "13px Segoe UI";
    ctx.fillText("Sin datos de entrenamiento registrados aun.", 24, H / 2);
    return;
  }

  const padL = 46, padR = 46, padT = 26, padB = 28;
  const plotW = W - padL - padR;
  const plotH = H - padT - padB;
  const maxLoss = Math.max(0.5, ...series.map((p) => p.loss));

  // Rejilla
  ctx.strokeStyle = "rgba(146, 173, 201, 0.14)";
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = padT + (plotH * i) / 4;
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(W - padR, y);
    ctx.stroke();
  }

  // Curva Loss (Cyan)
  ctx.strokeStyle = "#38e0f0";
  ctx.lineWidth = 2;
  ctx.beginPath();
  series.forEach((pt, idx) => {
    const x = padL + (idx / Math.max(1, series.length - 1)) * plotW;
    const y = padT + plotH - (pt.loss / maxLoss) * plotH;
    if (idx === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();

  // Curva Accuracy (Verde)
  ctx.strokeStyle = "#22c55e";
  ctx.lineWidth = 2;
  ctx.beginPath();
  series.forEach((pt, idx) => {
    const x = padL + (idx / Math.max(1, series.length - 1)) * plotW;
    const y = padT + plotH - (pt.accuracy / 100.0) * plotH;
    if (idx === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();

  // Etiquetas y leyenda
  const last = series[series.length - 1];
  ctx.font = "11px Consolas";
  ctx.fillStyle = "#38e0f0";
  ctx.fillText(`Loss: ${last.loss.toFixed(4)}`, padL, 16);
  ctx.fillStyle = "#22c55e";
  ctx.fillText(`Precision: ${last.accuracy.toFixed(1)}%`, padL + 130, 16);
  ctx.fillStyle = "#92adc9";
  ctx.fillText(`Epocas: ${series.length}`, W - 115, 16);
  ctx.fillText("0", padL - 22, H - padB);
  ctx.fillText(maxLoss.toFixed(2), 6, padT + 8);
  ctx.fillText("100%", W - 40, padT + 8);
}

/* ===================================================
   INICIALIZACION Y EVENTOS
   =================================================== */
document.addEventListener("DOMContentLoaded", () => {
  // Fechas iniciales
  document.getElementById("bmp-fecha").value = todayDDMMYYYY();
  document.getElementById("man-fecha").value = todayDDMMYYYY();

  startEcgOscilloscope();
  refreshStatus();
  loadSamples();

  // Hotspots de la sala medica + dock + botones cerrar
  document.querySelectorAll("[data-open-panel]").forEach((el) => {
    el.addEventListener("click", () => openPanel(el.dataset.openPanel));
  });
  document.querySelectorAll("[data-dock-panel]").forEach((el) => {
    el.addEventListener("click", () => openPanel(el.dataset.dockPanel));
  });
  document.querySelectorAll("[data-close-panel]").forEach((el) => {
    el.addEventListener("click", () => openPanel("room"));
  });

  document.getElementById("btn-room-view").addEventListener("click", () => openPanel("room"));

  document.getElementById("btn-audio-toggle").addEventListener("click", (e) => {
    state.audioEnabled = !state.audioEnabled;
    e.currentTarget.textContent = state.audioEnabled ? "Sonido: ON" : "Sonido: OFF";
    if (state.audioEnabled) playBeep(720, 0.14);
  });

  document.getElementById("btn-shutdown").addEventListener("click", async () => {
    try {
      await fetch("/api/shutdown", { method: "POST" });
    } catch (_) {}
    window.close();
  });

  // Dropzone BMP / Imagen
  const dropzone = document.getElementById("bmp-dropzone");
  const fileInput = document.getElementById("bmp-file-input");

  const handleSelectedFile = (file) => {
    if (!file) return;
    state.selectedFile = file;
    const chip = document.getElementById("bmp-file-name");
    chip.textContent = `${file.name} (${(file.size / 1024).toFixed(1)} KB)`;
    chip.classList.remove("hidden");

    const reader = new FileReader();
    reader.onload = () => {
      state.selectedFileDataUrl = reader.result;
      document.getElementById("bmp-orig-preview").src = reader.result;
    };
    reader.readAsDataURL(file);
  };

  dropzone.addEventListener("click", () => fileInput.click());
  fileInput.addEventListener("change", () => handleSelectedFile(fileInput.files[0]));
  dropzone.addEventListener("dragover", (e) => {
    e.preventDefault();
    dropzone.classList.add("dragover");
  });
  dropzone.addEventListener("dragleave", () => dropzone.classList.remove("dragover"));
  dropzone.addEventListener("drop", (e) => {
    e.preventDefault();
    dropzone.classList.remove("dragover");
    handleSelectedFile(e.dataTransfer.files[0]);
  });

  document.getElementById("form-bmp").addEventListener("submit", async (e) => {
    e.preventDefault();
    if (!state.selectedFile) {
      showToast("Selecciona una imagen ECG o haz clic en una muestra rapida.", true);
      return;
    }

    try {
      const pac = getPatientFormValues("bmp");
      const bmpBuf = await fileTo24BitBmpBuffer(state.selectedFile);
      const qs = new URLSearchParams(pac).toString();
      const data = await apiJson(`/api/analyze/bmp?${qs}`, {
        method: "POST",
        headers: { "Content-Type": "application/octet-stream" },
        body: bmpBuf
      });

      document.getElementById("bmp-result-empty").classList.add("hidden");
      document.getElementById("bmp-result-content").classList.remove("hidden");
      draw32x32Matrix(data.preview_32x32);
      renderDiagnosisHtml(document.getElementById("bmp-diagnosis-box"), data);
      setTelemetryDiagnosis(data.resultado.tipo_diagnostico);
      refreshStatus();
      showToast("ECG procesado por la red neuronal BMP");
    } catch (err) {
      showToast(err.message, true);
    }
  });

  // Sliders y Presets Manuales
  ["rr", "pr", "qrs", "qt", "bpm"].forEach((k) => {
    const input = document.getElementById(`man-${k}`);
    const out = document.getElementById(`val-${k}`);
    input.addEventListener("input", () => {
      out.textContent = input.value;
    });
  });

  document.querySelectorAll("[data-preset]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const p = MANUAL_PRESETS[btn.dataset.preset];
      if (!p) return;
      Object.entries(p).forEach(([k, val]) => {
        document.getElementById(`man-${k}`).value = val;
        document.getElementById(`val-${k}`).textContent = val;
      });
    });
  });

  document.getElementById("form-manual").addEventListener("submit", async (e) => {
    e.preventDefault();
    try {
      const pac = getPatientFormValues("man");
      const payload = {
        ...pac,
        rr: Number(document.getElementById("man-rr").value),
        pr: Number(document.getElementById("man-pr").value),
        qrs: Number(document.getElementById("man-qrs").value),
        qt: Number(document.getElementById("man-qt").value),
        bpm: Number(document.getElementById("man-bpm").value)
      };

      const data = await apiJson("/api/analyze/manual", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });

      const labels = ["RR", "PR", "QRS", "QT", "BPM"];
      document.getElementById("manual-norm-chips").innerHTML = data.normalizados
        .map((v, i) => `<span class="norm-chip">${labels[i]}_norm = ${v.toFixed(3)}</span>`)
        .join("");

      document.getElementById("manual-result-empty").classList.add("hidden");
      document.getElementById("manual-result-content").classList.remove("hidden");
      renderDiagnosisHtml(document.getElementById("manual-diagnosis-box"), data);
      setTelemetryDiagnosis(data.resultado.tipo_diagnostico, payload.bpm);
      refreshStatus();
      showToast("Evaluacion clinica completada");
    } catch (err) {
      showToast(err.message, true);
    }
  });

  // Filtros y acciones en tabla de casos
  document.querySelectorAll("[data-filter]").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll("[data-filter]").forEach((b) => b.classList.remove("active"));
      btn.classList.add("active");
      state.caseFilter = btn.dataset.filter;
      renderCasesTable();
    });
  });

  document.getElementById("cases-tbody").addEventListener("click", async (e) => {
    const btn = e.target.closest("[data-row-act]");
    if (!btn) return;
    const act = btn.dataset.rowAct;
    const idx = Number(btn.dataset.idx);

    if (act === "report") {
      fetchAndShowReport(btn.dataset.ced);
    } else if (act === "confirm") {
      await verifyCaseApi(idx, 1, 0, 0, 0);
      refreshCasesAndStats();
      refreshStatus();
    } else if (act === "discard") {
      await verifyCaseApi(idx, 1, 1, 0, 0);
      refreshCasesAndStats();
      refreshStatus();
    } else if (act === "correct") {
      openCorrectionModal(idx, Number(btn.dataset.tipo || 0), () => {
        refreshCasesAndStats();
        refreshStatus();
      });
    } else if (act === "delete") {
      const nom = btn.dataset.nombre || `#${idx + 1}`;
      if (!window.confirm(`¿Eliminar permanentemente el caso de "${nom}"?`)) return;
      try {
        await apiJson("/api/cases/delete", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ index: idx })
        });
        showToast("Caso eliminado del historial");
        refreshCasesAndStats();
        refreshStatus();
      } catch (err) {
        showToast(err.message, true);
      }
    }
  });

  document.getElementById("btn-clear-discarded").addEventListener("click", async () => {
    const numDesc = state.cases.filter((c) => c.descartado).length;
    if (numDesc === 0) {
      showToast("No hay casos descartados para limpiar.");
      return;
    }
    if (!window.confirm(`¿Eliminar permanentemente los ${numDesc} caso(s) descartados?`)) return;
    try {
      const res = await apiJson("/api/cases/clear-discarded", { method: "POST" });
      showToast(`${res.eliminados} caso(s) descartado(s) eliminados`);
      refreshCasesAndStats();
      refreshStatus();
    } catch (err) {
      showToast(err.message, true);
    }
  });

  document.getElementById("btn-search-report").addEventListener("click", () => {
    fetchAndShowReport(document.getElementById("report-search-cedula").value.trim());
  });
  document.getElementById("btn-print-report").addEventListener("click", () => window.print());

  // Modal correccion
  document.getElementById("correct-cancel").addEventListener("click", () => {
    document.getElementById("correct-modal").classList.add("hidden");
  });
  document.getElementById("correct-confirm").addEventListener("click", async () => {
    const nuevoTipo = Number(document.getElementById("correct-select").value);
    try {
      await verifyCaseApi(state.correctTargetIndex, 1, 0, 1, nuevoTipo);
      document.getElementById("correct-modal").classList.add("hidden");
      if (state.correctCallback) state.correctCallback(nuevoTipo);
    } catch (err) {
      showToast(err.message, true);
    }
  });

  // Entrenamiento Admin
  document.querySelectorAll("[data-train-target]").forEach((btn) => {
    btn.addEventListener("click", async () => {
      const target = btn.dataset.trainTarget;
      const pin = document.getElementById("train-pin").value;
      const fb = document.getElementById("train-feedback");
      fb.textContent = "Entrenando red(es) en C... por favor espera.";
      try {
        await apiJson("/api/train", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ target, pin })
        });
        fb.textContent = "Entrenamiento finalizado y pesos guardados en disco (.nn).";
        refreshStatus();
        refreshTrainingLogs();
        showToast("Redes neuronales entrenadas y guardadas");
      } catch (err) {
        fb.textContent = "";
        showToast(err.message, true);
      }
    });
  });
});
