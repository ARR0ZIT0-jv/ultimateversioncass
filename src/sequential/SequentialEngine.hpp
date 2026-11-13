#ifndef SEQUENTIAL_ENGINE_HPP
#define SEQUENTIAL_ENGINE_HPP

#include "core/Individual.hpp"
#include "core/SimulationConfig.hpp"
#include "core/SpatialGrid.hpp"
#include "utils/PRNG.hpp"
#include <vector>

class SequentialEngine {
public:
    explicit SequentialEngine(const SimulationConfig& config);

    void initialize();
    std::vector<DailyStats> run();
    DailyStats step(uint32_t current_day);

    const std::vector<Individual>& get_individuals_a() const { return individuals_a_; }
    const std::vector<Individual>& get_individuals_b() const { return individuals_b_; }
    const SimulationConfig& get_config() const { return config_; }

private:
    SimulationConfig config_;
    PRNG prng_;

    std::vector<Individual> individuals_a_;
    std::vector<Individual> individuals_b_;

    SpatialGrid grid_a_;
    SpatialGrid grid_b_;

    void move_and_bin(std::vector<Individual>& population, SpatialGrid& grid, uint8_t region_id);
    uint64_t evaluate_infections(std::vector<Individual>& population, const SpatialGrid& grid);
    uint64_t update_recoveries(std::vector<Individual>& population);
    void process_migration(uint64_t& mig_a_to_b, uint64_t& mig_b_to_a);
};

#endif // SEQUENTIAL_ENGINE_HPP
