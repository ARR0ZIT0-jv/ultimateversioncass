#include "SequentialEngine.hpp"
#include "utils/Timer.hpp"
#include <cmath>
#include <iostream>

SequentialEngine::SequentialEngine(const SimulationConfig& config)
    : config_(config),
      prng_(config.seed),
      grid_a_(config.space_width, config.space_height, config.contact_radius),
      grid_b_(config.space_width, config.space_height, config.contact_radius) {}

void SequentialEngine::initialize() {
    uint64_t pop_a = config_.population_size / 2;
    uint64_t pop_b = config_.population_size - pop_a;

    individuals_a_.clear();
    individuals_b_.clear();
    individuals_a_.reserve(pop_a + pop_a / 10);
    individuals_b_.reserve(pop_b + pop_b / 10);

    // Inicializar Region A
    for (uint64_t i = 0; i < pop_a; ++i) {
        Individual ind{};
        ind.id = i;
        ind.x = prng_.uniform_float();
        ind.y = prng_.uniform_float();
        ind.vx = prng_.uniform_range(-0.010f, 0.010f);
        ind.vy = prng_.uniform_range(-0.010f, 0.010f);
        ind.days_infected = 0;
        ind.infection_duration = static_cast<uint16_t>(prng_.uniform_int(config_.infectious_min, config_.infectious_max));
        ind.region_id = 0;
        ind.is_migrating = false;

        // Medidas de protección individual
        ind.mask_factor = prng_.bernoulli(config_.mask_usage_rate) ? config_.mask_efficiency : 0.0f;

        // Asignación de estado inicial:
        // Los infectados iniciales comienzan en Region A para evidenciar propagación transfronteriza
        if (i < config_.initial_infected) {
            ind.state = HealthState::INFECTED;
        } else if (prng_.bernoulli(config_.vaccination_rate)) {
            ind.state = HealthState::VACCINATED;
        } else {
            ind.state = HealthState::SUSCEPTIBLE;
        }

        individuals_a_.push_back(ind);
    }

    // Inicializar Region B (población inicialmente sana/susceptible)
    for (uint64_t i = 0; i < pop_b; ++i) {
        Individual ind{};
        ind.id = pop_a + i;
        ind.x = prng_.uniform_float();
        ind.y = prng_.uniform_float();
        ind.vx = prng_.uniform_range(-0.010f, 0.010f);
        ind.vy = prng_.uniform_range(-0.010f, 0.010f);
        ind.days_infected = 0;
        ind.infection_duration = static_cast<uint16_t>(prng_.uniform_int(config_.infectious_min, config_.infectious_max));
        ind.region_id = 1;
        ind.is_migrating = false;

        ind.mask_factor = prng_.bernoulli(config_.mask_usage_rate) ? config_.mask_efficiency : 0.0f;

        if (prng_.bernoulli(config_.vaccination_rate)) {
            ind.state = HealthState::VACCINATED;
        } else {
            ind.state = HealthState::SUSCEPTIBLE;
        }

        individuals_b_.push_back(ind);
    }
}

void SequentialEngine::move_and_bin(std::vector<Individual>& population, SpatialGrid& grid, uint8_t /*region_id*/) {
    grid.clear();
    for (size_t i = 0; i < population.size(); ++i) {
        auto& ind = population[i];

        // Actualizar posición cinemática
        ind.x += ind.vx;
        ind.y += ind.vy;

        // Rebote elástico en fronteras geográficas de la región [0, 1] x [0, 1]
        if (ind.x < 0.0f) { ind.x = -ind.x; ind.vx = -ind.vx; }
        else if (ind.x > 1.0f) { ind.x = 2.0f - ind.x; ind.vx = -ind.vx; }

        if (ind.y < 0.0f) { ind.y = -ind.y; ind.vy = -ind.vy; }
        else if (ind.y > 1.0f) { ind.y = 2.0f - ind.y; ind.vy = -ind.vy; }

        // Comprobación de migración transfronteriza
        ind.is_migrating = prng_.bernoulli(config_.mobility_rate);

        // Registro en la grilla espacial
        grid.insert(i, ind.x, ind.y);
    }
}

