# risksim_core — pure, deterministic, allocation-free math.
# No threads, no I/O. This is the scalar reference and the source of truth
# shared with the SIMD runtime and the Python research bindings.

add_library(risksim_core
    src/distributions.cpp
    src/risk.cpp
    src/linalg.cpp
    src/merton.cpp
    src/copula.cpp
    src/network.cpp
    src/contagion.cpp
)

target_include_directories(risksim_core PUBLIC include)
target_link_libraries(risksim_core PRIVATE risksim_options)

add_library(risksim::core ALIAS risksim_core)
