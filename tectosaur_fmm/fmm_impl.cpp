#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

#include "include/timing.hpp"
#include "fmm_impl.hpp"

template <size_t dim>
void traverse(FMMMat<dim>& mat, const OctreeNode<dim>& obs_n, const OctreeNode<dim>& src_n) {
    auto r_src = src_n.bounds.R();
    auto r_obs = obs_n.bounds.R();
    auto sep = hypot(sub(obs_n.bounds.center, src_n.bounds.center));

    // If outer_r * r_src + inner_r * r_obs is less than the separation, then
    // the relevant check surfaces for the two interacting cells don't
    // intersect.
    // That means it should be safe to perform approximate interactions. I add
    // a small safety factor just in case!
    double safety_factor = 0.98;
    if (mat.cfg.outer_r * r_src + mat.cfg.inner_r * r_obs < safety_factor * sep) {
        // If there aren't enough src or obs to justify using the approximation,
        // then just do a p2p direct calculation between the nodes.
        bool small_src = src_n.end - src_n.start < mat.surf.size();
        bool small_obs = obs_n.end - obs_n.start < mat.surf.size();

        if (small_src && small_obs) {
            mat.p2p.insert(obs_n, src_n);
        } else if (small_obs) {
            mat.m2p.insert(obs_n, src_n);
        } else if (small_src) {
            mat.p2l.insert(obs_n, src_n);
        } else {
            mat.m2l.insert(obs_n, src_n);
        }

        return;
    }

    if (src_n.is_leaf && obs_n.is_leaf) {
        mat.p2p.insert(obs_n, src_n);
        return;
    }

    bool split_src = ((r_obs < r_src) && !src_n.is_leaf) || obs_n.is_leaf;
    if (split_src) {
        for (size_t i = 0; i < OctreeNode<dim>::split; i++) {
            traverse(mat, obs_n, mat.src_tree.nodes[src_n.children[i]]);
        }
    } else {
        for (size_t i = 0; i < OctreeNode<dim>::split; i++) {
            traverse(mat, mat.obs_tree.nodes[obs_n.children[i]], src_n);
        }
    }
}

template <size_t dim>
void up_collect(FMMMat<dim>& mat, const OctreeNode<dim>& src_n) {
    mat.uc2e[src_n.height].insert(src_n, src_n);
    if (src_n.is_leaf) {
        mat.p2m.insert(src_n, src_n);
    } else {
        for (size_t i = 0; i < OctreeNode<dim>::split; i++) {
            auto child_n = mat.src_tree.nodes[src_n.children[i]];
            up_collect(mat, child_n);
            mat.m2m[src_n.height].insert(src_n, child_n);
        }
    }
}

template <size_t dim>
void down_collect(FMMMat<dim>& mat, const OctreeNode<dim>& obs_n) {
    mat.dc2e[obs_n.depth].insert(obs_n, obs_n);
    if (obs_n.is_leaf) {
        mat.l2p.insert(obs_n, obs_n);
    } else {
        for (size_t i = 0; i < OctreeNode<dim>::split; i++) {
            auto child_n = mat.obs_tree.nodes[obs_n.children[i]];
            down_collect(mat, child_n);
            mat.l2l[child_n.depth].insert(child_n, obs_n);
        }
    }
}

template <size_t dim>
FMMMat<dim>::FMMMat(Octree<dim> obs_tree, Octree<dim> src_tree, FMMConfig<dim> cfg,
        std::vector<std::array<double,dim>> surf):
    obs_tree(obs_tree),
    src_tree(src_tree),
    cfg(cfg),
    surf(surf)
{}

