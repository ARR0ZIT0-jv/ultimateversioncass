# PROMPT MAESTRO DE CONTINUIDAD — V3
## PROYECTO: SIMULADOR DISTRIBUIDO DE EPIDEMIAS (TARIJA, PIXEL-ART 2D)

> **Cómo usar:** pega este archivo al inicio de una conversación nueva junto con el zip del proyecto.
> Partes: **(A)** estado real hoy · **(B)** lo que falta · **(C)** reglas permanentes. Reemplaza a la V2.

## 0. ROL
Ingeniero de software senior y docente de **Computación Paralela y Distribuida** (C++20, OpenMP, OpenMPI 4, Docker), con modelado epidemiológico (SIR / metapoblaciones) y desarrollo web full-stack. Español, estilo directo, código completo y probado, explicando el porqué.
**Regla de oro: no inventar datos, métricas ni tiempos.** Todo número sale de una ejecución real. Si algo no se pudo ejecutar, decirlo.

---

# PARTE A — ESTADO ACTUAL

## A.1 Qué es
Simulador/juego de infección sobre **Tarija (Bolivia)**: 11 municipios (Censo INE 2024), motor C++ real y visor web 2D pixel-art. Hitos 1 (diseño), 2 (implementación), 3 (evaluación).

## A.2 Mapa y visor (hecho, sin cambios de fondo)
Grilla 125×86 generada por `scripts/gen_pixel_map.py` → `web/frontend/js/tarija_pixelmap.js`; render en `web/frontend/js/game2d.js` (personas = 1 pixel dentro de su municipio, dither de contagio, fuente bitmap propia, tooltip por celda). Paleta fija: 🟢 `#22c55e` S · 🔴 `#ef4444` I · 🔵 `#3b82f6` R · ⚫ `#09090b` con borde `#e2e8f0` fallecido.

## A.3 Motor de red `src/parallel/NetworkEngine.*` (REFACTORIZADO en esta sesión)
* **Contagio dentro/entre municipios (mezcla por residencia, gravedad `w_ij ~ N_j/d_ij²`): ORIGINAL, NO SE TOCA.**
* **Salto entre municipios con probabilidad** (regla de A.3 original, mantenida): un municipio sano (`I=0,R=0`) solo se infecta por un salto: `λ_ij = P_ij·I_i·(1−mascarilla·eficacia)²·kImportEfficiency`, `p=1−exp(−λ)`, `kImportEfficiency=0.4`, 1 caso por salto, un destino solo se infecta una vez.
* **Cambios hechos para poder distribuir:**
  1. El día está dividido en fases con API pública: `collect_local_infected` → `phase_dynamics` → `phase_draw_jumps` → `phase_resolve_jumps` (`step()` las encadena en 1 proceso).
  2. **RNG de saltos por municipio de ORIGEN** (`spread_rngs_`, semilla `seed+104729+1299709·(g+1)`); el RNG de dinámica por nodo conserva la semilla original `seed+7919·(g+1)`. Se sortea siempre un uniforme por destino (independiente del estado del destino) ⇒ el resultado no depende de la partición ni de los hilos.
  3. Los saltos se resuelven por el **dueño del destino**, en orden canónico (destino, origen): gana el origen de menor índice. Los saltos ya no encadenan dentro del mismo día (usan el estado posterior a la dinámica).
  4. `--replicas R` clona la red R veces (copias independientes, `g = r·11 + b`). **Es carga SINTÉTICA** y debe declararse así. Con R>1 el CSV suma las réplicas por municipio base.
  5. Cada proceso solo guarda RNG de los nodos que posee.
