#ifndef OPENMP_ENGINE_HPP
#define OPENMP_ENGINE_HPP

#include "core/Individual.hpp"
#include "core/SimulationConfig.hpp"
#include "core/SpatialGrid.hpp"
#include "utils/PRNG.hpp"
#include <vector>
#include <omp.h>

class OpenMPEngine {
public:
    explicit OpenMPEngine(const SimulationConfig& config, int num_threads = 0);

    void initialize();
    std::vector<DailyStats> run();
    DailyStats step(uint32_t current_day);

    int get_num_threads() const { return num_threads_; }
    void set_num_threads(int threads);

    const std::vector<Individual>& get_individuals_a() const { return individuals_a_; }
    const std::vector<Individual>& get_individuals_b() const { return individuals_b_; }

private:
    SimulationConfig config_;
    int num_threads_;
    std::vector<PRNG> prng_pool_;

    std::vector<Individual> individuals_a_;
    std::vector<Individual> individuals_b_;

    ParallelSpatialGrid grid_a_;
    ParallelSpatialGrid grid_b_;

    void move_and_bin(std::vector<Individual>& population, ParallelSpatialGrid& grid);
    uint64_t evaluate_infections(std::vector<Individual>& population, const ParallelSpatialGrid& grid);
    uint64_t update_recoveries(std::vector<Individual>& population);
    void process_migration(uint64_t& mig_a_to_b, uint64_t& mig_b_to_a);
};

#endif // OPENMP_ENGINE_HPP
