#!/usr/bin/env bash
# Hito 3 - benchmark reproducible, sin intervencion manual.
#   ./scripts/benchmark.sh            # usa el cluster Docker si hay Docker; si no, corre local
#   REPS=3 ./scripts/benchmark.sh     # menos repeticiones
set -euo pipefail
cd "$(dirname "$0")/.."
REPS="${REPS:-5}"
ARGS=(--reps "$REPS")

if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  echo "[benchmark] Levantando cluster (1 master + 2 workers)..."
  docker compose up -d --build
  docker compose exec -T master bash -c "cd /workspace && make clean && make && make mpi && bash scripts/smoke_test.sh"
  docker compose exec -T master bash -c "cd /workspace && python3 scripts/benchmark_net.py --mpi cluster ${ARGS[*]}"
else
  echo "[benchmark] Docker no disponible: se ejecuta en esta maquina (MPI solo si hay mpirun)."
  make
  command -v mpicxx >/dev/null 2>&1 && make mpi || echo "[benchmark] sin mpicxx: se omiten las filas MPI"
  python3 scripts/benchmark_net.py --mpi auto "${ARGS[@]}"
fi
echo "[benchmark] CSV: results/benchmarks/net_scalability.csv"
