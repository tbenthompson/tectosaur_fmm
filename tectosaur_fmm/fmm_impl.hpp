#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include "fmm_kernels.hpp"
#include "octree.hpp"
#include "blas_wrapper.hpp"
#include "translation_surf.hpp"

template <size_t dim>
struct FMMConfig {
    // The MAC needs to < (1.0 / (check_r - 1)) so that farfield
    // approximations aren't used when the check surface intersects the
    // target box. How about we just use that as our MAC, since errors
    // further from the check surface should flatten out!
    double inner_r;
    double outer_r;
    size_t order;
    Kernel<dim> kernel;
    std::vector<double> params;

    std::string kernel_name() const { return kernel.name; }
    int tensor_dim() const { return kernel.tensor_dim; }
};

template <size_t dim>
std::vector<double> c2e_solve(std::vector<std::array<double,dim>> surf,
    const Cube<dim>& bounds, double check_r, double equiv_r, const FMMConfig<dim>& cfg);

struct MatrixFreeOp {
    std::vector<int> obs_n_start;
    std::vector<int> obs_n_end;
    std::vector<int> obs_n_idx;
    std::vector<int> src_n_start;
    std::vector<int> src_n_end;
    std::vector<int> src_n_idx;

    template <size_t dim>
    void insert(const OctreeNode<dim>& obs_n, const OctreeNode<dim>& src_n) {
        obs_n_start.push_back(obs_n.start);
        obs_n_end.push_back(obs_n.end);
        obs_n_idx.push_back(obs_n.idx);
        src_n_start.push_back(src_n.start);
        src_n_end.push_back(src_n.end);
        src_n_idx.push_back(src_n.idx);
    }
};

template <size_t dim>
struct FMMMat {
    Octree<dim> obs_tree;
    Octree<dim> src_tree;
    FMMConfig<dim> cfg;
    std::vector<std::array<double,dim>> surf;

    MatrixFreeOp p2m;
    std::vector<MatrixFreeOp> m2m;
    MatrixFreeOp p2l;
    MatrixFreeOp m2l;
    std::vector<MatrixFreeOp> l2l;
    MatrixFreeOp p2p;
    MatrixFreeOp m2p;
    MatrixFreeOp l2p;

    std::vector<double> u2e_ops;
    std::vector<MatrixFreeOp> u2e;

    std::vector<double> d2e_ops;
    std::vector<MatrixFreeOp> d2e;

    FMMMat(Octree<dim> obs_tree, Octree<dim> src_tree, FMMConfig<dim> cfg,
        std::vector<std::array<double,dim>> surf);

    int tensor_dim() const { return cfg.tensor_dim(); }

    void p2m_matvec(double* out, double* in);
    void m2m_matvec(double* out, double* in, int level);
    void p2l_matvec(double* out, double* in);
    void m2l_matvec(double* out, double* in);
    void l2l_matvec(double* out, double* in, int level);
    void p2p_matvec(double* out, double* in);
    void m2p_matvec(double* out, double* in);
    void l2p_matvec(double* out, double* in);
    void d2e_matvec(double* out, double* in, int level);
    void u2e_matvec(double* out, double* in, int level);

    std::vector<double> m2m_eval(double* m_check);
    std::vector<double> m2p_eval(double* multipoles);
};

template <size_t dim>
FMMMat<dim> fmmmmmmm(const Octree<dim>& obs_tree, const Octree<dim>& src_tree,
    const FMMConfig<dim>& cfg);
