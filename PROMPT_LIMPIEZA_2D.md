# PROMPT — LIMPIEZA: DEJAR SOLO LA INTERFAZ 2D PIXEL-ART
> Pégalo en una conversación nueva junto con el zip del proyecto y `PROMPT_PROYECTO_V4.md`. El usuario YA autoriza borrar lo listado aquí; nada más.

## Rol y reglas
Ingeniero de software senior. Español, directo. **No inventar resultados:** después de cada bloque, compilar y probar de verdad, e informar lo que se ejecutó y lo que no.
Objetivo: el proyecto solo debe tener la interfaz nueva **`web/frontend/game2d.html`** (2D pixel-art de Tarija). Debe desaparecer la interfaz antigua con 3D/Three.js.

## Método (en cada bloque)
1. `grep -rn` de cada archivo/símbolo antes de borrarlo, para no dejar referencias rotas.
2. Borrar → `make clean && make` (0 warnings) → `make test_net_equal` en verde → `python3 web/backend/server.py` y comprobar `curl -s -o /dev/null -w "%{http_code}" http://localhost:8088/game2d.html` y `POST /api/game/tarija` (`mode:"net"` y `mode:"mpi"`).
3. NO tocar: `results/benchmarks/`, `results/simulations/bench_*.csv`, el contagio de `NetworkEngine`, `kImportEfficiency=0.4`, ni los scripts de hitos (`smoke_test.sh`, `benchmark.sh`, `benchmark_net.py`, `start.sh`).

## Bloque 1 — Frontend: quitar el 3D
Borrar: `web/frontend/js/three.min.js`, `js/OrbitControls.js`, `js/simulator3d.js`, `js/app.js`, `css/style.css`, `assets/yacuiba_map.png`, y `assets/tarija_map.jpg` si `grep` confirma que `game2d.*` no lo usa.
`web/frontend/index.html` (dashboard 3D viejo): reemplazarlo por una redirección mínima a `/game2d.html`, para que `http://localhost:8088/` abra la interfaz nueva. Comprobar que `game2d.html` no depende de nada borrado (`grep -n "three\|OrbitControls\|simulator3d\|app.js\|style.css" web/frontend/game2d.html`).

## Bloque 2 — Backend: quitar endpoints viejos
En `web/backend/server.py` borrar `handle_game_yacuiba` y su ruta `/api/game/yacuiba`, `SNAP_DIR` y su `os.makedirs`, y los endpoints que `game2d.js` no llama (`/api/history`, `/api/municipios`, `/api/simulate`, `/api/benchmark`, `/api/cluster`), verificándolo antes con `grep -n "fetch(" web/frontend/js/game2d.js`. Conservar `/api/epidemics`, `/api/game/tarija` y `_mpi_command`. Borrar `results/snapshots/`.

## Bloque 3 — C++: quitar lo de Godot/3D
Eliminar `src/utils/SnapshotWriter.hpp`, los flags `--snapshot/--sample` y toda mención a Godot/snapshots en `src/main.cpp` y `README.md`. **Preguntar antes** de eliminar el modo de agentes A/B (`MPIMaster/MPIWorker`, `--mode agents`, `--mode seq|omp`): hoy son la línea base del Hito 3 y por defecto se conservan.

## Bloque 4 — Resultados sueltos y repo
Borrar de `results/simulations/` todo lo que no sea `bench_*` (`run_*.csv`, `cluster_mpi_50k.csv`, `sequential_results.csv`, `game_tarija_*.csv`, `test_*.csv`). Crear `.gitignore` con: `*.o`, `epidemic_sim`, `epidemic_sim_mpi`, `epidemic_sim_mpi_shim`, `__pycache__/`, `results/simulations/*` y la excepción `!results/simulations/bench_*`.

## Bloque 5 — Seguridad
`infrastructure/ssh/id_rsa` es una clave privada versionada: generar el par en `Dockerfile.cluster` (`ssh-keygen -t ed25519 -N ""`, `authorized_keys` = clave pública) y quitar `infrastructure/ssh/id_rsa*` del repo con los `COPY` correspondientes. Para que los 3 contenedores compartan la clave, poner `image: epidemic-node` en los tres servicios de `docker-compose.yml` (se construye una sola vez). Verificar: `docker compose up -d --build` → `bash scripts/smoke_test.sh` dentro de `master` (SSH sin contraseña a worker1/worker2).

## Bloque 6 — README
Reescribir `README.md`: simulador de Tarija 2D, modo `net` (OpenMP y MPI Master/Worker), particiones, reproducibilidad (`make test_net_equal`), arranque con `./scripts/start.sh` y estructura de carpetas de los Hitos 2/3. Sin 3D, sin Godot, sin A/B (si se eliminó).

## Entrega
Zip final sin binarios ni `.o`, y lista exacta de: qué se borró, qué se probó (con salida real) y qué no se pudo probar (p. ej. Docker/OpenMPI si no estaban disponibles).
