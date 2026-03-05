#pragma once
#include "mat.hpp"
#include "vec.hpp"
#include <cassert>
#include <cmath>

#ifdef LX_CORE_MATH_DEBUG
#define QUAT_ASSERT(q) (q).assert_normalized()
#else
#define QUAT_ASSERT(q) ((void)0)
#endif

// 四元数简单介绍（类似复数）
// 乘法规则(Hamilton)
// i^2 = j^2 = k^2 = ijk = -1
// ij = k, jk = i, ki = j
// 乘法规则的目的是，定义之后有逆元存在。因此可以构成类似数域（但不存在交换律）的除环。
// - 可做幂级数展开、指数、对数等运算
// - 可以用来表示旋转，沿着v向量旋转w/2。
//   q = w + vi + vj + vk = (cos(theta/2) + sin(theta/2) * v)
//
// 对于一个3d点来说。 p = (0, (x,y,z) ) 定义为四元数
//
// 旋转公式用四元数运算表示为：(下面的是四元数乘法)
//   p' = q * p * q^{-1}
// 基于这个定义的，叫做右乘旋转 （因为它对应组合旋转公式 q1 = q0 * x） 。
// 此时，q0到q1的相对旋转为 q1 * q0^{-1} 。这也是从 q0 到 q1 的最小旋转。
//
// 四元数本身的定义，带有旋转轴和角度。在4维空间中，它的局部有角度和轴的绕动，其实能更好的表达
// 什么是一个旋转附近的旋转？
//
// 随后我们定义测地线距离，因为都在S^3球面上，因此用测地线弧长就是自然的结论了。
//    d(q0, q1) = 2 * acos(|q0 \cdot q1|)
// 定义旋转的测地线距离是很重要的，这方便我们在两个旋转中间进行均匀的插值。
//
// 方法是，沿着距离最短的路线（S^3上的测地线），就可以找到我们的插值点。而为了保证
// 速率的稳定，我们可以要求在测地线上的运动是线性的。
//
// 当然一个显然的思考就是，测地线上的距离定义，对每个维度的权重都一样。如果权重不一样呢？实际上
// 这个是调整内积的定义。有 <p0,p1>_W = p0 W p1
// 。此时我们测地线距离也发生了变化， 变成了： d(q0, q1) = 2 * acos(|<q0,q1>_W /
// (||q0||_W||q1||_W) )
// 所以，我们在S^3上得到的就是另一个曲线（是另一个测地线距离度量下的测地线）。
// 某种意义来说，这个曲线也是我们在特定场合下需要的一个旋转变化。
//
//
// S^n上测地线公式： p(\theta) = cos(\theta) p_0 + sin(\theta) * u_t
// 其中， u_t 是 p0 沿着测地线指向 p1 的切向量。 （可以借助二维来理解这个）
// - 具体u_t的计算，看下面。
//
// 切空间：切点+切向量组构成的空间。测地线的切向量只是切向量组空间中的一个元素。
// - 可以借助S^2来理解这个概念。对球来说，切空间就是切平面。
//
// 目标向量的投影：目标向量维度要高于切空间。因此可以去除掉垂直切空间的分量，从而得到投影。
// - 这个也可以借助S^2来理解。
// - 对这个投影进行归一化，得到的就是：沿着测地线的切向量 u_t 了。
// 因此可以得到公式： u_t = normalize(p_1-<p_0, p_1>p_0)
// - 如果距离是甲醛的，显然这里为 normalize_W(p_1-<p_0, p_1>_W p_0)
//
// 注意：我们有两个距离概念，
// - 一个是原始空间的距离（通过内积定义）。
// - 一个是测地线上的距离（另一个定义，用来两点测地线距离）。
//
// 下面就考虑普通欧式距离。我们可以对 S^n上的测地线公式进行整理
// p(s\theta) = cos(s\theta) p_0 +
// sin(s\theta)(p_1-cos(\theta)p_0)/\sqrt{(1+cos^2(\theta)-2*cos(\theta))}
//            = cos(s\theta) p_0 + sin(s\theta)(p_1-cos(\theta)p_0)/sin(\theta)
//            = 1/sin(\theta)[p_0
//            (cos(s\theta)sin(\theta)-sin(s\theta)cos(\theta)) + p_1
//            (sin(s\theta))]
// 带入和差化积公式 = 1/sin(\theta)(p_0 sin((1-s)\theta) + p_1 sin(s\theta))
// 出去外边的归一化项不看，实际上测地线 p_0, p_1 在 sin
// 乘子下的线性插值。这个算法就是SLERP
//
// 此外四元数还有一些重要计算：
// - q 旋转 v ：即 q * v * q^-1 。
// - 乘法计算公式
// - 矩阵转化
// 具体细节在下面函数前讲解

