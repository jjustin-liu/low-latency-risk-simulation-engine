// Minimal dense linear algebra for correlation matrices and small networks.
// Row-major, double precision. Deliberately small and dependency-free -- the
// matrices here are correlation matrices (n_assets) and exposure matrices
// (n_banks), i.e. hundreds of rows, not the millions where you'd reach for BLAS.
#ifndef RISKSIM_LINALG_HPP
#define RISKSIM_LINALG_HPP

#include <cstddef>
#include <span>
#include <vector>

namespace risksim {

class Matrix {
public:
    Matrix() = default;
    Matrix(std::size_t rows, std::size_t cols, double init = 0.0)
        : rows_(rows), cols_(cols), data_(rows * cols, init) {}

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t cols() const noexcept { return cols_; }

    [[nodiscard]] double& operator()(std::size_t i, std::size_t j) noexcept {
        return data_[i * cols_ + j];
    }
    [[nodiscard]] double operator()(std::size_t i, std::size_t j) const noexcept {
        return data_[i * cols_ + j];
    }

    [[nodiscard]] std::span<double> row(std::size_t i) noexcept {
        return {data_.data() + i * cols_, cols_};
    }
    [[nodiscard]] std::span<const double> row(std::size_t i) const noexcept {
        return {data_.data() + i * cols_, cols_};
    }

    [[nodiscard]] std::span<const double> data() const noexcept { return data_; }
    [[nodiscard]] std::span<double> data() noexcept { return data_; }

    [[nodiscard]] static Matrix identity(std::size_t n) {
        Matrix m(n, n);
        for (std::size_t i = 0; i < n; ++i) m(i, i) = 1.0;
        return m;
    }

private:
    std::size_t rows_ = 0;
    std::size_t cols_ = 0;
    std::vector<double> data_;
};

// Cholesky factorization of a symmetric positive-definite matrix A = L * L^T.
// Returns the lower-triangular L. Throws std::domain_error if A is not SPD
// (a non-positive pivot), which is exactly the check you want when validating a
// correlation matrix before copula sampling.
[[nodiscard]] Matrix cholesky(const Matrix& a);

// y = A * x
[[nodiscard]] std::vector<double> matvec(const Matrix& a, std::span<const double> x);

// y = A^T * x
[[nodiscard]] std::vector<double> matvec_transpose(const Matrix& a, std::span<const double> x);

// Solve the dense linear system A x = b by Gaussian elimination with partial
// pivoting. Throws std::domain_error if A is singular. Used by the
// Eisenberg-Noe fictitious-default solver (an independent cross-check of the
// Picard fixed-point solver).
[[nodiscard]] std::vector<double> solve(Matrix a, std::vector<double> b);

}  // namespace risksim

#endif  // RISKSIM_LINALG_HPP
