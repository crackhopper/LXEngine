#include "core/math/quat.hpp"
#include "core/math/transform.hpp"
#include "core/math/vec.hpp"
#include "core/platform/types.hpp"

#include <bit>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>

using namespace LX_core;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

constexpr f32 kEps = 1e-5f;

bool approx(f32 a, f32 b, f32 eps = kEps) {
  return std::fabs(a - b) <= eps;
}

bool approxVec3(const Vec3f &a, const Vec3f &b, f32 eps = kEps) {
  return approx(a.x, b.x, eps) && approx(a.y, b.y, eps) &&
         approx(a.z, b.z, eps);
}

bool approxQuat(const Quatf &a, const Quatf &b, f32 eps = kEps) {
  return approx(a.w, b.w, eps) && approxVec3(a.v, b.v, eps);
}

bool approxQuatOrientation(const Quatf &a, const Quatf &b, f32 eps = kEps) {
  return std::fabs(a.normalized().dot(b.normalized())) >= 1.0f - eps;
}

Quatf hamiltonProduct(const Quatf &lhs, const Quatf &rhs) {
  return Quatf(lhs.w * rhs.w - lhs.v.dot(rhs.v),
               lhs.v.cross(rhs.v) + rhs.v * lhs.w + lhs.v * rhs.w);
}

usize expectedHash(const Vec3f &v) {
  usize h = 0;
  for (int i = 0; i < 3; ++i) {
    hash_combine(h, std::hash<u32>()(std::bit_cast<u32>(v[i])));
  }
  return h;
}

usize expectedHash(const Vec2d &v) {
  usize h = 0;
  for (int i = 0; i < 2; ++i) {
    hash_combine(h, std::hash<u64>()(std::bit_cast<u64>(v[i])));
  }
  return h;
}

std::string captureStderr(const std::function<void()> &fn) {
  std::ostringstream capture;
  auto *original = std::cerr.rdbuf(capture.rdbuf());
  fn();
  std::cerr.rdbuf(original);
  return capture.str();
}

void testQuaternionMultiplyInplaceUsesOriginalScalar() {
  const f32 pi = std::acos(-1.0f);
  const Quatf qx = Quatf::fromAxisAngle(Vec3f{1.0f, 0.0f, 0.0f}, pi * 0.5f);
  const Quatf qy = Quatf::fromAxisAngle(Vec3f{0.0f, 1.0f, 0.0f}, pi * 0.5f);

  Quatf actual = qx;
  actual.multiply_inplace(qy);
  const Quatf expected = hamiltonProduct(qx, qy);

  EXPECT(approxQuat(actual, expected),
         "multiply_inplace must match Hamilton product");
}

void testQuaternionLeftMultiplyUsesOriginalScalar() {
  const f32 pi = std::acos(-1.0f);
  const Quatf qx = Quatf::fromAxisAngle(Vec3f{1.0f, 0.0f, 0.0f}, pi * 0.5f);
  const Quatf qy = Quatf::fromAxisAngle(Vec3f{0.0f, 1.0f, 0.0f}, pi * 0.5f);

  Quatf actual = qx;
  actual.left_multiply_inplace(qy);
  const Quatf expected = hamiltonProduct(qy, qx);

  EXPECT(approxQuat(actual, expected),
         "left_multiply_inplace must match left Hamilton product");
}

void testQuaternionConjugateProducesIdentityWhenMultiplied() {
  const f32 pi = std::acos(-1.0f);
  const Quatf q =
      Quatf::fromAxisAngle(Vec3f{0.0f, 0.0f, 1.0f}, pi * (2.0f / 3.0f));

  const Quatf actual = q * q.conjugate();
  const Quatf expected{};

  EXPECT(approxQuat(actual, expected),
         "q * conjugate(q) must produce identity for unit quaternions");
}

void testQuaternionRotateVector() {
  const f32 pi = std::acos(-1.0f);
  const Quatf q =
      Quatf::fromAxisAngle(Vec3f{0.0f, 0.0f, 1.0f}, pi * 0.5f).normalized();
  const Vec3f rotated = q.rotate(Vec3f{1.0f, 0.0f, 0.0f});

  EXPECT(approxVec3(rotated, Vec3f{0.0f, 1.0f, 0.0f}),
         "90-degree Z rotation must map +X to +Y");
}

