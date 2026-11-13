// ============================================================
//  Simulador Epidemias — Tarija en PIXEL ART (2D)
//  Mapa dibujado celda por celda desde js/tarija_pixelmap.js
//  (generado por scripts/gen_pixel_map.py: 11 municipios reales).
//  Conexión con motor C++ ./epidemic_sim --mode net via /api/game/tarija
// ============================================================

// Municipios (id = índice de nodo del motor C++). La posición de la ciudad
// sale de la grilla (PIX_SEEDS) para que siempre caiga dentro de su municipio.
const MUNI_META = [
  { id:0,  name:"Tarija (Cercado)",    shortName:"Tarija", pop:238942, prov:"Cercado" },
  { id:1,  name:"Yacuiba",             shortName:"Yacuiba",        pop:97577,  prov:"Gran Chaco" },
  { id:2,  name:"Villamontes",         shortName:"Villamontes",    pop:46010,  prov:"Gran Chaco" },
  { id:3,  name:"Bermejo",             shortName:"Bermejo",        pop:36967,  prov:"Aniceto Arce" },
  { id:4,  name:"Villa San Lorenzo",   shortName:"S.Lorenzo",    pop:30352,  prov:"Eustaquio Méndez" },
  { id:5,  name:"Entre Ríos",          shortName:"Entre Ríos",     pop:20846,  prov:"Burdet O'Connor" },
  { id:6,  name:"Padcaya",             shortName:"Padcaya",        pop:17783,  prov:"Aniceto Arce" },
  { id:7,  name:"Uriondo",             shortName:"Uriondo",        pop:15954,  prov:"José María Avilés" },
  { id:8,  name:"Caraparí",            shortName:"Caraparí",       pop:15650,  prov:"Gran Chaco" },
  { id:9,  name:"El Puente",           shortName:"El Puente",      pop:9504,   prov:"Eustaquio Méndez" },
  { id:10, name:"Yunchará",            shortName:"Yunchará",       pop:4763,   prov:"José María Avilés" }
];
const MUNIS = MUNI_META.map(m => ({ ...m, cx: PIX_SEEDS[m.id][0], cy: PIX_SEEDS[m.id][1] }));

// Cantidad de "personitas" (pixeles) por municipio según población
function getDotCount(pop) {
  if (pop > 120000) return 80;
  if (pop > 60000)  return 55;
  if (pop > 40000)  return 40;
  if (pop > 32000)  return 32;
  if (pop > 22000)  return 26;
  if (pop > 18000)  return 22;
  if (pop > 16000)  return 18;
  if (pop > 10000)  return 16;
  if (pop > 5000)   return 14;
  return 10;
}

// Colores base por provincia (mismo orden que PIX_PROVINCES)
const PROV_COLORS = [
  '#a66fc9', // Gran Chaco        — púrpura
  '#dba3b4', // Aniceto Arce      — rosado
  '#2eb4dc', // Eustaquio Méndez  — celeste
  '#62cf6e', // Burdet O'Connor   — verde
  '#b39468', // José María Avilés — café/tierra
  '#e3c21a'  // Cercado           — amarillo
];

// PRNG determinista
function mulberry32(seed) {
  return function() {
    seed |= 0; seed = seed + 0x6D2B79F5 | 0;
    let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}


let rawFrames = null;
let frames = null;
let currentFrame = null;
let selectedEpi = null;
let isPlaying = false;
let playInterval = null;

// Edades (4 grupos: 0-19, 20-39, 40-59, 60+)
let ageShares = [35, 32, 21, 12]; // suma = 100%
let clusterInfo = null;   // metadatos del clúster devueltos por la API
let muniOwner = null;     // id de municipio -> rank del worker que lo simula
let ownerLayer = null, ownerKey = '';

// Tasas de mortalidad por defecto (en caso de que la epidemia no traiga json)
const DEFAULT_MORTALITY = {
  "0_19": 0.0005,
  "20_39": 0.003,
  "40_59": 0.02,
  "60_mas": 0.08
};


// Colores
const C_S = '#22c55e'; // verde
const C_I = '#ef4444'; // rojo
const C_R = '#3b82f6'; // azul (recuperados)
const C_D = '#09090b'; // negro (fallecidos)

function dotColor(idx, n, s, i, r, d) {
  const tot = s + i + r + d || 1;
  const nI = Math.round(i / tot * n);
  const nD = Math.round(d / tot * n);
  const nR = Math.round(r / tot * n);

  if (idx < nI)           return C_I;
  if (idx < nI + nD)      return C_D;
  if (idx < nI + nD + nR) return C_R;
  return C_S;
}

// Obtener probabilidad de muerte ponderada de la epidemia seleccionada según la edad
function getEpidemicMortality() {
  const mTable = selectedEpi?.mortalidad_por_edad || DEFAULT_MORTALITY;
  const rates = [
    mTable["0_19"] ?? 0.0005,
    mTable["20_39"] ?? 0.003,
    mTable["40_59"] ?? 0.02,
    mTable["60_mas"] ?? 0.08
  ];

  let wSum = 0;
  for (let k = 0; k < 4; k++) {
    wSum += (ageShares[k] / 100.0) * rates[k];
  }
  return wSum;
}

// Procesa frames calculando fallecidos de acuerdo a la letalidad invisible de cada enfermedad
function processFramesWithDemographics(sourceFrames) {
  if (!sourceFrames) return null;
  const mortProb = getEpidemicMortality();

  return sourceFrames.map(fr => {
    const newNodes = fr.nodes.map(nd => {
      // nd: [S, I, R, V, newInf]
      const s = nd[0] + nd[3]; // V reincorporado a S
      const i = nd[1];
      const rawR = nd[2];
      const newInf = nd[4];

      // Fallecidos y recuperados según la letalidad por edad de la epidemia
      const dead = Math.round(rawR * mortProb);
      const recovered = Math.max(0, rawR - dead);

      return [s, i, recovered, newInf, dead]; // [S, I, R, newInf, Dead]
    });
    return { day: fr.day, nodes: newNodes, links: fr.links || [], workers: fr.workers, master: fr.master, cum: fr.cum };
  });
}


// ------------------------------------------------------------
//  Grilla pixelada
// ------------------------------------------------------------
const GRID = PIX_ROWS.map(r => Array.from(r, ch => ch === '.' ? -1 : parseInt(ch, 36)));
const BAYER4 = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];

