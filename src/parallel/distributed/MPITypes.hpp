#ifndef MPI_TYPES_HPP
#define MPI_TYPES_HPP

#include <cstdint>

// Estructura POD para difusión de configuración inicial (MPI_Bcast)
struct SimConfigMPI {
    uint64_t population_size;
    uint64_t initial_infected;
    float beta;
    float gamma;
    float contact_radius;
    float mobility_rate;
    float vaccination_rate;
    float mask_usage_rate;
    float mask_efficiency;
    uint32_t num_days;
    uint64_t seed;
    int omp_threads;
    uint16_t infectious_min;
    uint16_t infectious_max;
    float contacts_per_day;
};

// Estructura POD para intercambio de individuos migrantes entre Workers
struct MigrantData {
    uint64_t id;
    float x;
    float y;
    float vx;
    float vy;
    uint8_t state;
    uint16_t days_infected;
    uint16_t infection_duration;
    float mask_factor;
};

// Estructura POD para telemetría diaria enviada de cada Worker hacia el MASTER
struct NodeMetrics {
    int rank;
    uint32_t day;
    uint64_t population;
    uint64_t susceptible;
    uint64_t infected;
    uint64_t recovered;
    uint64_t vaccinated;
    uint64_t new_infections;
    uint64_t new_recoveries;
    uint64_t migrants_sent;
    uint64_t migrants_received;
    double compute_time_ms;
    double comm_time_ms;
};

#endif // MPI_TYPES_HPP
