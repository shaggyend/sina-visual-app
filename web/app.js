/* =========================================================================
   SINA-VISUAL PRO · Instrumentación Cardiológica & Telemetría Neuronal C11
   Frontend Controller (app.js)
   ========================================================================= */

const state = {
  activePanel: "room",
  status: null,
  cases: [],
  caseFilter: "all",
  selectedBmpFile: null,
  lastBmpCaseIndex: null,
  lastManualCaseIndex: null,
  correctModalCaseIndex: null,
  audioEnabled: false,
  audioCtx: null,
  hasBmpResult: false,
  ecgBpm: 72,
  ecgMode: "idle", // "idle" | 0: Normal, 1: Arritmia, 2: Infarto MI, 3: Antecedente MI
  lastReportText: "",
  lastReportCedula: "",
};

const DIAG_LABELS = [
  "Ritmo Sinusal Normal",
  "Arritmia / Latido Anormal",
  "Infarto Agudo de Miocardio (MI)",
  "Antecedente de Infarto (History MI)",
];

const CLINICAL_PRESETS = {
  normal: { rr: 830, pr: 160, qrs: 90, qt: 390, bpm: 72 },
  arritmia: { rr: 520, pr: 215, qrs: 128, qt: 455, bpm: 118 },
  infarto: { rr: 600, pr: 205, qrs: 152, qt: 495, bpm: 102 },
  mi: { rr: 600, pr: 205, qrs: 152, qt: 495, bpm: 102 },
  historial: { rr: 760, pr: 188, qrs: 118, qt: 432, bpm: 79 },
  hist_mi: { rr: 760, pr: 188, qrs: 118, qt: 432, bpm: 79 },
};

document.addEventListener("DOMContentLoaded", () => {
  initDefaultDates();
  initNavigation();
  initAudioSystem();
  initSceneEcgLoop();
  initBmpScanner();
  initManualEvaluator();
  initCasesPanel();
  initNeuralPanel();
  initShutdownButton();

  refreshSystemStatus();
  loadSampleChips();
});

function initDefaultDates() {
  const now = new Date();
  const dd = String(now.getDate()).padStart(2, "0");
  const mm = String(now.getMonth() + 1).padStart(2, "0");
  const yyyy = now.getFullYear();
  const todayStr = `${dd}/${mm}/${yyyy}`;

  const bmpFecha = document.getElementById("bmp-fecha");
  const manFecha = document.getElementById("man-fecha");
  if (bmpFecha && !bmpFecha.value) bmpFecha.value = todayStr;
  if (manFecha && !manFecha.value) manFecha.value = todayStr;
}

/* =========================================================================
   1. NAVEGACIÓN DE ESTACIONES Y CONTROL DE PANELES
   ========================================================================= */
function initNavigation() {
  document.querySelectorAll("[data-open-panel]").forEach((el) => {
    el.addEventListener("click", () => {
      openPanel(el.getAttribute("data-open-panel"));
    });
  });

  document.querySelectorAll("[data-dock-panel]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const target = btn.getAttribute("data-dock-panel");
      if (target === "room") {
        openPanel("room");
      } else if (state.activePanel === target) {
        openPanel("room");
      } else {
        openPanel(target);
      }
    });
  });

  document.querySelectorAll("[data-close-panel]").forEach((btn) => {
    btn.addEventListener("click", () => openPanel("room"));
  });

  const btnRoomView = document.getElementById("btn-room-view");
  if (btnRoomView) {
    btnRoomView.addEventListener("click", () => openPanel("room"));
  }

  const workspace = document.getElementById("panel-workspace");
  if (workspace) {
    workspace.addEventListener("click", (e) => {
      if (e.target === workspace) openPanel("room");
    });
  }

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") {
      const modal = document.getElementById("correct-modal");
      if (modal && !modal.classList.contains("hidden")) {
        modal.classList.add("hidden");
        return;
      }
      if (state.activePanel !== "room") {
        openPanel("room");
      }
    }
  });
}

function openPanel(panelName) {
  state.activePanel = panelName;
  const workspace = document.getElementById("panel-workspace");
  const allPanels = document.querySelectorAll(".clinical-panel");

  if (panelName === "room") {
    workspace.classList.add("hidden");
    workspace.setAttribute("aria-hidden", "true");
    document.body.classList.remove("panel-open");
    allPanels.forEach((p) => {
      p.classList.remove("active");
      p.classList.add("hidden");
    });
  } else {
    workspace.classList.remove("hidden");
    workspace.setAttribute("aria-hidden", "false");
    document.body.classList.add("panel-open");
    allPanels.forEach((p) => {
      const isTarget = p.id === `panel-${panelName}`;
      p.classList.toggle("active", isTarget);
      p.classList.toggle("hidden", !isTarget);
    });
  }

  document.querySelectorAll("[data-dock-panel]").forEach((btn) => {
    btn.classList.toggle(
      "active",
      btn.getAttribute("data-dock-panel") === panelName
    );
  });

  // Regla estricta de audio: silenciar inmediatamente al salir de #panel-bmp
  enforceAudioPolicy();

  if (panelName === "cases") {
    loadCasesAndStats();
  } else if (panelName === "neural") {
    refreshSystemStatus();
    loadTrainingCurves();
  }
}

/* =========================================================================
   2. SISTEMA DE SONIDO SINTETIZADO (EXCLUSIVO DEL ESCÁNER #panel-bmp)
   ========================================================================= */
function canPlayPulseAudio() {
  return (
    state.audioEnabled &&
    state.activePanel === "bmp" &&
    state.hasBmpResult &&
    state.ecgMode !== "idle"
  );
}

function enforceAudioPolicy() {
  if (!state.audioCtx) {
    syncAudioButtonState();
    return;
  }

  if (canPlayPulseAudio()) {
    if (state.audioCtx.state === "suspended") {
      state.audioCtx.resume().catch(() => {});
    }
  } else if (state.audioCtx.state === "running") {
    state.audioCtx.suspend().catch(() => {});
  }

  syncAudioButtonState();
}

function syncAudioButtonState() {
  const btn = document.getElementById("btn-audio-toggle");
  const txt = document.getElementById("audio-toggle-text");
  if (!btn || !txt) return;

  btn.setAttribute("aria-pressed", state.audioEnabled ? "true" : "false");
  btn.classList.toggle("is-on", state.audioEnabled && canPlayPulseAudio());
  btn.classList.toggle(
    "is-standby",
    state.audioEnabled && !canPlayPulseAudio()
  );

  if (!state.audioEnabled) {
    txt.textContent = "AUDIO OFF";
  } else if (state.hasBmpResult) {
    txt.textContent = "AUDIO ON";
  } else {
    txt.textContent = "AUDIO EN ESPERA";
  }
}

