# Simulador Distribuido de Epidemias — Tarija (2D Pixel-Art)

**TEL-420 Sistemas Paralelos y Distribuidos** · C++20 · OpenMP · OpenMPI 4 · Docker · Python 3

Simulador/juego de propagación de enfermedades sobre los **11 municipios de Tarija (Bolivia)** con motor
C++ real y visor web 2D pixel-art. Tres hitos: diseño (H1), implementación (H2) y evaluación (H3).

---

## Arranque rápido

```bash
# Opción 1 — script automático (recomienda Docker > mpirun local > shim)
./scripts/start.sh
# Luego abre: http://localhost:8088/game2d.html

# Opción 2 — manual con OpenMPI local
make && make mpi
python3 web/backend/server.py
```

El visor abre directamente en **`http://localhost:8088/`** (redirige a `game2d.html`).

---

## Modos de ejecución del motor

| Modo | Comando | Descripción |
|------|---------|-------------|
| `net` local | `./epidemic_sim --mode net [--threads T]` | Un proceso, OpenMP, 11 municipios |
| `net` MPI | `mpirun -np 3 ./epidemic_sim_mpi --mode net` | Master + Worker1 + Worker2 |
| `seq` | `./epidemic_sim --mode seq` | Secuencial (línea base) |
| `omp` | `./epidemic_sim --mode omp --threads T` | OpenMP (agentes, dos regiones) |

### Parámetros principales

```
--days N         Días a simular (defecto: 60)
--infected N     Infectados iniciales (defecto: 100)
--beta FLOAT     Tasa de transmisión (defecto: 0.035)
--gamma FLOAT    Tasa de recuperación (defecto: 0)
--min-days N     Duración mínima del período infeccioso
--max-days N     Duración máxima del período infeccioso
--contacts K     Contactos esperados por persona/día
--mobility FLOAT Probabilidad de movilidad entre municipios
--mask FLOAT     Fracción de uso de mascarillas
--seed UINT64    Semilla PRNG (defecto: 42)
--nodes CSV      CSV de municipios (defecto: data/tarija_municipios.csv)
--seed-node ID   Municipio donde empieza el brote (defecto: 1 = Yacuiba)
--partition P    geo | balanced | lista (modo MPI, defecto: geo)
--replicas R     Clona la red R veces — CARGA SINTÉTICA para escalabilidad
--output CSV     Ruta de salida
```

---

## Red de municipios (modo `net`)

La grilla 125×86 de Tarija se genera con `scripts/gen_pixel_map.py` →  
`web/frontend/js/tarija_pixelmap.js`. Cada persona es 1 pixel en su municipio.

**Paleta:** 🟢 `#22c55e` Susceptible · 🔴 `#ef4444` Infectado · 🔵 `#3b82f6` Recuperado · ⚫ `#09090b` Fallecido  
**Saltos:** ámbar (internos al mismo worker) · cian (mensaje MPI entre workers)  
**Workers:** W1 naranja `#fb923c` (Yacuiba, Villamontes, Entre Ríos, Caraparí) · W2 rosa `#f472b6` (resto)

### Motor de contagio (NO se modifica)

- Contagio intra/inter municipio: mezcla por residencia, gravedad `w_ij ~ N_j/d_ij²`.
- Saltos entre municipios: `λ_ij = P_ij·I_i·(1−mascarilla·eficacia)²·kImportEfficiency`,  
  `p=1−exp(−λ)`, `kImportEfficiency=0.4`, 1 caso por salto.
- RNG de saltos por municipio de **origen** (semilla determinista) → reproducible con cualquier partición.

### Particiones MPI (geo por defecto)

| Partición | Worker 1 | Worker 2 |
|-----------|----------|----------|
| `geo` | Yacuiba, Villamontes, Entre Ríos, Caraparí (180 083 hab.) | Tarija, Bermejo, S. Lorenzo, Padcaya, Uriondo, El Puente, Yunchará (354 265 hab.) |
| `balanced` | reparto voraz por población | — |

---

## Reproducibilidad

```bash
make test_net_equal   # 1 proceso (local) == 3 procesos (shim/MPI), con geo, balanced y réplicas
```

La prueba verifica que el CSV generado por el motor local (1 hilo) y por el modo distribuido (3 procesos)
son **byte a byte idénticos**. Debe quedar en verde tras cualquier cambio al motor.

---

## Clúster Docker (Hito 2)

```bash
docker compose up -d --build              # construye epidemic-node una sola vez; los 3 contenedores la usan
docker compose exec -T master bash scripts/smoke_test.sh
docker compose exec -T master make && docker compose exec -T master make mpi
# Abre el visor y selecciona "Clúster MPI"
```

