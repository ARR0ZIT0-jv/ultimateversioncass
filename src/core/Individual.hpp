#ifndef INDIVIDUAL_HPP
#define INDIVIDUAL_HPP

#include <cstdint>

enum class HealthState : uint8_t {
    SUSCEPTIBLE = 0,
    INFECTED = 1,
    RECOVERED = 2,
    VACCINATED = 3
};

struct Individual {
    uint64_t id;
    float x;
    float y;
    float vx;
    float vy;
    HealthState state;
    uint16_t days_infected;
    uint16_t infection_duration;
    uint8_t region_id;      // 0: Region A, 1: Region B
    float mask_factor;      // 0.0: sin proteccion, 0.7: reduce 70% contagio
    bool is_migrating;      // Marcado para cruzar de region
};

#endif // INDIVIDUAL_HPP