function initAudioSystem() {
  const btn = document.getElementById("btn-audio-toggle");
  if (!btn) return;

  btn.addEventListener("click", () => {
    state.audioEnabled = !state.audioEnabled;

    if (state.audioEnabled && !state.audioCtx) {
      const AudioContextClass =
        window.AudioContext || window.webkitAudioContext;
      if (AudioContextClass) {
        state.audioCtx = new AudioContextClass();
      }
    }

    enforceAudioPolicy();

    if (state.audioEnabled) {
      if (canPlayPulseAudio()) {
        playBeep(880, 0.06, "sine", 0.04);
        showToast(
          "Audio de telemetría activado en el Escáner ECG.",
          "info"
        );
      } else {
        showToast(
          "Audio armado. Sonará al procesar un electrocardiograma en el Escáner.",
          "info"
        );
      }
    } else {
      showToast("Audio del monitor silenciado.", "info");
    }
  });

  syncAudioButtonState();
}

function playBeep(freq = 880, duration = 0.08, type = "sine", gainVal = 0.045) {
  // Candado estricto: jamás emitir sonido fuera de #panel-bmp o sin escaneo activo
  if (!canPlayPulseAudio() || !state.audioCtx) return;

  try {
    if (state.audioCtx.state === "suspended") {
      state.audioCtx.resume();
    }
    const osc = state.audioCtx.createOscillator();
    const gain = state.audioCtx.createGain();
    osc.type = type;
    osc.frequency.setValueAtTime(freq, state.audioCtx.currentTime);

    gain.gain.setValueAtTime(0.0001, state.audioCtx.currentTime);
    gain.gain.exponentialRampToValueAtTime(
      gainVal,
      state.audioCtx.currentTime + 0.012
    );
    gain.gain.exponentialRampToValueAtTime(
      0.0001,
      state.audioCtx.currentTime + duration
    );

    osc.connect(gain);
    gain.connect(state.audioCtx.destination);
    osc.start();
    osc.stop(state.audioCtx.currentTime + duration + 0.015);
  } catch (_) {
    // Ignorar restricciones de reproducción automática del navegador
  }
}

/* =========================================================================
   3. OSCILOSCOPIO ECG EN VIVO (INTEGRADO EN #panel-bmp)
   ========================================================================= */
