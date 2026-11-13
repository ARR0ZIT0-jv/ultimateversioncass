#include "MPIWorker.hpp"
#include <omp.h>
#include <iostream>
#include <cmath>

constexpr int TAG_METRICS = 100;
constexpr int TAG_MIGRANT_COUNT = 200;
constexpr int TAG_MIGRANT_DATA = 201;

MPIWorker::MPIWorker(MPI_Comm comm, int rank)
    : comm_(comm),
      rank_(rank),
      peer_rank_(rank == 1 ? 2 : 1),
      omp_threads_(1),
      grid_(1.0f, 1.0f, 0.015f) {}

void MPIWorker::receive_configuration() {
    SimConfigMPI cfg{};
    MPI_Bcast(&cfg, sizeof(SimConfigMPI), MPI_BYTE, 0, comm_);

    config_.population_size = cfg.population_size;
    config_.initial_infected = cfg.initial_infected;
    config_.beta = cfg.beta;
    config_.gamma = cfg.gamma;
    config_.contact_radius = cfg.contact_radius;
    config_.mobility_rate = cfg.mobility_rate;
    config_.vaccination_rate = cfg.vaccination_rate;
    config_.mask_usage_rate = cfg.mask_usage_rate;
    config_.mask_efficiency = cfg.mask_efficiency;
    config_.num_days = cfg.num_days;
    config_.seed = cfg.seed;
    omp_threads_ = cfg.omp_threads;
    config_.infectious_min = cfg.infectious_min;
    config_.infectious_max = cfg.infectious_max;
    config_.contacts_per_day = cfg.contacts_per_day;
    // La grilla debe tener celdas >= radio de contacto (el radio ya viene calibrado desde MASTER)
    grid_ = ParallelSpatialGrid(1.0f, 1.0f, config_.contact_radius);

    omp_set_num_threads(omp_threads_);

    // Inicializar pool PRNG para este Worker
    prng_pool_.clear();
    prng_pool_.reserve(omp_threads_);
    for (int t = 0; t < omp_threads_; ++t) {
        uint64_t worker_seed = config_.seed + static_cast<uint64_t>(rank_ * 100003 + t * 10007 + 7);
        prng_pool_.emplace_back(worker_seed);
    }
}

void MPIWorker::initialize_local_population() {
    uint64_t total_pop = config_.population_size;
    uint64_t pop_a = total_pop / 2;
    uint64_t pop_b = total_pop - pop_a;

    uint64_t local_pop_size = (rank_ == 1) ? pop_a : pop_b;
    uint64_t id_offset = (rank_ == 1) ? 0 : pop_a;
    uint8_t local_region = (rank_ == 1) ? 0 : 1;

    local_population_.resize(local_pop_size);

    #pragma omp parallel num_threads(omp_threads_)
    {
        int tid = omp_get_thread_num();
        auto& local_prng = prng_pool_[tid];

        #pragma omp for schedule(static)
        for (uint64_t i = 0; i < local_pop_size; ++i) {
            Individual ind{};
            ind.id = id_offset + i;
            ind.x = local_prng.uniform_float();
            ind.y = local_prng.uniform_float();
            ind.vx = local_prng.uniform_range(-0.010f, 0.010f);
            ind.vy = local_prng.uniform_range(-0.010f, 0.010f);
            ind.days_infected = 0;
            ind.infection_duration = static_cast<uint16_t>(local_prng.uniform_int(config_.infectious_min, config_.infectious_max));
            ind.region_id = local_region;
            ind.is_migrating = false;
            ind.mask_factor = local_prng.bernoulli(config_.mask_usage_rate) ? config_.mask_efficiency : 0.0f;

            if (rank_ == 1 && i < config_.initial_infected) {
                ind.state = HealthState::INFECTED;
            } else if (local_prng.bernoulli(config_.vaccination_rate)) {
                ind.state = HealthState::VACCINATED;
            } else {
                ind.state = HealthState::SUSCEPTIBLE;
            }

            local_population_[i] = ind;
        }
    }
}

