#ifndef CSV_WRITER_HPP
#define CSV_WRITER_HPP

#include "../core/SimulationConfig.hpp"
#include <fstream>
#include <vector>
#include <iostream>
#include <iomanip>

class CSVWriter {
public:
    static bool write_daily_stats(const std::string& filepath, const std::vector<DailyStats>& stats) {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "[ERROR] No se pudo abrir para escritura: " << filepath << std::endl;
            return false;
        }

        // Cabecera CSV
        file << "day,susceptible,infected,recovered,vaccinated,new_infections,new_recoveries,"
             << "region_a_pop,region_b_pop,region_a_infected,region_b_infected,"
             << "migrants_a_to_b,migrants_b_to_a,step_time_ms\n";

        for (const auto& s : stats) {
            file << s.day << ","
                 << s.susceptible << ","
                 << s.infected << ","
                 << s.recovered << ","
                 << s.vaccinated << ","
                 << s.new_infections << ","
                 << s.new_recoveries << ","
                 << s.region_a_pop << ","
                 << s.region_b_pop << ","
                 << s.region_a_infected << ","
                 << s.region_b_infected << ","
                 << s.migrants_a_to_b << ","
                 << s.migrants_b_to_a << ","
                 << std::fixed << std::setprecision(4) << s.step_time_ms << "\n";
        }

        file.close();
        return true;
    }
};

#endif // CSV_WRITER_HPP
