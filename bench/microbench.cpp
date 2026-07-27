// Google Benchmark microbenchmarks for the RiskSim hot paths.
//
// Run:   risksim_bench --benchmark_format=json > results/micro.json
// (the bench.yml CI workflow feeds this JSON to github-action-benchmark)
#include <benchmark/benchmark.h>

#include <string>
#include <vector>

#include "risksim/contagion.hpp"
#include "risksim/copula.hpp"
#include "risksim/linalg.hpp"
#include "risksim/network.hpp"
#include "risksim/rng.hpp"
#include "risksim/risk.hpp"
#include "risksim/runtime/spsc_ring.hpp"
#include "risksim/sim/monte_carlo.hpp"
#include "risksim/simd/simd_kernels.hpp"
#include "risksim/lob/order_book.hpp"

using namespace risksim;

namespace {
ExposureNetwork make_net(std::size_t core, std::size_t periph, std::uint64_t seed = 1) {
    CorePeripheryParams p;
    p.n_core = core;
    p.n_periphery = periph;
    p.seed = seed;
    return generate_core_periphery(p);
}
Matrix equicorr(std::size_t n, double rho) {
    Matrix c = Matrix::identity(n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            if (i != j) c(i, j) = rho;
    return c;
}
}  // namespace

// ---- RNG throughput --------------------------------------------------------
static void BM_PhiloxNormal(benchmark::State& state) {
    const Philox rng(12345);
    std::uint64_t i = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(rng.normal(i++));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PhiloxNormal);

// ---- Contagion propagation (one shock, one network) ------------------------
static void BM_Contagion(benchmark::State& state, const ContagionModel& model) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const ExposureNetwork net = make_net(n / 10, n - n / 10);
    std::vector<double> shock(net.n, 0.0);
    for (std::size_t i = 0; i < net.n / 10; ++i) shock[i] = 0.8;
    for (auto _ : state) {
        benchmark::DoNotOptimize(model.propagate(net, shock).system_loss);
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["nodes"] = static_cast<double>(net.n);
    state.counters["edges"] = static_cast<double>([&] {
        std::size_t e = 0;
        for (std::size_t i = 0; i < net.n; ++i)
            for (std::size_t j = 0; j < net.n; ++j)
                if (net.liabilities(i, j) > 0) ++e;
        return e;
    }());
}
static void BM_EisenbergNoe(benchmark::State& s) { BM_Contagion(s, EisenbergNoe{}); }
static void BM_DebtRankDyn(benchmark::State& s) { BM_Contagion(s, DebtRank{true}); }
static void BM_Furfine(benchmark::State& s) { BM_Contagion(s, Furfine{1.0}); }
BENCHMARK(BM_EisenbergNoe)->Arg(100)->Arg(500);
BENCHMARK(BM_DebtRankDyn)->Arg(100)->Arg(500);
BENCHMARK(BM_Furfine)->Arg(100)->Arg(500);

// ---- Monte Carlo throughput (paths/sec) ------------------------------------
static void BM_SystemicMonteCarlo(benchmark::State& state) {
    const ExposureNetwork net = make_net(8, 92);
    const std::vector<double> vol(net.n, 0.30);
    const sim::SystemicMonteCarlo engine(net, vol);
    const StudentTCopula copula(equicorr(net.n, 0.5), 4.0);
    const Furfine model(1.0);
    sim::Config cfg;
    cfg.num_paths = 5000;
    cfg.threads = 1;  // measure single-thread path rate
    for (auto _ : state) {
        const sim::Result r = engine.run(copula, model, cfg);
        benchmark::DoNotOptimize(r.es);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(cfg.num_paths));
}
BENCHMARK(BM_SystemicMonteCarlo)->Unit(benchmark::kMillisecond);

// ---- Lock-free ring push+pop ----------------------------------------------
static void BM_SpscRingPushPop(benchmark::State& state) {
    runtime::SpscRing<std::uint64_t, 1024> ring;
    std::uint64_t v = 0;
    for (auto _ : state) {
        ring.try_push(v++);
        benchmark::DoNotOptimize(ring.try_pop());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SpscRingPushPop);

// ---- VaR/ES estimator ------------------------------------------------------
static void BM_EmpiricalVarEs(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const Philox rng(7);
    std::vector<double> losses(n);
    for (std::size_t i = 0; i < n; ++i) losses[i] = rng.normal(i);
    for (auto _ : state) {
        benchmark::DoNotOptimize(empirical_var_es(losses, 0.975).es);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_EmpiricalVarEs)->Arg(100000)->Arg(1000000);

// ---- SIMD vs scalar normal-CDF (Merton PD / copula marginal map) -----------
// Compare the Highway-vectorized batch against the scalar std::erfc reference.
// The ratio of these two is the honest SIMD speedup for this kernel.
static std::vector<float> simd_input(std::size_t n) {
    const Philox rng(3);
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) x[i] = static_cast<float>(6.0 * (rng.uniform(i) - 0.5));
    return x;
}
static void BM_NormCdfScalar(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const std::vector<float> x = simd_input(n);
    std::vector<float> out(n);
    for (auto _ : state) {
        simd::norm_cdf_batch_scalar(x, out);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
    state.SetLabel(std::string("target=") + simd::active_target());
}
static void BM_NormCdfScalarPoly(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const std::vector<float> x = simd_input(n);
    std::vector<float> out(n);
    for (auto _ : state) {
        simd::norm_cdf_batch_scalar_poly(x, out);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
static void BM_NormCdfSimd(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const std::vector<float> x = simd_input(n);
    std::vector<float> out(n);
    for (auto _ : state) {
        simd::norm_cdf_batch(x, out);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
    state.SetLabel(std::string("target=") + simd::active_target());
}
BENCHMARK(BM_NormCdfScalar)->Arg(1000000);       // libm std::erfc (accuracy oracle)
BENCHMARK(BM_NormCdfScalarPoly)->Arg(1000000);   // same A&S poly, scalar
BENCHMARK(BM_NormCdfSimd)->Arg(1000000);         // same A&S poly, vectorized

// ---- Limit order book: hot-path op latency ---------------------------------
// ns/op IS the per-operation latency (Google Benchmark reports it directly).
static void BM_LobAddCancel(benchmark::State& state) {
    lob::OrderBook book(4096, 1024);
    std::vector<lob::Fill> fills;
    fills.reserve(16);
    std::uint64_t i = 0;
    for (auto _ : state) {
        // Non-crossing add (rests) then cancel -> add+cancel round trip over 256
        // price levels (exercises the level array + occupancy bitset).
        const lob::Price p = 100 + static_cast<lob::Price>(i++ & 255u);
        const lob::OrderId id = book.add_limit(lob::Side::Buy, p, 10, fills);
        benchmark::DoNotOptimize(id);
        book.cancel(id);
    }
    state.SetItemsProcessed(state.iterations() * 2);  // one add + one cancel
}
BENCHMARK(BM_LobAddCancel);

static void BM_LobMatch(benchmark::State& state) {
    lob::OrderBook book(4096, 1024);
    std::vector<lob::Fill> fills;
    fills.reserve(16);
    constexpr lob::Price p = 2000;
    constexpr lob::Qty q = 10;
    book.add_limit(lob::Side::Sell, p, q, fills);
    fills.clear();
    for (auto _ : state) {
        book.add_limit(lob::Side::Buy, p, q, fills);   // fully matches the ask (1 fill)
        book.add_limit(lob::Side::Sell, p, q, fills);  // replenish -> steady state
        benchmark::DoNotOptimize(fills.data());
        fills.clear();
    }
    state.SetItemsProcessed(state.iterations());  // one match per iteration
}
BENCHMARK(BM_LobMatch);

BENCHMARK_MAIN();
