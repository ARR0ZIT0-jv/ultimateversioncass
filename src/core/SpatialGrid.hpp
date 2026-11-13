#ifndef SPATIAL_GRID_HPP
#define SPATIAL_GRID_HPP

#include "Individual.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <atomic>

// Grilla espacial paralela lock-free de alto rendimiento basada en Particle Linked-List
class ParallelSpatialGrid {
public:
    ParallelSpatialGrid(float width, float height, float cell_size)
        : width_(width), height_(height), cell_size_(cell_size) {
        cols_ = std::max(1, static_cast<int>(std::floor(width_ / cell_size_)));
        rows_ = std::max(1, static_cast<int>(std::floor(height_ / cell_size_)));
        total_cells_ = cols_ * rows_;
        head_.assign(total_cells_, -1);
    }

    void reset(size_t pop_size) {
        head_.assign(total_cells_, -1);
        if (next_.size() < pop_size) {
            next_.resize(pop_size, -1);
        }
    }

    int get_cell_index(float x, float y) const {
        int cx = std::clamp(static_cast<int>(x / cell_size_), 0, cols_ - 1);
        int cy = std::clamp(static_cast<int>(y / cell_size_), 0, rows_ - 1);
        return cy * cols_ + cx;
    }

    // Inserción atómica lock-free O(1) thread-safe
    void insert_atomic(int individual_idx, float x, float y) {
        int cell_idx = get_cell_index(x, y);
        std::atomic_ref<int> cell_head(head_[cell_idx]);
        int old_head = cell_head.exchange(individual_idx, std::memory_order_relaxed);
        next_[individual_idx] = old_head;
    }

    void get_neighbor_cells(int cx, int cy, std::vector<int>& neighbors) const {
        neighbors.clear();
        for (int dy = -1; dy <= 1; ++dy) {
            int ny = cy + dy;
            if (ny < 0 || ny >= rows_) continue;
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = cx + dx;
                if (nx < 0 || nx >= cols_) continue;
                neighbors.push_back(ny * cols_ + nx);
            }
        }
    }

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    int total_cells() const { return total_cells_; }
    int get_head(int cell_idx) const { return head_[cell_idx]; }
    int get_next(int individual_idx) const { return next_[individual_idx]; }

private:
    float width_;
    float height_;
    float cell_size_;
    int cols_;
    int rows_;
    int total_cells_;
    std::vector<int> head_;
    std::vector<int> next_;
};

// Grilla espacial secuencial estándar
class SpatialGrid {
public:
    SpatialGrid(float width, float height, float cell_size)
        : width_(width), height_(height), cell_size_(cell_size) {
        cols_ = std::max(1, static_cast<int>(std::floor(width_ / cell_size_)));
        rows_ = std::max(1, static_cast<int>(std::floor(height_ / cell_size_)));
        cells_.resize(cols_ * rows_);
    }

    void clear() {
        for (auto& cell : cells_) cell.clear();
    }

    void insert(size_t individual_idx, float x, float y) {
        int cx = std::clamp(static_cast<int>(x / cell_size_), 0, cols_ - 1);
        int cy = std::clamp(static_cast<int>(y / cell_size_), 0, rows_ - 1);
        cells_[cy * cols_ + cx].push_back(individual_idx);
    }

    int get_cell_index(float x, float y) const {
        int cx = std::clamp(static_cast<int>(x / cell_size_), 0, cols_ - 1);
        int cy = std::clamp(static_cast<int>(y / cell_size_), 0, rows_ - 1);
        return cy * cols_ + cx;
    }

    void get_neighbor_cells(int cx, int cy, std::vector<int>& neighbors) const {
        neighbors.clear();
        for (int dy = -1; dy <= 1; ++dy) {
            int ny = cy + dy;
            if (ny < 0 || ny >= rows_) continue;
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = cx + dx;
                if (nx < 0 || nx >= cols_) continue;
                neighbors.push_back(ny * cols_ + nx);
            }
        }
    }

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    const std::vector<size_t>& get_cell(int cell_idx) const { return cells_[cell_idx]; }

private:
    float width_, height_, cell_size_;
    int cols_, rows_;
    std::vector<std::vector<size_t>> cells_;
};

#endif // SPATIAL_GRID_HPP