void MPIWorker::local_step(uint32_t /*day*/, std::vector<MigrantData>& outgoing_migrants,
                           uint64_t& new_inf, uint64_t& new_rec, double& compute_time_ms) {
    double t_start = MPI_Wtime();
    size_t pop_size = local_population_.size();

    // 1. Cinemática y grilla espacial paralela
    grid_.reset(pop_size);
    #pragma omp parallel num_threads(omp_threads_)
    {
        int tid = omp_get_thread_num();
        auto& local_prng = prng_pool_[tid];

        #pragma omp for schedule(static)
        for (size_t i = 0; i < pop_size; ++i) {
            auto& ind = local_population_[i];
            ind.x += ind.vx;
            ind.y += ind.vy;

            if (ind.x < 0.0f) { ind.x = -ind.x; ind.vx = -ind.vx; }
            else if (ind.x > 1.0f) { ind.x = 2.0f - ind.x; ind.vx = -ind.vx; }

            if (ind.y < 0.0f) { ind.y = -ind.y; ind.vy = -ind.vy; }
            else if (ind.y > 1.0f) { ind.y = 2.0f - ind.y; ind.vy = -ind.vy; }

            ind.is_migrating = local_prng.bernoulli(config_.mobility_rate);
            grid_.insert_atomic(static_cast<int>(i), ind.x, ind.y);
        }
    }

    // 2. Evaluación de contagios OpenMP
    float r2 = config_.contact_radius * config_.contact_radius;
    std::vector<uint8_t> to_infect(pop_size, 0);

    #pragma omp parallel num_threads(omp_threads_)
    {
        int tid = omp_get_thread_num();
        auto& local_prng = prng_pool_[tid];
        std::vector<int> local_neighbor_cells;
        local_neighbor_cells.reserve(9);

        #pragma omp for schedule(guided, 64)
        for (size_t i = 0; i < pop_size; ++i) {
            if (local_population_[i].state != HealthState::SUSCEPTIBLE) continue;

            const auto& ind_s = local_population_[i];
            int cell_idx = grid_.get_cell_index(ind_s.x, ind_s.y);
            int cx = cell_idx % grid_.cols();
            int cy = cell_idx / grid_.cols();

            grid_.get_neighbor_cells(cx, cy, local_neighbor_cells);

            bool got_infected = false;
            for (int c_idx : local_neighbor_cells) {
                for (int j_idx = grid_.get_head(c_idx); j_idx != -1; j_idx = grid_.get_next(j_idx)) {
                    const auto& ind_other = local_population_[j_idx];
                    if (ind_other.state == HealthState::INFECTED) {
                        float dx = ind_s.x - ind_other.x;
                        float dy = ind_s.y - ind_other.y;
                        float d2 = dx * dx + dy * dy;

                        if (d2 <= r2) {
                            float p_eff = config_.beta * (1.0f - ind_s.mask_factor) * (1.0f - ind_other.mask_factor);
                            if (local_prng.bernoulli(p_eff)) {
                                got_infected = true;
                                break;
                            }
                        }
                    }
                }
                if (got_infected) break;
            }

            if (got_infected) {
                to_infect[i] = 1;
            }
        }
    }

    new_inf = 0;
    #pragma omp parallel for reduction(+:new_inf) schedule(static) num_threads(omp_threads_)
    for (size_t i = 0; i < pop_size; ++i) {
        if (to_infect[i]) {
            local_population_[i].state = HealthState::INFECTED;
            local_population_[i].days_infected = 0;
            new_inf++;
        }
    }

    // 3. Recuperaciones OpenMP
    new_rec = 0;
    #pragma omp parallel num_threads(omp_threads_) reduction(+:new_rec)
    {
        int tid = omp_get_thread_num();
        auto& local_prng = prng_pool_[tid];

        #pragma omp for schedule(static)
        for (size_t i = 0; i < pop_size; ++i) {
            auto& ind = local_population_[i];
            if (ind.state == HealthState::INFECTED) {
                ind.days_infected++;
                if (ind.days_infected >= ind.infection_duration || local_prng.bernoulli(config_.gamma)) {
                    ind.state = HealthState::RECOVERED;
                    new_rec++;
                }
            }
        }
    }

    // 4. Extracción de migrantes hacia el nodo Worker par
    outgoing_migrants.clear();
    std::vector<Individual> kept;
    kept.reserve(pop_size);

    for (auto& ind : local_population_) {
        if (ind.is_migrating) {
            MigrantData m{};
            m.id = ind.id;
            // Coordenada x ajustada a la frontera de entrada de la otra región
            m.x = (rank_ == 1) ? (0.05f + prng_pool_[0].uniform_float() * 0.1f)
                               : (0.85f + prng_pool_[0].uniform_float() * 0.1f);
            m.y = ind.y;
            m.vx = ind.vx;
            m.vy = ind.vy;
            m.state = static_cast<uint8_t>(ind.state);
            m.days_infected = ind.days_infected;
            m.infection_duration = ind.infection_duration;
            m.mask_factor = ind.mask_factor;
            outgoing_migrants.push_back(m);
        } else {
            kept.push_back(ind);
        }
    }
    local_population_ = std::move(kept);

    compute_time_ms = (MPI_Wtime() - t_start) * 1000.0;
}