template <size_t dim>
void interact_pts(const FMMConfig<dim>& cfg, double* out, double* in,
    const std::array<double,dim>* obs_pts, const std::array<double,dim>* obs_ns,
    size_t n_obs, size_t obs_pt_start,
    const std::array<double,dim>* src_pts, const std::array<double,dim>* src_ns,
    size_t n_src, size_t src_pt_start) 
{
    if (n_obs == 0 || n_src == 0) {
        return;
    }

    double* out_val_start = &out[cfg.tensor_dim() * obs_pt_start];
    double* in_val_start = &in[cfg.tensor_dim() * src_pt_start];
    cfg.kernel.mf_f(
        NBodyProblem<dim>{obs_pts, obs_ns, src_pts, src_ns, n_obs, n_src, cfg.params.data()},
        out_val_start, in_val_start
    );
}


template <size_t dim>
void FMMMat<dim>::p2m_matvec(double* out, double *in) {
    for (size_t i = 0; i < p2m.obs_n_idx.size(); i++) {
        auto src_n = src_tree.nodes[p2m.src_n_idx[i]];
        auto check = inscribe_surf(src_n.bounds, cfg.outer_r, surf);
        interact_pts(
            cfg, out, in,
            check.data(), surf.data(), 
            surf.size(), src_n.idx * surf.size(),
            &src_tree.pts[src_n.start], &src_tree.normals[src_n.start],
            src_n.end - src_n.start, src_n.start
        );
    }
}

template <size_t dim>
void FMMMat<dim>::m2m_matvec(double* out, double *in, int level) {
    for (size_t i = 0; i < m2m[level].obs_n_idx.size(); i++) {
        auto parent_n = src_tree.nodes[m2m[level].obs_n_idx[i]];
        auto child_n = src_tree.nodes[m2m[level].src_n_idx[i]];
        auto check = inscribe_surf(parent_n.bounds, cfg.outer_r, surf);
        auto equiv = inscribe_surf(child_n.bounds, cfg.inner_r, surf);
        interact_pts(
            cfg, out, in,
            check.data(), surf.data(), 
            surf.size(), parent_n.idx * surf.size(),
            equiv.data(), surf.data(), 
            surf.size(), child_n.idx * surf.size()
        );
    }
}

template <size_t dim>
void FMMMat<dim>::p2l_matvec(double* out, double* in) {
    for (size_t i = 0; i < p2l.obs_n_idx.size(); i++) {
        auto obs_n = obs_tree.nodes[p2l.obs_n_idx[i]];
        auto src_n = src_tree.nodes[p2l.src_n_idx[i]];

        auto check = inscribe_surf(obs_n.bounds, cfg.inner_r, surf);
        interact_pts(
            cfg, out, in,
            check.data(), surf.data(), 
            surf.size(), obs_n.idx * surf.size(),
            &src_tree.pts[src_n.start], &src_tree.normals[src_n.start],
            src_n.end - src_n.start, src_n.start
        );
    }
}

template <size_t dim>
void FMMMat<dim>::m2l_matvec(double* out, double* in) {
    for (size_t i = 0; i < m2l.obs_n_idx.size(); i++) {
        auto obs_n = obs_tree.nodes[m2l.obs_n_idx[i]];
        auto src_n = src_tree.nodes[m2l.src_n_idx[i]];

        auto check = inscribe_surf(obs_n.bounds, cfg.inner_r, surf);
        auto equiv = inscribe_surf(src_n.bounds, cfg.inner_r, surf);
        interact_pts(
            cfg, out, in,
            check.data(), surf.data(), 
            surf.size(), obs_n.idx * surf.size(),
            equiv.data(), surf.data(), 
            surf.size(), src_n.idx * surf.size()
        );
    }
}


template <size_t dim>
void FMMMat<dim>::l2l_matvec(double* out, double* in, int level) {
    for (size_t i = 0; i < l2l[level].obs_n_idx.size(); i++) {
        auto child_n = obs_tree.nodes[l2l[level].obs_n_idx[i]];
        auto parent_n = obs_tree.nodes[l2l[level].src_n_idx[i]];

        auto check = inscribe_surf(child_n.bounds, cfg.inner_r, surf);
        auto equiv = inscribe_surf(parent_n.bounds, cfg.outer_r, surf);
        interact_pts(
            cfg, out, in,
            check.data(), surf.data(), 
            surf.size(), child_n.idx * surf.size(),
            equiv.data(), surf.data(), 
            surf.size(), parent_n.idx * surf.size()
        );
    }
}

