#include "NetWorker.hpp"
#include "parallel/NetworkEngine.hpp"
#include "core/SimulationConfig.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <vector>

namespace {
constexpr int TAG_I = 210, TAG_CNT = 211, TAG_DATA = 212, TAG_TEL = 213, TAG_MET = 214, TAG_EVT = 215;
}

void NetWorker::run() {
    // 1) Configuracion y particion difundidas por el Master (MPI_Bcast)
    NetConfigMPI cfg{};
    MPI_Bcast(&cfg, sizeof(NetConfigMPI), MPI_BYTE, 0, comm_);

    SimulationConfig sc;
    sc.initial_infected = cfg.initial_infected; sc.seed = cfg.seed;
    sc.beta = cfg.beta; sc.gamma = cfg.gamma; sc.mobility_rate = cfg.mobility_rate;
    sc.vaccination_rate = cfg.vaccination_rate; sc.mask_usage_rate = cfg.mask_usage_rate;
    sc.mask_efficiency = cfg.mask_efficiency; sc.contacts_per_day = cfg.contacts_per_day;
    sc.num_days = cfg.num_days; sc.infectious_min = cfg.infectious_min; sc.infectious_max = cfg.infectious_max;
    if (sc.infectious_max < sc.infectious_min) sc.infectious_max = sc.infectious_min;

    NetworkEngine eng(sc, cfg.omp_threads, cfg.replicas);
    if (!eng.load_nodes(cfg.nodes_path)) {
        std::cerr << "[WORKER " << rank_ << "] no se pudo leer " << cfg.nodes_path << "\n";
        MPI_Abort(comm_, 2);
    }
    std::vector<int> owner(cfg.owner, cfg.owner + cfg.n_base);
    eng.initialize(cfg.seed_node, owner, rank_);

    const int peer = (rank_ == 1) ? 2 : 1;
    const size_t N = eng.total_nodes();

    // 2) Presentacion al Master
    NetWorkerInfo info{};
    info.rank = rank_; info.pid = static_cast<int>(getpid()); info.omp_threads = cfg.omp_threads;
    for (int b = 0; b < cfg.n_base; ++b) if (owner[b] == rank_) ++info.owned_base;
    gethostname(info.host, sizeof(info.host) - 1);
    MPI_Send(&info, sizeof(info), MPI_BYTE, 0, TAG_TEL, comm_);

    std::vector<uint64_t> mine, theirs(N), all(N);
    MPI_Status st;

    for (uint32_t day = 1; day <= cfg.num_days; ++day) {
        NetTelemetry tel{};
        tel.rank = rank_; tel.day = day;
        double t_compute = 0.0, t_comm = 0.0;

        // --- cómputo: infectados propios al inicio del dia
        double t0 = MPI_Wtime();
        eng.collect_local_infected(mine);
        t_compute += MPI_Wtime() - t0;

        // --- comunicación 1: vector de infectados (MPI_Sendrecv simétrico con el otro worker)
        t0 = MPI_Wtime();
        MPI_Sendrecv(mine.data(), static_cast<int>(N * sizeof(uint64_t)), MPI_BYTE, peer, TAG_I,
                     theirs.data(), static_cast<int>(N * sizeof(uint64_t)), MPI_BYTE, peer, TAG_I, comm_, &st);
        t_comm += MPI_Wtime() - t0;
        tel.msgs_sent++; tel.msgs_recv++; tel.bytes_sent += N * 8; tel.bytes_recv += N * 8;
        for (size_t g = 0; g < N; ++g) all[g] = mine[g] + theirs[g];   // cada nodo tiene un unico dueño

        // --- cómputo: contagio original + sorteo de saltos
        t0 = MPI_Wtime();
        eng.phase_dynamics(all);
        std::vector<SpreadEvent> cands;
        eng.phase_draw_jumps(cands);
        std::vector<SpreadMessage> out, local;
        for (const auto& c : cands) {
            const SpreadMessage m{c.day, c.from, c.to, static_cast<uint32_t>(c.cases)};
            (owner[static_cast<size_t>(c.to) % cfg.n_base] == rank_ ? local : out).push_back(m);
        }
        t_compute += MPI_Wtime() - t0;

        // --- comunicación 2: saltos que cruzan de worker (conteo primero, datos después)
        uint64_t n_out = out.size(), n_in = 0;
        t0 = MPI_Wtime();
        MPI_Sendrecv(&n_out, sizeof(n_out), MPI_BYTE, peer, TAG_CNT, &n_in, sizeof(n_in), MPI_BYTE, peer, TAG_CNT, comm_, &st);
        std::vector<SpreadMessage> in(n_in);
        MPI_Sendrecv(out.data(), static_cast<int>(n_out * sizeof(SpreadMessage)), MPI_BYTE, peer, TAG_DATA,
                     in.data(), static_cast<int>(n_in * sizeof(SpreadMessage)), MPI_BYTE, peer, TAG_DATA, comm_, &st);
        t_comm += MPI_Wtime() - t0;
        tel.msgs_sent += 2; tel.msgs_recv += 2;
        tel.bytes_sent += 8 + n_out * sizeof(SpreadMessage); tel.bytes_recv += 8 + n_in * sizeof(SpreadMessage);
        tel.jumps_sent = n_out; tel.jumps_received = n_in;

        // --- cómputo: resolver saltos con destino propio (locales + recibidos)
        t0 = MPI_Wtime();
        std::vector<SpreadEvent> mine_c;
        for (const auto& m : local) mine_c.push_back({m.day, m.from, m.to, m.cases});
        for (const auto& m : in)    mine_c.push_back({m.day, m.from, m.to, m.cases});
        eng.phase_resolve_jumps(std::move(mine_c));
        t_compute += MPI_Wtime() - t0;

        // --- informe al Master: telemetría + métricas por municipio propio + saltos resueltos
        std::vector<NetNodeMetric> met;
        met.reserve(eng.owned().size());
        for (int g : eng.owned()) {
            const auto& n = eng.nodes()[g];
            met.push_back({static_cast<uint32_t>(g), n.S, n.I, n.R, n.V, n.new_infections});
        }
        std::vector<SpreadMessage> evs;
        for (const auto& e : eng.events()) evs.push_back({e.day, e.from, e.to, static_cast<uint32_t>(e.cases)});

        tel.n_nodes = static_cast<uint32_t>(met.size());
        tel.n_events = static_cast<uint32_t>(evs.size());
        tel.compute_ms = t_compute * 1000.0;
        tel.comm_ms = t_comm * 1000.0;
        tel.msgs_sent += 3;
        tel.bytes_sent += sizeof(tel) + met.size() * sizeof(NetNodeMetric) + evs.size() * sizeof(SpreadMessage);

        MPI_Send(&tel, sizeof(tel), MPI_BYTE, 0, TAG_TEL, comm_);
        MPI_Send(met.data(), static_cast<int>(met.size() * sizeof(NetNodeMetric)), MPI_BYTE, 0, TAG_MET, comm_);
        MPI_Send(evs.data(), static_cast<int>(evs.size() * sizeof(SpreadMessage)), MPI_BYTE, 0, TAG_EVT, comm_);

        MPI_Barrier(comm_);   // fin del dia: nadie avanza hasta que todos terminaron
    }
}
