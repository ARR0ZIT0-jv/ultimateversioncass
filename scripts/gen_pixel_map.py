#!/usr/bin/env python3
"""
Genera la grilla "pixel-art" del departamento de Tarija con sus 11 municipios.

Entrada : web/frontend/assets/tarija_map.jpg   (mapa de provincias, 500x346)
Salida  : web/frontend/js/tarija_pixelmap.js   (grilla de celdas 125x86 -> id de municipio)

Pasos:
  1. Reduce la imagen a bloques de 4x4 px y clasifica cada bloque por color
     de provincia (o fondo). El texto negro del mapa se ignora.
  2. Limpia ruido (islas, huecos).
  3. Divide cada provincia en sus municipios reales (Voronoi restringido a la
     provincia, con la ciudad principal de cada municipio como semilla).

Uso:  python3 -I scripts/gen_pixel_map.py
"""
import json
import os
import sys
from collections import deque

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "web/frontend/assets/tarija_map.jpg")
OUT = os.path.join(ROOT, "web/frontend/js/tarija_pixelmap.js")
PREVIEW = sys.argv[1] if len(sys.argv) > 1 else None

BLOCK = 4  # px de la imagen original por celda

# Colores de cada provincia en la imagen original
PROVINCES = {
    "Gran Chaco":        (180, 120, 204),
    "Aniceto Arce":      (214, 196, 196),
    "Eustaquio Méndez":  (4, 184, 220),
    "Burdet O'Connor":   (104, 240, 112),
    "José María Avilés": (180, 164, 144),
    "Cercado":           (212, 196, 6),
}
PROV_LIST = list(PROVINCES)

# Peso de cada municipio en el reparto (>1 = más superficie); por defecto 1.
WEIGHT = {1: 1.3}

# Municipios (id, provincia, nombre, x, y) con la semilla en px de la imagen
# original (500x346): ubicación de la ciudad principal dentro de su provincia.
# Si una semilla cae fuera de su provincia, se ajusta a la celda más cercana.
MUNIS = [
    (0,  "Cercado",           "Tarija (Cercado)",  135, 120),
    (1,  "Gran Chaco",        "Yacuiba",           340, 203),   # frontera sur
    (2,  "Gran Chaco",        "Villamontes",       375,  70),
    (3,  "Aniceto Arce",      "Bermejo",           172, 295),
    (4,  "Eustaquio Méndez",  "Villa San Lorenzo", 110,  82),
    (5,  "Burdet O'Connor",   "Entre Ríos",        205, 105),
    (6,  "Aniceto Arce",      "Padcaya",           140, 190),
    (7,  "José María Avilés", "Uriondo",            85, 140),
    (8,  "Gran Chaco",        "Caraparí",          270, 145),
    (9,  "Eustaquio Méndez",  "El Puente",          65,  60),
    (10, "José María Avilés", "Yunchará",           22, 155),
]

im = np.array(Image.open(SRC).convert("RGB")).astype(int)
H0, W0, _ = im.shape
GW, GH = W0 // BLOCK, H0 // BLOCK

pal = np.array([PROVINCES[p] for p in PROV_LIST])


def classify_block(blk):
    px = blk.reshape(-1, 3)
    dark = px.max(axis=1) < 110            # texto y bordes negros: se ignoran
    white = (px.min(axis=1) > 235)
    px_c = px[~dark & ~white]
    n_white = white.sum()
    if len(px_c) == 0:
        return -1
    d = ((px_c[:, None, :] - pal[None, :, :]) ** 2).sum(axis=2)
    near = d.argmin(axis=1)
    ok = d.min(axis=1) < 55 ** 2
    near = near[ok]
    if len(near) == 0:
        return -1
    counts = np.bincount(near, minlength=len(PROV_LIST))
    if counts.max() <= n_white * 0.6:
        return -1
    return int(counts.argmax())


prov = np.full((GH, GW), -1, dtype=int)
for gy in range(GH):
    for gx in range(GW):
        blk = im[gy * BLOCK:(gy + 1) * BLOCK, gx * BLOCK:(gx + 1) * BLOCK]
        prov[gy, gx] = classify_block(blk)

# --- Limpieza -------------------------------------------------------------
N4 = [(1, 0), (-1, 0), (0, 1), (0, -1)]


