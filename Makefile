CXX = g++
MPICXX = mpicxx
CXXFLAGS = -std=c++20 -O3 -fopenmp -Wall -Wextra -pedantic -DOMPI_SKIP_MPICXX
INCLUDES = -Isrc

SRC_CORE = src/core/Individual.hpp src/core/SimulationConfig.hpp src/core/SpatialGrid.hpp
SRC_ENGINE = src/sequential/SequentialEngine.cpp src/parallel/OpenMPEngine.cpp src/parallel/NetworkEngine.cpp
SRC_DIST = src/parallel/distributed/MPIMaster.cpp src/parallel/distributed/MPIWorker.cpp src/parallel/distributed/NetMaster.cpp src/parallel/distributed/NetWorker.cpp src/parallel/NetworkEngine.cpp
SRC_UTILS = src/utils/PRNG.hpp src/utils/Timer.hpp src/utils/CSVWriter.hpp

TARGET_LOCAL = epidemic_sim
TARGET_MPI = epidemic_sim_mpi

all: $(TARGET_LOCAL)

$(TARGET_LOCAL): $(SRC_ENGINE:.cpp=.o) src/main.o
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

mpi: $(TARGET_MPI)

$(TARGET_MPI): $(SRC_DIST:.cpp=.o) src/mpi_main.o
	$(MPICXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

src/parallel/distributed/%.o: src/parallel/distributed/%.cpp
	$(MPICXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

src/mpi_main.o: src/mpi_main.cpp
	$(MPICXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f src/sequential/*.o src/parallel/*.o src/parallel/distributed/*.o src/*.o $(TARGET_LOCAL) $(TARGET_MPI) epidemic_sim_mpi_shim
	rm -f results/simulations/test_*.csv

clean-obj:
	rm -f src/sequential/*.o src/parallel/*.o src/parallel/distributed/*.o src/*.o

test_seq: $(TARGET_LOCAL)
	./$(TARGET_LOCAL) --mode seq --pop 50000 --infected 100 --days 30 --output results/simulations/test_seq_50k.csv

test_omp: $(TARGET_LOCAL)
	./$(TARGET_LOCAL) --mode omp --threads 4 --pop 50000 --infected 100 --days 30 --output results/simulations/test_omp_50k.csv

# ---- Pruebas del protocolo Master/Worker SIN OpenMPI (shim de procesos, NO es MPI real) ----
SHIM_FLAGS = -std=c++20 -O2 -fopenmp -Wall -Wextra -pedantic -Itests/mpi_shim -Isrc
epidemic_sim_mpi_shim: $(SRC_DIST) src/mpi_main.cpp
	$(CXX) $(SHIM_FLAGS) -o $@ $^

NET_ARGS = --days 60 --infected 101 --beta 0.035 --gamma 0 --min-days 6 --max-days 10 --contacts 10 --mobility 0.03 --mask 0.3 --seed 42
test_net_equal: $(TARGET_LOCAL) epidemic_sim_mpi_shim
	./$(TARGET_LOCAL) --mode net --threads 1 $(NET_ARGS) --output results/simulations/test_net_local.csv > /dev/null
	./epidemic_sim_mpi_shim --mode net --threads 2 $(NET_ARGS) --output results/simulations/test_net_mpi.csv > /dev/null
	cmp results/simulations/test_net_local.csv results/simulations/test_net_mpi.csv && echo "OK: net local (1 proceso) == net MPI (3 procesos)"

.PHONY: all mpi clean clean-obj test_seq test_omp test_net_equal
