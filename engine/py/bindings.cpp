// Python bindings for the RiskSim C++ core (module: _risksim, imported as
// `risksim`).
//
// The whole point of these bindings is that the Python model-validation suite
// calls the *exact* shipping C++ kernels -- the same distributions, copulas,
// contagion models, and Monte-Carlo evaluation loop that run in production -- so
// the notebooks/tests validate the code that ships, not a re-implementation.
//
// Two high-level helpers (`generate_network`, `simulate_system_losses`) are
// implemented here on top of the public classes because the validation work
// needs the *raw per-path losses* for backtesting, while
// SystemicMonteCarlo::run only returns aggregates. The per-path loop below is a
// faithful mirror of SystemicMonteCarlo::evaluate.

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "risksim/contagion.hpp"
#include "risksim/copula.hpp"
#include "risksim/distributions.hpp"
#include "risksim/linalg.hpp"
#include "risksim/network.hpp"
#include "risksim/risk.hpp"
#include "risksim/rng.hpp"

namespace nb = nanobind;
using namespace risksim;

namespace {

// Opaque, reference-counted wrapper so Python can hold an ExposureNetwork alive
// and pass it back into simulate_system_losses.
struct NetworkHandle {
    ExposureNetwork net;
    std::size_t n_core = 0;
    std::size_t n_periphery = 0;
    std::uint64_t seed = 0;
};

// Copy a 1-D float64 numpy array into a std::vector<double>.
std::vector<double> to_vector(nb::ndarray<const double, nb::ndim<1>, nb::c_contig> arr) {
    const std::size_t n = arr.shape(0);
    std::vector<double> out(n);
    const double* p = arr.data();
    for (std::size_t i = 0; i < n; ++i) out[i] = p[i];
    return out;
}

// Build a numpy array that owns a heap-allocated copy of `v` (no dangling).
nb::ndarray<nb::numpy, double> make_numpy(std::vector<double>&& v) {
    auto* held = new std::vector<double>(std::move(v));
    nb::capsule owner(held, [](void* p) noexcept { delete static_cast<std::vector<double>*>(p); });
    const std::size_t n = held->size();
    return nb::ndarray<nb::numpy, double>(held->data(), {n}, owner);
}

// Equicorrelation matrix: 1 on the diagonal, `rho` off-diagonal.
Matrix equicorrelation(std::size_t n, double rho) {
    Matrix c = Matrix::identity(n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            if (i != j) c(i, j) = rho;
    return c;
}

std::unique_ptr<ContagionModel> make_model(const std::string& name) {
    if (name == "eisenberg-noe") return std::make_unique<EisenbergNoe>();
    if (name == "debtrank") return std::make_unique<DebtRank>(/*dynamical=*/true);
    if (name == "furfine") return std::make_unique<Furfine>(1.0);
    throw std::invalid_argument("unknown contagion model: " + name +
                                " (expected 'eisenberg-noe' | 'debtrank' | 'furfine')");
}

// ---- Low-level distribution wrappers --------------------------------------

double py_norm_cdf(double x) { return norm_cdf(x); }
double py_norm_inv_cdf(double p) { return norm_inv_cdf(p); }
double py_students_t_cdf(double x, double nu) { return students_t_cdf(x, nu); }
double py_students_t_inv_cdf(double p, double nu) { return students_t_inv_cdf(p, nu); }
double py_t_copula_tail_dependence(double rho, double nu) {
    return t_copula_tail_dependence(rho, nu);
}

// empirical_var_es(numpy, alpha) -> (var, es)
std::pair<double, double> py_empirical_var_es(
    nb::ndarray<const double, nb::ndim<1>, nb::c_contig> losses, double alpha) {
    const std::span<const double> s(losses.data(), losses.shape(0));
    const VarEs ve = empirical_var_es(s, alpha);
    return {ve.var, ve.es};
}

std::pair<double, double> py_gaussian_var_es(double mu, double sigma, double alpha) {
    const VarEs ve = gaussian_var_es(mu, sigma, alpha);
    return {ve.var, ve.es};
}

// ---- High-level helpers ----------------------------------------------------

std::shared_ptr<NetworkHandle> generate_network(std::size_t n_core, std::size_t n_periphery,
                                                std::uint64_t seed, double capital_ratio) {
    CorePeripheryParams params;
    params.n_core = n_core;
    params.n_periphery = n_periphery;
    params.capital_ratio = capital_ratio;
    params.seed = seed;
    auto h = std::make_shared<NetworkHandle>();
    h->net = generate_core_periphery(params);
    h->n_core = n_core;
    h->n_periphery = n_periphery;
    h->seed = seed;
    return h;
}

// Build a network from real balance-sheet marginals: reconstruct the confidential
// bilateral liability matrix from public marginals (total interbank assets =
// column sums, total interbank liabilities = row sums) via max-entropy
// (Sinkhorn) or minimum-density, then attach real equity + external assets.
std::shared_ptr<NetworkHandle> network_from_marginals(
    nb::ndarray<const double, nb::ndim<1>, nb::c_contig> assets_in,
    nb::ndarray<const double, nb::ndim<1>, nb::c_contig> liabilities_out,
    nb::ndarray<const double, nb::ndim<1>, nb::c_contig> equity,
    nb::ndarray<const double, nb::ndim<1>, nb::c_contig> external_assets,
    const std::string& method) {
    const std::vector<double> a = to_vector(assets_in);
    const std::vector<double> l = to_vector(liabilities_out);
    const std::vector<double> eq = to_vector(equity);
    const std::vector<double> ext = to_vector(external_assets);
    const std::size_t n = l.size();
    if (a.size() != n || eq.size() != n || ext.size() != n)
        throw std::invalid_argument("network_from_marginals: array length mismatch");

    Matrix liab = (method == "min-density") ? reconstruct_min_density(a, l)
                                            : reconstruct_max_entropy(a, l);
    auto h = std::make_shared<NetworkHandle>();
    h->net.n = n;
    h->net.liabilities = std::move(liab);
    h->net.external_assets = ext;
    h->net.equity = eq;
    return h;
}

// simulate_calibrated -> (per_path_losses, var, es)
//
// Like simulate_system_losses but takes a PER-BANK volatility vector and a FULL
// correlation matrix (n x n), so it can be driven by real equity-return
// statistics rather than a single scalar and equicorrelation.
std::tuple<nb::ndarray<nb::numpy, double>, double, double> simulate_calibrated(
    std::shared_ptr<NetworkHandle> handle,
    nb::ndarray<const double, nb::ndim<1>, nb::c_contig> node_vol,
    nb::ndarray<const double, nb::ndim<2>, nb::c_contig> corr, const std::string& copula, double nu,
    std::uint64_t num_paths, std::uint64_t seed, const std::string& model_name, double alpha,
    bool antithetic) {
    if (!handle) throw std::invalid_argument("null network handle");
    const ExposureNetwork& net = handle->net;
    const std::size_t n = net.n;
    if (node_vol.shape(0) != n || corr.shape(0) != n || corr.shape(1) != n)
        throw std::invalid_argument("simulate_calibrated: dimension mismatch with network");

    Matrix corr_m(n, n);
    const double* cp = corr.data();
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) corr_m(i, j) = cp[i * n + j];
    const std::vector<double> vol = to_vector(node_vol);

    const std::unique_ptr<ContagionModel> model = make_model(model_name);
    const bool is_t = (copula == "t");
    if (!is_t && copula != "gaussian")
        throw std::invalid_argument("unknown copula: " + copula + " (expected 'gaussian' | 't')");
    GaussianCopula gcop = is_t ? GaussianCopula(Matrix::identity(n)) : GaussianCopula(corr_m);
    StudentTCopula tcop =
        is_t ? StudentTCopula(corr_m, nu) : StudentTCopula(Matrix::identity(n), nu);

    const std::size_t per_path = antithetic ? 2u : 1u;
    std::vector<double> losses;
    losses.reserve(static_cast<std::size_t>(num_paths) * per_path);
    std::vector<double> u(n);
    std::vector<double> shock(n);

    auto evaluate = [&](bool reflect) -> double {
        for (std::size_t i = 0; i < n; ++i) {
            const double ui = reflect ? (1.0 - u[i]) : u[i];
            const double ret = vol[i] * norm_inv_cdf(ui);
            shock[i] = std::clamp(-ret, 0.0, 1.0);
        }
        return model->propagate(net, shock).system_loss;
    };

    for (std::uint64_t p = 0; p < num_paths; ++p) {
        DrawCursor cursor(seed, p);
        if (is_t)
            tcop.sample(cursor, u);
        else
            gcop.sample(cursor, u);
        losses.push_back(evaluate(false));
        if (antithetic) losses.push_back(evaluate(true));
    }
    const VarEs ve = empirical_var_es(losses, alpha);
    return {make_numpy(std::move(losses)), ve.var, ve.es};
}

// simulate_system_losses -> (per_path_losses, var, es)
//
// Mirrors SystemicMonteCarlo::evaluate exactly: for each path p, draw correlated
// uniforms u from the copula using DrawCursor(seed, p), map u[i] through the
// standard-normal marginal to a return vol*z, take the clamped downside as the
// external-asset shock, run the contagion model, and record system_loss.
// Antithetic pairs each path with its radial reflection (u -> 1-u).
std::tuple<nb::ndarray<nb::numpy, double>, double, double> simulate_system_losses(
    std::shared_ptr<NetworkHandle> handle, double node_vol, double correlation,
    const std::string& copula, double nu, std::uint64_t num_paths, std::uint64_t seed,
    const std::string& model_name, double alpha, bool antithetic) {
    if (!handle) throw std::invalid_argument("null network handle");
    const ExposureNetwork& net = handle->net;
    const std::size_t n = net.n;

    const Matrix corr = equicorrelation(n, correlation);
    const std::unique_ptr<ContagionModel> model = make_model(model_name);

    const bool is_t = (copula == "t");
    if (!is_t && copula != "gaussian")
        throw std::invalid_argument("unknown copula: " + copula + " (expected 'gaussian' | 't')");

    // Build the chosen copula once (Cholesky factor is reused across samples).
    GaussianCopula gcop = is_t ? GaussianCopula(Matrix::identity(n)) : GaussianCopula(corr);
    StudentTCopula tcop = is_t ? StudentTCopula(corr, nu) : StudentTCopula(Matrix::identity(n), nu);

    const std::size_t per_path = antithetic ? 2u : 1u;
    std::vector<double> losses;
    losses.reserve(static_cast<std::size_t>(num_paths) * per_path);

    std::vector<double> u(n);
    std::vector<double> shock(n);

    auto evaluate = [&](bool reflect) -> double {
        for (std::size_t i = 0; i < n; ++i) {
            const double ui = reflect ? (1.0 - u[i]) : u[i];
            const double z = norm_inv_cdf(ui);
            const double ret = node_vol * z;               // asset return
            shock[i] = std::clamp(-ret, 0.0, 1.0);         // downside as fractional shock
        }
        return model->propagate(net, shock).system_loss;
    };

    for (std::uint64_t p = 0; p < num_paths; ++p) {
        DrawCursor cursor(seed, p);
        if (is_t)
            tcop.sample(cursor, u);
        else
            gcop.sample(cursor, u);
        losses.push_back(evaluate(false));
        if (antithetic) losses.push_back(evaluate(true));
    }

    const VarEs ve = empirical_var_es(losses, alpha);
    return {make_numpy(std::move(losses)), ve.var, ve.es};
}

}  // namespace

