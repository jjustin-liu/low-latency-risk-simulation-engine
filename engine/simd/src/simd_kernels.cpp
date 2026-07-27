// Highway implementation of the batched normal CDF, with runtime dispatch.
#include "risksim/simd/simd_kernels.hpp"

#include <cmath>

// Highway foreach_target boilerplate: this translation unit is re-included once
// per SIMD target, and HWY_DYNAMIC_DISPATCH picks the best at runtime.
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "simd_kernels.cpp"
#include "hwy/foreach_target.h"  // must precede highway.h
#include "hwy/highway.h"
#include "hwy/contrib/math/math-inl.h"

HWY_BEFORE_NAMESPACE();
namespace risksim::simd {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

// Abramowitz & Stegun 7.1.26 erf approximation (max abs error ~1.5e-7), then
// Phi(x) = 0.5 * (1 + erf(x / sqrt(2))).
template <class D, class V>
V NormCdfV(D d, V x) {
    const V inv_sqrt2 = hn::Set(d, 0.7071067811865476f);
    const V z = hn::Mul(x, inv_sqrt2);
    const V az = hn::Abs(z);

    const V one = hn::Set(d, 1.0f);
    const V p = hn::Set(d, 0.3275911f);
    const V t = hn::Div(one, hn::MulAdd(p, az, one));

    const V a1 = hn::Set(d, 0.254829592f);
    const V a2 = hn::Set(d, -0.284496736f);
    const V a3 = hn::Set(d, 1.421413741f);
    const V a4 = hn::Set(d, -1.453152027f);
    const V a5 = hn::Set(d, 1.061405429f);
    // poly = t*(a1 + t*(a2 + t*(a3 + t*(a4 + t*a5))))
    V poly = hn::MulAdd(t, a5, a4);
    poly = hn::MulAdd(t, poly, a3);
    poly = hn::MulAdd(t, poly, a2);
    poly = hn::MulAdd(t, poly, a1);
    poly = hn::Mul(t, poly);

    const V ex = hn::Exp(d, hn::Neg(hn::Mul(az, az)));  // exp(-z^2)
    const V erf_abs = hn::Sub(one, hn::Mul(poly, ex));
    const V erf = hn::CopySign(erf_abs, z);  // erf is odd
    return hn::Mul(hn::Set(d, 0.5f), hn::Add(one, erf));
}

void NormCdfBatchImpl(const float* HWY_RESTRICT x, float* HWY_RESTRICT out, std::size_t n) {
    const hn::ScalableTag<float> d;
    const std::size_t lanes = hn::Lanes(d);
    std::size_t i = 0;
    for (; i + lanes <= n; i += lanes) {
        hn::Store(NormCdfV(d, hn::LoadU(d, x + i)), d, out + i);
    }
    if (i < n) {
        const std::size_t rem = n - i;
        hn::Store(NormCdfV(d, hn::LoadN(d, x + i, rem)), d, out + i);  // masked tail
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace risksim::simd
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace risksim::simd {

HWY_EXPORT(NormCdfBatchImpl);

void norm_cdf_batch(std::span<const float> x, std::span<float> out) {
    HWY_DYNAMIC_DISPATCH(NormCdfBatchImpl)(x.data(), out.data(), x.size());
}

void norm_cdf_batch_scalar(std::span<const float> x, std::span<float> out) {
    constexpr double kInvSqrt2 = 0.7071067811865476;
    for (std::size_t i = 0; i < x.size(); ++i) {
        out[i] = static_cast<float>(0.5 * std::erfc(-static_cast<double>(x[i]) * kInvSqrt2));
    }
}

// Scalar twin of the SIMD kernel: identical A&S float polynomial, no vectors.
void norm_cdf_batch_scalar_poly(std::span<const float> x, std::span<float> out) {
    for (std::size_t i = 0; i < x.size(); ++i) {
        const float z = x[i] * 0.7071067811865476f;
        const float az = std::fabs(z);
        const float t = 1.0f / (1.0f + 0.3275911f * az);
        float poly = 1.061405429f;
        poly = -1.453152027f + t * poly;
        poly = 1.421413741f + t * poly;
        poly = -0.284496736f + t * poly;
        poly = 0.254829592f + t * poly;
        poly *= t;
        const float erf_abs = 1.0f - poly * std::exp(-az * az);
        const float erf = std::copysign(erf_abs, z);
        out[i] = 0.5f * (1.0f + erf);
    }
}

const char* active_target() {
    // Best SIMD target supported at runtime (lowest set bit = most capable in
    // Highway's ordering).
    const int64_t supported = hwy::SupportedTargets();
    const int64_t best = supported & (-supported);
    return hwy::TargetName(best);
}

}  // namespace risksim::simd
#endif  // HWY_ONCE
