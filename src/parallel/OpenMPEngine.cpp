#include "OpenMPEngine.hpp"
#include "utils/Timer.hpp"
#include <cmath>
#include <iostream>

OpenMPEngine::OpenMPEngine(const SimulationConfig& config, int num_threads)
    : config_(config),
      num_threads_(num_threads),
      grid_a_(config.space_width, config.space_height, config.contact_radius),
      grid_b_(config.space_width, config.space_height, config.contact_radius) {
    if (num_threads_ <= 0) {
        num_threads_ = omp_get_max_threads();
    }
    set_num_threads(num_threads_);
}

void OpenMPEngine::set_num_threads(int threads) {
    num_threads_ = threads;
    omp_set_num_threads(num_threads_);

    prng_pool_.clear();
    prng_pool_.reserve(num_threads_);
    for (int t = 0; t < num_threads_; ++t) {
        prng_pool_.emplace_back(config_.seed + static_cast<uint64_t>(t * 10007 + 1));
    }
}

void OpenMPEngine::initialize() {
    uint64_t pop_a = config_.population_size / 2;
    uint64_t pop_b = config_.population_size - pop_a;

    individuals_a_.resize(pop_a);
    individuals_b_.resize(pop_b);

    #pragma omp parallel num_threads(num_threads_)
    {
        int tid = omp_get_thread_num();
        auto& local_prng = prng_pool_[tid];

        #pragma omp for schedule(static)
        for (uint64_t i = 0; i < pop_a; ++i) {
            Individual ind{};
            ind.id = i;
            ind.x = local_prng.uniform_float();
            ind.y = local_prng.uniform_float();
            ind.vx = local_prng.uniform_range(-0.010f, 0.010f);
            ind.vy = local_prng.uniform_range(-0.010f, 0.010f);
            ind.days_infected = 0;
            ind.infection_duration = static_cast<uint16_t>(local_prng.uniform_int(config_.infectious_min, config_.infectious_max));
            ind.region_id = 0;
            ind.is_migrating = false;
            ind.mask_factor = local_prng.bernoulli(config_.mask_usage_rate) ? config_.mask_efficiency : 0.0f;

            if (i < config_.initial_infected) {
                ind.state = HealthState::INFECTED;
            } else if (local_prng.bernoulli(config_.vaccination_rate)) {
                ind.state = HealthState::VACCINATED;
            } else {
                ind.state = HealthState::SUSCEPTIBLE;
            }

            individuals_a_[i] = ind;
        }

        #pragma omp for schedule(static)
        for (uint64_t i = 0; i < pop_b; ++i) {
            Individual ind{};
            ind.id = pop_a + i;
            ind.x = local_prng.uniform_float();
            ind.y = local_prng.uniform_float();
            ind.vx = local_prng.uniform_range(-0.010f, 0.010f);
            ind.vy = local_prng.uniform_range(-0.010f, 0.010f);
            ind.days_infected = 0;
            ind.infection_duration = static_cast<uint16_t>(local_prng.uniform_int(config_.infectious_min, config_.infectious_max));
            ind.region_id = 1;
            ind.is_migrating = false;
            ind.mask_factor = local_prng.bernoulli(config_.mask_usage_rate) ? config_.mask_efficiency : 0.0f;

            if (local_prng.bernoulli(config_.vaccination_rate)) {
                ind.state = HealthState::VACCINATED;
            } else {
                ind.state = HealthState::SUSCEPTIBLE;
            }

            individuals_b_[i] = ind;
        }
    }
}

