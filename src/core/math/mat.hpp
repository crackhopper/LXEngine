#pragma once
#include "../platform/types.hpp"
#include "vec.hpp"
#include <cassert>
#include <cmath>

namespace LX_core {

template <typename T> using Vec3T = Vec3<T>;
template <typename T> using Vec4T = Vec4<T>;

template <typename T> struct Mat4T {
  // column-major: m[column][row]
  T m[4][4];

  Mat4T() {
    for (int c = 0; c < 4; c++)
      for (int r = 0; r < 4; r++)
        m[c][r] = (c == r) ? T(1) : T(0);
  }

  Mat4T(const T vals[16]) {
    for (int i = 0; i < 16; i++)
      m[i / 4][i % 4] = vals[i];
  }

  static Mat4T identity() { return Mat4T(); }

  // 访问元素
  T &operator()(int row, int col) { return m[col][row]; }
  const T &operator()(int row, int col) const { return m[col][row]; }
  // -------------------------
  // Matrix multiply
  // -------------------------
  Mat4T operator*(const Mat4T &o) const {
    Mat4T r;

    for (int c = 0; c < 4; c++) {
      for (int rrow = 0; rrow < 4; rrow++) {
        r.m[c][rrow] = m[0][rrow] * o.m[c][0] + m[1][rrow] * o.m[c][1] +
                       m[2][rrow] * o.m[c][2] + m[3][rrow] * o.m[c][3];
      }
    }

    return r;
  }

  // -------------------------
  // Matrix * vector
  // -------------------------
  template <typename U> Vec4T<U> operator*(const Vec4T<U> &v) const {
    Vec4T<U> r;

    r.x = m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w;
    r.y = m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w;
    r.z = m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w;
    r.w = m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w;

    return r;
  }

  // -------------------------
  // Translation
  // -------------------------
  static Mat4T translate(const Vec3T<T> &t) {
    Mat4T r;

    r.m[3][0] = t.x;
    r.m[3][1] = t.y;
    r.m[3][2] = t.z;

    return r;
  }

  // -------------------------
  // Scale
  // -------------------------
  static Mat4T scale(const Vec3T<T> &s) {
    Mat4T r;

    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;

    return r;
  }

  // -------------------------
  // Rotation
  // -------------------------
  static Mat4T rotate(T angleRad, const Vec3T<T> &axis) {
    Vec3T<T> a = axis.normalized();

    T c = std::cos(angleRad);
    T s = std::sin(angleRad);
    T t = 1 - c;

    Mat4T r;

    r.m[0][0] = t * a.x * a.x + c;
    r.m[0][1] = t * a.x * a.y + s * a.z;
    r.m[0][2] = t * a.x * a.z - s * a.y;
    r.m[0][3] = 0;

    r.m[1][0] = t * a.x * a.y - s * a.z;
    r.m[1][1] = t * a.y * a.y + c;
    r.m[1][2] = t * a.y * a.z + s * a.x;
    r.m[1][3] = 0;

    r.m[2][0] = t * a.x * a.z + s * a.y;
    r.m[2][1] = t * a.y * a.z - s * a.x;
    r.m[2][2] = t * a.z * a.z + c;
    r.m[2][3] = 0;

    r.m[3][0] = 0;
    r.m[3][1] = 0;
    r.m[3][2] = 0;
    r.m[3][3] = 1;

    return r;
  }

  // -------------------------
  // Perspective (Vulkan style)
  // -------------------------
  static Mat4T perspective(T fovYRad, T aspect, T zNear, T zFar) {
    Mat4T r{};

    T f = T(1) / std::tan(fovYRad / 2);

    r.m[0][0] = f / aspect;
    r.m[1][1] = f;

    r.m[2][2] = zFar / (zNear - zFar);
    r.m[2][3] = -1;

    r.m[3][2] = (zFar * zNear) / (zNear - zFar);

    return r;
  }

  // -------------------------
  // Orthographic
  // -------------------------
  static Mat4T orthographic(T l, T rgt, T b, T t, T n, T f) {
    Mat4T r{};

    r.m[0][0] = 2 / (rgt - l);
    r.m[1][1] = 2 / (t - b);
    r.m[2][2] = -2 / (f - n);

    r.m[3][0] = -(rgt + l) / (rgt - l);
    r.m[3][1] = -(t + b) / (t - b);
    r.m[3][2] = -(f + n) / (f - n);

    r.m[3][3] = 1;

    return r;
  }

  // -------------------------
  // LookAt
  // -------------------------
  static Mat4T lookAt(const Vec3T<T> &eye, const Vec3T<T> &target,
                      const Vec3T<T> &up) {
    Vec3T<T> z = (eye - target).normalized();
    Vec3T<T> x = up.cross(z).normalized();
    Vec3T<T> y = z.cross(x);

    Mat4T r;

    r.m[0][0] = x.x;
    r.m[0][1] = x.y;
    r.m[0][2] = x.z;
    r.m[1][0] = y.x;
    r.m[1][1] = y.y;
    r.m[1][2] = y.z;
    r.m[2][0] = z.x;
    r.m[2][1] = z.y;
    r.m[2][2] = z.z;

    r.m[3][0] = -x.dot(eye);
    r.m[3][1] = -y.dot(eye);
    r.m[3][2] = -z.dot(eye);

    return r;
  }
};

using Mat4f = Mat4T<f32>;
using Mat4d = Mat4T<f64>;

} // namespace LX_core