* Efecto de (2)-(3): la secuencia de saltos cambió respecto a la medición vieja (COVID, foco Yacuiba, 101 inf., máscara 30 %, movilidad 3 %, 60 días, semilla 42). Hoy, nueva medida real: llegadas Tarija d2, Bermejo d4, Caraparí d5, Villamontes d12, S. Lorenzo d17, El Puente d18, Padcaya d28, Entre Ríos d29, Uriondo d35, Yunchará d51 (10 saltos; 6 de 10 cruzan de worker con partición `geo`).
* Modo local: `./epidemic_sim --mode net [--threads T] [--replicas R] …` imprime `[ENLACES]` (`[dia,origen,destino,casos,cruza]`, cruza=0), `[NET_RESUMEN]`, `[MOVILIDAD]`, `[EXITO]`.

## A.4 Modo MPI `net` (NUEVO, hecho) — Master y Workers trabajan de verdad
Archivos: `src/parallel/distributed/{NetTypes.hpp,NetPartition.hpp,NetMaster.*,NetWorker.*}`, `src/mpi_main.cpp` (flag `--mode net|agents`, `--partition`, `--replicas`, `--seed-node`, `--nodes`; el modo se lee en TODOS los ranks).
* **Partición** (`--partition geo|balanced|1,2,2,…`, defecto `geo`): Worker 1 = Yacuiba, Villamontes, Entre Ríos, Caraparí (**180 083 hab.**); Worker 2 = Tarija, Bermejo, S. Lorenzo, Padcaya, Uriondo, El Puente, Yunchará (**354 265 hab.**). `balanced` = reparto voraz por población.
* **Master (rank 0), no simula:** carga la red solo para guardar estado/partición; `MPI_Bcast(NetConfigMPI)` (config + partición + ruta del CSV); recibe `NetWorkerInfo` (host, pid) y cada día `NetTelemetry` + `NetNodeMetric[]` + `SpreadMessage[]` de cada worker; `MPI_Barrier`; arma frame, CSV y líneas `[NET_INFO] [NET_TELEMETRIA] [NET_MASTER] [ENLACES](5.º campo = cruza worker) [NET_RESUMEN]`.
* **Worker 1/2:** simulan solo sus municipios con OpenMP. Cada día: (1) `MPI_Sendrecv` del vector de infectados (N uint64) con el otro worker; (2) contagio original local; (3) sorteo de saltos; los de destino propio se aplican local, los que cruzan viajan como `SpreadMessage` POD por `MPI_Sendrecv` (conteo primero, datos después); (4) resolución en el dueño del destino; (5) informe al Master; (6) `MPI_Barrier`. Miden `compute_ms`, `comm_ms`, saltos y mensajes/bytes MPI enviados/recibidos.
* **Reproducibilidad comprobada** con `make test_net_equal`: CSV de 1 proceso == CSV de 3 procesos para `geo`, `balanced` y una lista alterna, con R=1 y R=20; 3 repeticiones MPI dan el mismo md5; 1 hilo == 4 hilos.
* El modo antiguo de agentes A/B (`--mode agents` / por defecto en `mpi_main`) **sigue en el código** (no se eliminó nada) y compila/corre.

## A.5 API y visor
* `POST /api/game/tarija` con `mode:"net"` (local) o `mode:"mpi"` (clúster). Para `mpi` elige, en orden: **(1) `docker`** (`docker compose exec -T -w /workspace master mpirun --hostfile infrastructure/hosts.ini -np 3 …`, rutas `/workspace/…`), **(2) `local`** (`mpirun --oversubscribe -np 3 ./epidemic_sim_mpi`, “clúster simulado en 1 máquina”), **(3) `shim`** (`epidemic_sim_mpi_shim`, procesos de prueba, NO MPI real). Responde `cluster:{mode,kind,label,workers[],summary}` y por frame `workers[]` (compute_ms, comm_ms, saltos y msgs/bytes env/rec) y `master{wait_ms,assemble_ms}`; `links = [origen,destino,casos,cruza]`.
* `game2d.html`: selector **5 · Ejecución** (Local OpenMP / Clúster MPI); banner con el tipo real de ejecución (verde docker, ámbar local, rojo shim); **panel de 3 tarjetas** (Master, Worker 1, Worker 2: estado, municipios, ms de cómputo/comunicación día y acumulado, mensajes y saltos MPI, bytes, barra de ocupación, LED pixel); **borde y tinte de cada municipio con el color de su worker** (W1 naranja `#fb923c`, W2 rosa `#f472b6`, Master blanco); saltos internos ámbar, **mensaje MPI entre workers cian** con paquete cian/rojo; tooltip “Lo simula: Worker N”; desglose total = cómputo + comunicación + espera.
* Visor: `python3 web/backend/server.py` → `http://localhost:8088/game2d.html`. Probado en Chromium headless: modo local y modo clúster sin errores de JS.

