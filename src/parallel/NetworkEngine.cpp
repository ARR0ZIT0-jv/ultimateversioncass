#include "NetworkEngine.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <omp.h>

// Probabilidad de que un viajero infectado siembre un brote en el destino.
static constexpr double kImportEfficiency = 0.4;

NetworkEngine::NetworkEngine(const SimulationConfig& config, int threads, int replicas)
    : config_(config), threads_(std::max(1, threads)), replicas_(std::max(1, replicas)) {}

bool NetworkEngine::load_nodes(const std::string& csv_path) {
    std::ifstream in(csv_path);
    if (!in) { std::cerr << "[ERROR] No se pudo abrir " << csv_path << "\n"; return false; }
    std::string line;
    std::getline(in, line); // cabecera
    base_.clear();
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string f[5];
        for (auto& x : f) std::getline(ss, x, ',');
        NetNode n;
        n.id = std::stoi(f[0]);
        n.name = f[1];
        n.population = std::stoull(f[2]);
        n.lat = std::stod(f[3]);
        n.lon = std::stod(f[4]);
        base_.push_back(n);
    }
    nodes_.clear();
    for (int r = 0; r < replicas_; ++r) nodes_.insert(nodes_.end(), base_.begin(), base_.end());
    return base_.size() >= 2;
}

void NetworkEngine::build_mobility() {
    const size_t n = base_.size();
    P_.assign(n, std::vector<double>(n, 0.0));
    const double m = std::clamp<double>(config_.mobility_rate, 0.0, 1.0);
    for (size_t i = 0; i < n; ++i) {
        double norm = 0.0;
        std::vector<double> w(n, 0.0);
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            // distancia plana aproximada en km (1 grado ~ 111 km; correccion de longitud por latitud)
            const double dy = (base_[i].lat - base_[j].lat) * 111.0;
            const double dx = (base_[i].lon - base_[j].lon) * 111.0 * std::cos(base_[i].lat * 3.14159265358979 / 180.0);
            const double d2 = std::max(1.0, dx * dx + dy * dy);
            w[j] = static_cast<double>(base_[j].population) / d2; // gravedad
            norm += w[j];
        }
        for (size_t j = 0; j < n; ++j) P_[i][j] = (i == j) ? (1.0 - m) : (norm > 0 ? m * w[j] / norm : 0.0);
    }
    // poblacion presente en cada municipio j (residentes + visitantes), constante en el tiempo
    popmix_.assign(n, 0.0);
    for (size_t j = 0; j < n; ++j)
        for (size_t i = 0; i < n; ++i) popmix_[j] += P_[i][j] * static_cast<double>(base_[i].population);
}

int NetworkEngine::owner_of(size_t g) const {
    if (owner_base_.empty()) return 0;
    return owner_base_[g % base_.size()];
}

void NetworkEngine::initialize(int seed_node, const std::vector<int>& owner_of_base, int my_rank) {
    build_mobility();
    const size_t B = base_.size();
    const size_t N = B * static_cast<size_t>(replicas_);
    owner_base_ = owner_of_base;
    if (my_rank < 0 || owner_base_.size() != B) owner_base_.clear();

    nodes_.clear();
    for (int r = 0; r < replicas_; ++r) nodes_.insert(nodes_.end(), base_.begin(), base_.end());

    owned_.clear();
    is_owned_.assign(N, 0);
    for (size_t g = 0; g < N; ++g)
        if (owner_base_.empty() || owner_base_[g % B] == my_rank) { owned_.push_back(static_cast<int>(g)); is_owned_[g] = 1; }

    // RNG: dinamica local (misma semilla por nodo que el motor original) y saltos (por nodo de origen)
    rngs_.clear(); rngs_.resize(N);
    spread_rngs_.clear(); spread_rngs_.resize(N);
    for (int g : owned_) {
        rngs_[g] = std::make_unique<std::mt19937_64>(config_.seed + 7919ULL * (static_cast<uint64_t>(g) + 1));
        spread_rngs_[g] = std::make_unique<std::mt19937_64>(config_.seed + 104729ULL + 1299709ULL * (static_cast<uint64_t>(g) + 1));
    }
    events_.clear();
    prev_.assign(N, 0.0);
    day_ = 0;

    seed_node = std::clamp<int>(seed_node, 0, static_cast<int>(B) - 1);
    for (size_t g = 0; g < N; ++g) {
        auto& n = nodes_[g];
        n.V = static_cast<uint64_t>(std::llround(config_.vaccination_rate * static_cast<double>(n.population)));
        n.I = 0; n.R = 0; n.new_infections = 0;
        n.S = n.population - n.V;
        if (g % B == static_cast<size_t>(seed_node)) {
            const uint64_t i0 = std::min<uint64_t>(config_.initial_infected, n.S);
            n.I = i0;
            n.S -= i0;
        }
    }
}

void NetworkEngine::collect_local_infected(std::vector<uint64_t>& I_all) const {
    I_all.assign(nodes_.size(), 0);
    for (int g : owned_) I_all[g] = nodes_[g].I;
}

