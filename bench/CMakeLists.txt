# Benchmark harness: Google Benchmark microbenchmarks + a standalone,
# coordinated-omission-free latency harness. Built only with -DRISKSIM_BUILD_BENCH=ON.
CPMAddPackage(
    NAME benchmark
    GITHUB_REPOSITORY google/benchmark
    VERSION 1.9.0
    OPTIONS
        "BENCHMARK_ENABLE_TESTING OFF"
        "BENCHMARK_ENABLE_INSTALL OFF"
        "BENCHMARK_ENABLE_WERROR OFF"
)

add_executable(risksim_bench microbench.cpp)
target_link_libraries(risksim_bench
    PRIVATE risksim::core risksim::sim risksim::simd risksim::lob risksim::runtime
            benchmark::benchmark benchmark::benchmark_main)

add_executable(risksim_latency latency_harness.cpp)
target_link_libraries(risksim_latency
    PRIVATE risksim::core risksim::sim risksim::runtime)
