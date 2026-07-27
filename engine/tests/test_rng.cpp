#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "risksim/rng.hpp"

using risksim::philox4x32_10;
using risksim::Philox;

// Known-answer tests from the Random123 reference (kat_vectors.txt),
// philox4x32 with 10 rounds. If these pass, the bijection is bit-correct.
TEST(Philox, KnownAnswerVectors) {
    EXPECT_EQ(philox4x32_10({0, 0, 0, 0}, {0, 0}),
              (std::array<std::uint32_t, 4>{0x6627e8d5u, 0xe169c58du, 0xbc57ac4cu, 0x9b00dbd8u}));

    EXPECT_EQ(philox4x32_10({0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu},
                            {0xffffffffu, 0xffffffffu}),
              (std::array<std::uint32_t, 4>{0x408f276du, 0x41c83b0eu, 0xa20bc7c6u, 0x6d5451fdu}));

    EXPECT_EQ(philox4x32_10({0x243f6a88u, 0x85a308d3u, 0x13198a2eu, 0x03707344u},
                            {0xa4093822u, 0x299f31d0u}),
              (std::array<std::uint32_t, 4>{0xd16cfe09u, 0x94fdccebu, 0x5001e420u, 0x24126ea1u}));
}

// constexpr-evaluable: proves the generator has no runtime-only dependencies.
TEST(Philox, ConstexprEvaluation) {
    constexpr auto out = philox4x32_10({0, 0, 0, 0}, {0, 0});
    static_assert(out[0] == 0x6627e8d5u);
    SUCCEED();
}

TEST(Philox, UniformInOpenInterval) {
    const Philox rng(0xDEADBEEFu);
    for (std::uint64_t i = 0; i < 100000; ++i) {
        const double u = rng.uniform(i);
        EXPECT_GT(u, 0.0);
        EXPECT_LT(u, 1.0);
    }
}

// The whole point of a counter-based RNG: value at index i is a pure function of
// (seed, stream, i). Independent of order, thread count, and history.
TEST(Philox, Reproducibility) {
    const Philox a(42);
    const Philox b(42);
    for (std::uint64_t i = 0; i < 1000; ++i) {
        EXPECT_EQ(a.uniform(i), b.uniform(i));
        EXPECT_EQ(a.normal(i), b.normal(i));
    }
    // Different seed -> different stream.
    const Philox c(43);
    EXPECT_NE(a.uniform(7), c.uniform(7));
}

TEST(Philox, NormalMomentsApproximatelyStandard) {
    const Philox rng(12345);
    const std::uint64_t n = 2'000'000;
    double sum = 0.0;
    double sumsq = 0.0;
    for (std::uint64_t i = 0; i < n; ++i) {
        const double z = rng.normal(i);
        sum += z;
        sumsq += z * z;
    }
    const double mean = sum / static_cast<double>(n);
    const double var = sumsq / static_cast<double>(n) - mean * mean;
    EXPECT_NEAR(mean, 0.0, 5e-3);
    EXPECT_NEAR(var, 1.0, 5e-3);
}
