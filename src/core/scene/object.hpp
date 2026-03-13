#pragma once
#include "../math/mat.hpp"
#include "components/base.hpp"
#include "components/material.hpp"
#include "components/mesh.hpp"
#include "components/skeleton.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace LX_core {

class IRenderable {
public:
  virtual ~IRenderable() = default;
  virtual IRenderResourcePtr getVertexBuffer() const = 0;
  virtual IRenderResourcePtr getIndexBuffer() const = 0;
  virtual std::vector<IRenderResourcePtr> getDescriptorResources() const = 0;
  virtual ShaderPtr getVertexShader() const = 0;
  virtual ShaderPtr getFragmentShader() const = 0;
  virtual ResourcePassFlag getPassMask() const = 0;
  virtual VertexFormat getVertexFormat() const = 0;

  virtual IRenderResourcePtr getPushConstant() const{
    return nullptr;
  }  
};

using IRenderablePtr = std::shared_ptr<IRenderable>;

// push constant
struct alignas(16) ObjectPC : public IRenderResource {
  ObjectPC(ResourcePassFlag passFlag = ResourcePassFlag::Forward)
      : passFlag(passFlag) {}

  struct Param {
    Mat4f model = Mat4f::identity();
    int enableLighting = 1;
    int enableSkinning = 0;
    int padding[2] = {0, 0};
  };
  Param param;

  virtual ResourcePassFlag getPassFlag() const override { return passFlag; }
  virtual ResourceType getType() const override {
    return ResourceType::PushConstant;
  }
  virtual const void *getRawData() const override { return &param; }
  virtual u32 getByteSize() const override { return sizeof(Param); }

private:
  ResourcePassFlag passFlag = ResourcePassFlag::Forward;
};

using ObjectPCPtr = std::shared_ptr<ObjectPC>;

// 渲染子网格，先仅支持1个网格。
template <typename VType> struct RenderableSubMesh : public IRenderable {
public:
  MeshPtr<VType> mesh;
  MaterialPtr material;
  std::optional<SkeletonPtr> skeleton;
  std::optional<ObjectPCPtr> objectPC;


  virtual IRenderResourcePtr getVertexBuffer() const{
    return mesh->vertexBuffer;
  }
  virtual IRenderResourcePtr getIndexBuffer() const{
    return mesh->indexBuffer;
  }
  virtual std::vector<IRenderResourcePtr> getDescriptorResources() const{
    auto res=material->getDescriptorResources();
    std::vector<IRenderResourcePtr> ret{res.begin(),
                                        res.end()};
    if (skeleton.has_value()) {
      auto skRes=skeleton.value()->getRenderResources();
      ret.insert(ret.end(), skRes.begin(),
                 skRes.end());
    }
    return ret;
  }
  virtual ShaderPtr getVertexShader() const{
    return material->getVertexShader();
  }
  virtual ShaderPtr getFragmentShader() const{
    return material->getFragmentShader();
  }
  virtual ResourcePassFlag getPassMask() const{
    return material->getPassFlag();
  }
  virtual VertexFormat getVertexFormat() const{
    return VType::format();
  }
};

// template <typename VertexType> class RenderableMesh {
// public:
//   RenderableMesh() = default;
//   ~RenderableMesh() = default;

//   virtual std::vector<IRenderResourcePtr> getRenderResources() const override {
//     std::vector<IRenderResourcePtr> ret{objectPC->getRenderResources().begin(),
//                                         objectPC->getRenderResources().end()};
//     for (auto &subObject : m_subObjects) {
//       auto &resources = subObject.getRenderResources();
//       ret.insert(ret.end(), resources.begin(), resources.end());
//     }
//     return ret;
//   }

// private:
//   std::vector<RenderableSubMesh<VertexType>> m_subObjects;
//   ObjectPCPtr objectPC;
// };

// template <typename VertexType>
// using RenderableMeshPtr = std::shared_ptr<RenderableMesh<VertexType>>;

} // namespace LX_core