#!/usr/bin/env bash
# Build (Release) and run the RiskSim benchmark suite, writing results to
# bench/results/. For reproducible latency numbers, pin cores / disable turbo /
# set the performance governor first (see docs/benchmarks.md) -- this script does
# NOT do that for you, and prints a reminder.
set -euo pipefail
cd "$(dirname "$0")/.."

echo ">> reminder: for publishable numbers, pin cores + disable turbo (see docs/benchmarks.md)"

cmake -S . -B build/bench -DCMAKE_BUILD_TYPE=Release -DRISKSIM_BUILD_BENCH=ON \
      -DCPM_SOURCE_CACHE=.cpm-cache >/dev/null
cmake --build build/bench -j >/dev/null

mkdir -p bench/results
echo ">> microbenchmarks (Google Benchmark)"
./build/bench/bench/risksim_bench \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > bench/results/micro.json
./build/bench/bench/risksim_bench --benchmark_repetitions=3 \
    --benchmark_report_aggregates_only=true 2>/dev/null || true

echo ">> latency harness (coordinated-omission-free)"
./build/bench/bench/risksim_latency --ticks 20000 --warmup 2000 --hz 5000

echo ">> results in bench/results/: micro.json, latency.json, latency.md"
