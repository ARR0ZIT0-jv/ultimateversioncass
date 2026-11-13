#include "core/SimulationConfig.hpp"
#include "sequential/SequentialEngine.hpp"
#include "parallel/OpenMPEngine.hpp"
#include "parallel/NetworkEngine.hpp"
#include "utils/CSVWriter.hpp"
#include "utils/Timer.hpp"
#include <algorithm>
#include <memory>
#include <iostream>
#include <string>
#include <iomanip>
#include <omp.h>
#include <sys/resource.h>

void print_help(const char* prog) {
    std::cout << "Uso: " << prog << " [opciones]\n"
              << "Opciones epidemiologicas y de ejecucion:\n"
              << "  --mode <seq|omp|mpi>     Modo de simulacion (defecto: seq)\n"
              << "  --threads <N>            Hilos OpenMP (defecto: max del sistema)\n"
              << "  --pop <N>                Poblacion total (defecto: 50000)\n"
              << "  --infected <N>           Infectados iniciales (defecto: 100)\n"
              << "  --days <N>               Dias a simular (defecto: 60)\n"
              << "  --beta <float>           Tasa de transmision (defecto: 0.35)\n"
              << "  --gamma <float>          Tasa de recuperacion (defecto: 0.07)\n"
              << "  --mobility <float>       Tasa de movilidad A <-> B (defecto: 0.01)\n"
              << "  --vaccination <float>    Porcentaje de vacunacion (defecto: 0.10)\n"
              << "  --mask <float>           Uso de mascarillas (defecto: 0.30)\n"
              << "  --seed <uint64>          Semilla PRNG (defecto: 42)\n"
              << "  --min-days <N>           Duracion minima del periodo infeccioso (defecto: 10)\n"
              << "  --max-days <N>           Duracion maxima del periodo infeccioso (defecto: 20)\n"
              << "  --contacts <float>       Contactos esperados por persona/dia (0 = usar radio fijo)\n"
              << "  --nodes <csv>            (modo net) CSV de municipios\n"
              << "  --seed-node <id>         (modo net) municipio donde empieza el brote\n"
              << "  --replicas <R>           (modo net) clona la red R veces (carga SINTETICA para escalabilidad)\n"
              << "  --output <ruta>          Ruta archivo CSV de salida\n"
              << "  --help                   Muestra esta ayuda\n";
}