function initSceneEcgLoop() {
  const canvas = document.getElementById("scene-ecg-canvas");
  if (!canvas) return;
  const ctx = canvas.getContext("2d");

  const W = canvas.width;
  const H = canvas.height;
  const buffer = new Float32Array(W);
  buffer.fill(H * 0.55);

  let writeX = 0;
  let phase = 0;
  let beatArmed = true;

  function sampleEcgWaveform(p, mode) {
    if (mode === "idle") {
      return 0;
    }
    let y = 0;
    // Onda P
    y += 0.12 * Math.exp(-Math.pow((p - 0.18) / 0.032, 2));
    // Depresión Q
    y -= 0.16 * Math.exp(-Math.pow((p - 0.35) / 0.01, 2));
    // Pico R
    const rPeak = mode === 1 ? 1.15 : mode === 2 ? 0.85 : 1.0;
    y += rPeak * Math.exp(-Math.pow((p - 0.38) / 0.012, 2));
    // Valle S
    y -= 0.28 * Math.exp(-Math.pow((p - 0.41) / 0.012, 2));

    // Elevación del segmento ST en Infarto Agudo (MI)
    if (mode === 2) {
      y += 0.34 * Math.exp(-Math.pow((p - 0.52) / 0.075, 2));
    }
    // Onda T invertida en antecedente de MI
    if (mode === 3) {
      y -= 0.22 * Math.exp(-Math.pow((p - 0.64) / 0.048, 2));
    } else {
      y += 0.24 * Math.exp(-Math.pow((p - 0.64) / 0.048, 2));
    }
    return y;
  }

  let lastTime = performance.now();

  function frame(now) {
    const dt = Math.min(0.05, (now - lastTime) / 1000);
    lastTime = now;

    const isIdle = state.ecgMode === "idle" || !state.hasBmpResult;
    const bpm = isIdle ? 60 : Math.max(40, Math.min(180, state.ecgBpm || 72));
    // Velocidad clínica calmada de barrido
    const cyclesPerSec = (bpm / 60) * 0.55;
    const pixelsPerSec = isIdle ? 52 : 76;
    const steps = Math.max(1, Math.round(pixelsPerSec * dt));

    for (let s = 0; s < steps; s++) {
      let irregularJitter = 1.0;
      if (!isIdle && state.ecgMode === 1) {
        irregularJitter = 0.78 + 0.45 * Math.sin(now * 0.003);
      }

      phase += (cyclesPerSec * irregularJitter) / pixelsPerSec;
      if (phase >= 1.0) {
        phase -= 1.0;
        beatArmed = true;
      }

      // Disparo del pulso sonoro exclusivamente en el pico R cuando #panel-bmp está activo
      if (!isIdle && beatArmed && phase >= 0.37 && phase <= 0.43) {
        beatArmed = false;
        if (state.ecgMode === 2) {
          playBeep(520, 0.09, "triangle", 0.04);
        } else if (state.ecgMode === 1) {
          playBeep(660, 0.065, "sine", 0.035);
        } else {
          playBeep(780, 0.055, "sine", 0.03);
        }
      }

      const normY = sampleEcgWaveform(phase, isIdle ? "idle" : state.ecgMode);
      const centerY = H * 0.56;
      const amp = H * 0.34;
      buffer[writeX] = centerY - normY * amp;
      writeX = (writeX + 1) % W;
    }

    // Pantalla negra mate de osciloscopio (#0A0B0A)
    ctx.fillStyle = "#0a0b0a";
    ctx.fillRect(0, 0, W, H);

    // Retícula milimétrica de osciloscopio
    ctx.lineWidth = 1;
    for (let gx = 0; gx < W; gx += 16) {
      ctx.strokeStyle =
        gx % 80 === 0
          ? "rgba(0, 230, 118, 0.13)"
          : "rgba(0, 230, 118, 0.05)";
      ctx.beginPath();
      ctx.moveTo(gx, 0);
      ctx.lineTo(gx, H);
      ctx.stroke();
    }
    for (let gy = 0; gy < H; gy += 16) {
      ctx.strokeStyle =
        gy % 80 === 0
          ? "rgba(0, 230, 118, 0.13)"
          : "rgba(0, 230, 118, 0.05)";
      ctx.beginPath();
      ctx.moveTo(0, gy);
      ctx.lineTo(W, gy);
      ctx.stroke();
    }

    // Trazo en Verde Fósforo Clínico (#00E676) reservado para el osciloscopio
    ctx.save();
    ctx.strokeStyle = isIdle ? "rgba(0, 230, 118, 0.42)" : "#00e676";
    ctx.lineWidth = isIdle ? 1.5 : 2.1;
    ctx.shadowColor = "rgba(0, 230, 118, 0.45)";
    ctx.shadowBlur = isIdle ? 0 : 6;
    ctx.beginPath();

    const gap = 22;
    let started = false;
    for (let x = 0; x < W; x++) {
      const distAhead = (x - writeX + W) % W;
      if (distAhead < gap) {
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

    // Cabezal de barrido fosforescente
    const headX = (writeX - 1 + W) % W;
    const headY = buffer[headX];
    ctx.fillStyle = "#b9f6ca";
    ctx.beginPath();
    ctx.arc(headX, headY, isIdle ? 2.2 : 3.0, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();

    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
}

function setScannerTelemetry(tipoDiagnostico, bpmValue, metaText) {
  state.hasBmpResult = true;
  state.ecgMode = tipoDiagnostico;

  if (bpmValue) {
    state.ecgBpm = bpmValue;
  } else {
    state.ecgBpm =
      tipoDiagnostico === 0
        ? 72
        : tipoDiagnostico === 1
        ? 114
        : tipoDiagnostico === 2
        ? 104
        : 80;
  }

  const monitor = document.getElementById("ecg-monitor");
  const bpmEl = document.getElementById("holo-bpm-value");
  const labelEl = document.getElementById("holo-state-label");
  const metaEl = document.getElementById("bmp-scan-meta");

  if (bpmEl) bpmEl.textContent = String(Math.round(state.ecgBpm));
  if (labelEl) {
    labelEl.textContent = (
      DIAG_LABELS[tipoDiagnostico] || "TRAZO ANALIZADO"
    ).toUpperCase();
  }
  if (metaEl && metaText) {
    metaEl.textContent = metaText;
  }

  if (monitor) {
    if (tipoDiagnostico === 2) {
      monitor.dataset.alert = "critical";
    } else if (tipoDiagnostico === 1 || tipoDiagnostico === 3) {
      monitor.dataset.alert = "warning";
    } else {
      monitor.dataset.alert = "normal";
    }
  }

  enforceAudioPolicy();
}

/* =========================================================================
   4. ESTADO GLOBAL DEL SISTEMA (GET /api/status)
   ========================================================================= */
async function refreshSystemStatus() {
  try {
    const res = await fetch("/api/status");
    const data = await res.json();
    if (!data.ok) return;
    state.status = data;

    const pillBmp = document.getElementById("pill-bmp");
    const pillMan = document.getElementById("pill-manual");
    const txtBmp = document.getElementById("status-bmp-text");
    const txtMan = document.getElementById("status-manual-text");
    const txtCases = document.getElementById("status-cases-count");

    if (pillBmp && txtBmp) {
      const r = data.red_bmp;
      pillBmp.classList.toggle("ready", !!r.entrenada);
      txtBmp.textContent = r.entrenada
        ? `LISTA · ${(r.precision_final * 100).toFixed(1)}%`
        : "SIN PESOS";
    }

    if (pillMan && txtMan) {
      const r = data.red_manual;
      pillMan.classList.toggle("ready", !!r.entrenada);
      txtMan.textContent = r.entrenada
        ? `LISTA · ${(r.precision_final * 100).toFixed(1)}%`
        : "SIN PESOS";
    }

    if (txtCases && data.estadisticas) {
      txtCases.textContent = String(data.estadisticas.total);
    }

    renderNeuralStatusCards(data);
  } catch (err) {
    console.error("Error consultando /api/status:", err);
  }
}

/* =========================================================================
   5. ESCÁNER DE IMÁGENES ECG (#panel-bmp) + CONVERSIÓN CLIENTE A BMP 24-BIT
   ========================================================================= */
function initBmpScanner() {
  const dropzone = document.getElementById("bmp-dropzone");
  const fileInput = document.getElementById("bmp-file-input");
  const fileNameLabel = document.getElementById("bmp-file-name");
  const form = document.getElementById("form-bmp");

  renderEmptyMatrixCanvas();

  if (!dropzone || !fileInput || !form) return;

  dropzone.addEventListener("click", () => fileInput.click());

  dropzone.addEventListener("dragover", (e) => {
    e.preventDefault();
    dropzone.classList.add("dragover");
  });

  dropzone.addEventListener("dragleave", () => {
    dropzone.classList.remove("dragover");
  });

  dropzone.addEventListener("drop", (e) => {
    e.preventDefault();
    dropzone.classList.remove("dragover");
    if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
      selectBmpFile(e.dataTransfer.files[0], fileNameLabel);
    }
  });

  fileInput.addEventListener("change", () => {
    if (fileInput.files && fileInput.files.length > 0) {
      selectBmpFile(fileInput.files[0], fileNameLabel);
    }
  });

  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    if (!state.selectedBmpFile) {
      showToast(
        "Seleccione primero un archivo ECG (.bmp, .png, .jpg) o use una muestra rápida.",
        "error"
      );
      return;
    }

    const btn = document.getElementById("btn-analyze-bmp");
    const origText = btn.textContent;
    btn.disabled = true;
    btn.textContent = "Digitalizando y clasificando matriz 32×32...";

    try {
      const bmpBuffer = await fileTo24BitBmpBuffer(state.selectedBmpFile);
      const params = new URLSearchParams({
        nombre:
          document.getElementById("bmp-nombre").value.trim() || "Paciente",
        cedula: document.getElementById("bmp-cedula").value.trim() || "S/C",
        medico:
          document.getElementById("bmp-medico").value.trim() ||
          "Dr. Guardia",
        fecha: document.getElementById("bmp-fecha").value.trim() || "Hoy",
      });

      const res = await fetch(`/api/analyze/bmp?${params.toString()}`, {
        method: "POST",
        headers: { "Content-Type": "application/octet-stream" },
        body: bmpBuffer,
      });

      const data = await res.json();
      if (!data.ok) {
        showToast(data.error || "Error al procesar la imagen ECG.", "error");
        return;
      }

      handleBmpAnalysisSuccess(data);
      showToast(
        "Electrocardiograma procesado por la Red Neuronal n1.",
        "success"
      );
    } catch (err) {
      console.error(err);
      showToast(
        "No se pudo convertir o analizar el archivo de imagen.",
        "error"
      );
    } finally {
      btn.disabled = false;
      btn.textContent = origText;
    }
  });
}

function selectBmpFile(file, labelEl) {
  state.selectedBmpFile = file;
  if (labelEl) {
    labelEl.textContent = `ARCHIVO LISTO: ${file.name} (${(
      file.size / 1024
    ).toFixed(1)} KB)`;
    labelEl.classList.remove("hidden");
  }

  const reader = new FileReader();
  reader.onload = (e) => {
    showOriginalPreview(e.target.result);
  };
  reader.readAsDataURL(file);
}

function showOriginalPreview(srcUrl) {
  const previewImg = document.getElementById("bmp-orig-preview");
  const emptyHint = document.getElementById("bmp-orig-empty");
  if (previewImg) {
    previewImg.src = srcUrl;
    previewImg.classList.remove("hidden");
  }
  if (emptyHint) {
    emptyHint.classList.add("hidden");
  }
}

async function loadSampleChips() {
  const container = document.getElementById("samples-list");
  if (!container) return;

  try {
    const res = await fetch("/api/samples");
    const data = await res.json();
    if (!data.ok || !data.samples || data.samples.length === 0) {
      container.innerHTML = `<span class="samples-empty">No se encontraron muestras en la raíz del proyecto.</span>`;
      return;
    }

    container.innerHTML = "";
    data.samples.forEach((sampleName) => {
      const chip = document.createElement("button");
      chip.type = "button";
      chip.className = "chip";
      chip.textContent = sampleName;
      chip.addEventListener("click", () => runSampleAnalysis(sampleName));
      container.appendChild(chip);
    });
  } catch (_) {
    container.innerHTML = `<span class="samples-empty">Error cargando muestras locales.</span>`;
  }
}

async function runSampleAnalysis(sampleName) {
  showOriginalPreview(`/samples/${encodeURIComponent(sampleName)}`);
  const fileNameLabel = document.getElementById("bmp-file-name");
  if (fileNameLabel) {
    fileNameLabel.textContent = `MUESTRA PRECARGADA: ${sampleName}`;
    fileNameLabel.classList.remove("hidden");
  }

  const payload = {
    sample: sampleName,
    nombre:
      document.getElementById("bmp-nombre").value.trim() ||
      `Paciente (${sampleName.replace(".bmp", "")})`,
    cedula:
      document.getElementById("bmp-cedula").value.trim() || "V-18900123",
    medico:
      document.getElementById("bmp-medico").value.trim() || "Dr. Guardia",
    fecha: document.getElementById("bmp-fecha").value.trim() || "Hoy",
  };

  try {
    const res = await fetch("/api/analyze/sample", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await res.json();
    if (!data.ok) {
      showToast(data.error || "Error al analizar muestra.", "error");
      return;
    }
    handleBmpAnalysisSuccess(data);
    showToast(`Muestra ${sampleName} evaluada con éxito.`, "success");
  } catch (_) {
    showToast("Error de red analizando muestra local.", "error");
  }
}

function handleBmpAnalysisSuccess(data) {
  state.lastBmpCaseIndex = data.case_index;
  render32x32Matrix(data.preview_32x32);

  const box = document.getElementById("bmp-diagnosis-box");
  if (box) {
    box.innerHTML = renderDiagnosisHtml(data.resultado, data.case_index);
    bindDiagnosisActions(box, data.case_index, data.resultado.tipo_diagnostico);
  }

  const metaText = `${data.ancho_orig || 0}×${data.alto_orig || 0} PX · CASO #${
    (data.case_index ?? 0) + 1
  }`;
  setScannerTelemetry(data.resultado.tipo_diagnostico, null, metaText);
  refreshSystemStatus();
}

function renderEmptyMatrixCanvas() {
  const canvas = document.getElementById("bmp-matrix-canvas");
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const W = canvas.width;
  const H = canvas.height;
  const cellW = W / 32;
  const cellH = H / 32;

  ctx.fillStyle = "#111210";
  ctx.fillRect(0, 0, W, H);

  ctx.strokeStyle = "rgba(244, 241, 234, 0.045)";
  ctx.lineWidth = 0.5;
  for (let i = 0; i <= 32; i += 4) {
    ctx.beginPath();
    ctx.moveTo(i * cellW, 0);
    ctx.lineTo(i * cellW, H);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(0, i * cellH);
    ctx.lineTo(W, i * cellH);
    ctx.stroke();
  }
}

function render32x32Matrix(arr) {
  const canvas = document.getElementById("bmp-matrix-canvas");
  if (!canvas || !Array.isArray(arr) || arr.length < 1024) return;
  const ctx = canvas.getContext("2d");
  const W = canvas.width;
  const H = canvas.height;
  const cellW = W / 32;
  const cellH = H / 32;

  ctx.fillStyle = "#111210";
  ctx.fillRect(0, 0, W, H);

  for (let y = 0; y < 32; y++) {
    for (let x = 0; x < 32; x++) {
      const v = arr[y * 32 + x] || 0;
      const norm = Math.min(1, Math.max(0, v / 255));

      if (norm > 0.05) {
        // Estética térmica/instrumental: hueso cálido sobre carbón con acento terracota en trazos densos
        const r = Math.round(34 + norm * 210);
        const g = Math.round(36 + Math.pow(norm, 1.15) * 195);
        const b = Math.round(32 + Math.pow(norm, 1.35) * 170);
        ctx.fillStyle = `rgb(${r}, ${g}, ${b})`;
      } else {
        ctx.fillStyle = "#161715";
      }

      ctx.fillRect(x * cellW, y * cellH, cellW + 0.35, cellH + 0.35);
      ctx.strokeStyle = "rgba(244, 241, 234, 0.04)";
      ctx.lineWidth = 0.45;
      ctx.strokeRect(x * cellW, y * cellH, cellW, cellH);
    }
  }
}

/**
 * Convierte cualquier archivo de imagen (.bmp, .png, .jpg, .webp) en el navegador
 * a un ArrayBuffer con formato BMP de 24 bits sin compresión (BI_RGB) para que el
 * parser nativo en C11 (`cargar_bmp`) pueda leerlo directamente.
 */
async function fileTo24BitBmpBuffer(file) {
  const name = (file.name || "").toLowerCase();
  if (name.endsWith(".bmp")) {
    const raw = await file.arrayBuffer();
    if (raw.byteLength > 54) {
      const view = new DataView(raw);
      const sig = view.getUint16(0, true);
      const bpp = view.getUint16(28, true);
      const comp = view.getUint32(30, true);
      if (sig === 0x4d42 && (bpp === 24 || bpp === 32) && comp === 0) {
        return raw;
      }
    }
  }

  const bitmap = await createImageBitmap(file);
  const maxDim = 2500;
  let w = bitmap.width;
  let h = bitmap.height;
  if (w > maxDim || h > maxDim) {
    const scale = maxDim / Math.max(w, h);
    w = Math.max(32, Math.round(w * scale));
    h = Math.max(32, Math.round(h * scale));
  }

  const offCanvas = document.createElement("canvas");
  offCanvas.width = w;
  offCanvas.height = h;
  const ctx = offCanvas.getContext("2d");

  ctx.fillStyle = "#FFFFFF";
  ctx.fillRect(0, 0, w, h);
  ctx.drawImage(bitmap, 0, 0, w, h);

  const imgData = ctx.getImageData(0, 0, w, h).data;
  const rowStride = Math.floor((24 * w + 31) / 32) * 4;
  const pixelArraySize = rowStride * h;
  const fileSize = 54 + pixelArraySize;

  const buffer = new ArrayBuffer(fileSize);
  const view = new DataView(buffer);
  const bytes = new Uint8Array(buffer);

  // Cabecera BITMAPFILEHEADER (14 bytes)
  view.setUint8(0, 0x42); // 'B'
  view.setUint8(1, 0x4d); // 'M'
  view.setUint32(2, fileSize, true);
  view.setUint32(6, 0, true);
  view.setUint32(10, 54, true);

  // Cabecera BITMAPINFOHEADER (40 bytes)
  view.setUint32(14, 40, true);
  view.setInt32(18, w, true);
  view.setInt32(22, h, true); // positivo = bottom-up
  view.setUint16(26, 1, true);
  view.setUint16(28, 24, true); // 24 bits por píxel
  view.setUint32(30, 0, true); // BI_RGB
  view.setUint32(34, pixelArraySize, true);
  view.setInt32(38, 2835, true);
  view.setInt32(42, 2835, true);
  view.setUint32(46, 0, true);
  view.setUint32(50, 0, true);

  for (let y = 0; y < h; y++) {
    const bmpRow = h - 1 - y;
    const rowOffset = 54 + bmpRow * rowStride;
    for (let x = 0; x < w; x++) {
      const srcIdx = (y * w + x) * 4;
      const dstIdx = rowOffset + x * 3;
      bytes[dstIdx + 0] = imgData[srcIdx + 2]; // Blue
      bytes[dstIdx + 1] = imgData[srcIdx + 1]; // Green
      bytes[dstIdx + 2] = imgData[srcIdx + 0]; // Red
    }
  }

  return buffer;
}

/* =========================================================================
   6. EVALUADOR DE PARÁMETROS CLÍNICOS (#panel-manual)
   ========================================================================= */
function initManualEvaluator() {
  const sliders = [
    { id: "man-rr", valId: "val-rr", unit: "ms" },
    { id: "man-pr", valId: "val-pr", unit: "ms" },
    { id: "man-qrs", valId: "val-qrs", unit: "ms" },
    { id: "man-qt", valId: "val-qt", unit: "ms" },
    { id: "man-bpm", valId: "val-bpm", unit: "lpm" },
  ];

  sliders.forEach(({ id, valId, unit }) => {
    const input = document.getElementById(id);
    const out = document.getElementById(valId);
    if (input && out) {
      updateSliderVisual(input, out, unit);
      input.addEventListener("input", () => {
        updateSliderVisual(input, out, unit);
      });
    }
  });

  const presetButtons = document.querySelectorAll("[data-preset]");
  presetButtons.forEach((btn) => {
    btn.addEventListener("click", () => {
      const p = CLINICAL_PRESETS[btn.getAttribute("data-preset")];
      if (!p) return;
      presetButtons.forEach((b) => b.classList.toggle("active", b === btn));
      setSliderValue("man-rr", "val-rr", p.rr, "ms");
      setSliderValue("man-pr", "val-pr", p.pr, "ms");
      setSliderValue("man-qrs", "val-qrs", p.qrs, "ms");
      setSliderValue("man-qt", "val-qt", p.qt, "ms");
      setSliderValue("man-bpm", "val-bpm", p.bpm, "lpm");
      showToast(
        "Perfil de intervalos precargado en los controles.",
        "info"
      );
    });
  });

  const form = document.getElementById("form-manual");
  if (!form) return;

  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    const bpmVal = parseFloat(document.getElementById("man-bpm").value);
    const payload = {
      nombre:
        document.getElementById("man-nombre").value.trim() || "Paciente",
      cedula: document.getElementById("man-cedula").value.trim() || "S/C",
      medico:
        document.getElementById("man-medico").value.trim() || "Dr. Guardia",
      fecha: document.getElementById("man-fecha").value.trim() || "Hoy",
      rr: parseFloat(document.getElementById("man-rr").value),
      pr: parseFloat(document.getElementById("man-pr").value),
      qrs: parseFloat(document.getElementById("man-qrs").value),
      qt: parseFloat(document.getElementById("man-qt").value),
      bpm: bpmVal,
    };

    try {
      const res = await fetch("/api/analyze/manual", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });
      const data = await res.json();
      if (!data.ok) {
        showToast(
          data.error || "Error en evaluación de intervalos.",
          "error"
        );
        return;
      }

      state.lastManualCaseIndex = data.case_index;
      renderNormalizedChips(data.normalizados);

      const box = document.getElementById("manual-diagnosis-box");
      if (box) {
        box.innerHTML = renderDiagnosisHtml(data.resultado, data.case_index);
        bindDiagnosisActions(
          box,
          data.case_index,
          data.resultado.tipo_diagnostico
        );
      }

      // Nota: no activamos el monitor de audio del Escáner aquí; el sonido es exclusivo de #panel-bmp
      refreshSystemStatus();
      showToast(
        "Intervalos evaluados por la Red Neuronal Clínica n2.",
        "success"
      );
    } catch (_) {
      showToast("Error de conexión con el servidor C11.", "error");
    }
  });
}

