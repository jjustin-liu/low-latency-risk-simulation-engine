#include "risksim/risk.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "risksim/distributions.hpp"

namespace risksim {

// ---- Welford ---------------------------------------------------------------

void Welford::add(double x) noexcept {
    const std::size_t n1 = n_;
    n_ += 1;
    const double delta = x - mean_;
    const double delta_n = delta / static_cast<double>(n_);
    const double delta_n2 = delta_n * delta_n;
    const double term1 = delta * delta_n * static_cast<double>(n1);
    mean_ += delta_n;
    m4_ += term1 * delta_n2 * static_cast<double>(n_ * n_ - 3 * n_ + 3) +
           6.0 * delta_n2 * m2_ - 4.0 * delta_n * m3_;
    m3_ += term1 * delta_n * static_cast<double>(n_ - 2) - 3.0 * delta_n * m2_;
    m2_ += term1;
}

double Welford::variance() const noexcept {
    return n_ > 1 ? m2_ / static_cast<double>(n_ - 1) : 0.0;
}

double Welford::stddev() const noexcept { return std::sqrt(variance()); }

double Welford::standard_error() const noexcept {
    return n_ > 1 ? stddev() / std::sqrt(static_cast<double>(n_)) : 0.0;
}

double Welford::skewness() const noexcept {
    if (n_ < 2 || m2_ <= 0.0) return 0.0;
    return std::sqrt(static_cast<double>(n_)) * m3_ / std::pow(m2_, 1.5);
}

double Welford::excess_kurtosis() const noexcept {
    if (n_ < 2 || m2_ <= 0.0) return 0.0;
    return static_cast<double>(n_) * m4_ / (m2_ * m2_) - 3.0;
}

void Welford::merge(const Welford& b) noexcept {
    if (b.n_ == 0) return;
    if (n_ == 0) {
        *this = b;
        return;
    }
    const double na = static_cast<double>(n_);
    const double nb = static_cast<double>(b.n_);
    const double nt = na + nb;
    const double delta = b.mean_ - mean_;
    const double delta2 = delta * delta;
    const double delta3 = delta2 * delta;
    const double delta4 = delta2 * delta2;

    const double mean = mean_ + delta * nb / nt;
    const double m2 = m2_ + b.m2_ + delta2 * na * nb / nt;
    const double m3 = m3_ + b.m3_ + delta3 * na * nb * (na - nb) / (nt * nt) +
                      3.0 * delta * (na * b.m2_ - nb * m2_) / nt;
    const double m4 = m4_ + b.m4_ +
                      delta4 * na * nb * (na * na - na * nb + nb * nb) / (nt * nt * nt) +
                      6.0 * delta2 * (na * na * b.m2_ + nb * nb * m2_) / (nt * nt) +
                      4.0 * delta * (na * b.m3_ - nb * m3_) / nt;

    n_ += b.n_;
    mean_ = mean;
    m2_ = m2;
    m3_ = m3;
    m4_ = m4;
}

// ---- TailEstimator ---------------------------------------------------------

TailEstimator::TailEstimator(std::size_t expected_samples, double alpha) : alpha_(alpha) {
    if (!(alpha > 0.0 && alpha < 1.0)) {
        throw std::invalid_argument("alpha must be in (0,1)");
    }
    const double kf = std::ceil((1.0 - alpha) * static_cast<double>(expected_samples));
    k_ = std::max<std::size_t>(1, static_cast<std::size_t>(kf));
    heap_.reserve(k_);
}

void TailEstimator::add(double loss) {
    if (heap_.size() < k_) {
        heap_.push_back(loss);
        std::push_heap(heap_.begin(), heap_.end(), std::greater<>{});
        return;
    }
    // Heap full: the root is the smallest retained loss. Replace it only if the
    // new loss is larger, keeping the k largest.
    if (loss > heap_.front()) {
        std::pop_heap(heap_.begin(), heap_.end(), std::greater<>{});
        heap_.back() = loss;
        std::push_heap(heap_.begin(), heap_.end(), std::greater<>{});
    }
}

double TailEstimator::value_at_risk() const {
    if (heap_.empty()) throw std::runtime_error("no samples");
    return heap_.front();  // k-th largest loss = min of the retained tail
}

double TailEstimator::expected_shortfall() const {
    if (heap_.empty()) throw std::runtime_error("no samples");
    double sum = 0.0;
    for (double v : heap_) sum += v;
    return sum / static_cast<double>(heap_.size());
}

// ---- Batch reference + closed forms ----------------------------------------

VarEs empirical_var_es(std::span<const double> losses, double alpha) {
    if (losses.empty()) throw std::runtime_error("no samples");
    std::vector<double> buf(losses.begin(), losses.end());
    const std::size_t n = buf.size();
    // k = size of the worst tail; VaR is the k-th largest loss.
    const std::size_t k =
        std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil((1.0 - alpha) * n)));
    // Partition so the largest k elements sit at the end.
    const auto cut = buf.begin() + static_cast<std::ptrdiff_t>(n - k);
    std::nth_element(buf.begin(), cut, buf.end());
    const double var = *cut;  // smallest of the top-k
    double sum = 0.0;
    for (auto it = cut; it != buf.end(); ++it) sum += *it;
    return {var, sum / static_cast<double>(k)};
}

VarEs gaussian_var_es(double mu, double sigma, double alpha) noexcept {
    const double z = norm_inv_cdf(alpha);
    const double var = mu + sigma * z;
    const double es = mu + sigma * norm_pdf(z) / (1.0 - alpha);
    return {var, es};
}

}  // namespace risksim
