cmake_minimum_required(VERSION 3.24)

project(RiskSim
    VERSION 0.1.0
    DESCRIPTION "Microsecond-latency systemic-risk simulation engine"
    LANGUAGES CXX)

# ---- Global settings -------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Export compile_commands.json for clang-tidy / editors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CPM)

# ---- Warnings (opt-in strict; sanitizers select via RISKSIM_SANITIZER) ----
add_library(risksim_warnings INTERFACE)
target_compile_options(risksim_warnings INTERFACE
    -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Wpedantic)

# ---- Sanitizer selection ---------------------------------------------------
set(RISKSIM_SANITIZER "none" CACHE STRING "Sanitizer: none | asan-ubsan | tsan")
set_property(CACHE RISKSIM_SANITIZER PROPERTY STRINGS none asan-ubsan tsan)

add_library(risksim_sanitizer INTERFACE)
if(RISKSIM_SANITIZER STREQUAL "asan-ubsan")
    target_compile_options(risksim_sanitizer INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(risksim_sanitizer INTERFACE -fsanitize=address,undefined)
elseif(RISKSIM_SANITIZER STREQUAL "tsan")
    target_compile_options(risksim_sanitizer INTERFACE
        -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(risksim_sanitizer INTERFACE -fsanitize=thread)
endif()

# Aggregate "options" interface every RiskSim target links.
add_library(risksim_options INTERFACE)
target_link_libraries(risksim_options INTERFACE risksim_warnings risksim_sanitizer)

option(RISKSIM_BUILD_BENCH "Build the benchmark harness (Google Benchmark + HdrHistogram)" OFF)
option(RISKSIM_BUILD_PY "Build the Python (nanobind) bindings" OFF)

enable_testing()
add_subdirectory(engine)

if(RISKSIM_BUILD_BENCH)
    add_subdirectory(bench)
endif()