void NetworkEngine::phase_dynamics(const std::vector<uint64_t>& I_all) {
    const size_t B = base_.size();
    const size_t N = nodes_.size();
    const double mask_mult = std::pow(1.0 - config_.mask_usage_rate * config_.mask_efficiency, 2.0);
    const double beta_eff = config_.beta * mask_mult * config_.contacts_per_day;
    const double mean_d = 0.5 * (config_.infectious_min + config_.infectious_max);
    const double p_rec = 1.0 / std::max(1.0, mean_d);

    ++day_;
    events_.clear();

    // 1) Prevalencia presente en cada municipio j (visitantes incluidos)  [CONTAGIO ORIGINAL, sin cambios]
    //    Cada replica es una red independiente (mezcla solo dentro de la replica).
    #pragma omp parallel for schedule(static) num_threads(threads_)
    for (size_t g = 0; g < N; ++g) {
        const size_t r = g / B, j = g % B;
        double inf_j = 0.0;
        for (size_t i = 0; i < B; ++i) inf_j += P_[i][j] * static_cast<double>(I_all[r * B + i]);
        prev_[g] = popmix_[j] > 0.0 ? inf_j / popmix_[j] : 0.0;
    }

    // 2) Contagios y recuperaciones por nodo propio (cada hilo usa solo el RNG de su nodo).
    //    Un municipio que la epidemia aun no ha tocado (I=0 y R=0) no se contagia "por mezcla":
    //    solo puede infectarse por un salto con probabilidad (fase 3).
    const int64_t K = static_cast<int64_t>(owned_.size());
    #pragma omp parallel for schedule(static) num_threads(threads_)
    for (int64_t k = 0; k < K; ++k) {
        const size_t g = static_cast<size_t>(owned_[k]);
        const size_t r = g / B, b = g % B;
        auto& node = nodes_[g];
        const bool reached = node.I > 0 || node.R > 0;
        double hazard = 0.0;
        if (reached) for (size_t j = 0; j < B; ++j) hazard += P_[b][j] * prev_[r * B + j];
        const double lambda = 1.0 - std::exp(-beta_eff * hazard);
        std::binomial_distribution<uint64_t> inf_dist(node.S, std::clamp(lambda, 0.0, 1.0));
        std::binomial_distribution<uint64_t> rec_dist(node.I, p_rec);
        const uint64_t new_inf = node.S > 0 ? inf_dist(*rngs_[g]) : 0;
        const uint64_t new_rec = node.I > 0 ? rec_dist(*rngs_[g]) : 0;
        node.S -= new_inf;
        node.I = node.I + new_inf - new_rec;
        node.R += new_rec;
        node.new_infections = new_inf;
    }
}

void NetworkEngine::phase_draw_jumps(std::vector<SpreadEvent>& candidates) {
    const size_t B = base_.size();
    const double mask_mult = std::pow(1.0 - config_.mask_usage_rate * config_.mask_efficiency, 2.0);
    const int64_t K = static_cast<int64_t>(owned_.size());
    std::vector<std::vector<SpreadEvent>> bucket(static_cast<size_t>(K));

    // Por cada origen infectado se sortea UN numero por cada destino (siempre, sin mirar el estado del
    // destino): asi el consumo del RNG del origen solo depende del origen y no de la particion.
    #pragma omp parallel for schedule(static) num_threads(threads_)
    for (int64_t k = 0; k < K; ++k) {
        const size_t g = static_cast<size_t>(owned_[k]);
        const auto& src = nodes_[g];
        if (src.I == 0) continue;
        const size_t r = g / B, b = g % B;
        auto& rng = *spread_rngs_[g];
        for (size_t j = 0; j < B; ++j) {
            if (j == b) continue;
            const double u = std::uniform_real_distribution<double>(0.0, 1.0)(rng);
            const double lam = P_[b][j] * static_cast<double>(src.I) * mask_mult * kImportEfficiency;
            const double p = 1.0 - std::exp(-lam);
            if (u < p) bucket[k].push_back({day_, static_cast<int>(g), static_cast<int>(r * B + j), 1});
        }
    }
    for (auto& v : bucket) candidates.insert(candidates.end(), v.begin(), v.end());
}

void NetworkEngine::phase_resolve_jumps(std::vector<SpreadEvent> candidates) {
    // Orden canonico (destino, origen): el origen de menor indice gana el destino.
    std::sort(candidates.begin(), candidates.end(), [](const SpreadEvent& a, const SpreadEvent& b) {
        return a.to != b.to ? a.to < b.to : a.from < b.from;
    });
    for (const auto& c : candidates) {
        if (c.to < 0 || static_cast<size_t>(c.to) >= nodes_.size() || !is_owned_[c.to]) continue;
        auto& dst = nodes_[c.to];
        if (dst.I > 0 || dst.R > 0 || dst.S == 0) continue;   // ya tocado por la epidemia
        dst.S -= 1; dst.I += 1; dst.new_infections += 1;
        events_.push_back({day_, c.from, c.to, 1});
    }
}

void NetworkEngine::step() {
    std::vector<uint64_t> I_all;
    collect_local_infected(I_all);
    phase_dynamics(I_all);
    std::vector<SpreadEvent> cands;
    phase_draw_jumps(cands);
    phase_resolve_jumps(std::move(cands));
}

bool NetworkEngine::write_csv(const std::string& path, const std::vector<std::vector<NetNode>>& history) const {
    std::ofstream out(path);
    if (!out) return false;
    out << "day,id,name,susceptible,infected,recovered,vaccinated,new_infections\n";
    const size_t B = base_.size();
    for (size_t d = 0; d < history.size(); ++d) {
        if (replicas_ == 1) {
            for (const auto& nd : history[d])
                out << d << ',' << nd.id << ',' << nd.name << ',' << nd.S << ',' << nd.I << ',' << nd.R << ','
                    << nd.V << ',' << nd.new_infections << '\n';
        } else {
            for (size_t b = 0; b < B; ++b) {
                uint64_t S = 0, I = 0, R = 0, V = 0, N = 0;
                for (int r = 0; r < replicas_; ++r) {
                    const auto& nd = history[d][static_cast<size_t>(r) * B + b];
                    S += nd.S; I += nd.I; R += nd.R; V += nd.V; N += nd.new_infections;
                }
                out << d << ',' << base_[b].id << ',' << base_[b].name << ',' << S << ',' << I << ',' << R << ','
                    << V << ',' << N << '\n';
            }
        }
    }
    return true;
}
