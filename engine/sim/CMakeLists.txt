# risksim_sim — header-only Monte Carlo orchestration over risksim_core.
find_package(Threads REQUIRED)

add_library(risksim_sim INTERFACE)
target_include_directories(risksim_sim INTERFACE include)
target_link_libraries(risksim_sim INTERFACE risksim::core Threads::Threads)

add_library(risksim::sim ALIAS risksim_sim)