function gridAt(x, y) {
  return (x < 0 || y < 0 || x >= PIX_W || y >= PIX_H) ? -1 : GRID[y][x];
}
function provOf(id) { return id < 0 ? -1 : PIX_MUNI_PROV[id]; }

function hexToRgb(h) {
  const n = parseInt(h.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}
function shade(hex, f) {
  const [r, g, b] = hexToRgb(hex);
  const c = v => Math.max(0, Math.min(255, Math.round(v)));
  return `rgb(${c(r * f)},${c(g * f)},${c(b * f)})`;
}

// Color de cada municipio: color de su provincia con un tono distinto por municipio
const MUNI_COLOR = {};
(function () {
  const seen = {};
  MUNIS.forEach(m => {
    const p = PIX_MUNI_PROV[m.id];
    const k = seen[p] = (seen[p] === undefined ? 0 : seen[p] + 1);
    MUNI_COLOR[m.id] = shade(PROV_COLORS[p], [1.0, 0.86, 1.12][k % 3]);
  });
})();

// Tipo de borde de cada celda: 0 = interior, 1 = límite municipal,
// 2 = límite provincial, 3 = contorno del departamento
const EDGE = GRID.map((row, y) => row.map((id, x) => {
  if (id < 0) return 0;
  let kind = 0;
  for (const [dx, dy] of [[1,0],[-1,0],[0,1],[0,-1]]) {
    const nid = gridAt(x + dx, y + dy);
    if (nid < 0) return 3;
    if (nid !== id) {
      if (provOf(nid) !== provOf(id)) { if (provOf(id) > provOf(nid)) kind = Math.max(kind, 2); }
      else if (id > nid) kind = Math.max(kind, 1);
    }
  }
  return kind;
}));

// Celdas de cada municipio
const MUNI_CELLS = {};
MUNIS.forEach(m => MUNI_CELLS[m.id] = []);
for (let y = 0; y < PIX_H; y++)
  for (let x = 0; x < PIX_W; x++)
    if (GRID[y][x] >= 0) MUNI_CELLS[GRID[y][x]].push([x, y]);

// ------------------------------------------------------------
//  Fuente bitmap 3x5 (texto 100% pixelado, sin suavizado)
// ------------------------------------------------------------
const FONT = {
  A:'010101111101101', B:'110101110101110', C:'011100100100011', D:'110101101101110',
  E:'111100110100111', F:'111100110100100', G:'011100101101011', H:'101101111101101',
  I:'111010010010111', J:'001001001101010', K:'101101110101101', L:'100100100100111',
  M:'101111111101101', N:'110101101101101', O:'010101101101010', P:'110101110100100',
  Q:'010101101110011', R:'110101110101101', S:'011100010001110', T:'111010010010010',
  U:'101101101101111', V:'101101101101010', W:'101101111111101', X:'101101010101101',
  Y:'101101010010010', Z:'111001010100111',
  '0':'111101101101111', '1':'010110010010111', '2':'110001010100111', '3':'110001010001110',
  '4':'101101111001001', '5':'111100110001110', '6':'011100111101111', '7':'111001010010010',
  '8':'111101111101111', '9':'111101111001110',
  '%':'101001010100101', '.':'000000000000010', '/':'001001010100100', ' ':'000000000000000'
};

function layoutText(text) {
  const out = [];
  for (const ch of text.toUpperCase().normalize('NFD')) {
    if (/[̀-ͯ]/.test(ch)) { if (out.length) out[out.length - 1].acc = true; continue; }
    out.push({ g: FONT[ch] || FONT[' '], acc: false });
  }
  return out;
}

function paintGlyphs(ctx, glyphs, ox, oy, S, color) {
  ctx.fillStyle = color;
  glyphs.forEach((gl, i) => {
    const bx = ox + i * 4 * S;
    for (let k = 0; k < 15; k++) {
      if (gl.g[k] === '1') ctx.fillRect(bx + (k % 3) * S, oy + Math.floor(k / 3) * S, S, S);
    }
    if (gl.acc) ctx.fillRect(bx + 2 * S, oy - S, S, S);
  });
}

// Etiqueta con contorno negro, pre-renderizada y cacheada
const labelCache = new Map();
function getLabel(text, color, S) {
  const key = text + '|' + color + '|' + S;
  let c = labelCache.get(key);
  if (c) return c;
  const glyphs = layoutText(text);
  const w = (glyphs.length * 4 - 1) * S + 2 * S;
  const h = 5 * S + 3 * S;
  c = document.createElement('canvas');
  c.width = w; c.height = h;
  const cx = c.getContext('2d');
  for (let dy = -1; dy <= 1; dy++)
    for (let dx = -1; dx <= 1; dx++)
      if (dx || dy) paintGlyphs(cx, glyphs, S + dx * S, 2 * S + dy * S, S, '#050a16');
  paintGlyphs(cx, glyphs, S, 2 * S, S, color);
  labelCache.set(key, c);
  return c;
}

// ------------------------------------------------------------
//  Puntos (personas): celdas interiores de cada municipio
// ------------------------------------------------------------
let DOTS = {}; // id -> [[cellX, cellY], ...]

function initDots() {
  const rng = mulberry32(12345);
  MUNIS.forEach(m => {
    const want = getDotCount(m.pop);
    const cand = MUNI_CELLS[m.id]
      .filter(([x, y]) => EDGE[y][x] === 0 && !(x === m.cx && y === m.cy))
      .map(([x, y]) => ({ x, y, k: Math.hypot(x - m.cx, y - m.cy) * (0.6 + rng() * 0.8) }))
      .sort((a, b) => a.k - b.k)
      .slice(0, want);
    DOTS[m.id] = cand.map(c => [c.x, c.y]);
  });
}

// ------------------------------------------------------------
//  Render
// ------------------------------------------------------------
let CELL = 6;            // px por celda (entero)
let baseLayer = null;    // mapa estático pre-dibujado

function buildBaseLayer() {
  const W = PIX_W * CELL, H = PIX_H * CELL;
  const c = document.createElement('canvas');
  c.width = W; c.height = H;
  const g = c.getContext('2d');

  // Fondo con trama de tablero
  g.fillStyle = '#0b1730';
  g.fillRect(0, 0, W, H);
  g.fillStyle = '#0e1d3b';
  for (let y = 0; y < PIX_H; y++)
    for (let x = (y & 1); x < PIX_W; x += 2) g.fillRect(x * CELL, y * CELL, CELL, CELL);

  // Sombra desplazada 1 celda
  g.fillStyle = '#050a16';
  for (let y = 0; y < PIX_H; y++)
    for (let x = 0; x < PIX_W; x++)
      if (GRID[y][x] >= 0) g.fillRect((x + 1) * CELL, (y + 1) * CELL, CELL, CELL);

  // Tierra
  for (let y = 0; y < PIX_H; y++) {
    for (let x = 0; x < PIX_W; x++) {
      const id = GRID[y][x];
      if (id < 0) continue;
      const e = EDGE[y][x];
      let col;
      if (e === 3)      col = '#1a1033';
      else if (e === 2) col = '#2b1d47';
      else if (e === 1) col = shade(PROV_COLORS[PIX_MUNI_PROV[id]], 0.48);
      else {
        col = MUNI_COLOR[id];
        // textura: algunas celdas ligeramente más oscuras / claras
        const h = (x * 73856093 ^ y * 19349663) >>> 0;
        if (h % 11 === 0) col = shade(PROV_COLORS[PIX_MUNI_PROV[id]], 0.80);
        else if (h % 17 === 0) col = shade(PROV_COLORS[PIX_MUNI_PROV[id]], 1.18);
      }
      g.fillStyle = col;
      g.fillRect(x * CELL, y * CELL, CELL, CELL);
    }
  }
  baseLayer = c;
}

// ------------------------------------------------------------
//  Saltos de contagio: línea punteada de pixeles desde el municipio infectado
//  hacia el municipio sano al que "saltó" la infección ese día. Dura LINK_DAYS días
//  (se desvanece) y un paquete viaja de origen a destino.
// ------------------------------------------------------------
const LINK_DAYS = 4;

function bresenham(x0, y0, x1, y1) {
  const pts = [];
  let dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0);
  const sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  let err = dx + dy, x = x0, y = y0;
  for (;;) {
    pts.push([x, y]);
    if (x === x1 && y === y1) break;
    const e2 = 2 * err;
    if (e2 >= dy) { err += dy; x += sx; }
    if (e2 <= dx) { err += dx; y += sy; }
  }
  return pts;
}