void testTransformIdentityToMat4() {
  const Transform t = Transform::identity();
  const Mat4f m = t.toMat4();
  EXPECT(approx(m(0, 0), 1.0f) && approx(m(1, 1), 1.0f) &&
             approx(m(2, 2), 1.0f) && approx(m(3, 3), 1.0f),
         "identity transform must produce identity matrix");
  EXPECT(approx(m(0, 3), 0.0f) && approx(m(1, 3), 0.0f) &&
             approx(m(2, 3), 0.0f),
         "identity transform must have zero translation");
}

void testTransformFromMat4TranslationOnly() {
  const Transform t =
      Transform::fromMat4(Mat4f::translate(Vec3f{1.0f, 2.0f, 3.0f}));
  EXPECT(approxVec3(t.translation, Vec3f{1.0f, 2.0f, 3.0f}),
         "translation-only matrix must preserve translation");
  EXPECT(approxQuatOrientation(t.rotation, Quatf{}),
         "translation-only matrix must preserve identity rotation");
  EXPECT(approxVec3(t.scale, Vec3f{1.0f, 1.0f, 1.0f}),
         "translation-only matrix must preserve unit scale");
}

void testTransformStrictTrsRoundTrip() {
  Transform original;
  original.translation = Vec3f{1.0f, -2.0f, 0.5f};
  original.rotation =
      Quatf::fromAxisAngle(Vec3f{0.0f, 1.0f, 0.0f}, 0.75f).normalized();
  original.scale = Vec3f{2.0f, 3.0f, 4.0f};

  const Transform actual = Transform::fromMat4(original.toMat4());
  EXPECT(approxVec3(actual.translation, original.translation),
         "strict TRS round-trip must preserve translation");
  EXPECT(approxQuatOrientation(actual.rotation, original.rotation),
         "strict TRS round-trip must preserve rotation orientation");
  EXPECT(approxVec3(actual.scale, original.scale),
         "strict TRS round-trip must preserve scale");
}

void testTransformWarnsOnShearInput() {
  Mat4f sheared = Mat4f::identity();
  sheared(0, 1) = 0.5f;
  const std::string warning = captureStderr([&]() {
    (void)Transform::fromMat4(sheared);
  });
  EXPECT(warning.find("[WARN] Transform::fromMat4") != std::string::npos,
         "sheared matrix must emit warning");
}

void testTransformWarnsOnNegativeScaleRepair() {
  const Mat4f reflected = Mat4f::scale(Vec3f{1.0f, -2.0f, 3.0f});
  Transform actual;
  const std::string warning = captureStderr([&]() {
    actual = Transform::fromMat4(reflected);
  });
  EXPECT(warning.find("[WARN] Transform::fromMat4") != std::string::npos,
         "negative-scale repair must emit warning");
  EXPECT(actual.scale.x < 0.0f, "negative-scale repair keeps X negative");
  EXPECT(actual.scale.y > 0.0f && actual.scale.z > 0.0f,
         "negative-scale repair flips Y/Z positive");
}

void testVecFloatHashUsesBitCastReference() {
  const Vec3f v{1.25f, -0.0f, std::numeric_limits<f32>::infinity()};
  const usize actual = Vec3f::Hash{}(v);
  const usize expected = expectedHash(v);

  EXPECT(actual == expected, "Vec3f::Hash must use bit_cast-based float hash");
  EXPECT(actual == Vec3f::Hash{}(v), "Vec3f::Hash must be deterministic");
}

void testVecDoubleHashUsesBitCastReference() {
  const Vec2d v{1.0 / 3.0, -0.0};
  const usize actual = Vec2d::Hash{}(v);
  const usize expected = expectedHash(v);

  EXPECT(actual == expected, "Vec2d::Hash must use bit_cast-based double hash");
  EXPECT(actual == Vec2d::Hash{}(v), "Vec2d::Hash must be deterministic");
}

} // namespace

int main() {
  testQuaternionMultiplyInplaceUsesOriginalScalar();
  testQuaternionLeftMultiplyUsesOriginalScalar();
  testQuaternionConjugateProducesIdentityWhenMultiplied();
  testQuaternionRotateVector();
  testTransformIdentityToMat4();
  testTransformFromMat4TranslationOnly();
  testTransformStrictTrsRoundTrip();
  testTransformWarnsOnShearInput();
  testTransformWarnsOnNegativeScaleRepair();
  testVecFloatHashUsesBitCastReference();
  testVecDoubleHashUsesBitCastReference();

  if (failures == 0) {
    std::cout << "[PASS] All math tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed.\n";
  }
  return failures == 0 ? 0 : 1;
}
