#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "risksim/runtime/spsc_ring.hpp"

using risksim::runtime::SpscRing;

TEST(SpscRing, FifoOrderAndEmptyFull) {
    SpscRing<int, 8> ring;
    EXPECT_EQ(ring.capacity(), 8u);
    EXPECT_FALSE(ring.try_pop().has_value());

    for (int i = 0; i < 8; ++i) EXPECT_TRUE(ring.try_push(i));
    EXPECT_FALSE(ring.try_push(99));  // full

    for (int i = 0; i < 8; ++i) {
        const auto v = ring.try_pop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i);  // FIFO
    }
    EXPECT_FALSE(ring.try_pop().has_value());  // empty again
}

TEST(SpscRing, WrapsAroundManyTimes) {
    SpscRing<int, 4> ring;
    for (int round = 0; round < 100; ++round) {
        EXPECT_TRUE(ring.try_push(round));
        const auto v = ring.try_pop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, round);
    }
}

// Concurrent stress test. Run under ThreadSanitizer (cmake --preset tsan) to
// verify the acquire/release contract: every value crosses exactly once and in
// order, with no data race on the payload.
TEST(SpscRing, ConcurrentNoLossNoDupInOrder) {
    SpscRing<std::uint64_t, 1024> ring;
    constexpr std::uint64_t kN = 2'000'000;

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kN;) {
            if (ring.try_push(i)) {
                ++i;
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::uint64_t expected = 0;
    bool ok = true;
    while (expected < kN) {
        const auto v = ring.try_pop();
        if (v.has_value()) {
            if (*v != expected) ok = false;  // ordering / loss / dup check
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    EXPECT_TRUE(ok);
    EXPECT_EQ(expected, kN);
}
