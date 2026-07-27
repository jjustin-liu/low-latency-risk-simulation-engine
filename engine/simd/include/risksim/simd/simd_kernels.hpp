// Vectorized hot-path kernels, with a scalar reference as the correctness oracle.
//
// The batched standard-normal CDF is used in Merton PD (Phi(-DD) over a portfolio
// of firms) and in the Gaussian-copula marginal map. Written once with Google
// Highway, it dispatches at runtime to the best available SIMD target -- NEON on
// Apple/Graviton, AVX2/AVX-512 on x86 -- so the SAME source is measured per
// architecture. Every SIMD result is validated against the scalar reference
// within a documented tolerance (float erf is not bit-identical to std::erf; the
// difference is the polynomial approximation error, ~1e-6).
#ifndef RISKSIM_SIMD_KERNELS_HPP
#define RISKSIM_SIMD_KERNELS_HPP

#include <cstddef>
#include <span>

namespace risksim::simd {

// out[i] = Phi(x[i]) = 0.5 * erfc(-x[i]/sqrt(2)), computed in double via std::erfc.
// The ground-truth reference.
void norm_cdf_batch_scalar(std::span<const float> x, std::span<float> out);

// Same A&S float polynomial as the SIMD kernel, but SCALAR. Used to isolate the
// pure vectorization speedup in benchmarks (SIMD vs this = same algorithm, only
// the SIMD differs), separate from the algorithm speedup vs libm's std::erfc.
void norm_cdf_batch_scalar_poly(std::span<const float> x, std::span<float> out);

// Same, vectorized (Highway, runtime-dispatched). Result matches the scalar
// reference to within ~2e-6 absolute (float A&S erf approximation).
void norm_cdf_batch(std::span<const float> x, std::span<float> out);

// Name of the SIMD target chosen at runtime (e.g. "NEON", "AVX2", "AVX3"),
// for the benchmark report.
const char* active_target();

}  // namespace risksim::simd

#endif  // RISKSIM_SIMD_KERNELS_HPP
