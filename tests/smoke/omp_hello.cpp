// Smoke test OpenMP: confirma que gcc compila con -fopenmp y que se crean hilos.
#include <omp.h>
#include <cstdio>
int main() {
    int n = 0;
    #pragma omp parallel reduction(+:n)
    { n += 1; }
    std::printf("OpenMP OK: %d hilos (max=%d)\n", n, omp_get_max_threads());
    return n > 0 ? 0 : 1;
}
