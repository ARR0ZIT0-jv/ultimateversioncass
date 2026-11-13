#ifndef NET_PARTITION_HPP
#define NET_PARTITION_HPP

// Particion de los municipios entre Worker 1 y Worker 2 (configurable con --partition).
//   geo        : Worker 1 = Gran Chaco + Burdet O'Connor (ids 1 Yacuiba, 2 Villamontes, 5 Entre Rios, 8 Caraparí)
//                Worker 2 = valle y altiplano (resto). Es la particion por defecto.
//   balanced   : reparto voraz (LPT) por poblacion: cada municipio, de mayor a menor, al worker menos cargado.
//   1,2,2,1... : lista explicita con el rank (1 o 2) de cada municipio base, en el orden del CSV.
#include "parallel/NetworkEngine.hpp"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

inline std::vector<int> make_partition(const std::vector<NetNode>& base, const std::string& spec) {
    const size_t B = base.size();
    std::vector<int> owner(B, 2);
    if (spec == "balanced") {
        std::vector<size_t> idx(B);
        std::iota(idx.begin(), idx.end(), 0);
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return base[a].population > base[b].population; });
        uint64_t load[3] = {0, 0, 0};
        for (size_t k : idx) {
            const int w = load[1] <= load[2] ? 1 : 2;
            owner[k] = w;
            load[w] += base[k].population;
        }
    } else if (spec.find(',') != std::string::npos) {
        std::stringstream ss(spec);
        std::string tok;
        size_t k = 0;
        while (std::getline(ss, tok, ',') && k < B) owner[k++] = (std::stoi(tok) == 1) ? 1 : 2;
    } else { // geo
        for (size_t k = 0; k < B; ++k) {
            const int id = base[k].id;
            owner[k] = (id == 1 || id == 2 || id == 5 || id == 8) ? 1 : 2;
        }
    }
    return owner;
}

#endif // NET_PARTITION_HPP
