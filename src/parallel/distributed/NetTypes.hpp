#ifndef NET_TYPES_HPP
#define NET_TYPES_HPP

// Estructuras POD del modo MPI `net` (red de municipios de Tarija).
// Todas viajan como bytes (MPI_BYTE): el clúster es homogeneo (mismo binario en cada contenedor).

#include <cstdint>

constexpr int kNetMaxBase = 64;       // maximo de municipios base soportados en la particion

// Configuracion difundida por el Master con MPI_Bcast (incluye la particion).
struct NetConfigMPI {
    uint64_t initial_infected;
    uint64_t seed;
    float beta, gamma, mobility_rate, vaccination_rate, mask_usage_rate, mask_efficiency, contacts_per_day;
    uint32_t num_days;
    uint16_t infectious_min, infectious_max;
    int32_t omp_threads;
    int32_t seed_node;
    int32_t replicas;
    int32_t n_base;
    int8_t owner[kNetMaxBase];        // owner[b] = rank (1 o 2) que simula el municipio base b
    char nodes_path[256];
};

// Mensaje de salto que CRUZA de un worker a otro (POD, como MigrantData en el diseno A/B).
struct SpreadMessage {
    uint32_t day;
    int32_t from;                     // indice global del municipio origen
    int32_t to;                       // indice global del municipio destino
    uint32_t cases;
};

// Estado de un municipio al final del dia (Worker -> Master).
struct NetNodeMetric {
    uint32_t gid;                     // indice global
    uint64_t S, I, R, V, new_inf;
};

// Telemetria diaria de un Worker (Worker -> Master), seguida de n_nodes NetNodeMetric y n_events SpreadMessage.
struct NetTelemetry {
    int32_t rank;
    uint32_t day;
    uint32_t n_nodes;                 // metricas de municipios que siguen
    uint32_t n_events;                // saltos resueltos (destino propio) que siguen
    double compute_ms;                // tiempo de calculo del dia (OpenMP)
    double comm_ms;                   // tiempo en MPI_Sendrecv entre workers
    uint64_t jumps_sent;              // saltos enviados al otro worker
    uint64_t jumps_received;          // saltos recibidos del otro worker
    uint64_t msgs_sent;               // mensajes MPI punto a punto enviados en el dia
    uint64_t msgs_recv;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
};

// Presentacion de cada Worker al Master (una vez, tras el Bcast).
struct NetWorkerInfo {
    int32_t rank;
    int32_t pid;
    int32_t omp_threads;
    int32_t owned_base;               // municipios base que posee
    char host[64];
};

#endif // NET_TYPES_HPP
