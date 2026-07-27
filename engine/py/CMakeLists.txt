# risksim _risksim — nanobind bindings exposing the shipping C++ core to the
# Python research / model-validation layer.
#
# The module is built ONLY when -DRISKSIM_BUILD_PY=ON (guarded in the parent
# engine/CMakeLists.txt). Point CMake at the target interpreter with
# -DPython_EXECUTABLE=$(which python).

find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)

# nanobind via CPM.
CPMAddPackage(
    NAME nanobind
    GITHUB_REPOSITORY wjakob/nanobind
    VERSION 2.4.0
)

nanobind_add_module(_risksim STABLE_ABI NB_STATIC bindings.cpp)

target_link_libraries(_risksim PRIVATE risksim::core risksim::sim)

# Emit the extension into the `risksim` shim package (engine/py/risksim/) so
# adding engine/py/ to sys.path makes `import risksim` work directly.
set(RISKSIM_PY_PKG "${CMAKE_CURRENT_SOURCE_DIR}/risksim")
set_target_properties(_risksim PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${RISKSIM_PY_PKG}"
)

# Copy the freshly-built module into the package after every build (covers
# multi-config generators that append a per-config subdir).
add_custom_command(TARGET _risksim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:_risksim> "${RISKSIM_PY_PKG}/"
    COMMENT "Staging _risksim module into engine/py/risksim/"
)
