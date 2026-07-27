include(GoogleTest)

add_executable(risksim_core_tests
    test_rng.cpp
    test_distributions.cpp
    test_risk.cpp
    test_linalg.cpp
    test_merton.cpp
    test_copula.cpp
    test_network.cpp
    test_contagion.cpp
    test_monte_carlo.cpp
    test_spsc_ring.cpp
    test_wire.cpp
    test_engine.cpp
    test_simd.cpp
    test_order_book.cpp
    test_fire_sale.cpp
)

target_link_libraries(risksim_core_tests
    PRIVATE risksim::core risksim::sim risksim::simd risksim::lob risksim::market risksim::runtime risksim_options GTest::gtest_main)

gtest_discover_tests(risksim_core_tests)
