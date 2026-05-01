#include "transform.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace LX_core {

namespace {

using Mat3 = std::array<std::array<f32, 3>, 3>;

constexpr f32 kPolarTolerance = 1e-5f;
constexpr f32 kDecompositionTolerance = 1e-4f;
constexpr int kMaxPolarIterations = 24;

Mat3 identity3() {
  return {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
}

Mat3 diagonal3(f32 x, f32 y, f32 z) {
  return {{{x, 0.0f, 0.0f}, {0.0f, y, 0.0f}, {0.0f, 0.0f, z}}};
}

Mat3 fromMat4Upper3x3(const Mat4f &m) {
  Mat3 result{};
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      result[row][col] = m(row, col);
    }
  }
  return result;
}

Mat3 transpose3(const Mat3 &m) {
  Mat3 result{};
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      result[row][col] = m[col][row];
    }
  }
  return result;
}

Mat3 add3(const Mat3 &a, const Mat3 &b) {
  Mat3 result{};
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      result[row][col] = a[row][col] + b[row][col];
    }
  }
  return result;
}

Mat3 scale3(const Mat3 &m, f32 scalar) {
  Mat3 result{};
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      result[row][col] = m[row][col] * scalar;
    }
  }
  return result;
}

Mat3 multiply3(const Mat3 &a, const Mat3 &b) {
  Mat3 result{};
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      f32 sum = 0.0f;
      for (int k = 0; k < 3; ++k) {
        sum += a[row][k] * b[k][col];
      }
      result[row][col] = sum;
    }
  }
  return result;
}

Vec3f getColumn(const Mat3 &m, int col) {
  return Vec3f{m[0][col], m[1][col], m[2][col]};
}

void setColumn(Mat3 &m, int col, const Vec3f &v) {
  m[0][col] = v.x;
  m[1][col] = v.y;
  m[2][col] = v.z;
}

