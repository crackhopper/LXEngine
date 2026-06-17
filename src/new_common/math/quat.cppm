module;
#include <cassert>
#include <cmath>

export module LX_New_Common.Math:Quat;

import LX_New_Common.Platform;
import :Vec;
import :Mat;

export namespace LX_New_Common {

template <typename T> struct QuatT {
  T w = T(1);
  Vec3T<T> v;

  QuatT() = default;
  QuatT(T wVal, T x, T y, T z) : w(wVal), v(x, y, z) {}
  QuatT(T wVal, const Vec3T<T> &vec) : w(wVal), v(vec) {}

  // ---------- 四元数乘法 ----------
  QuatT &multiply_inplace(const QuatT &o) {
    auto oldW = w;
    auto oldV = v;
    w = oldW * o.w - oldV.dot(o.v);
    v = oldV.cross(o.v) + o.v * oldW + oldV * o.w;
    return *this;
  }

  QuatT &left_multiply_inplace(const QuatT &o) {
    auto oldW = w;
    auto oldV = v;
    w = oldW * o.w - oldV.dot(o.v);
    v = o.v.cross(oldV) + o.v * oldW + oldV * o.w;
    return *this;
  }

  QuatT operator*(const QuatT &o) const {
    return QuatT(*this).multiply_inplace(o);
  }

  QuatT &operator*=(const QuatT &o) {
    multiply_inplace(o);
    return *this;
  }

  // ---------- 标量乘法（用于 slerp 等） ----------
  QuatT operator*(T s) const { return QuatT(w * s, v * s); }

  QuatT operator+(const QuatT &o) const { return QuatT(w + o.w, v + o.v); }

  // ---------- 长度 / 归一化 ----------
  T length() const { return std::sqrt(w * w + v.length2()); }

  bool is_normalized(T eps = T(1e-6)) const {
    T len2 = w * w + v.x * v.x + v.y * v.y + v.z * v.z;
    return std::abs(len2 - T(1)) < eps;
  }

  QuatT normalized() const {
    T len = length();
    return len > T(0) ? QuatT(w / len, v / len) : QuatT();
  }

  QuatT &normalize() {
    T len = length();
    if (len > T(0)) {
      w /= len;
      v /= len;
    }
    return *this;
  }

  // ---------- 共轭 ----------
  QuatT conjugate() const { return QuatT(w, -v); }

  // ---------- 旋转向量 ----------
  Vec3T<T> rotate(const Vec3T<T> &vec) const {
    const auto &q = *this;
    Vec3T<T> t = T(2) * q.v.cross(vec);
    return vec + q.v.cross(t) + q.w * t;
  }

  // ---------- 四元数到矩阵 ----------
  Mat4T<T> toMat4() const {
    T _2xx = T(2) * v.x * v.x;
    T _2yy = T(2) * v.y * v.y;
    T _2zz = T(2) * v.z * v.z;
    T _2xy = T(2) * v.x * v.y;
    T _2xz = T(2) * v.x * v.z;
    T _2yz = T(2) * v.y * v.z;
    T _2wx = T(2) * w * v.x;
    T _2wy = T(2) * w * v.y;
    T _2wz = T(2) * w * v.z;

    Mat4T<T> m;
    m(0, 0) = T(1) - _2yy - _2zz;
    m(0, 1) = _2xy - _2wz;
    m(0, 2) = _2xz + _2wy;
    m(0, 3) = T(0);
    m(1, 0) = _2xy + _2wz;
    m(1, 1) = T(1) - _2xx - _2zz;
    m(1, 2) = _2yz - _2wx;
    m(1, 3) = T(0);
    m(2, 0) = _2xz - _2wy;
    m(2, 1) = _2yz + _2wx;
    m(2, 2) = T(1) - _2xx - _2yy;
    m(2, 3) = T(0);
    m(3, 0) = T(0);
    m(3, 1) = T(0);
    m(3, 2) = T(0);
    m(3, 3) = T(1);
    return m;
  }

  // ---------- 从轴-角创建四元数 ----------
  static QuatT fromAxisAngle(const Vec3T<T> &axis, T angleRad) {
    Vec3T<T> a = axis.normalized();
    T half = angleRad / T(2);
    T s = std::sin(half);
    return QuatT(std::cos(half), a * s);
  }

  // ---------- 点乘 ----------
  T dot(const QuatT &o) const { return w * o.w + v.dot(o.v); }

  // ---------- 球面线性插值 ----------
  QuatT slerp(const QuatT &q1, T t) const {
    const T DOT_THRESHOLD = T(0.9995);
    T cosTheta = this->dot(q1);

    QuatT q1Copy = q1;

    if (cosTheta < T(0)) {
      q1Copy = QuatT(-q1.w, -q1.v);
      cosTheta = -cosTheta;
    }

    if (cosTheta > DOT_THRESHOLD) {
      QuatT result = (*this) * (T(1) - t) + q1Copy * t;
      return result.normalized();
    } else {
      T theta = std::acos(cosTheta);
      T sinTheta = std::sqrt(T(1) - cosTheta * cosTheta);

      T a = std::sin((T(1) - t) * theta) / sinTheta;
      T b = std::sin(t * theta) / sinTheta;

      QuatT result = (*this) * a + q1Copy * b;
      return result;
    }
  }
};

using Quatf = QuatT<f32>;
using Quatd = QuatT<f64>;

} // namespace LX_New_Common