function updateSliderVisual(input, out) {
  out.textContent = String(input.value);
  const min = parseFloat(input.min) || 0;
  const max = parseFloat(input.max) || 100;
  const val = parseFloat(input.value) || 0;
  const pct = Math.max(0, Math.min(100, ((val - min) / (max - min)) * 100));
  input.style.setProperty("--pct", `${pct.toFixed(1)}%`);
}

function setSliderValue(inputId, valId, val, unit) {
  const input = document.getElementById(inputId);
  const out = document.getElementById(valId);
  if (input && out) {
    input.value = val;
    updateSliderVisual(input, out, unit);
  }
}

function renderNormalizedChips(norms) {
  const container = document.getElementById("manual-norm-chips");
  if (!container || !Array.isArray(norms)) return;
  const labels = ["RR_norm", "PR_norm", "QRS_norm", "QT_norm", "BPM_norm"];
  container.innerHTML = norms
    .map(
      (v, i) =>
        `<span class="norm-chip"><b>${labels[i] || `X${i}`}</b> ${Number(
          v
        ).toFixed(3)}</span>`
    )
    .join("");
}

/* =========================================================================
   7. TARJETA COMPARTIDA DE DIAGNÓSTICO Y VERIFICACIÓN MÉDICA
   ========================================================================= */
