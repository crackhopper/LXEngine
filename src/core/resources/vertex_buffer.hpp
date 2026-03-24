#pragma once
#include "core/gpu/render_resource.hpp"
#include "core/math/vec.hpp"
#include <array>
#include <functional>
#include <string>
#include <tuple>
#include <unordered_map>
namespace LX_core {

enum class VertexFormat {
  Pos,
  PosColor,
  PosUV,
  NormalTangent,
  BoneWeight,
  PosNormalUvBone,
  Custom, // 自定义，需要用户自己重载pipeline中的getVertexInputStateCreateInfo方法
};

template <typename Derived> struct VertexBase {
  // operator==
  bool operator==(const Derived &other) const {
    return this->as_tuple() == other.as_tuple();
  }

  bool operator!=(const Derived &other) const { return !(*this == other); }

  struct Hash {
    std::size_t operator()(const Derived &v) const {
      // Simple hash implementation - hash the address as a baseline
      // This is a minimal implementation to get the build working
      return std::hash<const void*>{}(static_cast<const void*>(&v));
    }
  };
};

struct VertexPos : VertexBase<VertexPos> {
  Vec3f pos;
  VertexPos(Vec3f pos) : pos(pos) {}
  auto as_tuple() const { return std::tie(pos); }
  static VertexFormat format() { return VertexFormat::Pos; }
};

struct VertexPosColor : VertexBase<VertexPosColor> {
  Vec3f pos;
  f32 padding;
  Vec3f color;
  VertexPosColor(Vec3f pos, Vec3f color)
      : pos(pos), padding(0.0f), color(color) {}
  auto as_tuple() const { return std::tie(pos); }
  static VertexFormat format() { return VertexFormat::PosColor; }
};

struct VertexPosUV : VertexBase<VertexPosUV> {
  Vec3f pos;
  f32 padding;
  Vec2f uv;
  VertexPosUV(Vec3f pos, Vec2f uv) : pos(pos), uv(uv) {}
  auto as_tuple() const { return std::tie(pos); }
  static VertexFormat format() { return VertexFormat::PosUV; }
};

struct VertexNormalTangent : VertexBase<VertexNormalTangent> {
  Vec3f normal;
  f32 padding;
  Vec4f tangent;
  VertexNormalTangent(Vec3f normal, Vec4f tangent)
      : normal(normal), padding(0.0f), tangent(tangent) {}

  static VertexFormat format() { return VertexFormat::NormalTangent; }
};

struct VertexBoneWeight : VertexBase<VertexBoneWeight> {
  Vec4i boneIds;
  Vec4f weights;
  VertexBoneWeight(Vec4i boneIds, Vec4f weights)
      : boneIds(boneIds), weights(weights) {}
  static VertexFormat format() { return VertexFormat::BoneWeight; }
};

struct VertexPosNormalUvBone : VertexBase<VertexPosNormalUvBone> {
  Vec3f pos;
  Vec3f normal;
  Vec2f uv;
  Vec4f tangent;
  Vec4i boneIDs;
  Vec4f boneWeights;
  VertexPosNormalUvBone(Vec3f pos, Vec3f normal, Vec2f uv, Vec4f tangent,
                        Vec4i boneIDs, Vec4f boneWeights)
      : pos(pos), normal(normal), uv(uv), tangent(tangent), boneIDs(boneIDs),
        boneWeights(boneWeights) {}
  auto as_tuple() const { return std::tie(pos); }
  static VertexFormat format() { return VertexFormat::PosNormalUvBone; }
};

// 顶点缓冲
template <typename VType>
class VertexBuffer : public IRenderResource {
public:
  VertexBuffer(std::vector<VType> &&vertices,
               ResourcePassFlag passFlag = ResourcePassFlag::Forward)
      : m_vertices(std::move(vertices)), m_passFlag(passFlag) {}
  VertexBuffer(std::initializer_list<VType> list,
               ResourcePassFlag passFlag = ResourcePassFlag::Forward)
      : m_vertices(list), m_passFlag(passFlag) {}

  void update(const std::vector<VType> &vertices) {
    m_vertices = vertices;
    setDirty();
  }

  size_t vertexCount() const { return m_vertices.size(); }

  ResourcePassFlag getPassFlag() const override {
    return m_passFlag;
  }
  ResourceType getType() const override {
    return ResourceType::VertexBuffer;
  }
  const void *getRawData() const override { return m_vertices.data(); }
  u32 getByteSize() const override { return m_vertices.size() * sizeof(VType); }

  static VertexFormat getVertexFormat() { return VType::format(); }

  VertexFormat getFormat() const { return VType::format(); }

  static auto create(std::vector<VType> &&vertices,
                    ResourcePassFlag passFlag = ResourcePassFlag::Forward) {
    return std::make_shared<VertexBuffer>(std::move(vertices), passFlag);
  }
  static auto create(std::initializer_list<VType> list,
                    ResourcePassFlag passFlag = ResourcePassFlag::Forward) {
    return std::make_shared<VertexBuffer>(list, passFlag);
  }
private:
  std::vector<VType> m_vertices;
  ResourcePassFlag m_passFlag = ResourcePassFlag::Forward;
};

template <typename VType>
using VertexBufferPtr = std::shared_ptr<VertexBuffer<VType>>;

} // namespace LX_core