int main(int argc, char* argv[]) {
    SimulationConfig config;
    std::string mode = "seq";
    int threads = omp_get_max_threads();
    std::string nodes_path = "data/tarija_municipios.csv";
    int seed_node = 1; // Yacuiba
    int replicas = 1;  // (modo net) clones de la red: carga sintetica para estudiar escalabilidad

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) { mode = argv[++i]; }
        else if (arg == "--threads" && i + 1 < argc) { threads = std::stoi(argv[++i]); }
        else if (arg == "--pop" && i + 1 < argc) { config.population_size = std::stoull(argv[++i]); }
        else if (arg == "--infected" && i + 1 < argc) { config.initial_infected = std::stoull(argv[++i]); }
        else if (arg == "--days" && i + 1 < argc) { config.num_days = std::stoul(argv[++i]); }
        else if (arg == "--beta" && i + 1 < argc) { config.beta = std::stof(argv[++i]); }
        else if (arg == "--gamma" && i + 1 < argc) { config.gamma = std::stof(argv[++i]); }
        else if (arg == "--mobility" && i + 1 < argc) { config.mobility_rate = std::stof(argv[++i]); }
        else if (arg == "--vaccination" && i + 1 < argc) { config.vaccination_rate = std::stof(argv[++i]); }
        else if (arg == "--mask" && i + 1 < argc) { config.mask_usage_rate = std::stof(argv[++i]); }
        else if (arg == "--seed" && i + 1 < argc) { config.seed = std::stoull(argv[++i]); }
        else if (arg == "--min-days" && i + 1 < argc) { config.infectious_min = static_cast<uint16_t>(std::stoul(argv[++i])); }
        else if (arg == "--max-days" && i + 1 < argc) { config.infectious_max = static_cast<uint16_t>(std::stoul(argv[++i])); }
        else if (arg == "--contacts" && i + 1 < argc) { config.contacts_per_day = std::stof(argv[++i]); }
        else if (arg == "--nodes" && i + 1 < argc) { nodes_path = argv[++i]; }
        else if (arg == "--seed-node" && i + 1 < argc) { seed_node = std::stoi(argv[++i]); }
        else if (arg == "--replicas" && i + 1 < argc) { replicas = std::max(1, std::stoi(argv[++i])); }
        else if (arg == "--output" && i + 1 < argc) { config.output_file = argv[++i]; }
        else if (arg == "--help") {
            print_help(argv[0]);
            return 0;
        }
    }

    apply_contact_model(config);

    std::cout << "========================================================================\n"
              << " SIMULADOR DISTRIBUIDO DE PROPAGACION DE EPIDEMIAS\n"
              << " Modo: " << mode 
              << (mode == "omp" ? (" | Hilos OpenMP: " + std::to_string(threads)) : "")
              << " | Poblacion: " << config.population_size
              << " | Dias: " << config.num_days << "\n"
              << " Parametros: beta=" << config.beta << ", gamma=" << config.gamma
              << ", movilidad=" << config.mobility_rate << ", vacunacion=" << config.vaccination_rate << "\n"
              << "========================================================================\n";

    if (mode == "seq") {
        Timer total_timer;
        SequentialEngine engine(config);
        
        std::cout << "[INFO] Inicializando poblacion y particion geografica (Region A y B)...\n";
        engine.initialize();

        std::cout << "[INFO] Ejecutando simulación secuencial paso a paso...\n";
        std::cout << std::string(80, '-') << "\n";
        std::cout << std::setw(6) << "Dia"
                  << std::setw(10) << "Suscept"
                  << std::setw(10) << "Infect"
                  << std::setw(10) << "Recup"
                  << std::setw(10) << "Vacun"
                  << std::setw(11) << "Nuev.Inf"
                  << std::setw(11) << "Inf.RegA"
                  << std::setw(11) << "Inf.RegB"
                  << std::setw(11) << "Tiempo(ms)" << "\n";
        std::cout << std::string(80, '-') << "\n";

        std::vector<DailyStats> history;
        history.reserve(config.num_days);

        for (uint32_t day = 1; day <= config.num_days; ++day) {
            DailyStats s = engine.step(day);
            history.push_back(s);

            if (day == 1 || day % 5 == 0 || day == config.num_days) {
                std::cout << std::setw(6) << s.day
                          << std::setw(10) << s.susceptible
                          << std::setw(10) << s.infected
                          << std::setw(10) << s.recovered
                          << std::setw(10) << s.vaccinated
                          << std::setw(11) << s.new_infections
                          << std::setw(11) << s.region_a_infected
                          << std::setw(11) << s.region_b_infected
                          << std::setw(11) << std::fixed << std::setprecision(2) << s.step_time_ms << "\n";
            }
        }

        double total_ms = total_timer.elapsed_ms();
        std::cout << std::string(80, '-') << "\n";
        std::cout << "[EXITO] Simulacion secuencial completada en " 
                  << std::fixed << std::setprecision(2) << total_ms << " ms ("
                  << (total_ms / 1000.0) << " s).\n";

        if (CSVWriter::write_daily_stats(config.output_file, history)) {
            std::cout << "[INFO] Resultados guardados exitosamente en: " << config.output_file << "\n";
        }
    } else if (mode == "omp") {
        Timer total_timer;
        OpenMPEngine engine(config, threads);

        std::cout << "[INFO] Inicializando poblacion en paralelo (" << threads << " hilos OpenMP)...\n";
        engine.initialize();

        std::cout << "[INFO] Ejecutando simulación paralela OpenMP paso a paso...\n";
        std::cout << std::string(80, '-') << "\n";
        std::cout << std::setw(6) << "Dia"
                  << std::setw(10) << "Suscept"
                  << std::setw(10) << "Infect"
                  << std::setw(10) << "Recup"
                  << std::setw(10) << "Vacun"
                  << std::setw(11) << "Nuev.Inf"
                  << std::setw(11) << "Inf.RegA"
                  << std::setw(11) << "Inf.RegB"
                  << std::setw(11) << "Tiempo(ms)" << "\n";
        std::cout << std::string(80, '-') << "\n";

        std::vector<DailyStats> history;
        history.reserve(config.num_days);

        for (uint32_t day = 1; day <= config.num_days; ++day) {
            DailyStats s = engine.step(day);
            history.push_back(s);

            if (day == 1 || day % 5 == 0 || day == config.num_days) {
                std::cout << std::setw(6) << s.day
                          << std::setw(10) << s.susceptible
                          << std::setw(10) << s.infected
                          << std::setw(10) << s.recovered
                          << std::setw(10) << s.vaccinated
                          << std::setw(11) << s.new_infections
                          << std::setw(11) << s.region_a_infected
                          << std::setw(11) << s.region_b_infected
                          << std::setw(11) << std::fixed << std::setprecision(2) << s.step_time_ms << "\n";
            }
        }

        double total_ms = total_timer.elapsed_ms();
        std::cout << std::string(80, '-') << "\n";
        std::cout << "[EXITO] Simulacion OpenMP (" << threads << " hilos) completada en " 
                  << std::fixed << std::setprecision(2) << total_ms << " ms ("
                  << (total_ms / 1000.0) << " s).\n";

        if (CSVWriter::write_daily_stats(config.output_file, history)) {
            std::cout << "[INFO] Resultados guardados exitosamente en: " << config.output_file << "\n";
        }
    } else if (mode == "net") {
        Timer total_timer;
        NetworkEngine engine(config, threads, replicas);
        if (!engine.load_nodes(nodes_path)) return 1;
        engine.initialize(seed_node);
        std::vector<std::vector<NetNode>> history;
        history.push_back(engine.nodes());
        std::vector<SpreadEvent> links;
        uint64_t total_links = 0;
        double compute_ms = 0.0;
        for (uint32_t day = 1; day <= config.num_days; ++day) {
            Timer t_step;
            engine.step();
            compute_ms += t_step.elapsed_ms();
            history.push_back(engine.nodes());
            total_links += engine.events().size();
            if (replicas == 1) for (const auto& e : engine.events()) links.push_back(e);
        }
        {   // [dia, origen, destino, casos, cruza_worker]  (modo local: nunca cruza)
            std::cout << "[ENLACES] [";
            for (size_t k = 0; k < links.size(); ++k)
                std::cout << (k ? "," : "") << "[" << links[k].day << "," << links[k].from << ","
                          << links[k].to << "," << links[k].cases << ",0]";
            std::cout << "]\n";
        }
        std::cout << "[NET_RESUMEN] {\"modo\":\"net\",\"procesos\":1,\"hilos\":" << threads
                  << ",\"replicas\":" << replicas << ",\"nodos\":" << engine.total_nodes()
                  << ",\"saltos\":" << total_links << ",\"compute_ms\":" << std::fixed << std::setprecision(3)
                  << compute_ms << ",\"total_ms\":" << total_timer.elapsed_ms()
                  << ",\"peak_rss_kb\":" << []{ struct rusage ru; getrusage(RUSAGE_SELF, &ru); return static_cast<long>(ru.ru_maxrss); }()
                  << "}\n";
        uint64_t peak = 0; uint32_t peak_day = 0;
        for (size_t d = 0; d < history.size(); ++d) {
            uint64_t tot = 0;
            for (const auto& nd : history[d]) tot += nd.I;
            if (tot > peak) { peak = tot; peak_day = static_cast<uint32_t>(d); }
        }
        {
            std::cout << "[MOVILIDAD] [";
            const auto& P = engine.mobility();
            for (size_t i = 0; i < P.size(); ++i) {
                std::cout << (i ? "," : "") << "[";
                for (size_t j = 0; j < P[i].size(); ++j) std::cout << (j ? "," : "") << std::setprecision(6) << P[i][j];
                std::cout << "]";
            }
            std::cout << "]\n";
        }
        std::cout << "[EXITO] Red de " << engine.nodes().size() << " municipios simulada en "
                  << std::fixed << std::setprecision(2) << total_timer.elapsed_ms() << " ms | pico "
                  << peak << " infectados en el dia " << peak_day << "\n";
        if (engine.write_csv(config.output_file, history))
            std::cout << "[INFO] Resultados guardados exitosamente en: " << config.output_file << "\n";
        else { std::cerr << "[ERROR] No se pudo escribir " << config.output_file << "\n"; return 1; }
    } else {
        std::cerr << "[ERROR] Modo '" << mode << "' no reconocido. Modos validos: seq, omp, net.\n";
        return 1;
    }

    return 0;
}