f32 determinant3(const Mat3 &m) {
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
         m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
         m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

bool invert3(const Mat3 &m, Mat3 &outInverse) {
  const f32 det = determinant3(m);
  if (std::fabs(det) <= std::numeric_limits<f32>::epsilon()) {
    return false;
  }

  const f32 invDet = 1.0f / det;
  outInverse[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * invDet;
  outInverse[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * invDet;
  outInverse[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invDet;
  outInverse[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * invDet;
  outInverse[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invDet;
  outInverse[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * invDet;
  outInverse[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * invDet;
  outInverse[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * invDet;
  outInverse[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * invDet;
  return true;
}

f32 maxAbsDiff3(const Mat3 &a, const Mat3 &b) {
  f32 maxDiff = 0.0f;
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      maxDiff = std::max(maxDiff, std::fabs(a[row][col] - b[row][col]));
    }
  }
  return maxDiff;
}

Mat3 orthonormalizeColumns(const Mat3 &m) {
  Vec3f x = getColumn(m, 0);
  if (x.length2() <= std::numeric_limits<f32>::epsilon()) {
    x = Vec3f{1.0f, 0.0f, 0.0f};
  } else {
    x = x.normalized();
  }

  Vec3f y = getColumn(m, 1) - x * getColumn(m, 1).dot(x);
  if (y.length2() <= std::numeric_limits<f32>::epsilon()) {
    y = Vec3f{0.0f, 1.0f, 0.0f};
    y = (y - x * y.dot(x)).normalized();
  } else {
    y = y.normalized();
  }

  Vec3f z = x.cross(y);
  if (z.length2() <= std::numeric_limits<f32>::epsilon()) {
    z = Vec3f{0.0f, 0.0f, 1.0f};
  } else {
    z = z.normalized();
  }

  y = z.cross(x).normalized();

  Mat3 result = identity3();
  setColumn(result, 0, x);
  setColumn(result, 1, y);
  setColumn(result, 2, z);
  return result;
}

Mat3 properRotationFromMatrix(const Mat3 &a, bool &usedFallback,
                              bool &warnApproximation) {
  Mat3 current = a;
  usedFallback = false;

  for (int iteration = 0; iteration < kMaxPolarIterations; ++iteration) {
    Mat3 inverse{};
    if (!invert3(current, inverse)) {
      usedFallback = true;
      warnApproximation = true;
      return orthonormalizeColumns(a);
    }

    const Mat3 next =
        scale3(add3(current, transpose3(inverse)), 0.5f);
    if (maxAbsDiff3(next, current) <= kPolarTolerance) {
      current = next;
      break;
    }
    current = next;
  }

  return current;
}

Quatf quatFromRotationMatrix(const Mat3 &m) {
  const f32 trace = m[0][0] + m[1][1] + m[2][2];
  if (trace > 0.0f) {
    const f32 s = std::sqrt(trace + 1.0f) * 2.0f;
    return Quatf(0.25f * s, (m[2][1] - m[1][2]) / s,
                 (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s)
        .normalized();
  }

  if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
    const f32 s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
    return Quatf((m[2][1] - m[1][2]) / s, 0.25f * s,
                 (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s)
        .normalized();
  }

  if (m[1][1] > m[2][2]) {
    const f32 s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
    return Quatf((m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s,
                 0.25f * s, (m[1][2] + m[2][1]) / s)
        .normalized();
  }

  const f32 s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
  return Quatf((m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s,
               (m[1][2] + m[2][1]) / s, 0.25f * s)
      .normalized();
}

void applyInvolution(Mat3 &rotation, Mat3 &symmetric, const Mat3 &involution) {
  rotation = multiply3(rotation, involution);
  symmetric = multiply3(involution, symmetric);
}

void warnApproximateDecomposition(const char *reason) {
  std::cerr << "[WARN] Transform::fromMat4 approximated non-TRS matrix: "
            << reason << '\n';
}

} // namespace

Mat4f Transform::toMat4() const {
  return Mat4f::translate(translation) * rotation.normalized().toMat4() *
         Mat4f::scale(scale);
}

Transform Transform::fromMat4(const Mat4f &m) {
  Transform result;
  result.translation = Vec3f{m(0, 3), m(1, 3), m(2, 3)};

  bool warnApproximation = false;
  bool usedFallback = false;

  Mat3 rotation = properRotationFromMatrix(fromMat4Upper3x3(m), usedFallback,
                                           warnApproximation);
  Mat3 symmetric = multiply3(transpose3(rotation), fromMat4Upper3x3(m));

  if (determinant3(rotation) < 0.0f) {
    warnApproximation = true;
    applyInvolution(rotation, symmetric, diagonal3(-1.0f, 1.0f, 1.0f));
  }

  const f32 maxDiag =
      std::max({std::fabs(symmetric[0][0]), std::fabs(symmetric[1][1]),
                std::fabs(symmetric[2][2]), 1.0f});
  if (std::fabs(symmetric[0][1]) > kDecompositionTolerance * maxDiag ||
      std::fabs(symmetric[0][2]) > kDecompositionTolerance * maxDiag ||
      std::fabs(symmetric[1][0]) > kDecompositionTolerance * maxDiag ||
      std::fabs(symmetric[1][2]) > kDecompositionTolerance * maxDiag ||
      std::fabs(symmetric[2][0]) > kDecompositionTolerance * maxDiag ||
      std::fabs(symmetric[2][1]) > kDecompositionTolerance * maxDiag) {
    warnApproximation = true;
  }

  if (std::fabs(symmetric[1][1]) <= kDecompositionTolerance &&
      std::fabs(symmetric[2][2]) <= kDecompositionTolerance &&
      std::fabs(symmetric[0][0]) <= kDecompositionTolerance) {
    warnApproximation = true;
  }

  if (symmetric[1][1] < 0.0f && symmetric[2][2] < 0.0f) {
    warnApproximation = true;
    applyInvolution(rotation, symmetric, diagonal3(1.0f, -1.0f, -1.0f));
  } else if (symmetric[1][1] < 0.0f) {
    warnApproximation = true;
    applyInvolution(rotation, symmetric, diagonal3(-1.0f, -1.0f, 1.0f));
  } else if (symmetric[2][2] < 0.0f) {
    warnApproximation = true;
    applyInvolution(rotation, symmetric, diagonal3(-1.0f, 1.0f, -1.0f));
  } else if (symmetric[0][0] < 0.0f && symmetric[1][1] < 0.0f) {
    warnApproximation = true;
    applyInvolution(rotation, symmetric, diagonal3(-1.0f, -1.0f, 1.0f));
  } else if (symmetric[0][0] < 0.0f && symmetric[2][2] < 0.0f) {
    warnApproximation = true;
    applyInvolution(rotation, symmetric, diagonal3(-1.0f, 1.0f, -1.0f));
  }

  result.rotation = quatFromRotationMatrix(rotation);
  result.scale =
      Vec3f{symmetric[0][0], symmetric[1][1], symmetric[2][2]};

  if (usedFallback) {
    warnApproximateDecomposition("singular basis fallback");
  } else if (warnApproximation) {
    warnApproximateDecomposition("shear or negative-scale repair");
  }

  return result.normalized();
}

Transform Transform::operator*(const Transform &other) const {
  return Transform::fromMat4(toMat4() * other.toMat4());
}

} // namespace LX_core
