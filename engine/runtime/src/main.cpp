// RiskSim engine executable.
//
// Three-stage pipeline mirroring a real market-data engine:
//   ingress (stdin)  -> parses operator control lines into shock commands
//   compute (thread) -> SimEngine.step() builds a wire frame each tick
//   egress  (main)   -> pops frames from the lock-free ring, length-prefixes
//                       them, and writes to stdout (the bridge fans out to WebSocket)
//
// Transport is deliberately stdout/stdin: the bridge spawns this process and
// reads its stdout, so there are no sockets in the hot path and `--record` is a
// simple shell redirect:  risksim_engine --ticks 600 > data/replay.bin
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "risksim/runtime/sim_engine.hpp"
#include "risksim/runtime/spsc_ring.hpp"

using namespace risksim::runtime;

namespace {

using FramePtr = std::shared_ptr<std::vector<std::byte>>;
SpscRing<FramePtr, 32> g_ring;
std::atomic<bool> g_running{true};

// Shared operator-control state (ingress -> compute).
struct Control {
    std::mutex mu;
    std::vector<std::size_t> banks;
    double magnitude = 0.0;
    bool pending = false;
    bool clear = false;
};
Control g_control;

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// Parse "shock <bank> [bank...] <magnitude>" or "shock core <magnitude>" / "clear".
void handle_control_line(std::string_view line, std::size_t n_core) {
    // Trim.
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
        line.remove_suffix(1);
    if (line.empty()) return;

    if (line == "clear") {
        std::lock_guard lk(g_control.mu);
        g_control.clear = true;
        g_control.pending = true;
        return;
    }

    std::vector<std::string_view> tok;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        std::size_t j = i;
        while (j < line.size() && line[j] != ' ') ++j;
        if (j > i) tok.push_back(line.substr(i, j - i));
        i = j;
    }
    if (tok.empty() || tok[0] != "shock") return;

    std::vector<std::size_t> banks;
    double magnitude = 0.8;
    for (std::size_t t = 1; t < tok.size(); ++t) {
        if (tok[t] == "core") {
            for (std::size_t b = 0; b < n_core; ++b) banks.push_back(b);
            continue;
        }
        // A trailing decimal is the magnitude; an integer is a bank id.
        // (libc++ lacks std::from_chars for floating point, so use strtod.)
        if (tok[t].find('.') != std::string_view::npos) {
            const std::string s(tok[t]);
            magnitude = std::strtod(s.c_str(), nullptr);
        } else {
            std::size_t id = 0;
            const auto res = std::from_chars(tok[t].data(), tok[t].data() + tok[t].size(), id);
            if (res.ec == std::errc{}) banks.push_back(id);
        }
    }
    if (banks.empty()) return;
    std::lock_guard lk(g_control.mu);
    g_control.banks = std::move(banks);
    g_control.magnitude = magnitude;
    g_control.clear = false;
    g_control.pending = true;
}

void ingress_thread(std::size_t n_core) {
    std::string line;
    int ch;
    while (g_running.load(std::memory_order_relaxed) && (ch = std::getchar()) != EOF) {
        if (ch == '\n') {
            handle_control_line(line, n_core);
            line.clear();
        } else {
            line.push_back(static_cast<char>(ch));
        }
    }
}

void compute_thread(EngineConfig cfg, std::uint64_t max_ticks, double hz) {
    SimEngine engine(cfg);
    const auto period = std::chrono::duration<double>(1.0 / hz);
    auto next = std::chrono::steady_clock::now();

    for (std::uint64_t t = 0; g_running.load(std::memory_order_relaxed); ++t) {
        if (max_ticks > 0 && t >= max_ticks) break;

        // Drain pending control.
        {
            std::lock_guard lk(g_control.mu);
            if (g_control.pending) {
                if (g_control.clear)
                    engine.clear_shock();
                else
                    engine.apply_shock(g_control.banks, g_control.magnitude);
                g_control.pending = false;
            }
        }

        auto frame = std::make_shared<std::vector<std::byte>>(engine.step());
        // Stamp wall-clock emit time into the header (offset 24, u64) for the
        // browser's end-to-end latency HUD.
        if (frame->size() >= 32) {
            const std::uint64_t w = now_ns();
            std::memcpy(frame->data() + 24, &w, sizeof(w));
        }
        // Lossy tick channel: if the egress ring is full, drop the newest frame.
        (void) g_ring.try_push(frame);

        if (max_ticks == 0) {
            next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
            std::this_thread::sleep_until(next);
        }
    }
    g_running.store(false, std::memory_order_relaxed);
}

// Write all bytes to stdout; returns false on a broken pipe.
bool write_all(const std::byte* p, std::size_t n) {
    std::size_t off = 0;
    while (off < n) {
        const std::size_t wrote = std::fwrite(p + off, 1, n - off, stdout);
        if (wrote == 0) return false;
        off += wrote;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);

    EngineConfig cfg;
    std::uint64_t max_ticks = 0;  // 0 = run forever
    double hz = 10.0;

    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto next_val = [&]() -> std::string_view { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--ticks") max_ticks = std::strtoull(next_val().data(), nullptr, 10);
        else if (a == "--hz") hz = std::strtod(next_val().data(), nullptr);
        else if (a == "--core") cfg.n_core = std::strtoull(next_val().data(), nullptr, 10);
        else if (a == "--periphery") cfg.n_periphery = std::strtoull(next_val().data(), nullptr, 10);
        else if (a == "--seed") cfg.seed = std::strtoull(next_val().data(), nullptr, 10);
        else if (a == "--paths") cfg.mc_paths = static_cast<std::uint32_t>(std::strtoul(next_val().data(), nullptr, 10));
        else if (a == "--no-auto") cfg.auto_scenario = false;
    }

    std::thread ingress(ingress_thread, cfg.n_core);
    std::thread compute(compute_thread, cfg, max_ticks, hz);

    // Egress: drain the ring, length-prefix each frame, write to stdout.
    std::uint64_t emitted = 0;
    while (g_running.load(std::memory_order_relaxed) || g_ring.size_approx() > 0) {
        auto frame = g_ring.try_pop();
        if (!frame.has_value()) {
            if (!g_running.load(std::memory_order_relaxed)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const std::vector<std::byte>& f = **frame;
        const std::uint32_t len = static_cast<std::uint32_t>(f.size());
        std::byte prefix[4];
        std::memcpy(prefix, &len, 4);
        if (!write_all(prefix, 4) || !write_all(f.data(), f.size())) break;
        std::fflush(stdout);
        ++emitted;
    }

    g_running.store(false, std::memory_order_relaxed);
    if (compute.joinable()) compute.join();
    // Ingress blocks on stdin; detach so we don't hang on exit.
    ingress.detach();
    std::fprintf(stderr, "risksim-engine: emitted %llu frames\n",
                 static_cast<unsigned long long>(emitted));
    return 0;
}