def components(mask):
    seen = np.zeros_like(mask, dtype=bool)
    comps = []
    for y in range(GH):
        for x in range(GW):
            if mask[y, x] and not seen[y, x]:
                q = deque([(y, x)])
                seen[y, x] = True
                comp = []
                while q:
                    cy, cx = q.popleft()
                    comp.append((cy, cx))
                    for dx, dy in N4:
                        ny, nx = cy + dy, cx + dx
                        if 0 <= ny < GH and 0 <= nx < GW and mask[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = True
                            q.append((ny, nx))
                comps.append(comp)
    return comps


def neighbor_majority(y, x, exclude=None):
    cnt = {}
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            ny, nx = y + dy, x + dx
            if (dy or dx) and 0 <= ny < GH and 0 <= nx < GW:
                v = prov[ny, nx]
                if v >= 0 and v != exclude:
                    cnt[v] = cnt.get(v, 0) + 1
    return max(cnt, key=cnt.get) if cnt else -1


# a) solo la componente más grande de cada provincia
for pi in range(len(PROV_LIST)):
    comps = components(prov == pi)
    comps.sort(key=len, reverse=True)
    for comp in comps[1:]:
        for (y, x) in comp:
            prov[y, x] = -2  # marcar para reasignar
for _ in range(4):
    for y in range(GH):
        for x in range(GW):
            if prov[y, x] == -2:
                v = neighbor_majority(y, x)
                prov[y, x] = v if v >= 0 else -2
prov[prov == -2] = -1

# b) huecos de fondo encerrados -> tierra
bg = prov == -1
seen = np.zeros_like(bg)
q = deque()
for y in range(GH):
    for x in range(GW):
        if (y in (0, GH - 1) or x in (0, GW - 1)) and bg[y, x]:
            seen[y, x] = True
            q.append((y, x))
while q:
    cy, cx = q.popleft()
    for dx, dy in N4:
        ny, nx = cy + dy, cx + dx
        if 0 <= ny < GH and 0 <= nx < GW and bg[ny, nx] and not seen[ny, nx]:
            seen[ny, nx] = True
            q.append((ny, nx))
for y in range(GH):
    for x in range(GW):
        if bg[y, x] and not seen[y, x]:
            v = neighbor_majority(y, x)
            if v >= 0:
                prov[y, x] = v

# c) suavizado: una celda con >=3 vecinos (de 4) de otra clase adopta esa clase
for _ in range(2):
    new = prov.copy()
    for y in range(GH):
        for x in range(GW):
            cnt = {}
            for dx, dy in N4:
                ny, nx = y + dy, x + dx
                v = prov[ny, nx] if (0 <= ny < GH and 0 <= nx < GW) else -1
                cnt[v] = cnt.get(v, 0) + 1
            best = max(cnt, key=cnt.get)
            if best != prov[y, x] and cnt[best] >= 3:
                new[y, x] = best
    prov = new

# --- Municipios: Voronoi dentro de cada provincia ---------------------------
seeds = {}
for (mid, pname, name, ox, oy) in MUNIS:
    pi = PROV_LIST.index(pname)
    gx, gy = ox // BLOCK, oy // BLOCK
    if not (0 <= gy < GH and 0 <= gx < GW) or prov[gy, gx] != pi:
        cells = np.argwhere(prov == pi)
        d = ((cells - np.array([gy, gx])) ** 2).sum(axis=1)
        gy2, gx2 = cells[d.argmin()]
        print(f"  ajuste semilla {name}: ({gx},{gy}) -> ({gx2},{gy2})")
        gx, gy = int(gx2), int(gy2)
    seeds[mid] = (gx, gy, pi)

muni = np.full((GH, GW), -1, dtype=int)
for y in range(GH):
    for x in range(GW):
        pi = prov[y, x]
        if pi < 0:
            continue
        cand = [(mid, s) for mid, s in seeds.items() if s[2] == pi]
        best = min(cand, key=lambda c: (((c[1][0] - x) ** 2 + (c[1][1] - y) ** 2) ** 0.5) / WEIGHT.get(c[0], 1.0))
        muni[y, x] = best[0]

# --- Verificación ---------------------------------------------------------
print(f"Grilla {GW}x{GH}")
for (mid, pname, name, ox, oy) in MUNIS:
    n = int((muni == mid).sum())
    print(f"  {mid:>2} {name:<20} {pname:<18} celdas={n}")
    assert n > 0, f"municipio sin celdas: {name}"

# --- Salida JS ------------------------------------------------------------
CH = "0123456789a"
rows = []
for y in range(GH):
    rows.append("".join("." if muni[y, x] < 0 else CH[muni[y, x]] for x in range(GW)))

with open(OUT, "w", encoding="utf-8") as f:
    f.write("// GENERADO por scripts/gen_pixel_map.py — no editar a mano.\n")
    f.write("// Cada caracter = 1 celda del mapa. '.' = fuera del departamento;\n")
    f.write("// '0'..'9','a' = id del municipio (0..10).\n")
    f.write(f"const PIX_W = {GW};\nconst PIX_H = {GH};\n")
    f.write("const PIX_ROWS = [\n")
    for r in rows:
        f.write(f'  "{r}",\n')
    f.write("];\n")
    f.write("// Ciudad principal de cada municipio, en coordenadas de celda [x, y]\n")
    f.write("const PIX_SEEDS = " + json.dumps([[seeds[i][0], seeds[i][1]] for i in range(len(MUNIS))]) + ";\n")
    f.write("// Provincia de cada municipio (índice en PIX_PROVINCES)\n")
    f.write("const PIX_PROVINCES = " + json.dumps(PROV_LIST, ensure_ascii=False) + ";\n")
    f.write("const PIX_MUNI_PROV = " + json.dumps([PROV_LIST.index(m[1]) for m in MUNIS]) + ";\n")
print("OK ->", OUT)

if PREVIEW:
    cols = [(180, 120, 204), (225, 170, 185), (40, 185, 225), (95, 210, 105), (180, 150, 105), (230, 198, 25)]
    img = Image.new("RGB", (GW, GH), (15, 23, 42))
    for y in range(GH):
        for x in range(GW):
            mid = muni[y, x]
            if mid >= 0:
                base = cols[prov[y, x]]
                k = 0.85 + 0.15 * ((mid * 7) % 3) / 2
                img.putpixel((x, y), tuple(int(c * k) for c in base))
    for mid, (gx, gy, _) in seeds.items():
        img.putpixel((gx, gy), (255, 255, 255))
    img.resize((GW * 8, GH * 8), Image.NEAREST).save(PREVIEW)
