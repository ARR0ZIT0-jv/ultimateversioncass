#ifndef TIMER_HPP
#define TIMER_HPP

#include <chrono>

class Timer {
public:
    Timer() {
        reset();
    }

    void reset() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }

    double elapsed_ms() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end_time - start_time_).count();
    }

    double elapsed_seconds() const {
        return elapsed_ms() / 1000.0;
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
};

#endif // TIMER_HPP