## A.6 Entorno de pruebas sin OpenMPI (`tests/mpi_shim/mpi.h`)
Shim **solo de pruebas** (procesos con `fork` + socketpairs; implementa Init/rank/size/Send/Recv/Sendrecv/Bcast/Barrier/Wtime/Abort/Finalize). Targets: `make epidemic_sim_mpi_shim`, `make test_net_equal`. **No es MPI real ni sus tiempos sirven para el Hito 3.**

## A.7 Hito 3 (conservado, real)
`results/benchmarks/scalability_report.csv`, `INFORME_HITO3_EVALUACION.md`, `results/simulations/bench_*.csv` (agentes; Ryzen 5 5600G, Docker Ubuntu 24.04, 25 días, seed 42): N=10k Sec 35.44 ms / MPI 1397.25 ms (0.03×); N=50k Sec 168.01 ms, OpenMP 2 hilos 119.93 ms (1.40×, E=70 %); N=250k Sec 1952.41 ms, OpenMP 1 hilo 5769.26 ms, 4 hilos 2617.10 ms (2.20× vs 1 hilo).

## A.8 Qué NO se pudo verificar (sé honesto con el usuario)
En el entorno de la sesión **no había `mpicxx`, `mpirun` ni Docker**. El código MPI se compiló (0 warnings con `-Wall -Wextra -pedantic`) y se ejecutó **solo con el shim**. Falta probar con OpenMPI real: `make mpi` (con `mpicxx`) y `mpirun`. Todo número de MPI real debe medirse allí.

---

# PARTE B — LO QUE FALTA

## B.1 Validar con OpenMPI real (primero)
1. `docker compose up -d --build` → `docker compose exec master make clean && make && make mpi` (sin warnings).
2. `mpirun --allow-run-as-root --hostfile infrastructure/hosts.ini -np 3 ./epidemic_sim_mpi --mode net <args de NET_ARGS del Makefile> --output results/simulations/test_net_mpi.csv` y comparar con `./epidemic_sim --mode net --threads 1 …` (`cmp`). Debe ser idéntico.
3. Abrir el visor en modo clúster y confirmar banner verde “Clúster Docker real” y `host` = `worker1`/`worker2` en las tarjetas.
Si falla algo por MPI real (p. ej. `MPI_Sendrecv` con tamaños grandes, tamaño del `int` de conteo para R grandes), corregir y repetir.

## B.2 Hito 3 — evaluación del modo `net` (datos reales)
* Conservar el benchmark de agentes y su informe.
* Añadir al `scripts/run_benchmarks.py` mediciones de `--mode net`: 1 proceso vs MPI 3 procesos; OpenMP 1/2/4 hilos; `--replicas` R ∈ {1, 10, 100, 1000} (**carga sintética, declararlo**). `S(p)=T(1)/T(p)`, `E(p)=S(p)/p` desde los CSV/`[NET_RESUMEN]` (`compute_ms`, `comm_ms`, `total_ms`). Varias repeticiones, hardware y semilla indicados.
* Honestidad esperada: con R=1 (11 municipios) el costo de MPI domina y no escala; con R grande puede aparecer ganancia. Comparar partición `geo` vs `balanced`. Discutir Amdahl/Gustafson, 3 contenedores compitiendo por RAM y núcleos de una sola máquina, y que en un clúster físico con red real el costo de comunicación cambia.
* Actualizar `scalability_report.csv` e `INFORME_HITO3_EVALUACION.md` con tablas y gráficos reales.