namespace LX_core {
// ===================== Quaternion 模板 =====================
template <typename T> struct QuatT {
  // q = w + vi + vj + vk = (cos(theta/2) + sin(theta/2) * v)
  T w = T(1); // 逆时针旋转的角度（弧度）的一半
  Vec3T<T> v; // (x,y,z) 旋转轴的方向

  QuatT() = default;
  QuatT(T w, T x, T y, T z) : w(w), v(x, y, z) {}
  QuatT(T w, const Vec3T<T> &vec) : w(w), v(vec) {}

  // ---------- 基础算术 ----------
  // 四元数乘法 (w_1, v_1(x_1, y_1, z_1)) 和 (w_2, v_2(x_2, y_2, z_2))
  // 向量表达:
  // - w = w_1w_2-v_1\cdot v_2
  // - v = s_1v_2+ s_2v_1 +v1\cross v2
  // - 我们这里简化了实现，所以用了这个公式。
  // 优化算法：SIMD常用展开：
  // q_1 q_2 = (w_1+ix_1+jy_1+kz_1)q_2
  //         = (w_1, x_1, y_1, z_1) 每个位置分别乘以 (q_2, iq_2, jq_2, kq_2)
  //         在求和 【有点像内积】
  // 此时就可以用SIMD指令来加速了。因为相当于4个SIMD乘法+3个SIMD加法
  // 典型的解法：
  // r=w1*q2; r+=shuffle1(q2); r+=shuffle2(q2); r+=shuffle3(q2);
  // 具体来说shuffle1的解法：
  // -------  0. 预先准备
  // __m128 signmask_i = _mm_set_ps(-0.0f, 0.0f, 0.0f, -0.0f); // 预先存好
  // -------  1. 计算 iq2
  // t = _mm_shuffle_ps(q2, q2, _MM_SHUFFLE(3,2,0,1));   // 按照iq2的排列shffule
  // t = _mm_xor_ps(t, signmask_i); // 实际是xor操作，利用浮点数格式。得到iq2
  // -------  2. 计算 vec1 = x1 * iq2
  // vec1 = _mm_mul_ps(_mm_set1_ps(x1), t);
  // -------  3. 最后更新 r+=shuffle1(q2)
  // r = _mm_add_ps(r, t);
  // 如果用FMA加速。步骤2+3可以节省一个指令
  // r = _mm_fmadd_ps(_mm_set1_ps(x1), t, r);
  QuatT &multiply_inplace(const QuatT &o) {
    auto oldW = w;
    auto oldV = v;
    w = oldW * o.w - oldV.dot(o.v);
    v = oldV.cross(o.v) + o.v * w + oldV * o.w;
    return *this;
  }
  QuatT &left_multiply_inplace(const QuatT &o) {
    auto oldW = w;
    auto oldV = v;
    w = oldW * o.w - oldV.dot(o.v);
    v = o.v.cross(oldV) + o.v * w + oldV * o.w;
    return *this;
  }

  QuatT operator*(const QuatT &o) const {
    return QuatT(*this).multiply_inplace(o);
  }

  QuatT &operator*=(const QuatT &o) {
    multiply_inplace(o);
    return *this;
  }

  // ---------- 长度 / 归一化 ----------
  T length() const { return std::sqrt(w * w + v.length2()); }

  bool is_normalized(T eps = 1e-6) const {
    T len2 = w * w + v.x * v.x + v.y * v.y + v.z * v.z;
    return std::abs(len2 - 1) < eps;
  }

  void assert_normalized(T eps = 1e-6) const {
    assert(is_normalized(eps) && "Quat not normalized!");
  }

  QuatT normalized() const {
    T len = length();
    return len > 0 ? QuatT(w / len, v / len) : QuatT();
  }

  QuatT &normalize() {
    T len = length();
    if (len > 0) {
      w /= len;
      v /= len;
    }
    return *this;
  }

  // ---------- 共轭 ----------
  QuatT conjugate() const { return QuatT(w, -v); }

  // ---------- 四元数旋转向量 ----------
  // 四元数 q ，对向量v旋转：
  // v' = q * v * q^-1
  // q^-1 = (w, -v)
  // 推导后常用的公式为:
  // v' = v + 2 q_v \times (q_v \times v + q_w v)
  // 注意，更快的方式是用SIMD来直接按照坐标计算更新。这里略。
  Vec3T<T> rotate(const Vec3T<T> &v) const {
    QUAT_ASSERT(*this);
    const auto &q = *this;
    Vec3T<T> t = 2 * q.v.cross(v);
    Vec3T<T> v_prime = v + q.v.cross(t) + q.w * t;
    return v_prime;
  }

  // ---------- 四元数到矩阵 ----------
  // 这个是固定公式：
  // 1-2yy-2xx   2xy - 2wz  2xz + 2wy
  // 2xy + 2wz  1-2xx-2zz  2yz - 2wx
  // 2xz - 2wy  2yz + 2wx  1-2xx-2yy
  Mat4T<T> toMat4() const {
    QUAT_ASSERT(*this);
    T _2xx = 2 * v.x * v.x, _2yy = 2 * v.y * v.y, _2zz = 2 * v.z * v.z;
    T _2xy = 2 * v.x * v.y, _2xz = 2 * v.x * v.z, _2yz = 2 * v.y * v.z;
    T _2wx = 2 * w * v.x, _2wy = 2 * w * v.y, _2wz = 2 * w * v.z;

    Mat4T<T> m;
    m.m[0][0] = 1 - _2yy - _2zz;
    m.m[0][1] = _2xy - _2wz;
    m.m[0][2] = _2xz + _2wy;
    m.m[0][3] = 0;
    m.m[1][0] = _2xy + _2wz;
    m.m[1][1] = 1 - _2xx - _2zz;
    m.m[1][2] = _2yz - _2wx;
    m.m[1][3] = 0;
    m.m[2][0] = _2xz - _2wy;
    m.m[2][1] = _2yz + _2wx;
    m.m[2][2] = 1 - _2xx - _2yy;
    m.m[2][3] = 0;
    m.m[3][0] = 0;
    m.m[3][1] = 0;
    m.m[3][2] = 0;
    m.m[3][3] = 1;
    return m;
  }

  // ---------- 从轴-角创建四元数 ----------
  static QuatT fromAxisAngle(const Vec3T<T> &axis, T angleRad) {
    Vec3T<T> a = axis.normalized();
    T half = angleRad / 2;
    T s = std::sin(half);
    return QuatT(std::cos(half), a * s);
  }

  // ---------- 点乘 ----------
  T dot(const QuatT &o) const { return w * o.w + v.dot(o.v); }

  // ---------- 球面线性插值 ----------

  QuatT slerp(const QuatT &q1, T t) const {
    QUAT_ASSERT(*this);
    QUAT_ASSERT(q1);
    // 假设 *this = q0
    const T DOT_THRESHOLD = 0.9995; // 点积接近1时用线性插值
    T cosTheta = this->dot(q1);

    QuatT q1Copy = q1;

    // 确保沿最短路径插值
    // 注意，四元数里的\theta是转角的1/2，所以转角是 2\theta。因此我们如果转角是大弧，
    // 我们要取另外的路径，因此要反转 q，让内积为正。（即取补角）
    if (cosTheta < 0.0f) {
      q1Copy = QuatT(-q1.w, -q1.v);
      cosTheta = -cosTheta;
    }

    if (cosTheta > DOT_THRESHOLD) {
      // 线性插值，避免除零
      QuatT result = (*this) * (1 - t) + q1Copy * t;
      return result.normalized();
    } else {
      // SLERP
      T theta = std::acos(cosTheta);
      T sinTheta = std::sqrt(1 - cosTheta * cosTheta);

      T a = std::sin((1 - t) * theta) / sinTheta;
      T b = std::sin(t * theta) / sinTheta;

      QuatT result = (*this) * a + q1Copy * b;
      return result; // 已经单位化，因为 q0,q1 是单位四元数
    }
  }
};

// ===================== 类型别名 =====================
using Quatf = QuatT<f32>;
using Quatd = QuatT<f64>;

} // namespace LX_core