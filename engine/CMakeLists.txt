# ---- Third-party dependencies (pinned via CPM) -----------------------------
CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    VERSION 1.15.2
    OPTIONS
        "INSTALL_GTEST OFF"
        "gtest_force_shared_crt ON"
)

add_subdirectory(core)
add_subdirectory(sim)
add_subdirectory(simd)
add_subdirectory(lob)
add_subdirectory(market)
add_subdirectory(runtime)
add_subdirectory(tests)

if(RISKSIM_BUILD_PY)
    add_subdirectory(py)
endif()
