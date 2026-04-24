#pragma once
#include "core/platform/types.hpp" // i32, f32, f64
#include "core/utils/hash.hpp"
#include <bit>
#include <cassert>
#include <cmath>
#include <functional>
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

  // 取反
  Derived operator-() const {
    Derived r;
    for (int i = 0; i < N; i++)
      r[i] = -(*this)[i]; // 对每个元素取负
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
    usize operator()(const Derived &v) const {
      usize h = 0;
      for (int i = 0; i < N; ++i) {
        usize hi;
        if constexpr (std::is_floating_point<T>::value) {
          if constexpr (std::is_same_v<T, f32>) {
            hi = std::hash<u32>()(std::bit_cast<u32>(v[i]));
          } else if constexpr (std::is_same_v<T, f64>) {
            hi = std::hash<u64>()(std::bit_cast<u64>(v[i]));
          } else {
            static_assert(std::is_same_v<T, f32> || std::is_same_v<T, f64>,
                          "unsupported floating point type");
          }
        } else {
          hi = std::hash<T>()(v[i]);
        }
        hash_combine(h, hi);
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
    struct {
      T x, y;
    };
  };
  Vec2() = default;
  Vec2(T x, T y) : data{x, y} {}
};

template <typename T> struct Vec3 : VecBase<Vec3<T>, T, 3> {
  union {
    T data[3] = {};
    struct {
      T x, y, z;
    };
  };
  Vec3() = default;
  Vec3(T x, T y, T z) : data{x, y, z} {}

  // 叉乘只在 Vec3 中定义（右手坐标系）
  template <typename U = T>
  typename std::enable_if<std::is_floating_point<U>::value, Vec3>::type
  cross(const Vec3 &o) const {
    return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);
  }
};

template <typename T> struct Vec4 : VecBase<Vec4<T>, T, 4> {
  union {
    T data[4] = {};
    struct {
      T x, y, z, w;
    };
  };
  Vec4() = default;
  Vec4(T x, T y, T z, T w) : data{x, y, z, w} {}
  /**
   * @brief 将 Vec4 转换为 Vec3f
   */
  Vec3<f32> toVec3() const { return Vec3<f32>(x / w, y / w, z / w); }
};

template <typename S, typename Derived, typename T, int N>
std::enable_if_t<std::is_arithmetic_v<S> && std::is_convertible_v<S, T>, Derived>
operator*(S s, const VecBase<Derived, T, N> &v) {
  return static_cast<const Derived &>(v) * static_cast<T>(s);
}

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