uint64_t SequentialEngine::evaluate_infections(std::vector<Individual>& population, const SpatialGrid& grid) {
    uint64_t new_infections = 0;
    float r2 = config_.contact_radius * config_.contact_radius;
    std::vector<uint8_t> to_infect(population.size(), 0);

    std::vector<int> neighbor_cells;
    neighbor_cells.reserve(9);

    for (size_t i = 0; i < population.size(); ++i) {
        if (population[i].state != HealthState::SUSCEPTIBLE) continue;

        const auto& ind_s = population[i];
        int cell_idx = grid.get_cell_index(ind_s.x, ind_s.y);
        int cx = cell_idx % grid.cols();
        int cy = cell_idx / grid.cols();

        grid.get_neighbor_cells(cx, cy, neighbor_cells);

        bool got_infected = false;
        for (int c_idx : neighbor_cells) {
            const auto& cell_individuals = grid.get_cell(c_idx);
            for (size_t j_idx : cell_individuals) {
                const auto& ind_other = population[j_idx];
                if (ind_other.state == HealthState::INFECTED) {
                    float dx = ind_s.x - ind_other.x;
                    float dy = ind_s.y - ind_other.y;
                    float d2 = dx * dx + dy * dy;

                    if (d2 <= r2) {
                        float p_eff = config_.beta * (1.0f - ind_s.mask_factor) * (1.0f - ind_other.mask_factor);
                        if (prng_.bernoulli(p_eff)) {
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

    // Aplicar transiciones atómicamente al final del paso
    for (size_t i = 0; i < population.size(); ++i) {
        if (to_infect[i]) {
            population[i].state = HealthState::INFECTED;
            population[i].days_infected = 0;
            new_infections++;
        }
    }

    return new_infections;
}

uint64_t SequentialEngine::update_recoveries(std::vector<Individual>& population) {
    uint64_t new_recoveries = 0;
    for (auto& ind : population) {
        if (ind.state == HealthState::INFECTED) {
            ind.days_infected++;
            if (ind.days_infected >= ind.infection_duration || prng_.bernoulli(config_.gamma)) {
                ind.state = HealthState::RECOVERED;
                new_recoveries++;
            }
        }
    }
    return new_recoveries;
}

void SequentialEngine::process_migration(uint64_t& mig_a_to_b, uint64_t& mig_b_to_a) {
    mig_a_to_b = 0;
    mig_b_to_a = 0;

    std::vector<Individual> kept_a;
    std::vector<Individual> migrants_to_b;
    kept_a.reserve(individuals_a_.size());

    for (auto& ind : individuals_a_) {
        if (ind.is_migrating) {
            ind.region_id = 1;
            ind.is_migrating = false;
            // Coordenada x reasignada en la frontera oeste de Region B
            ind.x = 0.05f + prng_.uniform_float() * 0.1f;
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
            // Coordenada x reasignada en la frontera este de Region A
            ind.x = 0.85f + prng_.uniform_float() * 0.1f;
            migrants_to_a.push_back(ind);
            mig_b_to_a++;
        } else {
            kept_b.push_back(ind);
        }
    }
    individuals_b_ = std::move(kept_b);

    // Integrar migrantes a sus nuevas regiones
    individuals_a_.insert(individuals_a_.end(), migrants_to_a.begin(), migrants_to_a.end());
    individuals_b_.insert(individuals_b_.end(), migrants_to_b.begin(), migrants_to_b.end());
}

DailyStats SequentialEngine::step(uint32_t current_day) {
    Timer timer;

    // 1. Movimiento y actualización de grilla espacial en ambas regiones
    move_and_bin(individuals_a_, grid_a_, 0);
    move_and_bin(individuals_b_, grid_b_, 1);

    // 2. Evaluación de contactos y nuevos contagios locales
    uint64_t new_inf_a = evaluate_infections(individuals_a_, grid_a_);
    uint64_t new_inf_b = evaluate_infections(individuals_b_, grid_b_);

    // 3. Proceso de recuperaciones locales
    uint64_t new_rec_a = update_recoveries(individuals_a_);
    uint64_t new_rec_b = update_recoveries(individuals_b_);

    // 4. Migración transfronteriza entre Región A y Región B
    uint64_t mig_a_to_b = 0;
    uint64_t mig_b_to_a = 0;
    process_migration(mig_a_to_b, mig_b_to_a);

    double step_time = timer.elapsed_ms();

    // 5. Consolidación de estadísticas
    DailyStats stats{};
    stats.day = current_day;
    stats.new_infections = new_inf_a + new_inf_b;
    stats.new_recoveries = new_rec_a + new_rec_b;
    stats.region_a_pop = individuals_a_.size();
    stats.region_b_pop = individuals_b_.size();
    stats.migrants_a_to_b = mig_a_to_b;
    stats.migrants_b_to_a = mig_b_to_a;
    stats.step_time_ms = step_time;

    uint64_t s = 0, inf = 0, r = 0, v = 0;
    uint64_t inf_a = 0, inf_b = 0;

    for (const auto& ind : individuals_a_) {
        if (ind.state == HealthState::SUSCEPTIBLE) s++;
        else if (ind.state == HealthState::INFECTED) { inf++; inf_a++; }
        else if (ind.state == HealthState::RECOVERED) r++;
        else if (ind.state == HealthState::VACCINATED) v++;
    }

    for (const auto& ind : individuals_b_) {
        if (ind.state == HealthState::SUSCEPTIBLE) s++;
        else if (ind.state == HealthState::INFECTED) { inf++; inf_b++; }
        else if (ind.state == HealthState::RECOVERED) r++;
        else if (ind.state == HealthState::VACCINATED) v++;
    }

    stats.susceptible = s;
    stats.infected = inf;
    stats.recovered = r;
    stats.vaccinated = v;
    stats.region_a_infected = inf_a;
    stats.region_b_infected = inf_b;

    return stats;
}

std::vector<DailyStats> SequentialEngine::run() {
    initialize();
    std::vector<DailyStats> history;
    history.reserve(config_.num_days);

    for (uint32_t day = 1; day <= config_.num_days; ++day) {
        DailyStats s = step(day);
        history.push_back(s);
    }

    return history;
}