function drawSpreadLinks(ctx, frame) {
  if (!frame || !frames) return;
  const sz = Math.max(2, Math.round(CELL * 0.6));
  const off = Math.floor((CELL - sz) / 2);
  for (let k = LINK_DAYS - 1; k >= 0; k--) {
    const fr = frames[frame.day - k];
    if (!fr || !fr.links) continue;
    const alpha = 1 - k * 0.22;
    fr.links.forEach(([a, b, , cross]) => {
      const A = MUNIS[a], B = MUNIS[b];
      const pts = bresenham(A.cx, A.cy, B.cx, B.cy);
      ctx.globalAlpha = alpha;
      pts.forEach(([x, y], idx) => {
        if (idx % 2) return;                       // punteada
        ctx.fillStyle = '#050a16';
        ctx.fillRect(x * CELL + off - 1, y * CELL + off - 1, sz + 2, sz + 2);
        ctx.fillStyle = cross ? '#22d3ee' : '#fbbf24';   // cian = mensaje MPI entre workers; ámbar = salto interno
        ctx.fillRect(x * CELL + off, y * CELL + off, sz, sz);
      });
      // paquete de infección que viaja del origen al destino
      const pi = Math.min(pts.length - 1, Math.round((k + 1) / LINK_DAYS * (pts.length - 1)));
      const [px, py] = pts[pi];
      ctx.fillStyle = '#050a16';
      ctx.fillRect(px * CELL - 2, py * CELL - 2, CELL + 4, CELL + 4);
      ctx.fillStyle = cross ? '#22d3ee' : '#ef4444';
      ctx.fillRect(px * CELL, py * CELL, CELL, CELL);
      if (cross) { ctx.fillStyle = '#ef4444'; ctx.fillRect(px * CELL + 2, py * CELL + 2, Math.max(1, CELL - 4), Math.max(1, CELL - 4)); }
      // destello en el destino el día del salto
      if (k < 2) {
        ctx.strokeStyle = '#ef4444';
        ctx.lineWidth = 2;
        const r = (k + 2) * CELL;
        ctx.strokeRect(B.cx * CELL + CELL / 2 - r, B.cy * CELL + CELL / 2 - r, 2 * r, 2 * r);
      }
      ctx.globalAlpha = 1;
    });
  }
  ctx.globalAlpha = 1;
}

