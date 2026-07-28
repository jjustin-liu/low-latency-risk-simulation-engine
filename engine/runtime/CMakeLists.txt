# risksim_runtime — low-latency runtime: lock-free rings, wire codec, sim engine.
find_package(Threads REQUIRED)

add_library(risksim_runtime STATIC
    src/sim_engine.cpp
)
target_include_directories(risksim_runtime PUBLIC include)
target_link_libraries(risksim_runtime
    PUBLIC risksim::core risksim::sim risksim::market Threads::Threads
    PRIVATE risksim_options)

add_library(risksim::runtime ALIAS risksim_runtime)

# The engine executable: three-stage stdin -> compute -> stdout pipeline.
add_executable(risksim_engine src/main.cpp)
target_link_libraries(risksim_engine PRIVATE risksim::runtime risksim_options)
