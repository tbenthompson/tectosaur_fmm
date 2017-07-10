#pragma once
#include <array>
#include <cmath>

template <size_t dim>
inline double dot(const std::array<double,dim>& a, const std::array<double,dim>& b) {
    double out = 0;
    for (size_t d = 0; d < dim; d++) {
        out += a[d] * b[d];
    }
    return out;
}

template <size_t dim>
inline std::array<double,dim> sub(const std::array<double,dim>& a, const std::array<double,dim>& b) {
    std::array<double,dim> out;
    for (size_t d = 0; d < dim; d++) {
        out[d] = a[d] - b[d];
    }
    return out;
}

template <size_t dim>
inline double hypot(const std::array<double,dim>& v) {
    return std::sqrt(dot(v, v));
}

template <size_t dim>
inline double dist(const std::array<double,dim>& a, const std::array<double,dim>& b) {
    return hypot(sub(a,b));
}

template <size_t dim>
struct Cube {
    std::array<double,dim> center;
    double width;

    double R() const {
        return width * std::sqrt(static_cast<double>(dim));
    }
};

template <size_t dim>
Cube<dim> bounding_box(std::array<double,dim>* pts, size_t n_pts) {
    std::array<double,dim> center_of_mass{};
    for (size_t i = 0; i < n_pts; i++) {
        for (size_t d = 0; d < dim; d++) {
            center_of_mass[d] += pts[i][d];
        }
    }
    for (size_t d = 0; d < dim; d++) {
        center_of_mass[d] /= n_pts;
    }

    double max_width = 0.0;
    for (size_t i = 0; i < n_pts; i++) {
        for (size_t d = 0; d < dim; d++) {
            max_width = std::max(max_width, fabs(pts[i][d] - center_of_mass[d]));
        }
    }

    return {center_of_mass, max_width};
}

template <size_t dim>
std::array<size_t,dim> make_child_idx(size_t i) 
{
    std::array<size_t,dim> child_idx;
    for (int d = dim - 1; d >= 0; d--) {
        auto idx = i % 2;
        i = i >> 1;
        child_idx[d] = idx;
    }
    return child_idx;
}

template <size_t dim>
Cube<dim> get_subcell(const Cube<dim>& b, const std::array<size_t,dim>& idx)
{
    auto new_width = b.width / 2.0;
    auto new_center = b.center;
    for (size_t d = 0; d < dim; d++) {
        new_center[d] += ((static_cast<double>(idx[d]) * 2) - 1) * new_width;
    }
    return {new_center, new_width};
}

template <size_t dim>
int find_containing_subcell(const Cube<dim>& b, const std::array<double,dim>& pt) {
    int child_idx = 0;
    for (size_t d = 0; d < dim; d++) {
        if (pt[d] > b.center[d]) {
            child_idx++; 
        }
        if (d < dim - 1) {
            child_idx = child_idx << 1;
        }
    }
    return child_idx;
}

template <size_t dim>
bool in_box(const Cube<dim>& b, const std::array<double,dim>& pt) {
    for (size_t d = 0; d < dim; d++) {
        if (fabs(pt[d] - b.center[d]) >= (1.0 + 1e-14) * b.width) {
            return false;
        }
    }
    return true;
}