function renderDiagnosisHtml(res, caseIndex) {
  const probs = res.probabilidades || [0, 0, 0, 0];

  const barsHtml = DIAG_LABELS.map((nombre, idx) => {
    const pct = Math.max(0, Math.min(100, (probs[idx] || 0) * 100));
    const isTop = idx === res.tipo_diagnostico;
    return `
      <div class="bar-row${isTop ? " is-top" : ""}">
        <span>${escapeHtml(nombre)}</span>
        <div class="bar-track">
          <div class="bar-fill" style="width:${pct.toFixed(1)}%"></div>
        </div>
        <strong>${pct.toFixed(1)}%</strong>
      </div>
    `;
  }).join("");

  return `
    <div class="dx-card" data-urg="${res.nivel_urgencia}">
      <div class="dx-head">
        <div class="dx-code">CL-0${res.tipo_diagnostico}</div>
        <div>
          <span class="dx-kicker">DIAGNÓSTICO AUTOMÁTICO · CASO #${
            (caseIndex ?? 0) + 1
          }</span>
          <h4 class="dx-title">${escapeHtml(res.descripcion)}</h4>
        </div>
        <div class="dx-metrics">
          <div class="dx-metric">
            <span class="lbl">CONFIANZA</span>
            <span class="val">${(res.confianza * 100).toFixed(1)}%</span>
          </div>
          <div class="dx-metric">
            <span class="lbl">URGENCIA</span>
            <span class="val urg">${escapeHtml(res.urgencia_texto)}</span>
          </div>
        </div>
      </div>

      <p class="dx-rec">${escapeHtml(res.recomendacion)}</p>

      <div class="prob-list">
        ${barsHtml}
      </div>

      <div class="verify-bar" data-case-index="${caseIndex}">
        <button type="button" class="btn btn-primary btn-sm" data-verif="confirm">
          Confirmar diagnóstico
        </button>
        <button type="button" class="btn btn-secondary btn-sm" data-verif="correct">
          Corregir clase
        </button>
        <button type="button" class="btn btn-outline-danger btn-sm" data-verif="discard">
          Descartar (ruido)
        </button>
        <span class="verify-done hidden"></span>
      </div>
    </div>
  `;
}