function setCanvasSize(w, h) {
  const canvas = document.getElementById('map');
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  canvas.style.width = w + 'px';
  canvas.style.height = h + 'px';
}

function render(frame) {
  const canvas = document.getElementById('map');
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingEnabled = false;
  if (!baseLayer) return;
  setCanvasSize(PIX_W * CELL, PIX_H * CELL);

  // 1. Mapa base
  ctx.drawImage(baseLayer, 0, 0);
  // 1b. Borde/tinte de cada municipio con el color de SU worker (solo modo clúster)
  if (clusterInfo && clusterInfo.mode === 'mpi' && muniOwner) {
    const key = CELL + '|' + JSON.stringify(muniOwner);
    if (!ownerLayer || ownerKey !== key) { ownerLayer = buildOwnerLayer(); ownerKey = key; }
    ctx.drawImage(ownerLayer, 0, 0);
  }

  // 2. Contagio: relleno rojo tramado (dither) según % de infectados
  if (frame) {
    ctx.fillStyle = 'rgba(110, 18, 18, 0.55)';
    MUNIS.forEach(m => {
      const nd = frame.nodes[m.id];
      const tot = nd[0] + nd[1] + nd[2] + (nd[4] || 0);
      const pct = tot > 0 ? nd[1] / tot : 0;
      if (pct <= 0.002) return;
      const t = Math.min(1, pct * 3);
      MUNI_CELLS[m.id].forEach(([x, y]) => {
        if (EDGE[y][x] !== 0) return;
        if ((BAYER4[(y & 3) * 4 + (x & 3)] + 0.5) / 16 < t) ctx.fillRect(x * CELL, y * CELL, CELL, CELL);
      });
    });
  }

  // 3. Personas (1 pixel por persona simbólica, dentro de su municipio)
  const pad = Math.max(1, Math.floor(CELL / 6));
  MUNIS.forEach(m => {
    const offsets = DOTS[m.id] || [];
    const n = offsets.length;
    let s = 1, i = 0, r = 0, d = 0;
    if (frame) { const nd = frame.nodes[m.id]; s = nd[0]; i = nd[1]; r = nd[2]; d = nd[4] || 0; }
    offsets.forEach(([x, y], idx) => {
      const col = frame ? dotColor(idx, n, s, i, r, d) : C_S;
      const px = x * CELL + pad, py = y * CELL + pad, sz = CELL - 2 * pad;
      ctx.fillStyle = '#050a16';
      ctx.fillRect(px - 1, py - 1, sz + 2, sz + 2);
      if (col === C_D) {
        ctx.fillStyle = '#e2e8f0';
        ctx.fillRect(px, py, sz, sz);
        ctx.fillStyle = C_D;
        ctx.fillRect(px + 1, py + 1, Math.max(1, sz - 2), Math.max(1, sz - 2));
      } else {
        ctx.fillStyle = col;
        ctx.fillRect(px, py, sz, sz);
      }
    });
  });

  // 3b. Saltos de contagio entre municipios (líneas pixeladas con probabilidad)
  drawSpreadLinks(ctx, frame);

  // 4. Ciudades (marcador cuadrado) y etiquetas
  const S = Math.max(1, Math.floor(CELL / 3));
  MUNIS.forEach(m => {
    const px = m.cx * CELL, py = m.cy * CELL;
    ctx.fillStyle = '#050a16';
    ctx.fillRect(px - 2, py - 2, CELL + 4, CELL + 4);
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(px, py, CELL, CELL);
    ctx.fillStyle = '#facc15';
    ctx.fillRect(px + 2, py + 2, CELL - 4, CELL - 4);
  });
  MUNIS.forEach(m => {
    const lab = getLabel(m.shortName, '#ffffff', S);
    let lx = Math.round(m.cx * CELL + CELL / 2 - lab.width / 2);
    lx = Math.max(2, Math.min(PIX_W * CELL - lab.width - 2, lx));
    ctx.drawImage(lab, lx, m.cy * CELL - lab.height - 2);

    if (frame) {
      const nd = frame.nodes[m.id];
      const tot = nd[0] + nd[1] + nd[2] + (nd[4] || 0);
      const pct = tot > 0 ? (nd[1] / tot * 100) : 0;
      if (pct > 0.1) {
        const pl = getLabel(pct.toFixed(1) + '%', '#fca5a5', S);
        let px2 = Math.round(m.cx * CELL + CELL / 2 - pl.width / 2);
        px2 = Math.max(2, Math.min(PIX_W * CELL - pl.width - 2, px2));
        ctx.drawImage(pl, px2, m.cy * CELL + CELL + 2);
      }
    }
  });
}

