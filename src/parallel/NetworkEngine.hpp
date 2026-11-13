#ifndef NETWORK_ENGINE_HPP
#define NETWORK_ENGINE_HPP

// Motor de METAPOBLACION: red de municipios (nodos) conectados por movilidad (aristas).
// Cada nodo es un modelo SIR(+V) estocastico con mezcla por residencia (contagio original, SIN CAMBIOS).
// El salto a un municipio todavia SANO es un evento discreto con probabilidad:
//     lambda_ij = P_ij * I_i * (1 - mascarilla*eficacia)^2 * kImportEfficiency,   p = 1 - exp(-lambda_ij)
//
// DISENO PARA EJECUCION DISTRIBUIDA (Master/Worker):
//  * El dia se divide en fases con una API publica, de modo que el mismo codigo sirve para
//    1 proceso (step()) y para N workers MPI (cada uno posee un subconjunto de municipios).
//  * Cada municipio tiene SU PROPIO RNG para la dinamica local y SU PROPIO RNG de saltos
//    (por municipio de ORIGEN). Por eso el resultado no depende de como se parta la red,
//    ni del numero de hilos: 1 proceso == 3 procesos MPI con la misma semilla.
//  * `replicas` clona la red R veces (copias independientes) para estudiar escalabilidad:
//    es CARGA SINTETICA y debe declararse como tal en los informes.

#include "core/SimulationConfig.hpp"
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

struct NetNode {
    int id = 0;                 // id del municipio en el CSV (igual en todas las replicas)
    std::string name;
    uint64_t population = 0;
    double lat = 0.0, lon = 0.0;
    uint64_t S = 0, I = 0, R = 0, V = 0;
    uint64_t new_infections = 0;
};

// Salto de la infeccion de un municipio a otro (un "enlace" del dia `day`).
// from/to son indices GLOBALES de nodo (= id del municipio cuando replicas == 1).
// Es un POD: tambien viaja tal cual como mensaje MPI entre workers.
struct SpreadEvent {
    uint32_t day = 0;
    int from = 0, to = 0;
    uint64_t cases = 0;
};

class NetworkEngine {
public:
    NetworkEngine(const SimulationConfig& config, int threads, int replicas = 1);

    // Carga los municipios base desde CSV (id,nombre,poblacion,lat,lon). Devuelve false si falla.
    bool load_nodes(const std::string& csv_path);
    // Construye la matriz de movilidad P (gravedad). initialize() ya la llama.
    void build_mobility();

    // seed_node: municipio base donde comienza el brote (en cada replica).
    // owner_of_base[b] = rank dueno del municipio base b; my_rank = rank de este proceso.
    // Con my_rank < 0 (o owner vacio) este proceso posee TODOS los nodos.
    void initialize(int seed_node, const std::vector<int>& owner_of_base = {}, int my_rank = -1);

    // ---- Un dia completo en un solo proceso ----
    void step();

    // ---- Fases del dia (API para MPI) ----
    // F0: escribe en I_all (tamano total_nodes) los infectados de los nodos propios.
    void collect_local_infected(std::vector<uint64_t>& I_all) const;
    // F1+F2: prevalencia y dinamica local de los nodos propios (I_all = infectados al inicio del dia).
    void phase_dynamics(const std::vector<uint64_t>& I_all);
    // F3a: saltos candidatos desde los nodos propios (se sortea siempre, estado-independiente).
    void phase_draw_jumps(std::vector<SpreadEvent>& candidates);
    // F3b: aplica los candidatos cuyo destino es propio (menor origen gana; un destino solo se infecta una vez).
    void phase_resolve_jumps(std::vector<SpreadEvent> candidates);

    // ---- Consulta ----
    const std::vector<NetNode>& nodes() const { return nodes_; }
    std::vector<NetNode>& nodes_mutable() { return nodes_; }
    const std::vector<int>& owned() const { return owned_; }
    int owner_of(size_t g) const;
    const std::vector<NetNode>& base_nodes() const { return base_; }
    size_t base_size() const { return base_.size(); }
    size_t total_nodes() const { return nodes_.size(); }
    int replicas() const { return replicas_; }
    uint32_t day() const { return day_; }
    // Saltos ocurridos en el ultimo dia (solo los resueltos por este proceso).
    const std::vector<SpreadEvent>& events() const { return events_; }
    const std::vector<std::vector<double>>& mobility() const { return P_; }
    // replicas == 1: una fila por municipio y dia. replicas > 1: suma de todas las replicas por municipio base.
    bool write_csv(const std::string& path, const std::vector<std::vector<NetNode>>& history) const;

private:
    SimulationConfig config_;
    int threads_;
    int replicas_;
    std::vector<NetNode> base_;          // municipios base (R=1)
    std::vector<NetNode> nodes_;         // base_ repetidos `replicas_` veces
    std::vector<std::vector<double>> P_; // movilidad base (B x B)
    std::vector<double> popmix_;         // sum_i P_ij * N_i  (constante)
    std::vector<int> owner_base_;
    std::vector<int> owned_;             // indices globales propios (ascendente)
    std::vector<char> is_owned_;
    std::vector<std::unique_ptr<std::mt19937_64>> rngs_;       // dinamica local, por nodo
    std::vector<std::unique_ptr<std::mt19937_64>> spread_rngs_; // saltos, por nodo de ORIGEN
    std::vector<double> prev_;
    std::vector<SpreadEvent> events_;
    uint32_t day_ = 0;
};

#endif // NETWORK_ENGINE_HPP
