#ifndef MPI_MASTER_HPP
#define MPI_MASTER_HPP

#include <mpi.h>
#include "MPITypes.hpp"
#include "core/SimulationConfig.hpp"
#include <vector>
#include <string>

class MPIMaster {
public:
    MPIMaster(MPI_Comm comm, const SimulationConfig& config, int omp_threads_per_worker);

    void run();

private:
    MPI_Comm comm_;
    int rank_;
    int size_;
    SimulationConfig config_;
    int omp_threads_per_worker_;
    std::string output_file_;

    void broadcast_configuration();
    void collect_and_report_day(uint32_t day, std::vector<DailyStats>& history);
};

#endif // MPI_MASTER_HPP