## B.3 Limpieza (diferida por pedido del usuario; hacerla cuando lo pida)
Método: `grep -rn` de referencias antes de borrar; compilar y probar `/game2d.html` tras cada bloque; no tocar `results/benchmarks/` ni `results/simulations/bench_*.csv`.
* Borrar: `three.min.js`, `OrbitControls.js`, `simulator3d.js`, `assets/yacuiba_map.png`; `index.html`/`app.js`/`style.css` (dashboard viejo 3D) o reemplazarlos; `/api/game/yacuiba` + `handle_game_yacuiba`, `SNAP_DIR`, `results/snapshots/`; `src/utils/SnapshotWriter.hpp` y flags `--snapshot/--sample`/mención Godot; salidas sueltas en `results/simulations/` (salvo `bench_*`) + `.gitignore`; endpoints sin uso (`/api/history`, `/api/municipios`…).
* **`infrastructure/ssh/id_rsa` (clave privada en el repo):** generar el par en `Dockerfile.cluster`/`entrypoint.sh` y no versionarlo.
* Decidir con el usuario si se elimina el modo de agentes A/B (`MPIMaster/MPIWorker`, `--mode agents`) o se deja como línea base del Hito 3 (hoy se deja).
* Reescribir `README.md` (sin 3D, sin Godot, sin A/B; documentar el modo `net` MPI, partición, shim y reproducibilidad).

## B.4 Checklist de aceptación
- [x] `epidemic_sim --mode net` y `epidemic_sim_mpi --mode net` (3 procesos) dan el mismo CSV con la misma semilla *(verificado con el shim; falta con OpenMPI real → B.1)*.
- [x] El visor muestra Master/Worker1/Worker2 trabajando (ms, mensajes, municipios propios) y diferencia saltos internos vs cruce MPI.
- [x] Contagio intacto; `kImportEfficiency=0.4`.
- [x] Compila local y MPI-shim sin warnings.
- [ ] Validado con `mpicxx`/`mpirun`/Docker reales.
- [ ] Benchmarks del modo `net` regenerados con datos reales + informe Hito 3 actualizado.
- [ ] Limpieza (B.3) y README reescrito; sin claves privadas en el repo.

---

# PARTE C — REGLAS PERMANENTES
1. **2D pixel-art:** nada de 3D (Three.js/Godot) ni imagen JPG estirada; cada municipio es una región de la grilla.
2. **El contagio no se toca:** solo se calibra `kImportEfficiency` si el salto parece rápido/lento.
3. **Edades:** los 4 grupos suman siempre 100 %.
4. **Calibración realista:** mascarilla y β moderados (no 100 % de golpe).
5. **Paleta fija:** verde, rojo, azul, negro. Saltos: ámbar (internos) / cian (mensaje MPI entre workers). Workers: Master blanco, W1 naranja, W2 rosa.
6. **Datos reales siempre:** cada tiempo/speedup/eficiencia sale de una corrida medida, con hardware y semilla; los tiempos del shim NO cuentan.
7. **Reproducibilidad:** cualquier cambio al motor debe mantener `make test_net_equal` en verde (1 proceso == 3 procesos, y 1 hilo == N hilos).
8. **Ejecución:** `make` → `python3 web/backend/server.py` → `http://localhost:8088/game2d.html`. Clúster: `docker compose up -d --build`, `docker compose exec master make mpi`. Sin MPI instalado: `make epidemic_sim_mpi_shim` (solo pruebas).
9. **No borrar nada sin que el usuario lo pida** (ver B.3).