function setDay(d) {
  if (!frames || !frames[d]) return;
  const frame = frames[d];
  currentFrame = frame;

  document.getElementById('dayLbl').textContent = `Día ${frame.day}`;
  document.getElementById('daySlider').value = d;

  let S=0, I=0, R=0, D=0;
  frame.nodes.forEach(nd => {
    S += nd[0];
    I += nd[1];
    R += nd[2];
    D += (nd[4] || 0);
  });
  document.getElementById('ss').textContent = fmtN(S);
  document.getElementById('si').textContent = fmtN(I);
  document.getElementById('sr').textContent = fmtN(R);
  document.getElementById('sd').textContent = fmtN(D);

  updateClusterPanel(frame);
  render(frame);
}

function fmtN(n) {
  return n >= 1e6 ? (n/1e6).toFixed(2)+'M'
       : n >= 1e3 ? (n/1e3).toFixed(1)+'k'
       : n.toString();
}

// Cargar Epidemias
async function loadEpidemics() {
  try {
    const r = await fetch('/api/epidemics');
    const d = await r.json();
    const list = d.epidemias || [];
    const grid = document.getElementById('epiGrid');
    grid.innerHTML = '';
    list.forEach(epi => {
      const btn = document.createElement('button');
      btn.className = 'epi-btn';
      btn.innerHTML = `<span class="ico">${epi.icono}</span><span class="lbl">${epi.nombre}</span><span class="r0">R₀ ≈ ${epi.r0}</span>`;
      btn.onclick = () => {
        document.querySelectorAll('.epi-btn').forEach(b => b.classList.remove('sel'));
        btn.classList.add('sel');
        selectedEpi = epi;

        // Si ya hay datos simulados, recalcular fallecidos con la nueva epidemia seleccionada
        if (rawFrames) {
          frames = processFramesWithDemographics(rawFrames);
          let curr = parseInt(document.getElementById('daySlider').value);
          setDay(curr);
        }
      };
      grid.appendChild(btn);
    });
    if (grid.children.length) grid.children[0].click();
  } catch(e) {
    document.getElementById('status').textContent = '❌ Error cargando epidemias.';
  }
}

// Cargar Municipios en el selector de foco
async function loadMunis() {
  const sel = document.getElementById('seedNode');
  sel.innerHTML = '';
  MUNIS.forEach(m => {
    const o = document.createElement('option');
    o.value = m.id;
    o.textContent = `${m.name} (${m.prov})`;
    sel.appendChild(o);
  });
  sel.value = "1"; // Yacuiba por defecto
}

