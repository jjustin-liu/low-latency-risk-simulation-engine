#include "risksim/linalg.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace risksim {

Matrix cholesky(const Matrix& a) {
    const std::size_t n = a.rows();
    if (a.cols() != n) throw std::domain_error("cholesky: matrix must be square");
    Matrix l(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double sum = a(i, j);
            for (std::size_t k = 0; k < j; ++k) sum -= l(i, k) * l(j, k);
            if (i == j) {
                if (sum <= 0.0) {
                    throw std::domain_error("cholesky: matrix is not positive definite");
                }
                l(i, j) = std::sqrt(sum);
            } else {
                l(i, j) = sum / l(j, j);
            }
        }
    }
    return l;
}

std::vector<double> matvec(const Matrix& a, std::span<const double> x) {
    if (x.size() != a.cols()) throw std::invalid_argument("matvec: dimension mismatch");
    std::vector<double> y(a.rows(), 0.0);
    for (std::size_t i = 0; i < a.rows(); ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < a.cols(); ++j) sum += a(i, j) * x[j];
        y[i] = sum;
    }
    return y;
}

std::vector<double> matvec_transpose(const Matrix& a, std::span<const double> x) {
    if (x.size() != a.rows()) throw std::invalid_argument("matvec_transpose: dimension mismatch");
    std::vector<double> y(a.cols(), 0.0);
    for (std::size_t i = 0; i < a.rows(); ++i) {
        const double xi = x[i];
        for (std::size_t j = 0; j < a.cols(); ++j) y[j] += a(i, j) * xi;
    }
    return y;
}

std::vector<double> solve(Matrix a, std::vector<double> b) {
    const std::size_t n = a.rows();
    if (a.cols() != n || b.size() != n) throw std::invalid_argument("solve: dimension mismatch");
    for (std::size_t col = 0; col < n; ++col) {
        // Partial pivot: find the largest-magnitude entry in this column.
        std::size_t pivot = col;
        double best = std::fabs(a(col, col));
        for (std::size_t r = col + 1; r < n; ++r) {
            const double v = std::fabs(a(r, col));
            if (v > best) {
                best = v;
                pivot = r;
            }
        }
        if (best < 1e-300) throw std::domain_error("solve: singular matrix");
        if (pivot != col) {
            for (std::size_t c = 0; c < n; ++c) std::swap(a(col, c), a(pivot, c));
            std::swap(b[col], b[pivot]);
        }
        // Eliminate below.
        const double diag = a(col, col);
        for (std::size_t r = col + 1; r < n; ++r) {
            const double factor = a(r, col) / diag;
            if (factor == 0.0) continue;
            for (std::size_t c = col; c < n; ++c) a(r, c) -= factor * a(col, c);
            b[r] -= factor * b[col];
        }
    }
    // Back-substitution.
    std::vector<double> x(n, 0.0);
    for (std::size_t ii = n; ii-- > 0;) {
        double sum = b[ii];
        for (std::size_t c = ii + 1; c < n; ++c) sum -= a(ii, c) * x[c];
        x[ii] = sum / a(ii, ii);
    }
    return x;
}

}  // namespace risksim
