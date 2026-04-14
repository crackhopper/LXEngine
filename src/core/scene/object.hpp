#pragma once
#include "core/gpu/render_resource.hpp"
#include "core/math/mat.hpp"
#include "core/resources/material.hpp"
#include "core/resources/mesh.hpp"
#include "core/resources/skeleton.hpp"
#include "core/scene/pass.hpp"
#include <cstdint>
#include <cstring>
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
  template <typename T>
  void update(const T &params) {
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

  virtual IShaderPtr getShaderInfo() const = 0;
  virtual ObjectPCPtr getObjectInfo() const { return nullptr; }

  /// Structured object signature used to build PipelineKey via
  /// `compose(TypeTag::ObjectRender, {meshSig, skelSig})`.
  virtual StringID getRenderSignature(StringID pass) const = 0;

  /// 该 renderable 是否参与指定 pass。默认基于 getPassMask() 与
  /// passFlagFromStringID(pass) 做位与判断。子类可 override 以实现更细粒度的
  /// 过滤（例如根据 material 是否配置了 Pass_Shadow entry）。
  virtual bool supportsPass(StringID pass) const {
    const auto flag = passFlagFromStringID(pass);
    return (static_cast<std::uint32_t>(getPassMask()) &
            static_cast<std::uint32_t>(flag)) != 0;
  }
};

using IRenderablePtr = std::shared_ptr<IRenderable>;

// 渲染子网格，先仅支持1个网格。
struct RenderableSubMesh : public IRenderable {
public:
  MeshPtr mesh;
  MaterialPtr material;
  std::optional<SkeletonPtr> skeleton;
  ObjectPCPtr objectPC;

  RenderableSubMesh(MeshPtr mesh_, MaterialPtr material_,
                    SkeletonPtr skeleton_ = nullptr)
      : mesh(std::move(mesh_)), material(std::move(material_)) {
    if (skeleton_) {
      skeleton = skeleton_;
    }
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
      ret.push_back(std::dynamic_pointer_cast<IRenderResource>(
          skeleton.value()->getUBO()));
    }
    return ret;
  }
  virtual IShaderPtr getShaderInfo() const { return material->getShaderInfo(); }
  virtual ObjectPCPtr getObjectInfo() const { return objectPC; }
  virtual ResourcePassFlag getPassMask() const {
    return material->getPassFlag();
  }

  StringID getRenderSignature(StringID pass) const override {
    StringID meshSig = mesh ? mesh->getRenderSignature(pass) : StringID{};
    StringID skelSig = skeleton.has_value()
                           ? skeleton.value()->getRenderSignature()
                           : StringID{};
    StringID fields[] = {meshSig, skelSig};
    return GlobalStringTable::get().compose(TypeTag::ObjectRender, fields);
  }
};

} // namespace LX_core
