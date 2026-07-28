# Low Latency Risk Simulation Engine

[![ci-cpp](https://github.com/jjustin-liu/low-latency-risk-simulation-engine/actions/workflows/ci-cpp.yml/badge.svg)](https://github.com/jjustin-liu/low-latency-risk-simulation-engine/actions/workflows/ci-cpp.yml)
[![ci-python](https://github.com/jjustin-liu/low-latency-risk-simulation-engine/actions/workflows/ci-python.yml/badge.svg)](https://github.com/jjustin-liu/low-latency-risk-simulation-engine/actions/workflows/ci-python.yml)
[![bench](https://github.com/jjustin-liu/low-latency-risk-simulation-engine/actions/workflows/bench.yml/badge.svg)](https://github.com/jjustin-liu/low-latency-risk-simulation-engine/actions/workflows/bench.yml)

A microsecond-latency engine for simulating financial contagion and pricing systemic tail
risk. It models how a shock to one bank propagates through the system along **two coupled
channels** — counterparty default (who owes whom) and price-mediated fire sales (forced
selling that drives prices down and marks down everyone holding the same assets) — runs
Monte-Carlo simulation to estimate Value-at-Risk and Expected Shortfall, calibrates to real
bank market data, and streams the evolving network state to a GPU-rendered browser view in
real time.

The compute core is C++23. A Node service bridges it to the browser over a binary WebSocket
protocol; a Next.js + deck.gl frontend renders the contagion as it unfolds; and a Python
layer (bound to the exact shipping C++ via nanobind) runs the statistical validation.

---

## Table of contents

- [What it does](#what-it-does)
- [Architecture](#architecture)
- [Components](#components)
- [Performance](#performance)
- [Correctness & testing](#correctness--testing)
- [Quickstart](#quickstart)
- [Tech stack](#tech-stack)

---

## What it does

Banks are connected two ways, and a crisis spreads along both:

1. **Counterparty exposure.** Bank A borrows from bank B. If A defaults, B takes a loss, and
   if that loss exceeds B's capital, B fails too — a default cascade.
2. **Common holdings.** When a distressed bank must raise cash, it sells assets into a market
   with finite liquidity. Prices drop, and *every* other bank holding those assets is marked
   down, which can force *them* to sell — a fire-sale spiral.

The engine models both, and crucially runs the fire-sale channel **through a real order-book
matching engine**, so price impact emerges from the resting liquidity rather than an assumed
formula. A single tick of the simulation:

```
shock  →  contagion (counterparty + fire-sale)  →  per-bank distress
       →  Monte-Carlo systemic VaR / Expected Shortfall
       →  binary frame  →  bridge  →  browser (GPU render + risk dashboard)
```

You can drive it interactively — click to shock specific banks, or type a plain-English
instruction ("crash the three biggest banks by 40%") that an LLM turns into a structured,
validated shock, with a deterministic keyword fallback when offline.

---

## Architecture

```
proto/     wire schema — single source of truth; codegen emits both C++ and TS bindings
engine/    C++23 compute core, layered so dependencies point one way: runtime → sim → core
  core/      pure math: RNG, distributions, copulas, Merton/KMV, contagion models, VaR/ES
  sim/       Monte-Carlo driver: copula shocks → contagion → systemic loss distribution
  simd/      Highway-vectorized hot kernels (NEON / AVX), validated against a scalar oracle
  lob/       O(1) limit-order-book matching engine
  market/    price-mediated fire-sale contagion, executed through the order book
  runtime/   lock-free SPSC ring, wire codec, per-tick simulation loop, engine executable
  py/        nanobind bindings — the Python research layer calls the exact shipping core
bridge/    Node service: reads the engine's binary frame stream, fans it out over WebSocket
web/       Next.js + deck.gl frontend: contagion view, VaR/ES + loss-distribution, per-bank
           default-risk and fire-sale/order-book panels, latency HUD, shock controls
research/  Python: real-data calibration + statistical validation of every model
bench/     Google Benchmark microbenchmarks + a coordinated-omission-free latency harness
```

**One-way dependencies.** `core` is pure, deterministic, allocation-free, and free of
threads and I/O. `sim` schedules the pure `core` kernels; `runtime` handles concurrency and
transport. Nothing lower depends on anything higher. This is what makes the math unit-testable
in isolation, lets it serve as the reference implementation the SIMD kernels are checked
against, and lets it be reused verbatim from Python.

**Data flow.** The engine emits a stream of length-prefixed binary frames on stdout. The Node
bridge reassembles them and broadcasts each frame to connected browsers as a single WebSocket
message. In the browser, a Web Worker decodes the frame into typed arrays (zero per-node
object allocation) and hands them straight to deck.gl as GPU attributes, so the render stays
smooth at the simulation's tick rate.

**Wire protocol as a single source of truth.** The binary layout is defined once in
`proto/schema.json`; a codegen step emits both the C++ struct/serializer and the TypeScript
decoder. A CI job regenerates them and fails if the committed output drifts, so the two sides
can never disagree.

---

## Components

### Quantitative core (`engine/core`)

- **RNG — Philox4x32 counter-based.** The n-th draw is a pure function of `(seed, stream, n)`,
  with no sequential state. Results are therefore identical regardless of how many threads run
  or in what order, which makes the whole Monte Carlo bit-reproducible across core counts.
  Passes the reference known-answer vectors.
- **Distributions.** Normal PDF/CDF/inverse (Acklam + a Halley refinement), and Student-t
  CDF/inverse via the regularized incomplete beta. The log-gamma used there is a self-contained
  Lanczos implementation rather than `std::lgamma`, because `std::lgamma` writes a shared global
  (`signgam`) and is a data race under multithreading.
- **Copulas.** Gaussian and Student-t, sharing a Cholesky factor. The Student-t's degrees of
  freedom control tail dependence — the tendency for extremes to occur together — which the
  Gaussian copula assumes away. Calibration uses Kendall's τ for robustness.
- **Merton / KMV.** A structural credit model: a firm's equity is a call option on its assets,
  giving a distance-to-default and a default probability. Since asset value and volatility are
  unobservable, a KMV fixed-point iteration backs them out of observed equity value and vol.
- **Network reconstruction.** The true bilateral exposure matrix is confidential; only the
  per-bank totals are public. It's reconstructed two ways — maximum-entropy (Sinkhorn-Knopp)
  and minimum-density — which bracket the true contagion (the former spreads exposure and
  under-states it; the latter concentrates it and over-states it).
- **Contagion models.** Three, behind one interface:
  - *Eisenberg–Noe clearing* — solves the fixed point where each bank's ability to pay depends
    on what it receives, which depends on what everyone else can pay. Implemented two ways
    (Picard iteration and the fictitious-default linear solve) that cross-check each other.
  - *DebtRank* — captures distress *before* default (marking down claims on a stressed
    counterparty), in both the original single-pass and the reverberating dynamical form.
  - *Furfine* — a threshold default cascade, the intuitive baseline.
- **Risk measures.** VaR and Expected Shortfall. ES is computed exactly while streaming, via a
  fixed-capacity min-heap of the worst-k losses (`O(k)` memory, no full sort), alongside
  Welford's algorithm for numerically stable running moments.

### Monte-Carlo simulation (`engine/sim`)

Each path draws a correlated cross-section of bank asset returns from a copula, turns the
downside into a per-bank shock, runs a contagion model, and records the system-wide loss. The
distribution of that loss yields systemic VaR/ES — and swapping only the copula isolates how
much tail dependence matters. Includes antithetic variates and a control variate for variance
reduction, and parallelizes over disjoint Philox counter ranges so the result is identical for
1 thread or N.

### Limit order book (`engine/lob`)

A from-scratch matching engine built for deterministic, allocation-free operation:

- Integer tick prices with a **directly-indexed array of price levels** (no tree, no hashing
  on the hot path).
- An **intrusive FIFO doubly-linked list** per level for price-time priority.
- A **preallocated slab pool** of order nodes with a free list, so matching never allocates.
- **Generation-tagged handles** so a stale cancel of a filled or reused slot is safely ignored.
- **Occupancy bitsets** with hardware bit-scan to find the next best price level.

Add-limit, cancel, and best-quote reads are `O(1)`; a marketable order is `O(levels swept)`.
The same order stream always produces byte-identical fills.

### Fire-sale contagion (`engine/market`)

The price-mediated channel, layered on top of the order book. A distressed bank deleverages by
market-selling into a per-asset book seeded with resting liquidity; the fills push the price
down; every bank holding that asset marks to the new price, loses equity, and may sell in turn.
It runs in rounds until it settles, and can be coupled to the counterparty channel so a
defaulter's shortfall also propagates through the exposure network. Because both the book and
the deleveraging rule are integer/deterministic, the whole cascade is reproducible.

### SIMD kernels (`engine/simd`)

The normal CDF — used in Merton default probabilities and the Gaussian-copula map — vectorized
with Google Highway so the same source dispatches at runtime to NEON on Apple/ARM and AVX2/
AVX-512 on x86. Every SIMD result is checked against a scalar reference within a documented
tolerance, with the scalar version kept as the source of truth.

### Low-latency runtime (`engine/runtime`)

- **Lock-free SPSC ring buffer** (LMAX-Disruptor style) for handing frames between threads
  without locks: a power-of-two ring with the head/tail counters on separate cache lines, each
  side caching its view of the other so the steady state does zero cross-core loads, and an
  explicit acquire/release publication contract.
- **Per-tick simulation loop** that eases the shock toward a target, runs contagion for the
  current state, periodically refreshes the Monte-Carlo VaR/ES, and serializes a frame.
- **The `risksim_engine` executable** — a three-stage pipeline (stdin control → compute →
  stdout egress) whose stdout is the binary frame stream the bridge consumes. A `--record` run
  is just a shell redirect, producing a replayable capture.

### Bridge (`bridge/`)

A small Node service that spawns the engine (or replays a recorded capture), reassembles the
length-prefixed frames, and fans them out over WebSocket with a drop-to-latest policy under
backpressure. It also forwards operator commands to the engine and exposes Prometheus metrics.

### Frontend (`web/`)

A Next.js + deck.gl single-page app: the contagion network rendered on the GPU (nodes colored
by health, edges by exposure), a VaR/Expected-Shortfall dashboard, an on-screen latency/FPS
HUD, stress-scenario controls, and a natural-language shock console. Binary frames are decoded
in a Web Worker and fed to deck.gl as binary attributes so the UI never blocks.

### Research & validation (`research/`)

Python bound to the shipping C++ core via nanobind, so the notebooks validate the code that
runs, not a re-implementation. It calibrates the model to **11 large US banks (2018–2024)**
using real equity returns (Ledoit-Wolf-shrunk correlations) and balance-sheet sizes, and runs
the standard statistical backtests: Kupiec proportion-of-failures, Christoffersen conditional
coverage, the Basel traffic-light test, and Acerbi–Szekely for Expected Shortfall.

Selected findings, reproduced by the test suite:

- With identical marginals and correlation, the **Student-t copula raises systemic Expected
  Shortfall ~9.5%** over the Gaussian (synthetic); on the real bank data the gap is **+2.4% →
  +3.8%, widening deeper into the tail**.
- Backtesting rolling 99% VaR on the real bank portfolio, the Normal model is **rejected by the
  Kupiec test** (2.2% breaches vs a 1% target) and lands in the **Basel red zone** over the
  COVID window; the Student-t model holds up materially better.
- A forced liquidation moves the order book by a hand-verified 2% and marks down every common
  holder; overlap in holdings creates contagion while disjoint holdings do not.

---

## Performance

Microbenchmarks (Google Benchmark) and the latency harness, on an Apple M1 Pro (arm64):

| kernel | throughput / latency |
|---|--:|
| lock-free SPSC ring push + pop | **1.2 ns/op** |
| order-book add / cancel | **~23 ns/op** (167 M/s) |
| order-book match (cross a level) | ~32 ns |
| normal-CDF batch — SIMD (NEON) | 931 M/s (~2× vs the scalar polynomial, ~10× vs libm `erfc`) |
| Philox normal draw | 22 M/s |
| Eisenberg–Noe clearing (100 banks) | ~32 µs |
| DebtRank / Furfine (100 banks) | ~22 µs / ~5 µs |
| empirical VaR/ES (1M samples) | ~140 M/s |

Per-tick engine latency (counterparty contagion + Merton PD + price-mediated fire sale +
serialize, 100 nodes / ~600 edges), as exact order statistics from a coordinated-omission-free
harness (an open-loop, constant-arrival driver that includes queueing delay rather than hiding
it):

| measurement | p50 | p99 | p99.9 | max |
|---|--:|--:|--:|--:|
| service time (back-to-back) | 246 µs | 459 µs | 3.2 ms | 13 ms |

The counterparty-contagion path alone is ~22 µs; the balance is the price-mediated fire-sale
channel, which clears every distressed bank's forced selling through the order book on each
tick (and drives the tail). At the 10 Hz streaming rate this is ~400× under the per-tick
budget, so the render loop never waits on compute.

SIMD width is architecture-dependent: 4-wide float on this ARM machine (NEON), 8/16-wide on
x86 (AVX2/AVX-512) via the same runtime-dispatched source. Numbers regenerate from
`./bench/run.sh`.

---

## Correctness & testing

- **64 C++ tests** (GoogleTest) covering golden analytical values (Black-Scholes-consistent
  Merton, hand-computed Eisenberg–Noe clearing and order-book fills, closed-form VaR/ES),
  property checks, and integration. They pass in Debug and again clean under
  **AddressSanitizer/UBSan** and **ThreadSanitizer** — the latter over a 2-million-operation
  concurrent stress test of the lock-free ring.
- **30 Python tests** for the calibration pipeline and the statistical backtests.
- **CI** runs the C++ build + tests across Linux (gcc, clang) and macOS, the two sanitizer
  configurations, the nanobind build + Python validation, the benchmark harness, and a wire
  codegen drift check.
- The RNG design makes the Monte Carlo **bit-identical across thread counts**, and the order
  book **replays byte-identically** — both are asserted by tests.

---

## Quickstart

Dependencies (GoogleTest, Google Benchmark, Highway, nanobind) are fetched and pinned at
configure time via [CPM](https://github.com/cpm-cmake/CPM.cmake); no system packages required.

```bash
# Engine (C++23) — presets: debug | release | asan-ubsan | tsan
cmake --preset debug
cmake --build --preset debug
ctest --preset debug                 # 64 tests

# Benchmarks (writes bench/results/)
./bench/run.sh

# Python bindings + validation
cmake -S . -B build/py -DRISKSIM_BUILD_PY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/py --target _risksim
python -m pytest research/tests

# Bridge + frontend (bridge on :8080, web on :3000)
cd bridge && npm install && RISKSIM_MODE=replay node src/server.mjs   # or live mode
cd web && npm install && npm run dev
```

Record a replayable capture:

```bash
./build/release/engine/runtime/risksim_engine --ticks 600 > data/replay.bin
```

---

## Tech stack

**C++23** (CMake + CPM, GoogleTest, Google Benchmark, Google Highway, nanobind) ·
**Python** (numpy, scipy, nanobind bindings) ·
**Node** (WebSocket bridge) ·
**Next.js / React / deck.gl / Zustand / TypeScript** ·
**GitHub Actions** CI.

## License

[MIT](./LICENSE).