NB_MODULE(_risksim, m) {
    m.doc() = "RiskSim C++ core bindings: the shipping systemic-risk kernels exposed to Python.";

    // ---- Distributions ----
    m.def("norm_cdf", &py_norm_cdf, nb::arg("x"),
          "Standard normal CDF Phi(x) (via std::erfc).");
    m.def("norm_inv_cdf", &py_norm_inv_cdf, nb::arg("p"),
          "Standard normal inverse CDF / probit Phi^{-1}(p).");
    m.def("students_t_cdf", &py_students_t_cdf, nb::arg("x"), nb::arg("nu"),
          "Student-t CDF with nu degrees of freedom.");
    m.def("students_t_inv_cdf", &py_students_t_inv_cdf, nb::arg("p"), nb::arg("nu"),
          "Student-t inverse CDF (quantile) with nu degrees of freedom.");
    m.def("t_copula_tail_dependence", &py_t_copula_tail_dependence, nb::arg("rho"), nb::arg("nu"),
          "Lower=upper tail-dependence coefficient of a bivariate t-copula. >0 for finite nu, "
          "-> 0 as nu -> inf.");

    // ---- Risk ----
    m.def("empirical_var_es", &py_empirical_var_es, nb::arg("losses"), nb::arg("alpha"),
          "Empirical (VaR, ES) at level alpha from a 1-D array of losses.");
    m.def("gaussian_var_es", &py_gaussian_var_es, nb::arg("mu"), nb::arg("sigma"), nb::arg("alpha"),
          "Closed-form Normal(mu, sigma) (VaR, ES) at level alpha.");

    // ---- High-level helpers ----
    nb::class_<NetworkHandle>(m, "NetworkHandle",
                              "Opaque handle wrapping a C++ ExposureNetwork.")
        .def_ro("n_core", &NetworkHandle::n_core)
        .def_ro("n_periphery", &NetworkHandle::n_periphery)
        .def_ro("seed", &NetworkHandle::seed)
        .def_prop_ro("n", [](const NetworkHandle& h) { return h.net.n; })
        .def("__repr__", [](const NetworkHandle& h) {
            return "<NetworkHandle n=" + std::to_string(h.net.n) +
                   " core=" + std::to_string(h.n_core) +
                   " periphery=" + std::to_string(h.n_periphery) + ">";
        });

    m.def("generate_network", &generate_network, nb::arg("n_core"), nb::arg("n_periphery"),
          nb::arg("seed"), nb::arg("capital_ratio") = 0.08,
          "Generate a core-periphery interbank exposure network. Returns an opaque handle.");

    m.def("network_from_marginals", &network_from_marginals, nb::arg("assets_in"),
          nb::arg("liabilities_out"), nb::arg("equity"), nb::arg("external_assets"),
          nb::arg("method") = "max-entropy",
          "Reconstruct an interbank network from real balance-sheet marginals "
          "(method='max-entropy' | 'min-density') and attach equity + external assets.");

    m.def("simulate_calibrated", &simulate_calibrated, nb::arg("net_handle"), nb::arg("node_vol"),
          nb::arg("corr"), nb::arg("copula"), nb::arg("nu"), nb::arg("num_paths"), nb::arg("seed"),
          nb::arg("model"), nb::arg("alpha"), nb::arg("antithetic") = false,
          "Systemic Monte Carlo driven by a per-bank volatility vector and a full correlation "
          "matrix (real-data calibration). Returns (per_path_losses, var, es).");

    m.def("simulate_system_losses", &simulate_system_losses, nb::arg("net_handle"),
          nb::arg("node_vol"), nb::arg("correlation"), nb::arg("copula"), nb::arg("nu"),
          nb::arg("num_paths"), nb::arg("seed"), nb::arg("model"), nb::arg("alpha"),
          nb::arg("antithetic") = false,
          "Run the systemic Monte Carlo and return (per_path_losses, var, es). Mirrors the "
          "engine's per-path evaluation so Python gets the raw loss sample for backtesting.");
}
