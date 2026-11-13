#ifndef MPI_WORKER_HPP
#define MPI_WORKER_HPP

#include <mpi.h>
#include "MPITypes.hpp"
#include "core/Individual.hpp"
#include "core/SimulationConfig.hpp"
#include "core/SpatialGrid.hpp"
#include "utils/PRNG.hpp"
#include <vector>

class MPIWorker {
public:
    MPIWorker(MPI_Comm comm, int rank);

    void run();

private:
    MPI_Comm comm_;
    int rank_;
    int peer_rank_;
    SimulationConfig config_;
    int omp_threads_;

    std::vector<PRNG> prng_pool_;
    std::vector<Individual> local_population_;
    ParallelSpatialGrid grid_;

    void receive_configuration();
    void initialize_local_population();
    void local_step(uint32_t day, std::vector<MigrantData>& outgoing_migrants,
                    uint64_t& new_inf, uint64_t& new_rec, double& compute_time_ms);
    void exchange_migrants(const std::vector<MigrantData>& outgoing,
                           std::vector<MigrantData>& incoming, double& comm_time_ms);
    void integrate_migrants(const std::vector<MigrantData>& incoming);
    void report_metrics(uint32_t day, uint64_t new_inf, uint64_t new_rec,
                        uint64_t sent, uint64_t recvd, double compute_ms, double comm_ms);
};

#endif // MPI_WORKER_HPP
