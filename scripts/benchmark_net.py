#!/usr/bin/env python3
"""Benchmark del modo `net` (Hito 3): 1 proceso (T1) vs OpenMP 2/4 hilos vs MPI 3 procesos.
Todo numero sale de una corrida real (campo total_ms de [NET_RESUMEN]); se reporta la mediana de N repeticiones.
--replicas R>1 clona la red R veces: es CARGA SINTETICA (columna carga_sintetica)."""
import argparse, csv, json, os, platform, re, shutil, statistics, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BASE = ["--days", "60", "--infected", "101", "--beta", "0.035", "--gamma", "0", "--min-days", "6",
        "--max-days", "10", "--contacts", "10", "--mobility", "0.03", "--mask", "0.3", "--seed", "42"]

def cpu_model():
    try:
        for l in open("/proc/cpuinfo"):
            if l.startswith("model name"): return l.split(":", 1)[1].strip()
    except OSError: pass
    return platform.processor() or "desconocido"

def parse(out):
    m = re.search(r"\[NET_RESUMEN\]\s*(\{.*\})", out)
    return json.loads(m.group(1)) if m else None

def run(cmd, timeout):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    if r.returncode != 0: raise RuntimeError(r.stderr.strip() or r.stdout.strip()[-300:])
    j = parse(r.stdout)
    if not j: raise RuntimeError("sin [NET_RESUMEN] en la salida")
    return j

def mpi_prefix(mode):
    hostfile = os.path.join(ROOT, "infrastructure", "hosts.ini")
    if mode == "none" or not shutil.which("mpirun") or not os.path.exists(os.path.join(ROOT, "epidemic_sim_mpi")):
        return None
    if mode == "cluster" or (mode == "auto" and subprocess.run(["getent", "hosts", "worker1"], capture_output=True).returncode == 0):
        return ["mpirun", "--allow-run-as-root", "--hostfile", hostfile, "-np", "3"], "cluster (3 contenedores)"
    return ["mpirun", "--allow-run-as-root", "--oversubscribe", "-np", "3"], "mpirun local (1 maquina, oversubscribe)"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--replicas", type=int, nargs="+", default=[1, 10, 100, 1000])
    ap.add_argument("--threads", type=int, nargs="+", default=[1, 2, 4])
    ap.add_argument("--mpi", choices=["auto", "cluster", "local", "none"], default="auto")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--out", default=os.path.join(ROOT, "results", "benchmarks", "net_scalability.csv"))
    a = ap.parse_args()

    ncpu = os.cpu_count() or 1
    hw = f"{cpu_model()} | {ncpu} CPU visibles"
    mpi = mpi_prefix(a.mpi)
    print("=" * 78); print(" BENCHMARK modo net | semilla 42 | 60 dias | repeticiones:", a.reps)
    print(" Hardware:", hw)
    print(" MPI:", mpi[1] if mpi else "NO disponible (se omite; no se inventan valores)")
    if ncpu < max(a.threads): print(f" AVISO: hilos pedidos > CPU visibles ({ncpu}); esas filas miden sobresuscripcion, no escalado.")
    print("=" * 78)

    rows = []
    def record(R, conf, procs, thr, part, p_units, times, last):
        med = statistics.median(times)
        rows.append(dict(replicas=R, carga_sintetica="si" if R > 1 else "no", config=conf, procesos=procs, hilos=thr,
                         particion=part, unidades_p=p_units, reps=len(times), mediana_total_ms=round(med, 3),
                         min_ms=round(min(times), 3), max_ms=round(max(times), 3),
                         compute_ms=last.get("compute_ms", ""), comm_ms=last.get("comm_ms", ""),
                         peak_rss_kb=last.get("peak_rss_kb", ""), hardware=hw))
        return med

    for R in a.replicas:
        print(f"\n>>> replicas = {R} ({11*R} nodos){'  [carga sintetica]' if R > 1 else ''}")
        t1 = None
        for T in a.threads:
            out = f"results/simulations/bench_net_{R}_omp{T}.csv"
            cmd = ["./epidemic_sim", "--mode", "net", "--threads", str(T), "--replicas", str(R)] + BASE + ["--output", out]
            try:
                times, last = [], {}
                for _ in range(a.reps):
                    last = run(cmd, a.timeout); times.append(last["total_ms"])
            except Exception as e:
                print(f"  [ERROR] OpenMP {T}: {e}"); continue
            conf = "Secuencial (T1)" if T == 1 else f"OpenMP {T} hilos"
            med = record(R, conf, 1, T, "-", T, times, last)
            if T == 1: t1 = med
            s = (t1 / med) if t1 else float("nan")
            print(f"  {conf:22} T={med:10.3f} ms  S={s:5.2f}  E={100*s/T:6.1f}%")
        if mpi:
            for part in ("geo", "balanced"):
                for T in (1, 2):
                    out = f"results/simulations/bench_net_{R}_mpi_{part}_{T}.csv"
                    cmd = mpi[0] + ["./epidemic_sim_mpi", "--mode", "net", "--threads", str(T), "--replicas", str(R),
                                    "--partition", part] + BASE + ["--output", out]
                    try:
                        times, last = [], {}
                        for _ in range(a.reps):
                            last = run(cmd, a.timeout); times.append(last["total_ms"])
                    except Exception as e:
                        print(f"  [ERROR] MPI {part} x{T}: {e}"); continue
                    conf = f"MPI 3 proc + OpenMP {T} (hibrido, {part})" if T > 1 else f"MPI 3 proc ({part})"
                    med = record(R, conf, 3, T, part, 2 * T, times, last)
                    s = (t1 / med) if t1 else float("nan")
                    print(f"  {conf:36} T={med:10.3f} ms  S={s:5.2f}  E={100*s/(2*T):6.1f}%  comm={last.get('comm_ms')} ms")

    # S(p) y E(p) respecto a T1 de la misma carga (misma R)
    base = {r["replicas"]: r["mediana_total_ms"] for r in rows if r["procesos"] == 1 and r["hilos"] == 1}
    for r in rows:
        t1 = base.get(r["replicas"])
        r["speedup"] = round(t1 / r["mediana_total_ms"], 3) if t1 else ""
        r["eficiencia_pct"] = round(100 * t1 / r["mediana_total_ms"] / r["unidades_p"], 1) if t1 else ""
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    cols = ["replicas", "carga_sintetica", "config", "procesos", "hilos", "particion", "unidades_p", "reps",
            "mediana_total_ms", "min_ms", "max_ms", "compute_ms", "comm_ms", "speedup", "eficiencia_pct", "peak_rss_kb", "hardware"]
    with open(a.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols); w.writeheader(); w.writerows(rows)
    print(f"\n[EXITO] {len(rows)} filas -> {a.out}")

if __name__ == "__main__":
    sys.exit(main())
