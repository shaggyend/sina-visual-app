# SINA-VISUAL PRO — Estación Cardiológica con Redes Neuronales en C

Sistema híbrido de apoyo al diagnóstico electrocardiográfico (**ECG**) implementado desde cero en **C11**, con un servidor HTTP embebido sobre **WinSock2** y una interfaz visual de escritorio ambientada en una **sala médica UCI estilo videojuego 2D** con monitor ECG animado en tiempo real.

---

## Características Principales

### 1. Motor de Inteligencia Artificial en C Puro (sin frameworks externos)
Dos redes neuronales multicapa (**MLP**) independientes construidas desde cero:
- **Red de Visión ECG (BMP)** — Topología `1024 → 64 → 4`:
  - Parsea imágenes `.bmp` de 24/32 bits directamente desde archivo o memoria (`constructor_bmp.c`).
  - Convierte a escala de grises por luminancia (`0.299R + 0.587G + 0.114B`), redimensiona a `32×32` con muestreo centrado e invierte automáticamente fondos blancos de papel ECG.
- **Red de Variables Clínicas (Manual)** — Topología `5 → 12 → 4`:
  - Evalúa `RR (ms)`, `PR (ms)`, `QRS (ms)`, `QT (ms)` y `BPM` normalizados frente a rangos clínicos.
- **Entrenamiento & Persistencia**:
  - Activación **PReLU** (`α = 0.01`) en capas ocultas y **Sigmoide** en la capa de salida.
  - Inicialización **He** (`sqrt(2 / n_in)`), entropía cruzada binaria y SGD con barajado **Fisher-Yates** por época.
  - Guardado y carga validada de pesos entrenados (`.nn`) y logs independientes de pérdida y precisión por época (`training_log_bmp.csv`, `training_log_manual.csv`).

### 2. Interfaz Visual de Sala Médica (App + Modo CLI)
- **Escenario UCI 2D interactivo** (`web/`):
  - Monitor holográfico con onda **P-QRS-T** dibujada en vivo en `<canvas>`, adaptable al diagnóstico y BPM detectado (alerta visual normal, advertencia o crítica).
  - Estaciones interactivas en la sala: **Escáner BMP**, **Signos Vitales**, **Archivo Clínico & Reportes** y **Núcleo IA (Admin)**.
  - Conversión automática en cliente de `.png`/`.jpg` a `.bmp` 24-bit sin compresión y visualización en vivo de la matriz neuronal `32×32`.
  - Flujo médico completo: confirmar diagnóstico, corregir etiqueta clínica o descartar lectura por ruido, con generación e impresión de reportes por cédula.
- **Modo Consola Clásico (`sina_visual_cli.exe`)**:
  - Versión terminal ANSI completamente depurada con la misma lógica central compartida.

---

## Clasificación Clínica

El sistema clasifica en 4 categorías y asigna nivel de urgencia y recomendación médica:

| ID | Diagnóstico | Nivel de Urgencia |
|----|-------------|-------------------|
| `0` | Ritmo Sinusal Normal | `NINGUNA` |
| `1` | Arritmia Detectada | `MEDIA` / `ALTA` (si confianza > 85%) |
| `2` | Infarto de Miocardio (MI) | `ALTA` |
| `3` | Historial Post-MI | `MEDIA` |

---

## Estructura del Proyecto

```text
SINA-VISUAL-APP/
├── compilar.bat              # Script de compilacion (App Visual + CLI)
├── data/
│   ├── config.txt            # Topologias, hiperparametros, puerto y PIN admin
│   ├── dataset_bmp.csv       # Dataset de imagenes ECG 32x32 (397 muestras)
│   ├── dataset_manual.csv    # Dataset de variables clinicas ECG
│   ├── pesos_bmp.nn          # Pesos persistidos de la Red BMP
│   └── pesos_manual.nn       # Pesos persistidos de la Red Manual
├── samples/                  # Electrocardiogramas BMP de prueba listos para usar
├── src/
│   ├── neural_network.c/.h   # MLP, PReLU, Backprop, Fisher-Yates, pesos .nn
│   ├── constructor_bmp.c/.h  # Parser BMP (archivo/memoria), grises, resize
│   ├── dataset.c/.h          # Lector CSV robusto (BOM UTF-8) y normalizador
│   ├── classifier.c/.h       # Inferencia multi-clase y calculo de urgencia
│   ├── casos.c/.h            # Persistencia CSV (11 cols) y reportes .txt
│   ├── config.c/.h           # Configuracion externa y resolucion de rutas
│   ├── main_app.c            # Servidor HTTP WinSock2 + API REST + lanzador App
│   ├── main_cli.c            # Aplicacion de consola interactiva
│   └── interface.c/.h        # Utilidades de consola ANSI y validacion
├── web/                      # Frontend HTML5/CSS3/JS + escena UCI 2D
└── reports/                  # Logs de entrenamiento y reportes clinicos
```

---

## Compilación y Ejecución (Windows / MinGW GCC)

### Requisitos
- **GCC (MinGW / MSYS2)** en el `PATH`
- **Microsoft Edge** (o navegador moderno) para el modo ventana de escritorio

### Compilar
Ejecuta `compilar.bat` o compila desde terminal:

```bat
compilar.bat
```

Esto genera dos ejecutables en `output/`:
- `output\SINA-VISUAL.exe` — Aplicación visual completa (inicia servidor local en C y abre ventana tipo app).
- `output\sina_visual_cli.exe` — Versión de consola clásica.

### Ejecutar
```bat
.\output\SINA-VISUAL.exe
```
*(Opcional: `.\output\SINA-VISUAL.exe --no-browser` para iniciar únicamente el servidor local en `http://127.0.0.1:8080`.)*

La contraseña de administrador por defecto para re-entrenar modelos se configura en `data/config.txt` (`ADMIN_PIN=admin123`).

---

## Endpoints REST (Servidor C Embebido)

- `GET /api/status` — Estado, topologías y métricas de ambas redes neuronales.
- `GET /api/samples` — Lista muestras `.bmp` disponibles en `samples/`.
- `POST /api/analyze/bmp` — Recibe un binario BMP en memoria, lo procesa y clasifica.
- `POST /api/analyze/sample` — Evalúa una muestra de `samples/`.
- `POST /api/analyze/manual` — Evalúa las 5 variables electrocardiográficas.
- `GET /api/cases` / `POST /api/cases/verify` — Consulta, confirma, corrige o descarta casos.
- `GET /api/stats` / `GET /api/report?cedula=...` — Estadísticas y reporte clínico.
- `POST /api/train` / `GET /api/training-log` — Re-entrenamiento y series históricas.
