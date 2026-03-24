#pragma once
#include "core/math/mat.hpp"
#include "core/scene/components/base.hpp"
#include "core/scene/components/material.hpp"
#include "core/scene/components/mesh.hpp"
#include "core/scene/components/skeleton.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace LX_core {

// push constant - forward declaration and definition
struct ObjectPC : public IRenderResource {
  using Ptr = std::shared_ptr<ObjectPC>;

  // 默认提供最大 128 字节的存储空间，确保兼容所有 Pipeline
  alignas(16) uint8_t data[128] = {0};
  uint32_t activeSize = sizeof(PC_Base); // 默认只同步 Model 矩阵

  ObjectPC(ResourcePassFlag passFlag = ResourcePassFlag::Forward)
      : passFlag(passFlag) {
    // 初始化时设置默认 model 矩阵
    PC_Base base;
    memcpy(data, &base, sizeof(base));
  }

  // 提供一个模板方法，让材质（Material）能够根据需要更新数据
  template <typename T> void update(const T &params) {
    static_assert(sizeof(T) <= 128, "PushConstant block too large!");
    memcpy(data, &params, sizeof(T));
    activeSize = sizeof(T);
  }

  virtual ResourcePassFlag getPassFlag() const override { return passFlag; }
  virtual ResourceType getType() const override {
    return ResourceType::PushConstant;
  }
  virtual const void *getRawData() const override { return data; }
  virtual u32 getByteSize() const override { return activeSize; }

private:
  ResourcePassFlag passFlag;
};

using ObjectPCPtr = ObjectPC::Ptr;

class IRenderable {
public:
  virtual ~IRenderable() = default;
  virtual IRenderResourcePtr getVertexBuffer() const = 0;
  virtual IRenderResourcePtr getIndexBuffer() const = 0;
  virtual std::vector<IRenderResourcePtr> getDescriptorResources() const = 0;
  virtual ResourcePassFlag getPassMask() const = 0;
  virtual VertexFormat getVertexFormat() const = 0;

  virtual ShaderPtr getShaderInfo() const = 0;
  virtual ObjectPCPtr getObjectInfo() const { return nullptr; }
};

using IRenderablePtr = std::shared_ptr<IRenderable>;

// 渲染子网格，先仅支持1个网格。
template <typename VType> struct RenderableSubMesh : public IRenderable {
public:
  MeshPtr<VType> mesh;
  MaterialPtr material;
  std::optional<SkeletonPtr> skeleton;
  ObjectPCPtr objectPC;

  RenderableSubMesh(MeshPtr<VType> mesh, MaterialPtr material)
      : mesh(mesh), material(material) {
    objectPC = std::make_shared<ObjectPC>(material->getPassFlag());
  }

  virtual IRenderResourcePtr getVertexBuffer() const {
    return std::dynamic_pointer_cast<IRenderResource>(mesh->vertexBuffer);
  }
  virtual IRenderResourcePtr getIndexBuffer() const {
    return std::dynamic_pointer_cast<IRenderResource>(mesh->indexBuffer);
  }
  virtual std::vector<IRenderResourcePtr> getDescriptorResources() const {
    auto res = material->getDescriptorResources();
    std::vector<IRenderResourcePtr> ret{res.begin(), res.end()};
    if (skeleton.has_value()) {
      auto skRes = skeleton.value()->getRenderResources();
      ret.insert(ret.end(), skRes.begin(), skRes.end());
    }
    return ret;
  }
  virtual ShaderPtr getShaderInfo() const {
    return material->getShaderInfo();
  }
  virtual ObjectPCPtr getObjectInfo() const {
    return objectPC;
  }
  virtual ResourcePassFlag getPassMask() const {
    return material->getPassFlag();
  }
  virtual VertexFormat getVertexFormat() const { return VType::format(); }
};


} // namespace LX_core