void MPIWorker::exchange_migrants(const std::vector<MigrantData>& outgoing,
                                  std::vector<MigrantData>& incoming, double& comm_time_ms) {
    double t_start = MPI_Wtime();
    uint64_t num_outgoing = outgoing.size();
    uint64_t num_incoming = 0;
    MPI_Status status;

    // 1. Intercambio de conteo de migrantes (Sendrecv simétrico entre Rank 1 y Rank 2)
    MPI_Sendrecv(&num_outgoing, 1, MPI_UINT64_T, peer_rank_, TAG_MIGRANT_COUNT,
                 &num_incoming, 1, MPI_UINT64_T, peer_rank_, TAG_MIGRANT_COUNT,
                 comm_, &status);

    // 2. Intercambio de datos reales de individuos
    incoming.resize(num_incoming);
    MPI_Sendrecv(outgoing.data(), num_outgoing * sizeof(MigrantData), MPI_BYTE, peer_rank_, TAG_MIGRANT_DATA,
                 incoming.data(), num_incoming * sizeof(MigrantData), MPI_BYTE, peer_rank_, TAG_MIGRANT_DATA,
                 comm_, &status);

    comm_time_ms = (MPI_Wtime() - t_start) * 1000.0;
}

void MPIWorker::integrate_migrants(const std::vector<MigrantData>& incoming) {
    uint8_t target_region = (rank_ == 1) ? 0 : 1;
    for (const auto& m : incoming) {
        Individual ind{};
        ind.id = m.id;
        ind.x = m.x;
        ind.y = m.y;
        ind.vx = m.vx;
        ind.vy = m.vy;
        ind.state = static_cast<HealthState>(m.state);
        ind.days_infected = m.days_infected;
        ind.infection_duration = m.infection_duration;
        ind.region_id = target_region;
        ind.mask_factor = m.mask_factor;
        ind.is_migrating = false;

        local_population_.push_back(ind);
    }
}

void MPIWorker::report_metrics(uint32_t day, uint64_t new_inf, uint64_t new_rec,
                               uint64_t sent, uint64_t recvd, double compute_ms, double comm_ms) {
    NodeMetrics metrics{};
    metrics.rank = rank_;
    metrics.day = day;
    metrics.population = local_population_.size();
    metrics.new_infections = new_inf;
    metrics.new_recoveries = new_rec;
    metrics.migrants_sent = sent;
    metrics.migrants_received = recvd;
    metrics.compute_time_ms = compute_ms;
    metrics.comm_time_ms = comm_ms;

    uint64_t s = 0, inf = 0, r = 0, v = 0;
    #pragma omp parallel for reduction(+:s, inf, r, v) schedule(static) num_threads(omp_threads_)
    for (size_t i = 0; i < local_population_.size(); ++i) {
        auto st = local_population_[i].state;
        if (st == HealthState::SUSCEPTIBLE) s++;
        else if (st == HealthState::INFECTED) inf++;
        else if (st == HealthState::RECOVERED) r++;
        else if (st == HealthState::VACCINATED) v++;
    }

    metrics.susceptible = s;
    metrics.infected = inf;
    metrics.recovered = r;
    metrics.vaccinated = v;

    // Envío punto a punto de telemetría hacia MASTER (Rank 0)
    MPI_Send(&metrics, sizeof(NodeMetrics), MPI_BYTE, 0, TAG_METRICS, comm_);
}

void MPIWorker::run() {
    receive_configuration();
    initialize_local_population();

    std::vector<MigrantData> outgoing;
    std::vector<MigrantData> incoming;

    for (uint32_t day = 1; day <= config_.num_days; ++day) {
        uint64_t new_inf = 0, new_rec = 0;
        double compute_ms = 0.0, comm_ms = 0.0;

        // 1. Cómputo local multihilo
        local_step(day, outgoing, new_inf, new_rec, compute_ms);

        // 2. Comunicación distribuida entre Workers
        exchange_migrants(outgoing, incoming, comm_ms);

        // 3. Integración de nuevos residentes
        integrate_migrants(incoming);

        // 4. Reporte hacia MASTER (Rank 0)
        report_metrics(day, new_inf, new_rec, outgoing.size(), incoming.size(), compute_ms, comm_ms);

        // 5. Barrera de paso de día
        MPI_Barrier(comm_);
    }
}
