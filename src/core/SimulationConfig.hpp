#ifndef SIMULATION_CONFIG_HPP
#define SIMULATION_CONFIG_HPP

#include <cmath>
#include <cstdint>
#include <string>

struct SimulationConfig {
    uint64_t population_size = 50000;
    uint64_t initial_infected = 100;
    float beta = 0.35f;               // Tasa de transmisión de contacto
    float gamma = 0.07f;              // Tasa de recuperación (~14 días)
    float contact_radius = 0.015f;     // Radio de proximidad física
    float mobility_rate = 0.01f;       // Probabilidad diaria de cruzar de región A <-> B
    float vaccination_rate = 0.10f;    // Porcentaje de población vacunada
    float mask_usage_rate = 0.30f;     // Porcentaje con mascarilla/protección
    float mask_efficiency = 0.65f;     // Reducción de probabilidad de contagio
    uint32_t num_days = 60;            // Duración de la simulación
    uint64_t seed = 42;                // Semilla para reproducibilidad
    // Duracion del periodo infeccioso (dias), uniforme en [min, max]. Por defecto 10-20 (comportamiento original).
    uint16_t infectious_min = 10;
    uint16_t infectious_max = 20;
    // Contactos esperados por persona y dia. Si es > 0 el radio de contacto se calcula a partir de la
    // densidad de cada region (ver apply_contact_model); si es 0 se usa contact_radius tal cual.
    float contacts_per_day = 0.0f;
    float space_width = 1.0f;
    float space_height = 1.0f;
    std::string output_file = "results/simulations/sequential_results.csv";
};

// Calibra contact_radius para que cada persona tenga, en promedio, `contacts_per_day` vecinos
// dentro del radio (region = cuadrado unitario con N/2 individuos: k = (N/2) * pi * r^2).
inline void apply_contact_model(SimulationConfig& c) {
    if (c.contacts_per_day > 0.0f && c.population_size > 1) {
        const double n_region = static_cast<double>(c.population_size) / 2.0;
        c.contact_radius = static_cast<float>(std::sqrt(c.contacts_per_day / (3.14159265358979 * n_region)));
    }
    if (c.infectious_max < c.infectious_min) c.infectious_max = c.infectious_min;
}

struct DailyStats {
    uint32_t day;
    uint64_t susceptible;
    uint64_t infected;
    uint64_t recovered;
    uint64_t vaccinated;
    uint64_t new_infections;
    uint64_t new_recoveries;
    uint64_t region_a_pop;
    uint64_t region_b_pop;
    uint64_t region_a_infected;
    uint64_t region_b_infected;
    uint64_t migrants_a_to_b;
    uint64_t migrants_b_to_a;
    double step_time_ms;
};

#endif // SIMULATION_CONFIG_HPP
