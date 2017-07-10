import sys
import scipy.spatial
import scipy.sparse
import scipy.sparse.linalg
import numpy as np

from tectosaur.util.timer import Timer
import tectosaur_fmm.fmm_wrapper as fmm

from dimension import dim, module

quiet_tests = False
def test_print(*args, **kwargs):
    if not quiet_tests:
        print(*args, **kwargs)

def rand_pts(dim):
    def f(n, source):
        return np.random.rand(n, dim)
    return f

def ellipsoid_pts(n, source):
    a = 4.0
    b = 1.0
    c = 1.0
    uv = np.random.rand(n, 2)
    uv[:, 0] = (uv[:, 0] * np.pi) - np.pi / 2
    uv[:, 1] = (uv[:, 1] * 2 * np.pi) - np.pi
    x = a * np.cos(uv[:, 0]) * np.cos(uv[:, 1])
    y = b * np.cos(uv[:, 0]) * np.sin(uv[:, 1])
    z = c * np.sin(uv[:, 0])
    return np.array([x, y, z]).T

def m2p_test_pts(dim):
    def f(n, is_source):
        out = np.random.rand(n, dim)
        if is_source:
            out += 3.5
        return out
    return f

def run_full(n, make_pts, mac, order, kernel, params, ocl = False, max_pts_per_cell = None):
    if max_pts_per_cell is None:
        max_pts_per_cell = order
    t = Timer()
    obs_pts = make_pts(n, False)
    obs_ns = make_pts(n, False)
    obs_ns /= np.linalg.norm(obs_ns, axis = 1)[:,np.newaxis]
    src_pts = make_pts(n + 1, True)
    src_ns = make_pts(n + 1, True)
    src_ns /= np.linalg.norm(src_ns, axis = 1)[:,np.newaxis]
    t.report('gen random data')

    dim = obs_pts.shape[1]

    obs_kd = module[dim].Octree(obs_pts, obs_ns, max_pts_per_cell)
    src_kd = module[dim].Octree(src_pts, src_ns, max_pts_per_cell)
    t.report('build trees')
    fmm_mat = module[dim].fmmmmmmm(
        obs_kd, src_kd, module[dim].FMMConfig(1.1, mac, order, kernel, params)
    )
    t.report('setup fmm')

    tdim = fmm_mat.tensor_dim
    input_vals = np.ones(src_pts.shape[0] * tdim)
    n_outputs = obs_pts.shape[0] * tdim

    if ocl:
        est = fmm.eval_ocl(fmm_mat, input_vals)
    else:
        est = fmm.eval_cpu(fmm_mat, input_vals)
    t.report('eval fmm')
    # est2 = fmm.mf_direct_eval(kernel, obs_pts, obs_ns, src_pts, src_ns, params, input_vals)
    # t.report('eval direct')

    return (
        np.array(obs_kd.pts), np.array(obs_kd.normals),
        np.array(src_kd.pts), np.array(src_kd.normals), est
    )

def check(est, correct, accuracy):
    rmse = np.sqrt(np.mean((est - correct) ** 2))
    rms_c = np.sqrt(np.mean(correct ** 2))
    test_print("L2ERR: " + str(rmse / rms_c))
    test_print("MEANERR: " + str(np.mean(np.abs(est - correct)) / rms_c))
    test_print("MAXERR: " + str(np.max(np.abs(est - correct)) / rms_c))
    test_print("MEANRELERR: " + str(np.mean(np.abs((est - correct) / correct))))
    test_print("MAXRELERR: " + str(np.max(np.abs((est - correct) / correct))))
    lhs = est / rms_c
    rhs = correct / rms_c
    np.testing.assert_almost_equal(lhs, rhs, accuracy)

def check_kernel(K, obs_pts, obs_ns, src_pts, src_ns, est, accuracy = 3):
    dim = obs_pts.shape[1]
    tensor_dim = int(est.size / obs_pts.shape[0])
    correct_mat = module[dim].direct_eval(
        K, obs_pts, obs_ns, src_pts, src_ns, []
    ).reshape((obs_pts.shape[0] * tensor_dim, src_pts.shape[0] * tensor_dim))
    correct = correct_mat.dot(np.ones(src_pts.shape[0]))
    check(est, correct, accuracy)

def test_ones(dim):
    obs_pts, _, src_pts, _, est = run_full(5000, rand_pts(dim), 0.5, 1, "one",[])
    assert(np.all(np.abs(est - src_pts.shape[0]) < 1e-3))

def test_p2p(dim):
    K = "laplace_S"
    check_kernel(K, *run_full(
        1000, rand_pts(dim), 2.6, 1, K, [], max_pts_per_cell = 100000
    ), accuracy = 10)

def test_m2p(dim):
    K = "laplace_S"
    check_kernel(K, *run_full(
        10000, m2p_test_pts(dim), 2.6, 8 ** (dim - 1), K, [], max_pts_per_cell = 100000
    ), accuracy = 3)

def test_single_layer(dim):
    K = "laplace_S"
    check_kernel(K, *run_full(10000, rand_pts(dim), 2.6, 8 ** (dim - 1), K, []))

def test_double_layer(dim):
    K = "laplace_D"
    check_kernel(K, *run_full(10000, rand_pts(dim), 2.6, 8 ** (dim - 1), K, []))

def test_hypersingular(dim):
    K = "laplace_H"
    check_kernel(K, *run_full(10000, rand_pts(dim), 2.6, 8 ** (dim - 1), K, []))

def test_irregular():
    K = "laplace_S"
    check_kernel(K, *run_full(10000, ellipsoid_pts, 2.6, 35, K, []))
