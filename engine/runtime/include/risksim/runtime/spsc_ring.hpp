// Single-producer / single-consumer lock-free ring buffer (LMAX Disruptor style).
//
// Memory-ordering contract (the part an interviewer drills):
//   * Each side is the SOLE writer of its own index, so it loads/stores its own
//     index with memory_order_relaxed -- there is no cross-thread order to
//     establish there.
//   * Publication uses a release store / acquire load pair on the OTHER side's
//     index: the producer's release store of `head` happens-after its payload
//     write, so a consumer that acquire-loads the new `head` is guaranteed to see
//     the payload (no torn/stale read). Symmetrically, the consumer's release
//     store of `tail` frees a slot before the producer may overwrite it.
//   * `head` and `tail` live on separate cache lines (alignas) to avoid false
//     sharing, and each side CACHES its view of the other index so the steady
//     state performs zero cross-core loads (the Disruptor's core trick).
// On x86-TSO these compile to plain MOVs; on ARM they emit ldar/stlr -- hence the
// dedicated ThreadSanitizer stress test (and an ARM CI runner).
#ifndef RISKSIM_RUNTIME_SPSC_RING_HPP
#define RISKSIM_RUNTIME_SPSC_RING_HPP

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <vector>

namespace risksim::runtime {

#ifdef __cpp_lib_hardware_interference_size
inline constexpr std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t kCacheLine = 64;
#endif

// `Capacity` must be a power of two. Empty vs full is disambiguated by comparing
// the monotonic 64-bit counters (not the masked indices), so all `Capacity`
// slots are usable -- no reserved slot needed.
template <class T, std::size_t Capacity>
class SpscRing {
    static_assert(std::has_single_bit(Capacity), "Capacity must be a power of two");
    static constexpr std::uint64_t kMask = Capacity - 1;

public:
    SpscRing() : buf_(Capacity) {}
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    // Producer side. Returns false if the ring is full.
    bool try_push(const T& value) {
        const std::uint64_t head = head_.load(std::memory_order_relaxed);
        const std::uint64_t next = head + 1;
        if (next - cached_tail_ > Capacity) {                     // maybe full: refresh cache
            cached_tail_ = tail_.load(std::memory_order_acquire);  // (A) acquire freed-slot news
            if (next - cached_tail_ > Capacity) return false;      // genuinely full
        }
        buf_[head & kMask] = value;                    // (B) write payload
        head_.store(next, std::memory_order_release);  // (C) publish (release)
        return true;
    }

    // Consumer side. Returns std::nullopt if the ring is empty.
    std::optional<T> try_pop() {
        const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == cached_head_) {                                 // maybe empty: refresh cache
            cached_head_ = head_.load(std::memory_order_acquire);   // (D) acquire published news
            if (tail == cached_head_) return std::nullopt;          // genuinely empty
        }
        T value = buf_[tail & kMask];                     // (E) read payload
        tail_.store(tail + 1, std::memory_order_release);  // (F) free the slot (release)
        return value;
    }

    [[nodiscard]] std::size_t size_approx() const {
        return static_cast<std::size_t>(head_.load(std::memory_order_acquire) -
                                        tail_.load(std::memory_order_acquire));
    }
    [[nodiscard]] static constexpr std::size_t capacity() { return Capacity; }

private:
    // Producer-owned line.
    alignas(kCacheLine) std::atomic<std::uint64_t> head_{0};
    std::uint64_t cached_tail_ = 0;
    // Consumer-owned line.
    alignas(kCacheLine) std::atomic<std::uint64_t> tail_{0};
    std::uint64_t cached_head_ = 0;
    // Storage on its own line(s).
    alignas(kCacheLine) std::vector<T> buf_;
};

}  // namespace risksim::runtime

#endif  // RISKSIM_RUNTIME_SPSC_RING_HPP