function bindDiagnosisActions(container, caseIndex, currentTipo) {
  const bar = container.querySelector(".verify-bar");
  if (!bar) return;

  bar.querySelectorAll("[data-verif]").forEach((btn) => {
    btn.addEventListener("click", async () => {
      const action = btn.getAttribute("data-verif");
      if (action === "confirm") {
        await submitCaseVerification(
          caseIndex,
          1,
          0,
          0,
          currentTipo,
          bar,
          "DIAGNÓSTICO CONFIRMADO POR ESPECIALISTA"
        );
      } else if (action === "discard") {
        await submitCaseVerification(
          caseIndex,
          0,
          1,
          0,
          currentTipo,
          bar,
          "CASO DESCARTADO (ARTEFACTO / RUIDO)"
        );
      } else if (action === "correct") {
        openCorrectionModal(caseIndex, currentTipo, bar);
      }
    });
  });
}

async function submitCaseVerification(
  index,
  verificado,
  descartado,
  corregido,
  tipoCorrecto,
  barEl,
  statusText
) {
  try {
    const res = await fetch("/api/cases/verify", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        index,
        verificado,
        descartado,
        corregido,
        tipo_correcto: tipoCorrecto,
      }),
    });
    const data = await res.json();
    if (!data.ok) {
      showToast(data.error || "No se pudo actualizar el caso.", "error");
      return;
    }

    if (barEl) {
      barEl
        .querySelectorAll("button")
        .forEach((b) => b.classList.add("hidden"));
      const msg = barEl.querySelector(".verify-done");
      if (msg) {
        msg.textContent = statusText;
        msg.classList.remove("hidden");
      }
    }

    showToast(statusText, "success");
    refreshSystemStatus();
    if (state.activePanel === "cases") {
      loadCasesAndStats();
    }
  } catch (_) {
    showToast("Error al guardar verificación médica.", "error");
  }
}

/* =========================================================================
   8. HISTORIAL DE CASOS, ELIMINACIÓN Y REPORTES (#panel-cases)
   ========================================================================= */
function initCasesPanel() {
  document.querySelectorAll("[data-filter]").forEach((btn) => {
    btn.addEventListener("click", () => {
      document
        .querySelectorAll("[data-filter]")
        .forEach((b) => b.classList.remove("active"));
      btn.classList.add("active");
      state.caseFilter = btn.getAttribute("data-filter");
      renderCasesTable();
    });
  });

  const btnClearDiscarded = document.getElementById("btn-clear-discarded");
  if (btnClearDiscarded) {
    btnClearDiscarded.addEventListener("click", async () => {
      try {
        const res = await fetch("/api/cases/clear-discarded", {
          method: "POST",
        });
        const data = await res.json();
        if (!data.ok) {
          showToast(
            data.error || "No se pudieron limpiar los casos descartados.",
            "error"
          );
          return;
        }
        if (data.eliminados > 0) {
          showToast(
            `Se eliminaron ${data.eliminados} caso(s) descartado(s).`,
            "success"
          );
        } else {
          showToast("No hay casos marcados como descartados.", "info");
        }
        refreshSystemStatus();
        loadCasesAndStats();
      } catch (_) {
        showToast("Error limpiando casos descartados.", "error");
      }
    });
  }

  const btnSearch = document.getElementById("btn-search-report");
  const inputCed = document.getElementById("report-search-cedula");
  if (btnSearch && inputCed) {
    btnSearch.addEventListener("click", () => {
      const ced = inputCed.value.trim();
      if (!ced) {
        showToast("Ingrese una cédula para consultar el expediente.", "error");
        return;
      }
      fetchPatientReport(ced);
    });
    inputCed.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        btnSearch.click();
      }
    });
  }

  const btnPrint = document.getElementById("btn-print-report");
  if (btnPrint) {
    btnPrint.addEventListener("click", () => {
      if (!state.lastReportText) {
        showToast("Busque o seleccione primero un reporte clínico.", "info");
        return;
      }
      const win = window.open("", "_blank", "width=820,height=680");
      if (win) {
        win.document.write(
          `<pre style="font-family:'IBM Plex Mono',Consolas,monospace;font-size:13px;padding:28px;line-height:1.55;color:#111210;background:#f4f1ea;">${escapeHtml(
            state.lastReportText
          )}</pre>`
        );
        win.document.close();
        win.focus();
        win.print();
      }
    });
  }

  // Modal de corrección diagnóstica
  const btnCancelCorrect = document.getElementById("correct-cancel");
  const btnSaveCorrect = document.getElementById("correct-confirm");
  const modal = document.getElementById("correct-modal");

  if (btnCancelCorrect && modal) {
    btnCancelCorrect.addEventListener("click", () =>
      modal.classList.add("hidden")
    );
  }
  if (btnSaveCorrect && modal) {
    btnSaveCorrect.addEventListener("click", async () => {
      const sel = parseInt(
        document.getElementById("correct-select").value,
        10
      );
      if (state.correctModalCaseIndex !== null) {
        await submitCaseVerification(
          state.correctModalCaseIndex,
          1,
          0,
          1,
          sel,
          state.correctModalBarEl,
          `CORREGIDO A: ${DIAG_LABELS[sel].toUpperCase()}`
        );
      }
      modal.classList.add("hidden");
    });
  }
}

function openCorrectionModal(caseIndex, currentTipo, barEl = null) {
  state.correctModalCaseIndex = caseIndex;
  state.correctModalBarEl = barEl;
  const select = document.getElementById("correct-select");
  if (select) select.value = String(currentTipo || 0);
  const sub = document.getElementById("correct-modal-sub");
  if (sub) {
    sub.textContent = `Caso #${caseIndex + 1} · Clase actual: ${
      DIAG_LABELS[currentTipo] || "N/D"
    }`;
  }
  const modal = document.getElementById("correct-modal");
  if (modal) modal.classList.remove("hidden");
}

async function loadCasesAndStats() {
  try {
    const [casesRes, statsRes] = await Promise.all([
      fetch("/api/cases"),
      fetch("/api/stats"),
    ]);
    const casesData = await casesRes.json();
    const statsData = await statsRes.json();

    if (casesData.ok) {
      state.cases = casesData.cases || [];
      renderCasesTable();
    }
    if (statsData.ok) {
      renderStatsWidgets(statsData);
    }
  } catch (err) {
    console.error("Error cargando casos/estadísticas:", err);
  }
}

