#pragma once
#include "../platform/types.hpp" // i32, f32, f64
#include <cassert>
#include <cmath>
#include <type_traits>

namespace LX_core {

// ===================== Vec CRTP 基类 =====================
template <typename Derived, typename T, int N> struct VecBase {
  T &operator[](int i) {
    assert(i >= 0 && i < N);
    return static_cast<Derived &>(*this).data[i];
  }
  const T &operator[](int i) const {
    assert(i >= 0 && i < N);
    return static_cast<const Derived &>(*this).data[i];
  }

  // ---------- 基础算术运算 ----------
  Derived operator+(const Derived &o) const {
    Derived r;
    for (int i = 0; i < N; i++)
      r[i] = (*this)[i] + o[i];
    return r;
  }

  Derived operator-(const Derived &o) const {
    Derived r;
    for (int i = 0; i < N; i++)
      r[i] = (*this)[i] - o[i];
    return r;
  }

  Derived operator*(T s) const {
    Derived r;
    for (int i = 0; i < N; i++)
      r[i] = (*this)[i] * s;
    return r;
  }

  Derived operator/(T s) const {
    assert(s != 0);
    Derived r;
    for (int i = 0; i < N; i++)
      r[i] = (*this)[i] / s;
    return r;
  }

  Derived &operator+=(const Derived &o) {
    for (int i = 0; i < N; i++)
      (*this)[i] += o[i];
    return static_cast<Derived &>(*this);
  }

  Derived &operator-=(const Derived &o) {
    for (int i = 0; i < N; i++)
      (*this)[i] -= o[i];
    return static_cast<Derived &>(*this);
  }

  Derived &operator*=(T s) {
    for (int i = 0; i < N; i++)
      (*this)[i] *= s;
    return static_cast<Derived &>(*this);
  }

  // ---------- 安全 operator== ----------
  bool operator==(const Derived &o) const {
    if constexpr (std::is_floating_point<T>::value) {
      constexpr T EPS = static_cast<T>(1e-6);
      for (int i = 0; i < N; ++i)
        if (std::abs((*this)[i] - o[i]) > EPS)
          return false;
    } else {
      for (int i = 0; i < N; ++i)
        if ((*this)[i] != o[i])
          return false;
    }
    return true;
  }

  bool operator!=(const Derived &o) const { return !(*this == o); }

  // ---------- hash 支持；方便顶点去重 ----------
  struct Hash {
    std::size_t operator()(const Derived &v) const {
      std::size_t h = 0;
      for (int i = 0; i < N; ++i) {
        std::size_t hi;
        if constexpr (std::is_floating_point<T>::value) {
          hi = std::hash<long long>()(
              *reinterpret_cast<const long long *>(&v[i]));
        } else {
          hi = std::hash<T>()(v[i]);
        }
        h ^= hi + 0x9e3779b9 + (h << 6) + (h >> 2);
      }
      return h;
    }
  };
  // ---------- 浮点相关运算 ----------
  template <typename U = T>
  typename std::enable_if<std::is_floating_point<U>::value, U>::type
  length() const {
    U sum = 0;
    for (int i = 0; i < N; i++)
      sum += (*this)[i] * (*this)[i];
    return std::sqrt(sum);
  }

  template <typename U = T>
  typename std::enable_if<std::is_floating_point<U>::value, U>::type
  length2() const {
    U sum = 0;
    for (int i = 0; i < N; i++)
      sum += (*this)[i] * (*this)[i];
    return sum;
  }

  template <typename U = T>
  typename std::enable_if<std::is_floating_point<U>::value, Derived>::type
  normalized() const {
    U len = length();
    return len > 0 ? (*this) / len : Derived();
  }

  template <typename U = T>
  typename std::enable_if<std::is_floating_point<U>::value, U>::type
  dot(const Derived &o) const {
    U sum = 0;
    for (int i = 0; i < N; i++)
      sum += (*this)[i] * o[i];
    return sum;
  }
};

// ===================== Vec2 / Vec3 / Vec4 =====================
template <typename T> struct Vec2 : VecBase<Vec2<T>, T, 2> {
  union {
    T data[2] = {};
    struct { T x, y; };
  };
  Vec2() = default;
  Vec2(T x, T y) : data{x, y} {}
};

template <typename T> struct Vec3 : VecBase<Vec3<T>, T, 3> {
  union {
    T data[3] = {};
    struct { T x, y, z; };
  };
  Vec3() = default;
  Vec3(T x, T y, T z) : data{x, y, z} {}

  // 叉乘只在 Vec3 中定义（右手坐标系）
  template <typename U = T>
  typename std::enable_if<std::is_floating_point<U>::value, Vec3>::type
  cross(const Vec3 &o) const {
    return Vec3(y * o.z - z * o.y,
                z * o.x - x * o.z,
                x * o.y - y * o.x);
  }
};

template <typename T> struct Vec4 : VecBase<Vec4<T>, T, 4> {
  union {
    T data[4] = {};
    struct { T x, y, z, w; };
  };
  Vec4() = default;
  Vec4(T x, T y, T z, T w) : data{x, y, z, w} {}
};

// ===================== 类型别名 =====================
using Vec2i = Vec2<i32>;
using Vec3i = Vec3<i32>;
using Vec4i = Vec4<i32>;

using Vec2f = Vec2<f32>;
using Vec3f = Vec3<f32>;
using Vec4f = Vec4<f32>;

using Vec2d = Vec2<f64>;
using Vec3d = Vec3<f64>;
using Vec4d = Vec4<f64>;

} // namespace LX_core