La clave SSH se genera en `Dockerfile.cluster` (`ssh-keygen -t ed25519`) durante el build;
no se versiona ninguna clave privada en el repositorio.

---

## Benchmarks (Hito 3)

```bash
./scripts/benchmark.sh                    # Agentes: OpenMP 1/2/4 hilos, N ∈ {10k,50k,100k,250k}
python3 scripts/benchmark_net.py          # Red: modo net local vs MPI, réplicas R ∈ {1,10,100,1000}
```

Resultados en `results/benchmarks/scalability_report.csv` e `INFORME_HITO3_EVALUACION.md`.  
**Regla:** cada tiempo y speedup viene de una ejecución medida con hardware y semilla indicados.
Los tiempos del shim NO cuentan para el Hito 3.

---

## Estructura del proyecto

```
epidemic-simulator/
├── docker-compose.yml              # 3 servicios (epidemic-node): master, worker1, worker2
├── Makefile                        # all / mpi / epidemic_sim_mpi_shim / test_net_equal
│
├── infrastructure/
│   ├── Dockerfile.cluster          # Ubuntu 24.04, GCC, OpenMPI, SSH (clave generada en build)
│   ├── entrypoint.sh               # Sincronización de red y arranque de daemons
│   ├── hosts.ini                   # Slots MPI: master/worker1/worker2
│   └── ssh/config                  # StrictHostKeyChecking no
│
├── src/
│   ├── core/                       # Individual.hpp, SimulationConfig.hpp, SpatialGrid.hpp
│   ├── utils/                      # PRNG.hpp, Timer.hpp, CSVWriter.hpp
│   ├── sequential/                 # SequentialEngine (línea base T1)
│   ├── parallel/
│   │   ├── OpenMPEngine.*          # Motor agentes A/B con OpenMP (Hito 3 referencia)
│   │   ├── NetworkEngine.*         # Metapoblación SIR, 11 municipios (NO SE TOCA)
│   │   └── distributed/            # NetTypes, NetPartition, NetMaster, NetWorker,
│   │                               # MPIMaster, MPIWorker, MPITypes
│   ├── main.cpp                    # CLI local: --mode seq|omp|net
│   └── mpi_main.cpp                # CLI MPI: --mode net|agents, --partition, --replicas
│
├── tests/
│   ├── mpi_shim/mpi.h              # Shim para test_net_equal SIN OpenMPI real
│   └── smoke/                      # omp_hello.cpp, mpi_hello.cpp
│
├── web/
│   ├── backend/server.py           # API REST :8088 — /api/epidemics, /api/game/tarija
│   └── frontend/
│       ├── game2d.html             # Visor 2D pixel-art (interfaz principal)
│       ├── index.html              # Redirección a game2d.html
│       ├── assets/                 # (vacío — el mapa se genera en tarija_pixelmap.js)
│       └── js/
│           ├── tarija_pixelmap.js  # Grilla 125×86, NO SE TOCA
│           └── game2d.js           # Render, animación, panel Master/Worker, saltos MPI
│
├── data/
│   ├── tarija_municipios.csv       # 11 municipios, población, lat/lon
│   └── epidemics.json              # Parámetros de epidemias (COVID, influenza, etc.)
│
├── results/
│   ├── simulations/bench_*.csv     # Benchmarks de referencia (NO borrar)
│   └── benchmarks/                 # scalability_report.csv, INFORME_HITO3_EVALUACION.md
│
├── scripts/
│   ├── start.sh                    # Compilar + elegir modo MPI + arrancar servidor
│   ├── smoke_test.sh               # Hito 2: SSH, DNS, OpenMP, OpenMPI en contenedores
│   ├── benchmark.sh                # Hito 3: agentes, N y T variables
│   ├── benchmark_net.py            # Hito 3: modo net, réplicas sintéticas
│   ├── run_benchmarks.py           # Hito 3: script unificado
│   └── gen_pixel_map.py            # Genera tarija_pixelmap.js
│
└── docs/
    └── hito2-checklist.md
```

---

## Reglas permanentes

1. **El mapa no se toca:** `tarija_pixelmap.js` y el motor de contagio (`NetworkEngine`) son inmutables.
2. **Paleta fija:** verde S · rojo I · azul R · negro fallecido · ámbar salto interno · cian salto MPI.
3. **Datos reales:** cada tiempo/speedup/eficiencia viene de una corrida medida; los del shim no valen.
4. **`make test_net_equal` en verde** tras cualquier cambio al motor.
5. **`kImportEfficiency=0.4`** no se cambia sin justificación epidemiológica.
6. **Sin 3D:** no hay Three.js, Godot ni imágenes JPG estiradas.