// Botón Simular
async function runSim() {
  if (!selectedEpi) {
    document.getElementById('status').textContent = '⚠️ Elige una epidemia primero.';
    return;
  }
  const btn = document.getElementById('btnRun');
  btn.disabled = true;
  const execMode = document.getElementById('execMode').value;
  document.getElementById('status').textContent = execMode === 'mpi'
    ? '⏳ Ejecutando en el clúster MPI (Master + 2 Workers)...' : '⏳ Ejecutando motor C++ (simulando brote)...';

  stopPlay();

  const payload = {
    seed_node:      parseInt(document.getElementById('seedNode').value),
    days:           parseInt(document.getElementById('s-days').value),
    infected:       parseInt(document.getElementById('s-inf').value),
    contacts:       parseInt(document.getElementById('s-con').value),
    beta:           selectedEpi.beta,
    infectious_min: selectedEpi.infeccioso_min,
    infectious_max: selectedEpi.infeccioso_max,
    vaccination:    0.0,
    mask:           parseFloat(document.getElementById('s-msk').value),
    mobility:       parseFloat(document.getElementById('s-mob').value),
    seed:           42,
    mode:           execMode,
    threads:        execMode === 'mpi' ? 2 : 4
  };

  try {
    const res = await fetch('/api/game/tarija', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    const data = await res.json();

    if (!data.success) {
      document.getElementById('status').textContent = '❌ Error en motor: ' + (data.error || 'falló simulación');
      btn.disabled = false;
      return;
    }

    rawFrames = data.frames;
    setupCluster(data.cluster, rawFrames);
    frames = processFramesWithDemographics(rawFrames);

    const nDays = frames.length - 1;
    const pk = data.summary;

    const slider = document.getElementById('daySlider');
    slider.max = nDays;
    slider.value = 0;
    slider.disabled = false;

    document.getElementById('btnPlay').disabled = false;
    document.getElementById('peakInfo').textContent =
      `Pico: ${fmtN(pk.peak_infected)} infectados (Día ${pk.peak_day}) · ${data.elapsed_ms.toFixed(0)} ms` + clusterTimeText();

    document.getElementById('status').textContent =
      `✅ Simulación completada: ${nDays} días (${selectedEpi.nombre}).`;

    // Iniciar reproducción día a día
    setDay(0);
    startPlay();

  } catch (err) {
    document.getElementById('status').textContent = '❌ Error de conexión: ' + err.message;
  }
  btn.disabled = false;
}

// Reproducción automática día a día
function startPlay() {
  if (!frames || frames.length === 0) return;
  isPlaying = true;
  document.getElementById('btnPlay').textContent = '⏸ Pausar';
  if (playInterval) clearInterval(playInterval);

  playInterval = setInterval(() => {
    let curr = parseInt(document.getElementById('daySlider').value);
    if (curr >= frames.length - 1) {
      stopPlay();
      return;
    }
    setDay(curr + 1);
  }, 120);
}

function stopPlay() {
  isPlaying = false;
  if (playInterval) clearInterval(playInterval);
  playInterval = null;
  const btn = document.getElementById('btnPlay');
  if (btn) btn.textContent = '▶ Reproducir';
}

function togglePlay() {
  if (isPlaying) {
    stopPlay();
  } else {
    let curr = parseInt(document.getElementById('daySlider').value);
    if (curr >= (frames?.length || 0) - 1) {
      setDay(0);
    }
    startPlay();
  }
}

// Tooltip: la celda bajo el cursor dice a qué municipio pertenece
function setupTooltip() {
  const canvas = document.getElementById('map');
  const tt = document.getElementById('tt');

  canvas.addEventListener('mousemove', e => {
    if (!currentFrame) { tt.style.display = 'none'; return; }
    const rect = canvas.getBoundingClientRect();
    const cx = Math.floor((e.clientX - rect.left) / rect.width * PIX_W);
    const cy = Math.floor((e.clientY - rect.top) / rect.height * PIX_H);
    const id = gridAt(cx, cy);
    const best = id >= 0 ? MUNIS[id] : null;

    if (best) {
      const nd = currentFrame.nodes[best.id];
      const tot = nd[0] + nd[1] + nd[2] + (nd[4] || 0);
      const pct = tot > 0 ? (nd[1] / tot * 100).toFixed(1) : '0.0';
      document.getElementById('tt-name').textContent = `${best.name} (${best.prov})`;
      document.getElementById('tt-s').textContent = nd[0].toLocaleString();
      document.getElementById('tt-i').textContent = nd[1].toLocaleString();
      document.getElementById('tt-r').textContent = nd[2].toLocaleString();
      document.getElementById('tt-d').textContent = (nd[4] || 0).toLocaleString();
      document.getElementById('tt-n').textContent = nd[3].toLocaleString();
      document.getElementById('tt-pct').textContent = pct + ' %';
      const wrow = document.getElementById('tt-wrow');
      if (clusterInfo && clusterInfo.mode === 'mpi' && muniOwner) {
        document.getElementById('tt-w').textContent = 'Worker ' + muniOwner[best.id];
        wrow.style.display = 'flex';
      } else wrow.style.display = 'none';
      tt.style.display = 'block';
      tt.style.left = (e.clientX + 15) + 'px';
      tt.style.top  = (e.clientY - 15) + 'px';
    } else {
      tt.style.display = 'none';
    }
  });

  canvas.addEventListener('mouseleave', () => { tt.style.display = 'none'; });
}


// ------------------------------------------------------------
//  Clúster: Master + Worker 1 + Worker 2 (datos REALES de telemetría del motor MPI)
// ------------------------------------------------------------
const W_COL = { 0: '#e2e8f0', 1: '#fb923c', 2: '#f472b6' };
const W_NAME = { 0: 'Master', 1: 'Worker 1', 2: 'Worker 2' };

function fmtMs(x) { return x == null ? '—' : (x < 10 ? x.toFixed(2) : x.toFixed(1)) + ' ms'; }

function setupCluster(cl, fr) {
  clusterInfo = cl || null;
  muniOwner = null; ownerLayer = null;
  if (cl && cl.mode === 'mpi' && cl.workers) {
    muniOwner = {};
    cl.workers.forEach(w => (w.municipios || []).forEach(id => { muniOwner[id] = w.rank; }));
  }
  // acumulados por día (suma de lo medido hasta ese día)
  const cum = { 1: {c:0,m:0,ms:0,mr:0,js:0,jr:0,bs:0,br:0}, 2: {c:0,m:0,ms:0,mr:0,js:0,jr:0,bs:0,br:0}, 0: {w:0,a:0,recv:0} };
  fr.forEach(f => {
    if (f.workers) {
      f.workers.forEach(w => {
        const c = cum[w.rank];
        c.c += w.compute_ms; c.m += w.comm_ms; c.ms += w.msgs_enviados; c.mr += w.msgs_recibidos;
        c.js += w.saltos_enviados; c.jr += w.saltos_recibidos; c.bs += w.bytes_enviados; c.br += w.bytes_recibidos;
        cum[0].recv += 3;      // cada worker envía 3 mensajes al Master por día (telemetría, métricas, saltos)
      });
      if (f.master) { cum[0].w += f.master.wait_ms || 0; cum[0].a += f.master.assemble_ms || 0; }
    }
    f.cum = JSON.parse(JSON.stringify(cum));
  });
  const box = document.getElementById('cluster');
  box.classList.add('on');
  const ban = document.getElementById('clusterBanner');
  ban.className = 'k-' + (cl ? cl.kind : 'local');
  ban.textContent = cl ? '⚙ ' + cl.label : '';
  document.getElementById('legendWorkers').style.display = (muniOwner ? 'inline' : 'none');
}

function clusterTimeText() {
  const s = clusterInfo && clusterInfo.summary;
  if (!s) return '';
  if (clusterInfo.mode === 'mpi')
    return ` · MPI: total ${s.total_ms.toFixed(1)} ms = cómputo ${s.compute_ms.toFixed(1)} + comunicación ${s.comm_ms.toFixed(1)} (+espera/sinc.) · ${s.saltos_mpi}/${s.saltos} saltos cruzaron de worker`;
  return ` · cómputo ${s.compute_ms.toFixed(1)} ms`;
}

function muniNames(ids) {
  return (ids || []).map(i => MUNI_META[i] ? MUNI_META[i].shortName : i).join(', ');
}

function buildOwnerLayer() {
  const c = document.createElement('canvas');
  c.width = PIX_W * CELL; c.height = PIX_H * CELL;
  const g = c.getContext('2d');
  for (let y = 0; y < PIX_H; y++) {
    for (let x = 0; x < PIX_W; x++) {
      const id = GRID[y][x];
      if (id < 0) continue;
      const col = W_COL[muniOwner[id]] || '#fff';
      g.globalAlpha = EDGE[y][x] !== 0 ? 0.95 : 0.10;   // borde sólido + tinte suave
      g.fillStyle = col;
      g.fillRect(x * CELL, y * CELL, CELL, CELL);
    }
  }
  g.globalAlpha = 1;
  return c;
}

function updateClusterPanel(frame) {
  const box = document.getElementById('clusterCards');
  if (!clusterInfo) { box.innerHTML = ''; return; }
  const cum = frame.cum || null;
  const last = frames && frame.day >= frames[frames.length - 1].day;
  if (clusterInfo.mode !== 'mpi') {
    const s = clusterInfo.summary || {};
    box.style.gridTemplateColumns = '1fr';
    box.innerHTML = `<div class="ncard" style="--c:#fbbf24"><div class="nh"><span class="led"></span>Proceso local
      <span class="st">${last ? 'terminado' : 'simulando día ' + frame.day}</span></div>
      <div class="mun">Simula los ${MUNI_META.length} municipios en un solo proceso</div>
      <div class="nr"><span>Cómputo total</span><b>${fmtMs(s.compute_ms)}</b></div>
      <div class="nr"><span>Mensajes MPI</span><b>0 (no hay red)</b></div></div>`;
    return;
  }
  box.style.gridTemplateColumns = 'repeat(3,1fr)';
  const wk = {}; (frame.workers || []).forEach(w => { wk[w.rank] = w; });
  const wait = frame.master ? frame.master.wait_ms : 0;
  let html = '';
  // ---- Master
  const mc = cum ? cum[0] : { w: 0, a: 0, recv: 0 };
  html += `<div class="ncard" style="--c:${W_COL[0]}"><div class="nh"><span class="led"></span>Master · rank 0
    <span class="st">${frame.day === 0 ? 'Bcast config + partición' : last ? 'terminado' : 'coordinando día ' + frame.day}</span></div>
    <div class="mun">No simula: reparte la partición, recibe métricas y arma el frame</div>
    <div class="nr"><span>Espera a workers (día)</span><b>${frame.master ? fmtMs(wait) : '—'}</b></div>
    <div class="nr"><span>Ensamblar frame (día)</span><b>${frame.master ? fmtMs(frame.master.assemble_ms) : '—'}</b></div>
    <div class="nr"><span>MPI recibidos / enviados</span><b>${mc.recv} / ${frame.day === 0 ? 0 : 1} (Bcast)</b></div>
    <div class="nr"><span>Barreras (MPI_Barrier)</span><b>${frame.day}</b></div></div>`;
  // ---- Workers
  [1, 2].forEach(r => {
    const info = (clusterInfo.workers || []).find(w => w.rank === r) || {};
    const w = wk[r];
    const c = cum ? cum[r] : { c:0,m:0,ms:0,mr:0,js:0,jr:0,bs:0,br:0 };
    const busyC = w && wait > 0 ? Math.min(100, w.compute_ms / wait * 100) : 0;
    const busyM = w && wait > 0 ? Math.min(100 - busyC, w.comm_ms / wait * 100) : 0;
    const state = !w ? 'esperando' : (w.compute_ms >= w.comm_ms ? 'calculando' : 'comunicando');
    html += `<div class="ncard" style="--c:${W_COL[r]}"><div class="nh"><span class="led"></span>${W_NAME[r]} · rank ${r}
      <span class="st">${last ? 'terminado' : state}</span></div>
      <div class="mun">🏘 ${muniNames(info.municipios)} <span style="color:#64748b">(${(info.poblacion || 0).toLocaleString()} hab. · host ${info.host || '?'})</span></div>
      <div class="nr"><span>Cómputo día / acum.</span><b>${w ? fmtMs(w.compute_ms) : '—'} / ${fmtMs(c.c)}</b></div>
      <div class="nr"><span>Comunic. día / acum.</span><b>${w ? fmtMs(w.comm_ms) : '—'} / ${fmtMs(c.m)}</b></div>
      <div class="nr"><span>Mensajes MPI env/rec</span><b>${c.ms} / ${c.mr}</b></div>
      <div class="nr"><span>Saltos MPI env/rec</span><b>${c.js} / ${c.jr}</b></div>
      <div class="nr"><span>Bytes env/rec</span><b>${fmtN(c.bs)} / ${fmtN(c.br)}</b></div>
      <div class="bar" title="ocupación del día: cómputo (color del worker) + comunicación (cian)"><i class="bc" style="width:${busyC}%"></i><i class="bm" style="width:${busyM}%"></i></div></div>`;
  });
  box.innerHTML = html;
}

function setupExecSelector() {
  const sel = document.getElementById('execMode');
  const note = document.getElementById('execNote');
  const upd = () => {
    note.textContent = sel.value === 'mpi'
      ? 'Master + Worker 1 + Worker 2 (MPI). Usa Docker si el clúster está levantado; si no, mpirun local (3 procesos en esta máquina). Cada municipio lo simula un solo worker.'
      : 'Un solo proceso simula los 11 municipios.';
  };
  sel.addEventListener('change', upd);
  upd();
}

// Control acoplado de edades (interfaz limpia, suma 100%)
function setupAgeSliders() {
  const ageInputs = document.querySelectorAll('.age-slider');

  function updateAgeDisplay() {
    for (let k = 0; k < 4; k++) {
      document.getElementById(`v-age-${k}`).textContent = Math.round(ageShares[k]) + '%';
      ageInputs[k].value = Math.round(ageShares[k]);
    }
    const sum = Math.round(ageShares.reduce((a, b) => a + b, 0));
    const sumEl = document.getElementById('ageSum');
    sumEl.textContent = sum + '%';
    sumEl.style.color = sum === 100 ? '#22c55e' : '#f87171';
  }

  ageInputs.forEach(input => {
    input.addEventListener('input', e => {
      const idx = parseInt(e.target.dataset.idx);
      let newVal = parseFloat(e.target.value);
      newVal = Math.max(0, Math.min(100, newVal));

      const oldVal = ageShares[idx];
      const diff = newVal - oldVal;
      ageShares[idx] = newVal;

      const otherIndices = [0, 1, 2, 3].filter(i => i !== idx);
      const otherSum = otherIndices.reduce((acc, i) => acc + ageShares[i], 0);

      if (otherSum > 0) {
        otherIndices.forEach(i => {
          const ratio = ageShares[i] / otherSum;
          ageShares[i] = Math.max(0, ageShares[i] - diff * ratio);
        });
      } else {
        const share = (100 - newVal) / otherIndices.length;
        otherIndices.forEach(i => { ageShares[i] = Math.max(0, share); });
      }

      const currentSum = ageShares.reduce((a, b) => a + b, 0);
      if (currentSum > 0) {
        for (let k = 0; k < 4; k++) ageShares[k] = (ageShares[k] / currentSum) * 100.0;
      }

      updateAgeDisplay();

      if (rawFrames) {
        frames = processFramesWithDemographics(rawFrames);
        let curr = parseInt(document.getElementById('daySlider').value);
        setDay(curr);
      }
    });
  });

  updateAgeDisplay();
}

// Sliders y Controles
function setupControls() {
  [
    ['s-days', 'v-days', v => v],
    ['s-inf',  'v-inf',  v => v],
    ['s-msk',  'v-msk',  v => Math.round(v*100)+' %'],
    ['s-con',  'v-con',  v => v],
    ['s-mob',  'v-mob',  v => Math.round(v*100)+' %'],
  ].forEach(([id, vid, fmt]) => {
    const el = document.getElementById(id);
    document.getElementById(vid).textContent = fmt(el.value);
    el.addEventListener('input', () => { document.getElementById(vid).textContent = fmt(el.value); });
  });

  document.getElementById('daySlider').addEventListener('input', e => {
    stopPlay();
    setDay(+e.target.value);
  });

  document.getElementById('btnRun').addEventListener('click', runSim);
  document.getElementById('btnPlay').addEventListener('click', togglePlay);

  setupAgeSliders();
  setupExecSelector();
}

// Dimensionamiento del Canvas: celda de tamaño entero => píxeles perfectos
function setupCanvas() {
  const wrap = document.querySelector('.canvas-wrap');
  function resize() {
    const w = wrap.clientWidth || 800;
    const h = wrap.clientHeight || 554;
    const cell = Math.min(w / PIX_W, h / PIX_H);
    const newCell = Math.max(4, Math.floor(cell));
    if (newCell !== CELL || !baseLayer) {
      CELL = newCell;
      buildBaseLayer();
    }
    render(currentFrame);
  }
  new ResizeObserver(resize).observe(wrap);
  resize();
}

// Inicialización
window.addEventListener('DOMContentLoaded', async () => {
  initDots();
  setupControls();
  setupTooltip();
  setupCanvas();

  await Promise.all([loadEpidemics(), loadMunis()]);
});
