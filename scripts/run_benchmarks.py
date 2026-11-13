#!/usr/bin/env python3
import subprocess
import time
import os
import csv

WORKSPACE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUTPUT_CSV = os.path.join(WORKSPACE_DIR, "results", "benchmarks", "scalability_report.csv")

populations = [10000, 50000, 100000, 250000]
days = 25
seed = 42

modalities = [
    ("Secuencial", "./epidemic_sim", ["--mode", "seq"], 1),
    ("OpenMP (1 hilo)", "./epidemic_sim", ["--mode", "omp", "--threads", "1"], 1),
    ("OpenMP (2 hilos)", "./epidemic_sim", ["--mode", "omp", "--threads", "2"], 2),
    ("OpenMP (4 hilos)", "./epidemic_sim", ["--mode", "omp", "--threads", "4"], 4),
    ("MPI Distribuido (3 nodos)", "docker", [
        "compose", "exec", "-T", "master", "mpirun", "--allow-run-as-root",
        "--hostfile", "/workspace/infrastructure/hosts.ini", "-np", "3",
        "/workspace/epidemic_sim_mpi", "--threads", "2"
    ], 3)
]

print("================================================================================")
print(" SUITE AUTOMATIZADA DE EVALUACION DE RENDIMIENTO (HITO 3)")
print(f" Poblaciones: {populations} | Dias simulados: {days} | Semilla: {seed}")
print("================================================================================")

results = []

for pop in populations:
    print(f"\n>>> EVALUANDO ESCENARIO: Poblacion = {pop:,} individuos ({days} dias) <<<")
    t_seq = None

    for label, prog, extra_args, p_units in modalities:
        output_file = f"results/simulations/bench_{pop}_{p_units}.csv"
        
        if prog == "docker":
            cmd = ["docker"] + extra_args + [
                "--pop", str(pop), "--days", str(days), "--seed", str(seed),
                "--output", output_file
            ]
        else:
            cmd = [prog] + extra_args + [
                "--pop", str(pop), "--days", str(days), "--seed", str(seed),
                "--output", output_file
            ]

        t_start = time.perf_counter()
        try:
            res = subprocess.run(cmd, cwd=WORKSPACE_DIR, capture_output=True, text=True, timeout=120)
            elapsed_ms = (time.perf_counter() - t_start) * 1000.0

            if res.returncode != 0:
                print(f"  [ERROR] {label}: {res.stderr.strip()}")
                continue

            if label == "Secuencial":
                t_seq = elapsed_ms

            speedup = (t_seq / elapsed_ms) if (t_seq and elapsed_ms > 0) else 1.0
            efficiency = (speedup / p_units) * 100.0 if p_units > 0 else 100.0

            print(f"  * {label:28} | T = {elapsed_ms:8.2f} ms | S(p) = {speedup:5.2f}x | E(p) = {efficiency:6.1f}%")

            results.append({
                "poblacion": pop,
                "dias": days,
                "modalidad": label,
                "unidades_procesamiento": p_units,
                "tiempo_ms": round(elapsed_ms, 2),
                "speedup": round(speedup, 2),
                "eficiencia": round(efficiency, 1)
            })
        except Exception as e:
            print(f"  [EXCEPTION] {label}: {e}")

# Guardar informe CSV
with open(OUTPUT_CSV, "w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=["poblacion", "dias", "modalidad", "unidades_procesamiento", "tiempo_ms", "speedup", "eficiencia"])
    writer.writeheader()
    writer.writerows(results)

print("\n================================================================================")
print(f"[EXITO] Resultados consolidados y guardados en: {OUTPUT_CSV}")
print("================================================================================")