function renderCasesTable() {
  const tbody = document.getElementById("cases-tbody");
  if (!tbody) return;

  const filtered = state.cases.filter((c) => {
    if (state.caseFilter === "pending")
      return !c.verificado && !c.descartado && !c.corregido;
    if (state.caseFilter === "verified") return !!c.verificado;
    if (state.caseFilter === "corrected") return !!c.corregido;
    if (state.caseFilter === "discarded") return !!c.descartado;
    return true;
  });

  if (filtered.length === 0) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="8">No hay casos registrados en este filtro.</td></tr>`;
    return;
  }

  tbody.innerHTML = filtered
    .map((c) => {
      let badge = `<span class="tag tag-pending">Pendiente</span>`;
      if (c.descartado) {
        badge = `<span class="tag tag-discarded">Descartado</span>`;
      } else if (c.corregido) {
        badge = `<span class="tag tag-corrected">Corregido</span>`;
      } else if (c.verificado) {
        badge = `<span class="tag tag-verified">Verificado</span>`;
      }

      const modeBadge = `<span class="mode-tag">${
        c.usa_bmp ? "BMP 32×32" : "CLÍNICO 5V"
      }</span>`;

      return `
        <tr>
          <td class="mono">#${c.index + 1}</td>
          <td><strong>${escapeHtml(c.nombre)}</strong></td>
          <td class="mono">${escapeHtml(c.cedula)}</td>
          <td>${modeBadge}</td>
          <td>${escapeHtml(c.diagnostico)}</td>
          <td class="mono">${(c.confianza * 100).toFixed(1)}%</td>
          <td>${badge}</td>
          <td>
            <div class="row-actions">
              <button type="button" class="icon-btn" data-row-act="confirm" data-idx="${
                c.index
              }" data-tipo="${
        c.tipo_diagnostico
      }" title="Confirmar diagnóstico">OK</button>
              <button type="button" class="icon-btn" data-row-act="correct" data-idx="${
                c.index
              }" data-tipo="${
        c.tipo_diagnostico
      }" title="Corregir clase">CORR</button>
              <button type="button" class="icon-btn" data-row-act="discard" data-idx="${
                c.index
              }" data-tipo="${
        c.tipo_diagnostico
      }" title="Marcar como descartado">DESC</button>
              <button type="button" class="icon-btn" data-row-act="report" data-ced="${escapeHtml(
                c.cedula
              )}" title="Ver reporte clínico">REP</button>
              <button type="button" class="icon-btn danger" data-row-act="delete" data-idx="${
                c.index
              }" data-nombre="${escapeHtml(
        c.nombre
      )}" title="Eliminar caso permanentemente">ELIM</button>
            </div>
          </td>
        </tr>
      `;
    })
    .join("");

  tbody.querySelectorAll("[data-row-act]").forEach((btn) => {
    btn.addEventListener("click", async () => {
      const act = btn.getAttribute("data-row-act");
      const idx = parseInt(btn.getAttribute("data-idx"), 10);
      const tipo = parseInt(btn.getAttribute("data-tipo") || "0", 10);

      if (act === "confirm") {
        await submitCaseVerification(
          idx,
          1,
          0,
          0,
          tipo,
          null,
          `Caso #${idx + 1} confirmado.`
        );
      } else if (act === "discard") {
        await submitCaseVerification(
          idx,
          0,
          1,
          0,
          tipo,
          null,
          `Caso #${idx + 1} descartado.`
        );
      } else if (act === "correct") {
        openCorrectionModal(idx, tipo, null);
      } else if (act === "report") {
        const ced = btn.getAttribute("data-ced");
        const inputCed = document.getElementById("report-search-cedula");
        if (inputCed) inputCed.value = ced;
        fetchPatientReport(ced);
      } else if (act === "delete") {
        const nombre = btn.getAttribute("data-nombre") || `#${idx + 1}`;
        await deleteCaseByIndex(idx, nombre);
      }
    });
  });
}

async function deleteCaseByIndex(index, nombre) {
  try {
    const res = await fetch("/api/cases/delete", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ index }),
    });
    const data = await res.json();
    if (!data.ok) {
      showToast(data.error || "No se pudo eliminar el caso.", "error");
      return;
    }
    showToast(`Caso de ${nombre} eliminado de la base de datos.`, "info");
    refreshSystemStatus();
    loadCasesAndStats();
  } catch (_) {
    showToast("Error eliminando el caso.", "error");
  }
}

function renderStatsWidgets(stats) {
  const kpiGrid = document.getElementById("cases-kpi-grid");
  if (kpiGrid) {
    kpiGrid.innerHTML = `
      <div class="counter">
        <span class="counter-label">Total Casos</span>
        <span class="counter-val">${stats.total}</span>
      </div>
      <div class="counter">
        <span class="counter-label">Verificados</span>
        <span class="counter-val">${stats.verificados}</span>
      </div>
      <div class="counter c-pending">
        <span class="counter-label">Pendientes</span>
        <span class="counter-val">${stats.pendientes}</span>
      </div>
      <div class="counter c-corrected">
        <span class="counter-label">Corregidos</span>
        <span class="counter-val">${stats.corregidos}</span>
      </div>
      <div class="counter c-discarded">
        <span class="counter-label">Descartados</span>
        <span class="counter-val">${stats.descartados}</span>
      </div>
    `;
  }

  const barsContainer = document.getElementById("stats-bars");
  if (barsContainer && Array.isArray(stats.por_tipo)) {
    const validTotal = Math.max(1, stats.total - stats.descartados);
    barsContainer.innerHTML = DIAG_LABELS.map((label, i) => {
      const count = stats.por_tipo[i] || 0;
      const pct = Math.min(100, (count / validTotal) * 100);
      return `
        <div class="bar-row">
          <span>${escapeHtml(label)}</span>
          <div class="bar-track">
            <div class="bar-fill" style="width:${pct.toFixed(1)}%"></div>
          </div>
          <strong>${count} (${pct.toFixed(0)}%)</strong>
        </div>
      `;
    }).join("");
  }
}

async function fetchPatientReport(cedula) {
  const viewer = document.getElementById("report-viewer");
  if (!viewer) return;
  viewer.textContent = "Consultando expediente clínico...";

  try {
    const res = await fetch(
      `/api/report?cedula=${encodeURIComponent(cedula)}`
    );
    const data = await res.json();
    if (!data.ok) {
      viewer.textContent =
        data.error || "No se encontró expediente para esa cédula.";
      state.lastReportText = "";
      return;
    }
    state.lastReportText = data.report;
    state.lastReportCedula = data.cedula;
    viewer.textContent = data.report;
  } catch (_) {
    viewer.textContent = "Error consultando el reporte del paciente.";
  }
}

/* =========================================================================
   9. ARQUITECTURA NEURONAL Y ENTRENAMIENTO (#panel-neural)
   ========================================================================= */
function initNeuralPanel() {
  document.querySelectorAll("[data-train-target]").forEach((btn) => {
    btn.addEventListener("click", async () => {
      const target = btn.getAttribute("data-train-target");
      const pinInput = document.getElementById("train-pin");
      const feedback = document.getElementById("train-feedback");
      const pin = pinInput ? pinInput.value.trim() : "";

      if (!pin) {
        showToast("Ingrese el PIN de administrador (admin123).", "error");
        return;
      }

      if (feedback) {
        feedback.textContent =
          "Ejecutando backpropagation en C11... Por favor espere.";
      }
      document
        .querySelectorAll("[data-train-target]")
        .forEach((b) => (b.disabled = true));

      try {
        const res = await fetch("/api/train", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ target, pin }),
        });
        const data = await res.json();
        if (!data.ok) {
          if (feedback) {
            feedback.textContent =
              data.error || "Error durante el entrenamiento.";
          }
          showToast(data.error || "PIN inválido o error.", "error");
          return;
        }

        if (feedback) {
          feedback.textContent = `ENTRENAMIENTO COMPLETADO · BMP=${
            data.bmp_ok ? "OK" : "--"
          } · CLÍNICA=${data.manual_ok ? "OK" : "--"}`;
        }
        showToast(
          "Redes neuronales reentrenadas y guardadas en disco.",
          "success"
        );
        await refreshSystemStatus();
        await loadTrainingCurves();
      } catch (_) {
        if (feedback) {
          feedback.textContent = "Error de red durante el entrenamiento.";
        }
        showToast("Fallo de comunicación con el servidor.", "error");
      } finally {
        document
          .querySelectorAll("[data-train-target]")
          .forEach((b) => (b.disabled = false));
      }
    });
  });
}

