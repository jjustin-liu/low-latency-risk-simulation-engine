#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <stdexcept>

#include "risksim/linalg.hpp"

using namespace risksim;

TEST(Linalg, CholeskyKnownFactor) {
    Matrix a(2, 2);
    a(0, 0) = 4.0;
    a(0, 1) = 2.0;
    a(1, 0) = 2.0;
    a(1, 1) = 3.0;
    const Matrix l = cholesky(a);
    EXPECT_NEAR(l(0, 0), 2.0, 1e-12);
    EXPECT_NEAR(l(1, 0), 1.0, 1e-12);
    EXPECT_NEAR(l(1, 1), std::sqrt(2.0), 1e-12);
    EXPECT_NEAR(l(0, 1), 0.0, 1e-12);  // lower-triangular
}

TEST(Linalg, CholeskyReconstructsMatrix) {
    Matrix a = Matrix::identity(3);
    a(0, 1) = a(1, 0) = 0.5;
    a(0, 2) = a(2, 0) = 0.3;
    a(1, 2) = a(2, 1) = 0.4;
    const Matrix l = cholesky(a);
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < 3; ++k) s += l(i, k) * l(j, k);
            EXPECT_NEAR(s, a(i, j), 1e-12);
        }
    }
}

TEST(Linalg, CholeskyRejectsNonPositiveDefinite) {
    Matrix a(2, 2);
    a(0, 0) = 1.0;
    a(0, 1) = 2.0;
    a(1, 0) = 2.0;
    a(1, 1) = 1.0;  // eigenvalues 3, -1 -> not SPD
    EXPECT_THROW((void) cholesky(a), std::domain_error);
}

TEST(Linalg, MatVec) {
    Matrix a(2, 3);
    a(0, 0) = 1;
    a(0, 1) = 2;
    a(0, 2) = 3;
    a(1, 0) = 4;
    a(1, 1) = 5;
    a(1, 2) = 6;
    const std::array<double, 3> x{1.0, 1.0, 1.0};
    const auto y = matvec(a, x);
    ASSERT_EQ(y.size(), 2u);
    EXPECT_DOUBLE_EQ(y[0], 6.0);
    EXPECT_DOUBLE_EQ(y[1], 15.0);
    const std::array<double, 2> z{1.0, 1.0};
    const auto yt = matvec_transpose(a, z);
    ASSERT_EQ(yt.size(), 3u);
    EXPECT_DOUBLE_EQ(yt[0], 5.0);
    EXPECT_DOUBLE_EQ(yt[1], 7.0);
    EXPECT_DOUBLE_EQ(yt[2], 9.0);
}
