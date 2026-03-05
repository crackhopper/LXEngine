#pragma once
#include "../platform/types.hpp"
#include "vec.hpp" // Vec2/3/4
#include <cassert>
#include <cmath>

namespace LX_core {
template <typename T> using Vec3T = Vec3<T>;
// ===================== Mat4 模板 =====================
template <typename T> struct Mat4T {
  T m[4][4] = {};

  Mat4T() {
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        m[i][j] = (i == j) ? T(1) : T(0);
  }

  // ---------- 构造函数 ----------
  Mat4T(const T vals[16]) {
    for (int i = 0; i < 16; i++)
      m[i / 4][i % 4] = vals[i];
  }

  static Mat4T identity() { return Mat4T(); }

  // ---------- 矩阵乘法 ----------
  Mat4T operator*(const Mat4T &o) const {
    Mat4T r;
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++) {
        r.m[i][j] = 0;
        for (int k = 0; k < 4; k++)
          r.m[i][j] += m[i][k] * o.m[k][j];
      }
    return r;
  }

  // ---------- 矩阵乘向量 ----------
  template <typename U> Vec4T<U> operator*(const Vec4T<U> &v) const {
    Vec4T<U> r;
    r.x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w;
    r.y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w;
    r.z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w;
    r.w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w;
    return r;
  }

  // ---------- 平移矩阵 ----------
  static Mat4T translate(const Vec3T<T> &t) {
    Mat4T r;
    r.m[0][3] = t.x;
    r.m[1][3] = t.y;
    r.m[2][3] = t.z;
    return r;
  }

  // ---------- 缩放矩阵 ----------
  static Mat4T scale(const Vec3T<T> &s) {
    Mat4T r;
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;
    return r;
  }

  // ---------- 绕任意轴旋转 ----------
  static Mat4T rotate(T angleRad, const Vec3T<T> &axis) {
    static_assert(std::is_floating_point<T>::value,
                  "Rotation requires floating point type");

    Vec3T<T> a = axis.normalized();
    T c = std::cos(angleRad);
    T s = std::sin(angleRad);
    T t = 1 - c;

    Mat4T r;
    r.m[0][0] = t * a.x * a.x + c;
    r.m[0][1] = t * a.x * a.y - s * a.z;
    r.m[0][2] = t * a.x * a.z + s * a.y;
    r.m[0][3] = 0;

    r.m[1][0] = t * a.x * a.y + s * a.z;
    r.m[1][1] = t * a.y * a.y + c;
    r.m[1][2] = t * a.y * a.z - s * a.x;
    r.m[1][3] = 0;

    r.m[2][0] = t * a.x * a.z - s * a.y;
    r.m[2][1] = t * a.y * a.z + s * a.x;
    r.m[2][2] = t * a.z * a.z + c;
    r.m[2][3] = 0;

    r.m[3][0] = r.m[3][1] = r.m[3][2] = 0;
    r.m[3][3] = 1;
    return r;
  }

  // ---------- 透视投影 ----------
  static Mat4T perspective(T fovYRad, T aspect, T zNear, T zFar) {
    static_assert(std::is_floating_point<T>::value,
                  "Perspective requires floating point type");
    Mat4T r;
    T f = T(1) / std::tan(fovYRad / 2);
    r.m[0][0] = f / aspect;
    r.m[1][1] = f;
    r.m[2][2] = (zFar + zNear) / (zNear - zFar);
    r.m[2][3] = (2 * zFar * zNear) / (zNear - zFar);
    r.m[3][2] = -1;
    r.m[3][3] = 0;
    return r;
  }

  // ---------- LookAt 矩阵 ----------
  static Mat4T lookAt(const Vec3T<T> &eye, const Vec3T<T> &target,
                      const Vec3T<T> &up) {
    static_assert(std::is_floating_point<T>::value,
                  "LookAt requires floating point type");

    // -z 轴
    Vec3T<T> _z = (target - eye).normalized();
    // x轴 =  y cross z = -z cross y
    Vec3T<T> x = _z.cross(up).normalized();
    // 正交化： y = z cross x = x cross -z
    Vec3T<T> y = x.cross(_z);

    // 这个矩阵，
    // - 前三列向量，构成旋转。就是我们算的轴向量。
    //   - 技巧：用单位轴向量，带入被变换的向量，进行测试。
    //   - 考虑在新矩阵中的向量 (1,0,0) ，那么坐标显然是x轴。（容易测明白这样是正确的）
    // - 最后一列，是平移。考虑原点，带入进去后，应该是eye的位置。
    //   - eye是相机在世界中的坐标。我们矩阵的约束是把 (0,0,0,1) 带入，得到的应该是原点在当前矩阵的坐标。
    //   - 因此，是对 -eye 方向的逆变换（E * 原eye向量 = - 原点向量）。于是每个分量计算就是下面了。
    Mat4T r;
    r.m[0][0] = x.x;
    r.m[0][1] = x.y;
    r.m[0][2] = x.z;
    r.m[0][3] = -x.dot(eye);
    r.m[1][0] = y.x;
    r.m[1][1] = y.y;
    r.m[1][2] = y.z;
    r.m[1][3] = -y.dot(eye);
    r.m[2][0] = -_z.x;
    r.m[2][1] = -_z.y;
    r.m[2][2] = -_z.z;
    r.m[2][3] = _z.dot(eye);
    r.m[3][0] = 0;
    r.m[3][1] = 0;
    r.m[3][2] = 0;
    r.m[3][3] = 1;
    return r;
  }
};

// ===================== 类型别名 =====================
using Mat4f = Mat4T<f32>;
using Mat4d = Mat4T<f64>;

} // namespace LX_core