#!/usr/bin/env bash
# Smoke test del Hito 2: compilador con OpenMP, OpenMPI, red interna, SSH sin contrasena.
# Uso (desde el host):  docker compose exec -T master bash scripts/smoke_test.sh
set -u
cd "$(dirname "$0")/.."
ok=0; fail=0
check() { if "${@:2}" >/tmp/smoke.out 2>&1; then echo "  [OK]    $1"; ok=$((ok+1)); else echo "  [FALLO] $1"; sed 's/^/          /' /tmp/smoke.out | head -5; fail=$((fail+1)); fi; }

echo "== 1. Compilador y OpenMP =="
check "g++ disponible"            g++ --version
check "compila con -fopenmp"      g++ -std=c++20 -fopenmp -o /tmp/omp_hello tests/smoke/omp_hello.cpp
check "ejecuta programa OpenMP"   /tmp/omp_hello
[ -x /tmp/omp_hello ] && /tmp/omp_hello | sed 's/^/          /'

echo "== 2. OpenMPI =="
if command -v mpicxx >/dev/null; then
  check "mpicxx compila"          mpicxx -o /tmp/mpi_hello tests/smoke/mpi_hello.cpp
  check "mpirun local (-np 2)"    mpirun --allow-run-as-root --oversubscribe -np 2 /tmp/mpi_hello
else echo "  [OMITIDO] mpicxx no esta instalado en este equipo"; fi

echo "== 3. Red del cluster (solo dentro de los contenedores) =="
if getent hosts worker1 >/dev/null 2>&1; then
  for h in worker1 worker2; do
    check "resolucion de nombre $h" getent hosts $h
    check "ping a $h"               ping -c1 -W2 $h
    check "SSH sin contrasena a $h" ssh -o BatchMode=yes -o ConnectTimeout=3 $h hostname
  done
  check "mpirun en 3 nodos"         mpirun --allow-run-as-root --hostfile infrastructure/hosts.ini -np 3 /tmp/mpi_hello
  [ -s /tmp/smoke.out ] && sed 's/^/          /' /tmp/smoke.out
else echo "  [OMITIDO] no se resuelve 'worker1': no estas dentro del cluster Docker"; fi

echo; echo "Resultado: $ok OK, $fail con fallo"
[ "$fail" -eq 0 ]
