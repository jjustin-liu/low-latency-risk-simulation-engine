// Philox4x32-10 counter-based RNG (Salmon et al., "Parallel Random Numbers:
// As Easy as 1, 2, 3", SC'11).
//
// Why counter-based, not a stateful generator (Mersenne Twister etc.):
//   * The n-th draw is a pure function of (seed, stream, n). There is no
//     sequential state, so results are IDENTICAL regardless of how many threads
//     or in what order paths are computed -> bit-reproducible Monte Carlo
//     across 1 vs N cores. This is the property the validation suite relies on.
//   * Trivially parallel / SIMD-friendly: each path or block owns a disjoint
//     counter range; no locks, no per-thread seeding pitfalls.
//   * Tiny state, passes BigCrush; the same family backs NumPy's Generator and
//     JAX.
#ifndef RISKSIM_RNG_HPP
#define RISKSIM_RNG_HPP

#include <array>
#include <cstdint>

#include "risksim/distributions.hpp"

namespace risksim {

// Raw Philox4x32-10 bijection: maps a 128-bit counter + 64-bit key to a 128-bit
// output block (four uint32 words). `constexpr` so key vectors can be checked at
// compile time.
[[nodiscard]] constexpr std::array<std::uint32_t, 4> philox4x32_10(
    std::array<std::uint32_t, 4> ctr, std::array<std::uint32_t, 2> key) noexcept {
    constexpr std::uint32_t kM0 = 0xD2511F53u;
    constexpr std::uint32_t kM1 = 0xCD9E8D57u;
    constexpr std::uint32_t kW0 = 0x9E3779B9u;  // golden ratio
    constexpr std::uint32_t kW1 = 0xBB67AE85u;  // sqrt(3) - 1

    for (int round = 0; round < 10; ++round) {
        if (round > 0) {
            key[0] += kW0;
            key[1] += kW1;
        }
        const std::uint64_t p0 = std::uint64_t{kM0} * ctr[0];
        const std::uint64_t p1 = std::uint64_t{kM1} * ctr[2];
        const std::uint32_t hi0 = static_cast<std::uint32_t>(p0 >> 32);
        const std::uint32_t lo0 = static_cast<std::uint32_t>(p0);
        const std::uint32_t hi1 = static_cast<std::uint32_t>(p1 >> 32);
        const std::uint32_t lo1 = static_cast<std::uint32_t>(p1);
        ctr = {static_cast<std::uint32_t>(hi1 ^ ctr[1] ^ key[0]), lo1,
               static_cast<std::uint32_t>(hi0 ^ ctr[3] ^ key[1]), lo0};
    }
    return ctr;
}

// Stateless stream over the Philox output space. A `Philox` is just a (seed,
// stream) pair; call the addressed accessors with an absolute index and get the
// same value every time, on every platform.
class Philox {
public:
    constexpr Philox() noexcept = default;
    constexpr explicit Philox(std::uint64_t seed, std::uint64_t stream = 0) noexcept
        : seed_(seed), stream_(stream) {}

    // The `index`-th 128-bit output block (four uint32).
    [[nodiscard]] constexpr std::array<std::uint32_t, 4> block(std::uint64_t index) const noexcept {
        const std::array<std::uint32_t, 4> ctr = {
            static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(index >> 32),
            static_cast<std::uint32_t>(stream_), static_cast<std::uint32_t>(stream_ >> 32)};
        const std::array<std::uint32_t, 2> key = {static_cast<std::uint32_t>(seed_),
                                                  static_cast<std::uint32_t>(seed_ >> 32)};
        return philox4x32_10(ctr, key);
    }

    // The `i`-th uniform double in the open interval (0, 1). Each output block
    // yields two 53-bit doubles, so block = i/2, half = i%2.
    [[nodiscard]] double uniform(std::uint64_t i) const noexcept {
        const auto b = block(i >> 1);
        const unsigned half = static_cast<unsigned>(i & 1u);
        const std::uint64_t hi = b[half * 2];
        const std::uint64_t lo = b[half * 2 + 1];
        const std::uint64_t bits = (hi << 32) | lo;
        // Top 53 bits -> [0,1); shift to the open interval so inverse-CDF never
        // sees an exact 0 or 1.
        const double u = static_cast<double>(bits >> 11) * (1.0 / 9007199254740992.0);
        return u <= 0.0 ? (0.5 / 9007199254740992.0) : u;
    }

    // The `i`-th standard-normal draw, via inverse-CDF transform of `uniform(i)`.
    // Inverse-CDF (not Box-Muller) keeps the mapping index -> value one-to-one,
    // which antithetic variates and reproducibility checks depend on.
    [[nodiscard]] double normal(std::uint64_t i) const noexcept {
        return norm_inv_cdf(uniform(i));
    }

    [[nodiscard]] constexpr std::uint64_t seed() const noexcept { return seed_; }
    [[nodiscard]] constexpr std::uint64_t stream() const noexcept { return stream_; }

private:
    std::uint64_t seed_ = 0;
    std::uint64_t stream_ = 0;
};

// A sequential cursor over a single Philox stream. Draws advance an internal
// index, so a rejection sampler (e.g. Marsaglia-Tsang gamma) can consume a
// variable number of variates while staying reproducible: give each Monte-Carlo
// path its own stream (stream = path index) and every path replays identically
// regardless of thread count or order.
class DrawCursor {
public:
    constexpr explicit DrawCursor(Philox rng) noexcept : rng_(rng) {}
    constexpr DrawCursor(std::uint64_t seed, std::uint64_t stream) noexcept
        : rng_(seed, stream) {}

    [[nodiscard]] double uniform() noexcept { return rng_.uniform(i_++); }
    [[nodiscard]] double normal() noexcept { return rng_.normal(i_++); }
    [[nodiscard]] std::uint64_t drawn() const noexcept { return i_; }

private:
    Philox rng_;
    std::uint64_t i_ = 0;
};

}  // namespace risksim

#endif  // RISKSIM_RNG_HPP
