#!/usr/bin/env bash
# Arranque unico del visor 2D. Elige la mejor forma de ejecutar el modo "Cluster MPI":
#   1) Docker real (master + worker1 + worker2)   2) OpenMPI local (mpirun --oversubscribe)   3) shim de pruebas (NO es MPI real)
# Uso: ./scripts/start.sh            -> compila, prepara MPI y abre el servidor en http://localhost:8088/game2d.html
#      ./scripts/start.sh --no-server -> solo prepara y dice que modo MPI quedo disponible
set -uo pipefail
cd "$(dirname "$0")/.."
NO_SERVER=0; [ "${1:-}" = "--no-server" ] && NO_SERVER=1

echo "[start] Compilando motor local (OpenMP)..."
make || { echo "[start] ERROR: fallo make (necesitas g++ con C++20 y OpenMP)"; exit 1; }

MODE=none
if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  echo "[start] Docker detectado: levantando cluster (1 master + 2 workers)..."
  if docker compose up -d --build; then
    make clean-obj >/dev/null          # que el contenedor compile desde cero (otro compilador/libc)
    if docker compose exec -T master bash -c "cd /workspace && make mpi"; then MODE=docker; fi
    make clean-obj >/dev/null          # el host vuelve a compilar con su propio g++ cuando haga falta
  fi
fi
if [ "$MODE" = none ] && command -v mpicxx >/dev/null 2>&1 && command -v mpirun >/dev/null 2>&1; then
  echo "[start] OpenMPI local detectado: compilando epidemic_sim_mpi..."
  make mpi && MODE=local
fi
if [ "$MODE" = none ]; then
  echo "[start] AVISO: no hay Docker ni OpenMPI. Se usa el SHIM de pruebas (procesos fork, NO es MPI real; sus tiempos no sirven para el informe)."
  make epidemic_sim_mpi_shim && MODE=shim
fi

case "$MODE" in
  docker) echo "[start] Modo MPI: CLUSTER DOCKER REAL (banner verde en el visor)";;
  local)  echo "[start] Modo MPI: mpirun local, cluster simulado en 1 maquina (banner ambar)";;
  shim)   echo "[start] Modo MPI: SHIM de pruebas (banner rojo)";;
  *)      echo "[start] No se pudo preparar ningun modo MPI; solo funcionara 'Local OpenMP'";;
esac
[ "$NO_SERVER" = 1 ] && exit 0
echo "[start] Abre http://localhost:8088/game2d.html"
exec python3 web/backend/server.py
