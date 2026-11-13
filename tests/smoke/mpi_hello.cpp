// Smoke test MPI: cada rank informa en que host corre; rank 0 hace una reduccion.
#include <mpi.h>
#include <cstdio>
#include <unistd.h>
int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int r = 0, s = 0; MPI_Comm_rank(MPI_COMM_WORLD, &r); MPI_Comm_size(MPI_COMM_WORLD, &s);
    char host[64]; gethostname(host, sizeof host);
    int sum = 0; MPI_Reduce(&r, &sum, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    std::printf("MPI rank %d/%d en %s\n", r, s, host);
    if (r == 0) std::printf("MPI OK: suma de ranks = %d (esperado %d)\n", sum, s * (s - 1) / 2);
    MPI_Finalize();
    return 0;
}
