#include "MPIMaster.hpp"
#include "utils/CSVWriter.hpp"
#include <iostream>
#include <iomanip>

constexpr int TAG_METRICS = 100;

MPIMaster::MPIMaster(MPI_Comm comm, const SimulationConfig& config, int omp_threads_per_worker)
    : comm_(comm),
      config_(config),
      omp_threads_per_worker_(omp_threads_per_worker),
      output_file_(config.output_file) {
    MPI_Comm_rank(comm_, &rank_);
    MPI_Comm_size(comm_, &size_);
}

void MPIMaster::broadcast_configuration() {
    SimConfigMPI cfg{};
    cfg.population_size = config_.population_size;
    cfg.initial_infected = config_.initial_infected;
    cfg.beta = config_.beta;
    cfg.gamma = config_.gamma;
    cfg.contact_radius = config_.contact_radius;
    cfg.mobility_rate = config_.mobility_rate;
    cfg.vaccination_rate = config_.vaccination_rate;
    cfg.mask_usage_rate = config_.mask_usage_rate;
    cfg.mask_efficiency = config_.mask_efficiency;
    cfg.num_days = config_.num_days;
    cfg.seed = config_.seed;
    cfg.omp_threads = omp_threads_per_worker_;
    cfg.infectious_min = config_.infectious_min;
    cfg.infectious_max = config_.infectious_max;
    cfg.contacts_per_day = config_.contacts_per_day;

    // Difusión simultánea a todos los nodos (Rank 0 -> Rank 1 y Rank 2)
    MPI_Bcast(&cfg, sizeof(SimConfigMPI), MPI_BYTE, 0, comm_);
}

void MPIMaster::run() {
    std::cout << "========================================================================\n"
              << " CLUSTER MPI DISTRIBUIDO: SIMULADOR DE EPIDEMIAS\n"
              << " MASTER (Rank 0) activo | Nodos Workers en comunicador: " << (size_ - 1) << "\n"
              << " Poblacion Total: " << config_.population_size
              << " | Duracion: " << config_.num_days << " dias\n"
              << " Hilos OpenMP por Worker: " << omp_threads_per_worker_ << "\n"
              << "========================================================================\n";

    broadcast_configuration();

    std::vector<DailyStats> history;
    history.reserve(config_.num_days);

    std::cout << "[MASTER] Difundidos parametros epidemiologicos via MPI_Bcast.\n";
    std::cout << "[MASTER] Iniciando coordinacion de iteraciones distribuidas...\n";
    std::cout << std::string(90, '-') << "\n";
    std::cout << std::setw(5) << "Dia"
              << std::setw(9) << "Suscept"
              << std::setw(9) << "Infect"
              << std::setw(9) << "Recup"
              << std::setw(8) << "Vacun"
              << std::setw(10) << "Nuev.Inf"
              << std::setw(10) << "W1(RegA)"
              << std::setw(10) << "W2(RegB)"
              << std::setw(10) << "MigrTotal"
              << std::setw(10) << "Paso(ms)" << "\n";
    std::cout << std::string(90, '-') << "\n";

    double total_comm_time_ms = 0.0;
    double total_sim_start = MPI_Wtime();

    for (uint32_t day = 1; day <= config_.num_days; ++day) {
        double day_start = MPI_Wtime();

        NodeMetrics w1_metrics{}, w2_metrics{};
        MPI_Status status;

        double t_comm_start = MPI_Wtime();
        // Recepción de métricas de Worker 1 (Rank 1)
        MPI_Recv(&w1_metrics, sizeof(NodeMetrics), MPI_BYTE, 1, TAG_METRICS, comm_, &status);
        // Recepción de métricas de Worker 2 (Rank 2)
        MPI_Recv(&w2_metrics, sizeof(NodeMetrics), MPI_BYTE, 2, TAG_METRICS, comm_, &status);
        double t_comm_end = MPI_Wtime();
        total_comm_time_ms += (t_comm_end - t_comm_start) * 1000.0;

        // Barrera de sincronización global
        MPI_Barrier(comm_);
        double day_time_ms = (MPI_Wtime() - day_start) * 1000.0;

        DailyStats stats{};
        stats.day = day;
        stats.susceptible = w1_metrics.susceptible + w2_metrics.susceptible;
        stats.infected = w1_metrics.infected + w2_metrics.infected;
        stats.recovered = w1_metrics.recovered + w2_metrics.recovered;
        stats.vaccinated = w1_metrics.vaccinated + w2_metrics.vaccinated;
        stats.new_infections = w1_metrics.new_infections + w2_metrics.new_infections;
        stats.new_recoveries = w1_metrics.new_recoveries + w2_metrics.new_recoveries;
        stats.region_a_pop = w1_metrics.population;
        stats.region_b_pop = w2_metrics.population;
        stats.region_a_infected = w1_metrics.infected;
        stats.region_b_infected = w2_metrics.infected;
        stats.migrants_a_to_b = w1_metrics.migrants_sent;
        stats.migrants_b_to_a = w2_metrics.migrants_sent;
        stats.step_time_ms = day_time_ms;

        history.push_back(stats);

        if (day == 1 || day % 5 == 0 || day == config_.num_days) {
            std::cout << std::setw(5) << stats.day
                      << std::setw(9) << stats.susceptible
                      << std::setw(9) << stats.infected
                      << std::setw(9) << stats.recovered
                      << std::setw(8) << stats.vaccinated
                      << std::setw(10) << stats.new_infections
                      << std::setw(10) << stats.region_a_infected
                      << std::setw(10) << stats.region_b_infected
                      << std::setw(10) << (stats.migrants_a_to_b + stats.migrants_b_to_a)
                      << std::setw(10) << std::fixed << std::setprecision(2) << stats.step_time_ms << "\n";
        }
    }

    double total_sim_time_ms = (MPI_Wtime() - total_sim_start) * 1000.0;
    std::cout << std::string(90, '-') << "\n";
    std::cout << "[EXITO] Simulacion distribuida MPI finalizada.\n"
              << "  - Tiempo total de simulacion: " << std::fixed << std::setprecision(2) << total_sim_time_ms << " ms ("
              << (total_sim_time_ms / 1000.0) << " s)\n"
              << "  - Tiempo acumulado comunicacion Master-Workers: " << total_comm_time_ms << " ms\n";

    if (CSVWriter::write_daily_stats(output_file_, history)) {
        std::cout << "[INFO] Resultados guardados en: " << output_file_ << "\n";
    }
}
