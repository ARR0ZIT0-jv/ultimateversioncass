# Hito 2 — Checklist contra evidencia

| Requisito | Evidencia en el repo | Estado verificado |
|---|---|---|
| ≥3 nodos (1 master + 2 workers) con Docker Compose | `docker-compose.yml`, `infrastructure/Dockerfile.cluster` | Definido; **no ejecutado** en la sesión de desarrollo (sin Docker) |
| Red bridge interna, DNS, SSH sin contraseña | red `epidemic-net`, `infrastructure/ssh/`, `entrypoint.sh` | `scripts/smoke_test.sh` lo comprueba; **falta correrlo en el equipo con Docker** |
| gcc con OpenMP + OpenMPI + smoke test | `tests/smoke/omp_hello.cpp`, `mpi_hello.cpp`, `scripts/smoke_test.sh` | OpenMP: OK local. OpenMPI: pendiente de correr |
| Versión secuencial T1 del problema regional | `./epidemic_sim --mode net --threads 1` | Corre; reporta `total_ms` y `peak_rss_kb` |
| Correctitud | `make test_net_equal` (1 proceso == 3 procesos; 1 hilo == N hilos) | OK con el shim; falta con OpenMPI real |
| Huella de memoria | `peak_rss_kb` en `[NET_RESUMEN]` | Medida local (ver `results/benchmarks/net_scalability.csv` al correr el benchmark) |
| Primera versión paralela | OpenMP en `src/parallel/NetworkEngine.cpp`; MPI en `src/parallel/distributed/` | Compila sin warnings |
| Estructura oficial y README con `docker compose up -d` | `infrastructure/ src/ data/ scripts/ docs/`, README | Hecho |
