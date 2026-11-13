#ifndef PRNG_HPP
#define PRNG_HPP

#include <random>
#include <cstdint>

class PRNG {
public:
    explicit PRNG(uint64_t seed = 42) : rng_(seed), dist_unit_(0.0f, 1.0f) {}

    void set_seed(uint64_t seed) {
        rng_.seed(seed);
    }

    // Retorna flotante en [0.0, 1.0)
    float uniform_float() {
        return dist_unit_(rng_);
    }

    // Retorna flotante en [min_val, max_val)
    float uniform_range(float min_val, float max_val) {
        return min_val + (max_val - min_val) * dist_unit_(rng_);
    }

    // Retorna entero en [min_val, max_val]
    int uniform_int(int min_val, int max_val) {
        std::uniform_int_distribution<int> dist(min_val, max_val);
        return dist(rng_);
    }

    // Test Bernoulli con probabilidad p
    bool bernoulli(float p) {
        if (p <= 0.0f) return false;
        if (p >= 1.0f) return true;
        return dist_unit_(rng_) < p;
    }

private:
    std::mt19937_64 rng_;
    std::uniform_real_distribution<float> dist_unit_;
};

#endif // PRNG_HPP
