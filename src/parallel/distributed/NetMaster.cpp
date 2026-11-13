#include "NetMaster.hpp"
#include "NetPartition.hpp"
#include "parallel/NetworkEngine.hpp"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace {
constexpr int TAG_TEL = 213, TAG_MET = 214, TAG_EVT = 215;
}

int NetMaster::run() {
    const double t_start = MPI_Wtime();

    // 1) El Master carga la red solo para conocer municipios/particion y guardar el estado (no simula)
    NetworkEngine eng(config_, threads_, replicas_);
    if (!eng.load_nodes(nodes_path_)) { MPI_Abort(comm_, 2); return 2; }
    const size_t B = eng.base_size();
    if (B > static_cast<size_t>(kNetMaxBase)) { std::cerr << "[ERROR] Demasiados municipios\n"; MPI_Abort(comm_, 2); return 2; }
    const std::vector<int> owner = make_partition(eng.base_nodes(), partition_);

    NetConfigMPI cfg{};
    cfg.initial_infected = config_.initial_infected; cfg.seed = config_.seed;
    cfg.beta = config_.beta; cfg.gamma = config_.gamma; cfg.mobility_rate = config_.mobility_rate;
    cfg.vaccination_rate = config_.vaccination_rate; cfg.mask_usage_rate = config_.mask_usage_rate;
    cfg.mask_efficiency = config_.mask_efficiency; cfg.contacts_per_day = config_.contacts_per_day;
    cfg.num_days = config_.num_days; cfg.infectious_min = config_.infectious_min; cfg.infectious_max = config_.infectious_max;
    cfg.omp_threads = threads_; cfg.seed_node = seed_node_; cfg.replicas = replicas_; cfg.n_base = static_cast<int32_t>(B);
    for (size_t b = 0; b < B; ++b) cfg.owner[b] = static_cast<int8_t>(owner[b]);
    std::snprintf(cfg.nodes_path, sizeof(cfg.nodes_path), "%s", nodes_path_.c_str());

    std::cout << "========================================================================\n"
              << " CLUSTER MPI: RED DE MUNICIPIOS DE TARIJA\n"
              << " MASTER (rank 0) coordina | Worker 1 y Worker 2 simulan | hilos OpenMP/worker: " << threads_ << "\n"
              << " Municipios base: " << B << " | replicas: " << replicas_ << " | particion: " << partition_ << "\n"
              << "========================================================================\n";

    MPI_Bcast(&cfg, sizeof(NetConfigMPI), MPI_BYTE, 0, comm_);   // configuracion + particion a todos
    eng.initialize(seed_node_, owner, -1);                       // estado del dia 0 (identico al de los workers)

    // 2) Presentacion de los workers
    NetWorkerInfo winfo[3]{};
    MPI_Status st;
    for (int w = 1; w <= 2; ++w) MPI_Recv(&winfo[w], sizeof(NetWorkerInfo), MPI_BYTE, w, TAG_TEL, comm_, &st);

    uint64_t pop_w[3] = {0, 0, 0};
    std::ostringstream info;
    info << "{\"modo\":\"mpi\",\"procesos\":3,\"hilos\":" << threads_ << ",\"replicas\":" << replicas_
         << ",\"particion\":\"" << partition_ << "\",\"workers\":[";
    for (int w = 1; w <= 2; ++w) {
        info << (w > 1 ? "," : "") << "{\"rank\":" << w << ",\"host\":\"" << winfo[w].host << "\",\"pid\":" << winfo[w].pid
             << ",\"municipios\":[";
        bool first = true;
        for (size_t b = 0; b < B; ++b) if (owner[b] == w) {
            info << (first ? "" : ",") << eng.base_nodes()[b].id; first = false;
            pop_w[w] += eng.base_nodes()[b].population;
        }
        info << "],\"poblacion\":" << pop_w[w] << "}";
    }
    info << "]}";
    std::cout << "[NET_INFO] " << info.str() << "\n";
    std::cout << "[MASTER] Particion difundida con MPI_Bcast: Worker1=" << pop_w[1] << " hab., Worker2=" << pop_w[2] << " hab.\n";

    // 3) Bucle de dias: recibir de cada worker, barrera, armar frame
    std::vector<std::vector<NetNode>> history;
    history.push_back(eng.nodes());
    std::vector<SpreadEvent> links;
    std::vector<NetTelemetry> tels;
    std::ostringstream master_tel;
    uint64_t total_jumps = 0, total_cross = 0;
    double sum_compute = 0.0, sum_comm = 0.0, sum_wait = 0.0;

    for (uint32_t day = 1; day <= config_.num_days; ++day) {
        const double t_wait0 = MPI_Wtime();
        auto& nodes = eng.nodes_mutable();
        std::vector<SpreadEvent> day_events;
        double max_comp = 0.0, max_comm = 0.0;
        for (int w = 1; w <= 2; ++w) {
            NetTelemetry tel{};
            MPI_Recv(&tel, sizeof(tel), MPI_BYTE, w, TAG_TEL, comm_, &st);
            std::vector<NetNodeMetric> met(tel.n_nodes);
            std::vector<SpreadMessage> evs(tel.n_events);
            MPI_Recv(met.data(), static_cast<int>(met.size() * sizeof(NetNodeMetric)), MPI_BYTE, w, TAG_MET, comm_, &st);
            MPI_Recv(evs.data(), static_cast<int>(evs.size() * sizeof(SpreadMessage)), MPI_BYTE, w, TAG_EVT, comm_, &st);
            for (const auto& m : met) { auto& n = nodes[m.gid]; n.S = m.S; n.I = m.I; n.R = m.R; n.V = m.V; n.new_infections = m.new_inf; }
            for (const auto& e : evs) day_events.push_back({e.day, e.from, e.to, e.cases});
            tels.push_back(tel);
            max_comp = std::max(max_comp, tel.compute_ms);
            max_comm = std::max(max_comm, tel.comm_ms);
        }
        MPI_Barrier(comm_);
        const double wait_ms = (MPI_Wtime() - t_wait0) * 1000.0;

        const double t_asm0 = MPI_Wtime();
        std::sort(day_events.begin(), day_events.end(), [](const SpreadEvent& a, const SpreadEvent& b) { return a.to < b.to; });
        history.push_back(nodes);
        for (const auto& e : day_events) {
            ++total_jumps;
            if (owner[static_cast<size_t>(e.from) % B] != owner[static_cast<size_t>(e.to) % B]) ++total_cross;
            if (replicas_ == 1) links.push_back(e);
        }
        const double asm_ms = (MPI_Wtime() - t_asm0) * 1000.0;
        sum_compute += max_comp; sum_comm += max_comm; sum_wait += wait_ms;
        master_tel << (day > 1 ? "," : "") << "[" << day << "," << std::fixed << std::setprecision(4) << wait_ms << "," << asm_ms << "]";
    }
    const double total_ms = (MPI_Wtime() - t_start) * 1000.0;

    // 4) Salidas para el servidor web / los benchmarks
    std::cout << "[ENLACES] [";
    for (size_t k = 0; k < links.size(); ++k) {
        const bool cross = owner[static_cast<size_t>(links[k].from) % B] != owner[static_cast<size_t>(links[k].to) % B];
        std::cout << (k ? "," : "") << "[" << links[k].day << "," << links[k].from << "," << links[k].to << ","
                  << links[k].cases << "," << (cross ? 1 : 0) << "]";
    }
    std::cout << "]\n[NET_TELEMETRIA] [";
    for (size_t k = 0; k < tels.size(); ++k) {
        const auto& t = tels[k];
        std::cout << (k ? "," : "") << "[" << t.day << "," << t.rank << "," << std::fixed << std::setprecision(4) << t.compute_ms << ","
                  << t.comm_ms << "," << t.jumps_sent << "," << t.jumps_received << "," << t.msgs_sent << ","
                  << t.msgs_recv << "," << t.bytes_sent << "," << t.bytes_recv << "]";
    }
    std::cout << "]\n[NET_MASTER] [" << master_tel.str() << "]\n";
    {
        std::cout << "[MOVILIDAD] [";
        const auto& P = eng.mobility();
        for (size_t i = 0; i < P.size(); ++i) {
            std::cout << (i ? "," : "") << "[";
            for (size_t j = 0; j < P[i].size(); ++j) std::cout << (j ? "," : "") << std::setprecision(6) << P[i][j];
            std::cout << "]";
        }
        std::cout << "]\n";
    }
    uint64_t peak = 0; uint32_t peak_day = 0;
    for (size_t d = 0; d < history.size(); ++d) {
        uint64_t tot = 0;
        for (const auto& nd : history[d]) tot += nd.I;
        if (tot > peak) { peak = tot; peak_day = static_cast<uint32_t>(d); }
    }
    std::cout << "[NET_RESUMEN] {\"modo\":\"mpi\",\"procesos\":3,\"hilos\":" << threads_ << ",\"replicas\":" << replicas_
              << ",\"nodos\":" << eng.total_nodes() << ",\"saltos\":" << total_jumps << ",\"saltos_mpi\":" << total_cross
              << ",\"total_ms\":" << std::fixed << std::setprecision(3) << total_ms << ",\"compute_ms\":" << sum_compute
              << ",\"comm_ms\":" << sum_comm << ",\"espera_master_ms\":" << sum_wait << "}\n";
    std::cout << "[EXITO] Red de " << B << " municipios (x" << replicas_ << ") simulada en MPI (3 procesos) en "
              << std::fixed << std::setprecision(2) << total_ms << " ms | pico " << peak << " infectados en el dia " << peak_day << "\n";
    if (eng.write_csv(config_.output_file, history)) std::cout << "[INFO] Resultados guardados exitosamente en: " << config_.output_file << "\n";
    else { std::cerr << "[ERROR] No se pudo escribir " << config_.output_file << "\n"; return 1; }
    return 0;
}
