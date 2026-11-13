#!/usr/bin/env python3
import http.server
import json
import os
import shutil
import subprocess
import time
import urllib.parse
import csv
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler

PORT = 8088
WORKSPACE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
FRONTEND_DIR = os.path.join(WORKSPACE_DIR, "web", "frontend")
RESULTS_DIR = os.path.join(WORKSPACE_DIR, "results", "simulations")
DATA_DIR = os.path.join(WORKSPACE_DIR, "data")

os.makedirs(RESULTS_DIR, exist_ok=True)

class EpidemicAPIHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=FRONTEND_DIR, **kwargs)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/api/epidemics":
            self.handle_epidemics()
        else:
            super().do_GET()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        content_length = int(self.headers.get("Content-Length", 0))
        post_body = self.rfile.read(content_length).decode("utf-8") if content_length > 0 else "{}"

        try:
            data = json.loads(post_body)
        except Exception:
            data = {}

        if parsed.path == "/api/game/tarija":
            self.handle_game_tarija(data)
        else:
            self.send_error(404, "Endpoint no encontrado")

    def send_json(self, status_code, payload):
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.wfile.write(json.dumps(payload).encode("utf-8"))

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    # ------------------------------------------------------------------ juego 2D
    def handle_epidemics(self):
        with open(os.path.join(DATA_DIR, "epidemics.json"), encoding="utf-8") as f:
            self.send_json(200, json.load(f))

    @staticmethod
    def _engine_args(p):
        """Traduce los parametros del juego a argumentos reales del motor C++."""
        return [
            "--days", str(max(1, min(int(p.get("days", 60)), 180))),
            "--infected", str(max(1, int(p.get("infected", 20)))),
            "--beta", str(float(p.get("beta", 0.035))),
            "--gamma", "0",
            "--min-days", str(int(p.get("infectious_min", 6))),
            "--max-days", str(int(p.get("infectious_max", 10))),
            "--contacts", str(float(p.get("contacts", 10))),
            "--mobility", str(float(p.get("mobility", 0.01))),
            "--vaccination", str(float(p.get("vaccination", 0.0))),
            "--mask", str(float(p.get("mask", 0.0))),
            "--seed", str(int(p.get("seed", 42))),
        ]

    @staticmethod
    def _docker_master_up():
        """True si el contenedor `master` del clúster Docker está corriendo."""
        if not shutil.which("docker"):
            return False
        try:
            res = subprocess.run(["docker", "compose", "ps", "--format", "json"], cwd=WORKSPACE_DIR,
                                 capture_output=True, text=True, timeout=5)
            if res.returncode != 0:
                return False
            for line in res.stdout.strip().splitlines():
                c = json.loads(line)
                if c.get("Service") == "master" and "running" in c.get("State", "").lower():
                    return True
        except Exception:
            pass
        return False

    def _mpi_command(self, common):
        """Elige DÓNDE se ejecuta el modo MPI y lo etiqueta con honestidad.
        1) docker : 3 contenedores reales (master, worker1, worker2) con mpirun + hostfile
        2) local  : mpirun --oversubscribe -np 3 en esta máquina ("clúster simulado en 1 máquina")
        3) shim   : procesos de prueba SIN OpenMPI (tests/mpi_shim; NO es MPI real)"""
        if self._docker_master_up():
            # dentro de los contenedores el proyecto vive en /workspace (volumen compartido por los 3 nodos)
            common = [("/workspace/" + a) if a.startswith(("data/", "results/")) else a for a in common]
            cmd = ["docker", "compose", "exec", "-T", "-w", "/workspace", "master", "mpirun", "--allow-run-as-root",
                   "--hostfile", "/workspace/infrastructure/hosts.ini", "-np", "3", "/workspace/epidemic_sim_mpi", "--mode", "net"] + common
            return cmd, "docker", "Clúster Docker real: 3 contenedores (master, worker1, worker2)"
        if shutil.which("mpirun") and os.path.exists(os.path.join(WORKSPACE_DIR, "epidemic_sim_mpi")):
            cmd = ["mpirun", "--oversubscribe", "--allow-run-as-root", "-np", "3", "./epidemic_sim_mpi", "--mode", "net"] + common
            return cmd, "local", "Clúster simulado en 1 máquina (mpirun --oversubscribe, 3 procesos)"
        if os.path.exists(os.path.join(WORKSPACE_DIR, "epidemic_sim_mpi_shim")):
            cmd = ["./epidemic_sim_mpi_shim", "--mode", "net"] + common
            return cmd, "shim", "Procesos de prueba SIN OpenMPI (shim de tests: NO es MPI real)"
        return None, None, None

    def handle_game_tarija(self, p):
        """Red de municipios de Tarija. mode='net': un proceso + OpenMP. mode='mpi': Master + Worker1 + Worker2."""
        mode = "mpi" if str(p.get("mode", "net")) == "mpi" else "net"
        threads = max(1, min(int(p.get("threads", 4 if mode == "net" else 2)), 64))
        stamp = f"{int(time.time() * 1000)}"
        out_csv = f"results/simulations/game_tarija_{stamp}.csv"
        common = ["--threads", str(threads), "--nodes", "data/tarija_municipios.csv",
                  "--seed-node", str(int(p.get("seed_node", 1))), "--output", out_csv] + self._engine_args(p)
        if p.get("partition"):
            common += ["--partition", str(p["partition"])]
        kind, label = "local", "Un proceso con OpenMP (" + str(threads) + " hilos)"
        if mode == "mpi":
            cmd, kind, label = self._mpi_command(common)
            if cmd is None:
                self.send_json(503, {"error": "No hay forma de ejecutar el modo MPI. Cierra el servidor y arranca con ./scripts/start.sh "
                                              "(levanta Docker, o compila make mpi, o compila el shim de pruebas). "
                                              "Manual: docker compose up -d --build && docker compose exec -T master make mpi"})
                return
        else:
            cmd = ["./epidemic_sim", "--mode", "net"] + common
        t0 = time.time()
        proc = subprocess.run(cmd, cwd=WORKSPACE_DIR, capture_output=True, text=True, timeout=120)
        elapsed = (time.time() - t0) * 1000.0
        if proc.returncode != 0:
            self.send_json(500, {"error": "Error al ejecutar el motor", "details": proc.stderr or proc.stdout})
            return
        mobility = None
        links_by_day = {}
        info = None
        resumen = None
        tele = {}      # dia -> {rank: {...}}
        mtele = {}     # dia -> {wait_ms, assemble_ms}
        for line in proc.stdout.splitlines():
            if line.startswith("[MOVILIDAD] "):
                mobility = json.loads(line[len("[MOVILIDAD] "):])
            elif line.startswith("[ENLACES] "):
                # saltos entre municipios: [dia, origen, destino, casos, cruza_worker]
                for row in json.loads(line[len("[ENLACES] "):]):
                    d, a, b, c = row[:4]
                    links_by_day.setdefault(d, []).append([a, b, c, row[4] if len(row) > 4 else 0])
            elif line.startswith("[NET_INFO] "):
                info = json.loads(line[len("[NET_INFO] "):])
            elif line.startswith("[NET_RESUMEN] "):
                resumen = json.loads(line[len("[NET_RESUMEN] "):])
            elif line.startswith("[NET_TELEMETRIA] "):
                for d, rk, comp, comm, js, jr, ms, mr, bs, br in json.loads(line[len("[NET_TELEMETRIA] "):]):
                    tele.setdefault(d, {})[rk] = {"rank": rk, "compute_ms": comp, "comm_ms": comm,
                                                  "saltos_enviados": js, "saltos_recibidos": jr,
                                                  "msgs_enviados": ms, "msgs_recibidos": mr,
                                                  "bytes_enviados": bs, "bytes_recibidos": br}
            elif line.startswith("[NET_MASTER] "):
                for d, w, a in json.loads(line[len("[NET_MASTER] "):]):
                    mtele[d] = {"wait_ms": w, "assemble_ms": a}
        days = {}
        names = {}
        with open(os.path.join(WORKSPACE_DIR, out_csv), encoding="utf-8") as f:
            for r in csv.DictReader(f):
                d = int(r["day"]); i = int(r["id"]); names[i] = r["name"]
                days.setdefault(d, {})[i] = [int(r["susceptible"]), int(r["infected"]), int(r["recovered"]),
                                             int(r["vaccinated"]), int(r["new_infections"])]
        n = len(names)
        frames = []
        for d in sorted(days):
            fr = {"day": d, "nodes": [days[d][i] for i in range(n)], "links": links_by_day.get(d, [])}
            if d in tele:
                fr["workers"] = [tele[d][rk] for rk in sorted(tele[d])]
                fr["master"] = mtele.get(d, {})
            frames.append(fr)   # nodes: [S,I,R,V,nuevos]; links: [origen,destino,casos,cruza_worker]
        tot = [sum(nd[1] for nd in fr["nodes"]) for fr in frames]
        peak = max(tot)
        cluster = {"mode": mode, "kind": kind, "label": label, "processes": 3 if mode == "mpi" else 1, "threads": threads}
        if info:
            cluster["workers"] = info.get("workers", [])
            cluster["partition"] = info.get("particion")
        if resumen:
            cluster["summary"] = resumen
        tail = [l for l in proc.stdout.strip().splitlines()
                if not l.startswith(("[MOVILIDAD]", "[ENLACES]", "[NET_"))][-2:]
        self.send_json(200, {"success": True, "elapsed_ms": round(elapsed, 1), "threads": threads, "frames": frames,
                             "mobility": mobility, "cluster": cluster,
                             "summary": {"peak_infected": peak, "peak_day": tot.index(peak)},
                             "stdout_tail": tail})

if __name__ == "__main__":
    server_address = ("0.0.0.0", PORT)
    httpd = ThreadingHTTPServer(server_address, EpidemicAPIHandler)
    print(f"[API SERVER] Servidor Web y API iniciado en http://localhost:{PORT}")
    httpd.serve_forever()