function renderNeuralStatusCards(data) {
  const elBmp = document.getElementById("info-net-bmp");
  const elMan = document.getElementById("info-net-manual");

  if (elBmp && data.red_bmp) {
    const r = data.red_bmp;
    elBmp.innerHTML = `
      <dl class="spec-list">
        <div class="spec-row"><dt>Topología</dt><dd>${escapeHtml(
          r.topologia
        )}</dd></div>
        <div class="spec-row"><dt>Estado de pesos</dt><dd>${
          r.entrenada ? "CARGADOS (red_bmp.dat)" : "NO ENTRENADA"
        }</dd></div>
        <div class="spec-row"><dt>Épocas ejecutadas</dt><dd>${
          r.epocas
        }</dd></div>
        <div class="spec-row"><dt>Tasa de aprendizaje (LR)</dt><dd>${Number(
          r.tasa
        ).toFixed(4)}</dd></div>
        <div class="spec-row"><dt>Pérdida (Inicial → Final)</dt><dd>${Number(
          r.error_inicial
        ).toFixed(4)} → ${Number(r.error_final).toFixed(4)}</dd></div>
        <div class="spec-row"><dt>Precisión de validación</dt><dd>${(
          r.precision_final * 100
        ).toFixed(1)}%</dd></div>
      </dl>
    `;
  }

  if (elMan && data.red_manual) {
    const r = data.red_manual;
    elMan.innerHTML = `
      <dl class="spec-list">
        <div class="spec-row"><dt>Topología</dt><dd>${escapeHtml(
          r.topologia
        )}</dd></div>
        <div class="spec-row"><dt>Estado de pesos</dt><dd>${
          r.entrenada ? "CARGADOS (red_manual.dat)" : "NO ENTRENADA"
        }</dd></div>
        <div class="spec-row"><dt>Épocas ejecutadas</dt><dd>${
          r.epocas
        }</dd></div>
        <div class="spec-row"><dt>Tasa de aprendizaje (LR)</dt><dd>${Number(
          r.tasa
        ).toFixed(4)}</dd></div>
        <div class="spec-row"><dt>Pérdida (Inicial → Final)</dt><dd>${Number(
          r.error_inicial
        ).toFixed(4)} → ${Number(r.error_final).toFixed(4)}</dd></div>
        <div class="spec-row"><dt>Precisión de validación</dt><dd>${(
          r.precision_final * 100
        ).toFixed(1)}%</dd></div>
      </dl>
    `;
  }
}

async function loadTrainingCurves() {
  try {
    const res = await fetch("/api/training-log");
    const data = await res.json();
    if (!data.ok) return;
    drawTrainingChart("chart-bmp", data.bmp || []);
    drawTrainingChart("chart-manual", data.manual || []);
  } catch (err) {
    console.error("Error cargando curvas de entrenamiento:", err);
  }
}

function drawTrainingChart(canvasId, points) {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const W = canvas.width;
  const H = canvas.height;

  // Papel técnico hueso cálido
  ctx.fillStyle = "#fbfaf6";
  ctx.fillRect(0, 0, W, H);

  const padL = 44;
  const padR = 20;
  const padT = 22;
  const padB = 30;
  const plotW = W - padL - padR;
  const plotH = H - padT - padB;

  // Retícula técnica milimétrica
  ctx.strokeStyle = "#e4ddd0";
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = padT + (plotH * i) / 4;
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(W - padR, y);
    ctx.stroke();
  }
  for (let i = 0; i <= 6; i++) {
    const x = padL + (plotW * i) / 6;
    ctx.beginPath();
    ctx.moveTo(x, padT);
    ctx.lineTo(x, H - padB);
    ctx.stroke();
  }

  if (!points || points.length === 0) {
    ctx.fillStyle = "#6e6d68";
    ctx.font = "12px 'IBM Plex Mono', monospace";
    ctx.fillText(
      "SIN BITÁCORA DE ENTRENAMIENTO DISPONIBLE",
      padL + 16,
      H / 2
    );
    return;
  }

  const maxLoss = Math.max(...points.map((p) => p.loss), 1.0);

  // Curva de Pérdida (Loss) en Rojo Electrocardiográfico (#D93829)
  ctx.strokeStyle = "#d93829";
  ctx.lineWidth = 2.2;
  ctx.beginPath();
  points.forEach((p, idx) => {
    const x = padL + (idx / Math.max(1, points.length - 1)) * plotW;
    const y = padT + plotH - (p.loss / maxLoss) * plotH;
    if (idx === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();

  // Curva de Precisión (Accuracy) en Tinta Carbón (#1A1B18)
  ctx.strokeStyle = "#1a1b18";
  ctx.lineWidth = 2.0;
  ctx.beginPath();
  points.forEach((p, idx) => {
    const x = padL + (idx / Math.max(1, points.length - 1)) * plotW;
    const y = padT + plotH - Math.min(1, Math.max(0, p.accuracy)) * plotH;
    if (idx === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();

  // Leyenda técnica
  ctx.font = "600 11px 'IBM Plex Mono', monospace";
  ctx.fillStyle = "#d93829";
  ctx.fillText("■ LOSS (CROSS-ENTROPY)", padL + 8, padT + 13);
  ctx.fillStyle = "#1a1b18";
  ctx.fillText("■ ACCURACY (0–100%)", padL + 185, padT + 13);

  const firstEp = points[0].epoch;
  const lastEp = points[points.length - 1].epoch;
  ctx.fillStyle = "#6e6d68";
  ctx.fillText(`EP ${firstEp}`, padL, H - 9);
  ctx.fillText(`EP ${lastEp}`, W - padR - 58, H - 9);
}

/* =========================================================================
   10. APAGADO SEGURO DEL SERVIDOR Y NOTIFICACIONES TOAST
   ========================================================================= */
function initShutdownButton() {
  const btn = document.getElementById("btn-shutdown");
  if (!btn) return;

  btn.addEventListener("click", async () => {
    try {
      await fetch("/api/shutdown", { method: "POST" });
    } catch (_) {
      // El servidor cierra el socket inmediatamente
    }

    // Detener audio inmediatamente al apagar
    state.audioEnabled = false;
    enforceAudioPolicy();

    const screen = document.getElementById("shutdown-screen");
    if (screen) {
      screen.classList.remove("hidden");
    }
  });
}

function showToast(message, type = "info") {
  const container = document.getElementById("toast-container");
  if (!container) return;

  const toast = document.createElement("div");
  toast.className = `toast ${type}`;
  toast.textContent = message;
  container.appendChild(toast);

  setTimeout(() => {
    toast.style.opacity = "0";
    toast.style.transition = "opacity 0.25s ease";
    setTimeout(() => toast.remove(), 260);
  }, 3600);
}

function escapeHtml(str) {
  if (str === null || str === undefined) return "";
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}