template <size_t dim>
void FMMMat<dim>::p2p_matvec(double* out, double* in) {
    for (size_t i = 0; i < p2p.obs_n_idx.size(); i++) {
        auto obs_n = obs_tree.nodes[p2p.obs_n_idx[i]];
        auto src_n = src_tree.nodes[p2p.src_n_idx[i]];
        interact_pts(
            cfg, out, in,
            &obs_tree.pts[obs_n.start], &obs_tree.normals[obs_n.start],
            obs_n.end - obs_n.start, obs_n.start,
            &src_tree.pts[src_n.start], &src_tree.normals[src_n.start],
            src_n.end - src_n.start, src_n.start
        );
    }
}


template <size_t dim>
void FMMMat<dim>::m2p_matvec(double* out, double* in) {
    for (size_t i = 0; i < m2p.obs_n_idx.size(); i++) {
        auto obs_n = obs_tree.nodes[m2p.obs_n_idx[i]];
        auto src_n = src_tree.nodes[m2p.src_n_idx[i]];

        auto equiv = inscribe_surf(src_n.bounds, cfg.inner_r, surf);
        interact_pts(
            cfg, out, in,
            &obs_tree.pts[obs_n.start], &obs_tree.normals[obs_n.start],
            obs_n.end - obs_n.start, obs_n.start,
            equiv.data(), surf.data(),
            surf.size(), src_n.idx * surf.size()
        );
    }
}


template <size_t dim>
void FMMMat<dim>::l2p_matvec(double* out, double* in) {
    for (size_t i = 0; i < l2p.obs_n_idx.size(); i++) {
        auto obs_n = obs_tree.nodes[l2p.obs_n_idx[i]];

        auto equiv = inscribe_surf(obs_n.bounds, cfg.outer_r, surf);
        interact_pts(
            cfg, out, in,
            &obs_tree.pts[obs_n.start], &obs_tree.normals[obs_n.start],
            obs_n.end - obs_n.start, obs_n.start,
            equiv.data(), surf.data(),
            surf.size(), obs_n.idx * surf.size()
        );
    }
}

template <size_t dim>
void FMMMat<dim>::dc2e_matvec(double* out, double* in, int level) {
    int n_rows = cfg.tensor_dim() * surf.size();
    for (size_t i = 0; i < dc2e[level].obs_n_idx.size(); i++) {
        auto node_idx = dc2e[level].obs_n_idx[i];
        auto depth = obs_tree.nodes[node_idx].depth;
        double* op = &dc2e_ops[depth * n_rows * n_rows];
        matrix_vector_product(
            op, n_rows, n_rows, 
            &in[node_idx * n_rows],
            &out[node_idx * n_rows]
        );
    }
}

template <size_t dim>
void FMMMat<dim>::uc2e_matvec(double* out, double* in, int level) {
    int n_rows = cfg.tensor_dim() * surf.size();
    for (size_t i = 0; i < uc2e[level].src_n_idx.size(); i++) {
        auto node_idx = uc2e[level].src_n_idx[i];
        auto depth = src_tree.nodes[node_idx].depth;
        double* op = &uc2e_ops[depth * n_rows * n_rows];
        matrix_vector_product(
            op, n_rows, n_rows, 
            &in[node_idx * n_rows],
            &out[node_idx * n_rows]
        );
    }
}

