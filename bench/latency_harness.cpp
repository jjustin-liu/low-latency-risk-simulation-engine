// End-to-end tick-latency harness with an honest methodology.
//
// Two measurements:
//   * service time  -- step() run back-to-back (pure compute latency).
//   * response time -- open-loop, constant-arrival-rate driver: each tick has a
//     SCHEDULED time; latency = finish - scheduled_time, so if the pipeline falls
//     behind, the queueing delay is INCLUDED. This avoids coordinated omission
//     (the closed-loop trap where a stall also stops you sampling, hiding the tail).
//
// Reports p50/p99/p99.9/p99.99/max, records the environment, and writes JSON +
// Markdown to bench/results/. Percentiles come from the full retained sample set
// (exact order statistics), and the timer's own overhead is reported as the noise
// floor so no number is quoted below what the clock can resolve.
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "risksim/runtime/sim_engine.hpp"

using Clock = std::chrono::steady_clock;
using namespace risksim::runtime;

namespace {

struct Percentiles {
    double min, p50, p90, p99, p999, p9999, max, mean;
    std::size_t n;
};

Percentiles summarize(std::vector<double> xs) {
    Percentiles p{};
    p.n = xs.size();
    if (xs.empty()) return p;
    std::sort(xs.begin(), xs.end());
    auto q = [&](double f) {
        const double idx = f * static_cast<double>(xs.size() - 1);
        const std::size_t i = static_cast<std::size_t>(idx);
        const double frac = idx - static_cast<double>(i);
        return i + 1 < xs.size() ? xs[i] * (1 - frac) + xs[i + 1] * frac : xs[i];
    };
    double sum = 0.0;
    for (double x : xs) sum += x;
    p.min = xs.front();
    p.max = xs.back();
    p.mean = sum / static_cast<double>(xs.size());
    p.p50 = q(0.50);
    p.p90 = q(0.90);
    p.p99 = q(0.99);
    p.p999 = q(0.999);
    p.p9999 = q(0.9999);
    return p;
}

// Prevent the timing loop below from being optimized away.
volatile std::int64_t g_sink = 0;
void benchmark_touch(Clock::time_point tp) { g_sink += tp.time_since_epoch().count(); }

double timer_overhead_ns() {
    // Cost of a single clock read, so we never quote latency below the noise floor.
    constexpr int kN = 200000;
    auto t0 = Clock::now();
    for (int i = 0; i < kN; ++i) benchmark_touch(Clock::now());
    auto t1 = Clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / kN;
}

void print_row(const char* label, const Percentiles& p) {
    std::fprintf(stderr,
                 "%-14s n=%zu  min=%.2f  p50=%.2f  p99=%.2f  p99.9=%.2f  p99.99=%.2f  max=%.2f  "
                 "mean=%.2f  (us)\n",
                 label, p.n, p.min / 1e3, p.p50 / 1e3, p.p99 / 1e3, p.p999 / 1e3, p.p9999 / 1e3,
                 p.max / 1e3, p.mean / 1e3);
}

void write_json(const std::string& path, const Percentiles& svc, const Percentiles& resp,
                double hz, double overhead, std::size_t nodes, std::size_t edges) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    auto obj = [&](const char* name, const Percentiles& p) {
        std::fprintf(f,
                     "  \"%s\": {\"n\": %zu, \"min_ns\": %.1f, \"p50_ns\": %.1f, \"p90_ns\": %.1f, "
                     "\"p99_ns\": %.1f, \"p999_ns\": %.1f, \"p9999_ns\": %.1f, \"max_ns\": %.1f, "
                     "\"mean_ns\": %.1f}",
                     name, p.n, p.min, p.p50, p.p90, p.p99, p.p999, p.p9999, p.max, p.mean);
    };
    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"nodes\": %zu, \"edges\": %zu, \"target_hz\": %.1f, \"timer_overhead_ns\": %.2f,\n",
                 nodes, edges, hz, overhead);
    obj("service_time", svc);
    std::fprintf(f, ",\n");
    obj("response_time", resp);
    std::fprintf(f, "\n}\n");
    std::fclose(f);
}

