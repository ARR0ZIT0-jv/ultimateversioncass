#ifndef NET_MASTER_HPP
#define NET_MASTER_HPP

#include <mpi.h>
#include "NetTypes.hpp"
#include "core/SimulationConfig.hpp"
#include <string>

// Master (rank 0) del modo `net`: NO simula. Difunde configuracion y particion, recibe cada dia las
// metricas por municipio y la telemetria de cada worker, sincroniza (barrera) y arma el frame del dia.
class NetMaster {
public:
    NetMaster(MPI_Comm comm, const SimulationConfig& config, int omp_threads, int seed_node, int replicas,
              const std::string& nodes_path, const std::string& partition_spec)
        : comm_(comm), config_(config), threads_(omp_threads), seed_node_(seed_node), replicas_(replicas),
          nodes_path_(nodes_path), partition_(partition_spec) {}
    int run();   // 0 = ok
private:
    MPI_Comm comm_;
    SimulationConfig config_;
    int threads_, seed_node_, replicas_;
    std::string nodes_path_, partition_;
};

#endif
