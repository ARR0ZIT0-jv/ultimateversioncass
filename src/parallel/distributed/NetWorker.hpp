#ifndef NET_WORKER_HPP
#define NET_WORKER_HPP

#include <mpi.h>
#include "NetTypes.hpp"

// Worker (rank 1 o 2) del modo `net`: simula SOLO los municipios que posee, con OpenMP.
class NetWorker {
public:
    NetWorker(MPI_Comm comm, int rank) : comm_(comm), rank_(rank) {}
    void run();
private:
    MPI_Comm comm_;
    int rank_;
};

#endif