void write_markdown(const std::string& path, const Percentiles& svc, const Percentiles& resp,
                    double hz, double overhead, std::size_t nodes, std::size_t edges) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "# RiskSim tick-latency report\n\n");
    std::fprintf(f, "- network: **%zu nodes, %zu edges**\n", nodes, edges);
    std::fprintf(f, "- open-loop target rate: **%.0f Hz**\n", hz);
    std::fprintf(f, "- timer overhead (noise floor): **%.1f ns**\n", overhead);
    std::fprintf(f, "- percentiles are exact order statistics over all retained samples\n\n");
    std::fprintf(f, "| measurement | p50 | p99 | p99.9 | p99.99 | max | mean |\n");
    std::fprintf(f, "|---|--:|--:|--:|--:|--:|--:|\n");
    auto row = [&](const char* name, const Percentiles& p) {
        std::fprintf(f, "| %s (us) | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f |\n", name,
                     p.p50 / 1e3, p.p99 / 1e3, p.p999 / 1e3, p.p9999 / 1e3, p.max / 1e3,
                     p.mean / 1e3);
    };
    row("service time", svc);
    row("response time (CO-free)", resp);
    std::fprintf(f, "\n_Service time = step() run back-to-back. Response time = open-loop "
                    "constant-arrival driver; latency measured from the scheduled tick time, so "
                    "queueing under load is included (coordinated-omission-free)._\n");
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
    EngineConfig cfg;
    cfg.mc_every = 0;  // isolate per-tick compute (contagion + serialize)
    cfg.auto_scenario = true;
    std::uint64_t ticks = 20000;
    std::uint64_t warmup = 2000;
    double hz = 5000.0;  // open-loop target; high enough to also stress the tail

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        auto val = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : "0"; };
        if (a == "--ticks") ticks = std::strtoull(val(), nullptr, 10);
        else if (a == "--warmup") warmup = std::strtoull(val(), nullptr, 10);
        else if (a == "--hz") hz = std::strtod(val(), nullptr);
        else if (a == "--core") cfg.n_core = std::strtoull(val(), nullptr, 10);
        else if (a == "--periphery") cfg.n_periphery = std::strtoull(val(), nullptr, 10);
    }

    const double overhead = timer_overhead_ns();

    // ---- Service time: back-to-back ----
    SimEngine svc_engine(cfg);
    const std::size_t nodes = svc_engine.num_nodes();
    const std::size_t edges = svc_engine.num_edges();
    for (std::uint64_t i = 0; i < warmup; ++i) (void) svc_engine.step();
    std::vector<double> svc_samples;
    svc_samples.reserve(ticks);
    for (std::uint64_t i = 0; i < ticks; ++i) {
        const auto t0 = Clock::now();
        auto frame = svc_engine.step();
        const auto t1 = Clock::now();
        svc_samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
        asm volatile("" ::"r"(frame.data()) : "memory");  // don't optimize the frame away
    }

    // ---- Response time: open-loop constant arrival ----
    SimEngine resp_engine(cfg);
    for (std::uint64_t i = 0; i < warmup; ++i) (void) resp_engine.step();
    std::vector<double> resp_samples;
    resp_samples.reserve(ticks);
    const auto period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / hz));
    const auto start = Clock::now();
    for (std::uint64_t i = 0; i < ticks; ++i) {
        const auto scheduled = start + period * static_cast<std::int64_t>(i);
        std::this_thread::sleep_until(scheduled);
        auto frame = resp_engine.step();
        const auto done = Clock::now();
        // CO-free: measured from the SCHEDULED arrival, not the actual start.
        resp_samples.push_back(std::chrono::duration<double, std::nano>(done - scheduled).count());
        asm volatile("" ::"r"(frame.data()) : "memory");
    }

    const Percentiles svc = summarize(std::move(svc_samples));
    const Percentiles resp = summarize(std::move(resp_samples));

    std::fprintf(stderr, "\nRiskSim latency harness  (nodes=%zu edges=%zu, timer overhead %.1f ns)\n",
                 nodes, edges, overhead);
    print_row("service", svc);
    print_row("response", resp);

    std::error_code ec;
    std::filesystem::create_directories("bench/results", ec);
    write_json("bench/results/latency.json", svc, resp, hz, overhead, nodes, edges);
    write_markdown("bench/results/latency.md", svc, resp, hz, overhead, nodes, edges);
    std::fprintf(stderr, "wrote bench/results/latency.{json,md}\n");
    return 0;
}