template <size_t dim>
std::vector<double> c2e_solve(std::vector<std::array<double,dim>> surf,
    const Cube<dim>& bounds, double check_r, double equiv_r, const FMMConfig<dim>& cfg) 
{
    auto equiv_surf = inscribe_surf(bounds, equiv_r, surf);
    auto check_surf = inscribe_surf(bounds, check_r, surf);

    auto n_surf = surf.size();
    auto n_rows = n_surf * cfg.tensor_dim();

    std::vector<double> equiv_to_check(n_rows * n_rows);
    cfg.kernel.f(
        {
            check_surf.data(), surf.data(), 
            equiv_surf.data(), surf.data(),
            n_surf, n_surf,
            cfg.params.data()
        },
        equiv_to_check.data());

    // TODO: This should be much higher (1e-14 or so) if double precision is being used
    double eps = 1e-5; 
    auto pinv = qr_pseudoinverse(equiv_to_check.data(), n_rows, eps);

    return pinv;
}

template <size_t dim>
void build_uc2e(FMMMat<dim>& mat) {
    int n_rows = mat.cfg.tensor_dim() * mat.surf.size();
    mat.uc2e_ops.resize((mat.src_tree.max_height + 1) * n_rows * n_rows);
#pragma omp parallel for
    for (int i = 0; i < mat.src_tree.max_height + 1; i++) {
        double width = mat.src_tree.root().bounds.width / std::pow(2.0, static_cast<double>(i));
        std::array<double,dim> center{};
        Cube<dim> bounds(center, width);
        auto pinv = c2e_solve(mat.surf, bounds, mat.cfg.outer_r, mat.cfg.inner_r, mat.cfg);
        double* op_start = &mat.uc2e_ops[i * n_rows * n_rows];
        for (int j = 0; j < n_rows * n_rows; j++) {
            op_start[j] = pinv[j];
        }
    }
}

//TODO: This can be refactored to share lots of code with above.
template <size_t dim>
void build_dc2e(FMMMat<dim>& mat) {
    int n_rows = mat.cfg.tensor_dim() * mat.surf.size();
    mat.dc2e_ops.resize((mat.obs_tree.max_height + 1) * n_rows * n_rows);
#pragma omp parallel for
    for (int i = 0; i < mat.obs_tree.max_height + 1; i++) {
        double width = mat.obs_tree.root().bounds.width / std::pow(2.0, static_cast<double>(i));
        std::array<double,dim> center{};
        Cube<dim> bounds(center, width);
        auto pinv = c2e_solve(mat.surf, bounds, mat.cfg.inner_r, mat.cfg.outer_r, mat.cfg);
        double* op_start = &mat.dc2e_ops[i * n_rows * n_rows];
        for (int j = 0; j < n_rows * n_rows; j++) {
            op_start[j] = pinv[j];
        }
    }
}

template <size_t dim>
FMMMat<dim> fmmmmmmm(const Octree<dim>& obs_tree, const Octree<dim>& src_tree,
                const FMMConfig<dim>& cfg) {

    auto translation_surf = surrounding_surface<dim>(cfg.order);

    FMMMat<dim> mat(obs_tree, src_tree, cfg, translation_surf);

    mat.m2m.resize(mat.src_tree.max_height + 1);
    mat.l2l.resize(mat.obs_tree.max_height + 1);
    mat.uc2e.resize(mat.src_tree.max_height + 1);
    mat.dc2e.resize(mat.obs_tree.max_height + 1);

    build_uc2e(mat);
    build_dc2e(mat);
    up_collect(mat, mat.src_tree.root());
    down_collect(mat, mat.obs_tree.root());
    traverse(mat, mat.obs_tree.root(), mat.src_tree.root());

    return mat;
}

template 
FMMMat<2> fmmmmmmm(const Octree<2>& obs_tree, const Octree<2>& src_tree, const FMMConfig<2>& cfg);
template 
FMMMat<3> fmmmmmmm(const Octree<3>& obs_tree, const Octree<3>& src_tree, const FMMConfig<3>& cfg);
template struct FMMMat<2>;
template struct FMMMat<3>;
