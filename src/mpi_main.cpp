#include <mpi.h>
#include "core/SimulationConfig.hpp"
#include "parallel/distributed/MPIMaster.hpp"
#include "parallel/distributed/MPIWorker.hpp"
#include "parallel/distributed/NetMaster.hpp"
#include "parallel/distributed/NetWorker.hpp"
#include <algorithm>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    // Inicialización del entorno MPI con soporte multihilo
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);

    int rank = 0, size = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size < 3) {
        if (rank == 0) {
            std::cerr << "[ERROR] El simulador distribuido requiere minimo 3 procesos MPI:\n"
                      << "  - Rank 0: MASTER\n"
                      << "  - Rank 1: WORKER 1 (Region A)\n"
                      << "  - Rank 2: WORKER 2 (Region B)\n"
                      << "Procesos actuales: " << size << ". Abortando.\n";
        }
        MPI_Finalize();
        return 1;
    }

    SimulationConfig config;
    int threads_per_worker = 2; // Por defecto 2 hilos OpenMP por Worker

    // Modo: "agents" (diseno original A/B con individuos) o "net" (red de municipios de Tarija).
    // Se lee en TODOS los ranks para que cada uno sepa que rol ejecutar.
    std::string mode = "agents", nodes_path = "data/tarija_municipios.csv", partition = "geo";
    int seed_node = 1, replicas = 1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (a == "--nodes" && i + 1 < argc) nodes_path = argv[++i];
        else if (a == "--partition" && i + 1 < argc) partition = argv[++i];
        else if (a == "--seed-node" && i + 1 < argc) seed_node = std::stoi(argv[++i]);
        else if (a == "--replicas" && i + 1 < argc) replicas = std::max(1, std::stoi(argv[++i]));
    }

    if (rank == 0) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--pop" && i + 1 < argc) { config.population_size = std::stoull(argv[++i]); }
            else if (arg == "--infected" && i + 1 < argc) { config.initial_infected = std::stoull(argv[++i]); }
            else if (arg == "--days" && i + 1 < argc) { config.num_days = std::stoul(argv[++i]); }
            else if (arg == "--beta" && i + 1 < argc) { config.beta = std::stof(argv[++i]); }
            else if (arg == "--gamma" && i + 1 < argc) { config.gamma = std::stof(argv[++i]); }
            else if (arg == "--mobility" && i + 1 < argc) { config.mobility_rate = std::stof(argv[++i]); }
            else if (arg == "--vaccination" && i + 1 < argc) { config.vaccination_rate = std::stof(argv[++i]); }
            else if (arg == "--mask" && i + 1 < argc) { config.mask_usage_rate = std::stof(argv[++i]); }
            else if (arg == "--seed" && i + 1 < argc) { config.seed = std::stoull(argv[++i]); }
            else if (arg == "--min-days" && i + 1 < argc) { config.infectious_min = static_cast<uint16_t>(std::stoul(argv[++i])); }
            else if (arg == "--max-days" && i + 1 < argc) { config.infectious_max = static_cast<uint16_t>(std::stoul(argv[++i])); }
            else if (arg == "--contacts" && i + 1 < argc) { config.contacts_per_day = std::stof(argv[++i]); }
            else if (arg == "--threads" && i + 1 < argc) { threads_per_worker = std::stoi(argv[++i]); }
            else if (arg == "--output" && i + 1 < argc) { config.output_file = argv[++i]; }
        }
    }

    apply_contact_model(config);

    if (mode == "net") {
        int rc = 0;
        if (rank == 0) rc = NetMaster(MPI_COMM_WORLD, config, threads_per_worker, seed_node, replicas, nodes_path, partition).run();
        else if (rank == 1 || rank == 2) NetWorker(MPI_COMM_WORLD, rank).run();
        MPI_Finalize();
        return rc;
    }

    if (rank == 0) {
        // Ejecución en MASTER
        MPIMaster master(MPI_COMM_WORLD, config, threads_per_worker);
        master.run();
    } else if (rank == 1 || rank == 2) {
        // Ejecución en WORKER 1 o WORKER 2
        MPIWorker worker(MPI_COMM_WORLD, rank);
        worker.run();
    }

    MPI_Finalize();
    return 0;
}