void OpenMPEngine::move_and_bin(std::vector<Individual>& population, ParallelSpatialGrid& grid) {
    size_t pop_size = population.size();
    grid.reset(pop_size);

    #pragma omp parallel num_threads(num_threads_)
    {
        int tid = omp_get_thread_num();
        auto& local_prng = prng_pool_[tid];

        #pragma omp for schedule(static)
        for (size_t i = 0; i < pop_size; ++i) {
            auto& ind = population[i];
            ind.x += ind.vx;
            ind.y += ind.vy;

            if (ind.x < 0.0f) { ind.x = -ind.x; ind.vx = -ind.vx; }
            else if (ind.x > 1.0f) { ind.x = 2.0f - ind.x; ind.vx = -ind.vx; }

            if (ind.y < 0.0f) { ind.y = -ind.y; ind.vy = -ind.vy; }
            else if (ind.y > 1.0f) { ind.y = 2.0f - ind.y; ind.vy = -ind.vy; }

            ind.is_migrating = local_prng.bernoulli(config_.mobility_rate);

            // Inserción atómica lock-free O(1) sin asignaciones dinámicas
            grid.insert_atomic(static_cast<int>(i), ind.x, ind.y);
        }
    }
}

uint64_t OpenMPEngine::evaluate_infections(std::vector<Individual>& population, const ParallelSpatialGrid& grid) {
    size_t pop_size = population.size();
    float r2 = config_.contact_radius * config_.contact_radius;
    std::vector<uint8_t> to_infect(pop_size, 0);

    #pragma omp parallel num_threads(num_threads_)
    {
        int tid = omp_get_thread_num();
        auto& local_prng = prng_pool_[tid];
        std::vector<int> local_neighbor_cells;
        local_neighbor_cells.reserve(9);

        #pragma omp for schedule(guided, 64)
        for (size_t i = 0; i < pop_size; ++i) {
            if (population[i].state != HealthState::SUSCEPTIBLE) continue;

            const auto& ind_s = population[i];
            int cell_idx = grid.get_cell_index(ind_s.x, ind_s.y);
            int cx = cell_idx % grid.cols();
            int cy = cell_idx / grid.cols();

            grid.get_neighbor_cells(cx, cy, local_neighbor_cells);

            bool got_infected = false;
            for (int c_idx : local_neighbor_cells) {
                for (int j_idx = grid.get_head(c_idx); j_idx != -1; j_idx = grid.get_next(j_idx)) {
                    const auto& ind_other = population[j_idx];
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

    uint64_t new_infections = 0;
    #pragma omp parallel for reduction(+:new_infections) schedule(static) num_threads(num_threads_)
    for (size_t i = 0; i < pop_size; ++i) {
        if (to_infect[i]) {
            population[i].state = HealthState::INFECTED;
            population[i].days_infected = 0;
            new_infections++;
        }
    }

    return new_infections;
}

uint64_t OpenMPEngine::update_recoveries(std::vector<Individual>& population) {
    size_t pop_size = population.size();
    uint64_t new_recoveries = 0;

    #pragma omp parallel num_threads(num_threads_) reduction(+:new_recoveries)
    {
        int tid = omp_get_thread_num();
        auto& local_prng = prng_pool_[tid];

        #pragma omp for schedule(static)
        for (size_t i = 0; i < pop_size; ++i) {
            auto& ind = population[i];
            if (ind.state == HealthState::INFECTED) {
                ind.days_infected++;
                if (ind.days_infected >= ind.infection_duration || local_prng.bernoulli(config_.gamma)) {
                    ind.state = HealthState::RECOVERED;
                    new_recoveries++;
                }
            }
        }
    }

    return new_recoveries;
}

void OpenMPEngine::process_migration(uint64_t& mig_a_to_b, uint64_t& mig_b_to_a) {
    mig_a_to_b = 0;
    mig_b_to_a = 0;

    std::vector<Individual> kept_a;
    std::vector<Individual> migrants_to_b;
    kept_a.reserve(individuals_a_.size());

    for (auto& ind : individuals_a_) {
        if (ind.is_migrating) {
            ind.region_id = 1;
            ind.is_migrating = false;
            ind.x = 0.05f + prng_pool_[0].uniform_float() * 0.1f;
            migrants_to_b.push_back(ind);
            mig_a_to_b++;
        } else {
            kept_a.push_back(ind);
        }
    }
    individuals_a_ = std::move(kept_a);

    std::vector<Individual> kept_b;
    std::vector<Individual> migrants_to_a;
    kept_b.reserve(individuals_b_.size());

    for (auto& ind : individuals_b_) {
        if (ind.is_migrating) {
            ind.region_id = 0;
            ind.is_migrating = false;
            ind.x = 0.85f + prng_pool_[0].uniform_float() * 0.1f;
            migrants_to_a.push_back(ind);
            mig_b_to_a++;
        } else {
            kept_b.push_back(ind);
        }
    }
    individuals_b_ = std::move(kept_b);

    individuals_a_.insert(individuals_a_.end(), migrants_to_a.begin(), migrants_to_a.end());
    individuals_b_.insert(individuals_b_.end(), migrants_to_b.begin(), migrants_to_b.end());
}

DailyStats OpenMPEngine::step(uint32_t current_day) {
    Timer timer;

    move_and_bin(individuals_a_, grid_a_);
    move_and_bin(individuals_b_, grid_b_);

    uint64_t new_inf_a = evaluate_infections(individuals_a_, grid_a_);
    uint64_t new_inf_b = evaluate_infections(individuals_b_, grid_b_);

    uint64_t new_rec_a = update_recoveries(individuals_a_);
    uint64_t new_rec_b = update_recoveries(individuals_b_);

    uint64_t mig_a_to_b = 0;
    uint64_t mig_b_to_a = 0;
    process_migration(mig_a_to_b, mig_b_to_a);

    double step_time = timer.elapsed_ms();

    DailyStats stats{};
    stats.day = current_day;
    stats.new_infections = new_inf_a + new_inf_b;
    stats.new_recoveries = new_rec_a + new_rec_b;
    stats.region_a_pop = individuals_a_.size();
    stats.region_b_pop = individuals_b_.size();
    stats.migrants_a_to_b = mig_a_to_b;
    stats.migrants_b_to_a = mig_b_to_a;
    stats.step_time_ms = step_time;

    uint64_t s_a = 0, inf_a = 0, r_a = 0, v_a = 0;
    #pragma omp parallel for reduction(+:s_a, inf_a, r_a, v_a) schedule(static) num_threads(num_threads_)
    for (size_t i = 0; i < individuals_a_.size(); ++i) {
        auto st = individuals_a_[i].state;
        if (st == HealthState::SUSCEPTIBLE) s_a++;
        else if (st == HealthState::INFECTED) inf_a++;
        else if (st == HealthState::RECOVERED) r_a++;
        else if (st == HealthState::VACCINATED) v_a++;
    }

    uint64_t s_b = 0, inf_b = 0, r_b = 0, v_b = 0;
    #pragma omp parallel for reduction(+:s_b, inf_b, r_b, v_b) schedule(static) num_threads(num_threads_)
    for (size_t i = 0; i < individuals_b_.size(); ++i) {
        auto st = individuals_b_[i].state;
        if (st == HealthState::SUSCEPTIBLE) s_b++;
        else if (st == HealthState::INFECTED) inf_b++;
        else if (st == HealthState::RECOVERED) r_b++;
        else if (st == HealthState::VACCINATED) v_b++;
    }

    stats.susceptible = s_a + s_b;
    stats.infected = inf_a + inf_b;
    stats.recovered = r_a + r_b;
    stats.vaccinated = v_a + v_b;
    stats.region_a_infected = inf_a;
    stats.region_b_infected = inf_b;

    return stats;
}

std::vector<DailyStats> OpenMPEngine::run() {
    initialize();
    std::vector<DailyStats> history;
    history.reserve(config_.num_days);

    for (uint32_t day = 1; day <= config_.num_days; ++day) {
        history.push_back(step(day));
    }
    return history;
}
