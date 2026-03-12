#pragma once
#include "../math/vec.hpp"
#include <array>
#include <functional>
#include <tuple>
#include <unordered_map>
#include <string>
namespace LX_core {
// CRTP技术：基类模板使用派生类自身作为模板参数。
// - 本质上在基类中扩展子类的方法，但是用编译期多态实现。
// - 场景：多个类的差别仅仅是不同的成员差别时
template <typename Derived> struct VertexBase {
  // operator==
  bool operator==(const Derived &other) const {
    return as_tuple() == other.as_tuple();
  }

  bool operator!=(const Derived &other) const { return !(*this == other); }

  struct Hash {
    std::size_t operator()(const Derived &v) const {
      std::size_t h = 0;
      auto tup = v.as_tuple();
      Derived::apply_hash(
          h, tup, std::make_index_sequence<std::tuple_size_v<decltype(tup)>>{});
      return h;
    }
  };

private:
  template <typename Tuple, std::size_t... Is>
  static void apply_hash(std::size_t &h, const Tuple &t,
                         std::index_sequence<Is...>) {
    (..., (h ^= std::tuple_element_t<Is, Tuple>::Hash{}(std::get<Is>(t)) +
                0x9e3779b9 + (h << 6) + (h >> 2))); // 这个步骤要求 Tuple中每个元素的累都有Hash类型
  }
};
struct VertexPos : VertexBase<VertexPos> {
  Vec3f pos;
  VertexPos(Vec3f pos) : pos(pos) {}
  auto as_tuple() const { return std::tie(pos); }
};

struct VertexPosColor : VertexBase<VertexPosColor> {
  Vec3f pos;
  Vec3f color;
  VertexPosColor(Vec3f pos, Vec3f color) : pos(pos), color(color) {}
  auto as_tuple() const { return std::tie(pos, color); }
};

struct VertexPosNormalUV : VertexBase<VertexPosNormalUV> {
  Vec3f pos;
  Vec3f normal;
  Vec2f uv;
  VertexPosNormalUV(Vec3f pos, Vec3f normal, Vec2f uv)
      : pos(pos), normal(normal), uv(uv) {}

  auto as_tuple() const { return std::tie(pos, normal, uv); }
};


// 渲染流程使用的顶点格式
struct alignas(16) VertexBlinnPhong : VertexBase<VertexBlinnPhong> {
  Vec3f pos;
  f32 padding1;
  Vec3f normal;
  f32 padding2;
  Vec2f uv;
  f32 padding3[2];
  Vec3f color;
  f32 padding4;
  Vec4f tangent;
  Vec4i boneIDs;
  Vec4f boneWeights;
};
} // namespace LX_core

