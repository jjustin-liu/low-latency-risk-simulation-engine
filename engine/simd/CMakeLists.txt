# risksim_simd — Highway-vectorized hot-path kernels + scalar reference.
CPMAddPackage(
    NAME highway
    GITHUB_REPOSITORY google/highway
    GIT_TAG 1.2.0
    VERSION 1.2.0
    OPTIONS
        "HWY_ENABLE_TESTS OFF"
        "HWY_ENABLE_EXAMPLES OFF"
        "HWY_ENABLE_INSTALL OFF"
        "HWY_ENABLE_CONTRIB ON"
)

add_library(risksim_simd STATIC src/simd_kernels.cpp)
target_include_directories(risksim_simd PUBLIC include PRIVATE src)
# Highway headers are warning-noisy under our strict flags, so link only core +
# hwy here (no risksim_options) and keep the strict-warning gate on our code via
# tests/bench that consume this target.
target_link_libraries(risksim_simd PUBLIC risksim::core PRIVATE hwy hwy_contrib)

add_library(risksim::simd ALIAS risksim_simd)
