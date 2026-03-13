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

  virtual ResourcePassFlag
  getPassFlag() const override {
    return passFlag;
  }
  virtual ResourceType getType() const override {
    return ResourceType::PushConstant;
  }
  virtual const void *getRawData() const override { return &param; }
  virtual u32 getByteSize() const override { return sizeof(Param); }

private:
  ResourcePassFlag passFlag = ResourcePassFlag::Forward;
};

using ObjectPCPtr = std::shared_ptr<ObjectPC>;

template <typename VertexType> class RenderableMesh : public IComponent {
public:
  template <typename VType> struct SubObject : public IComponent {
    MeshPtr<VType> mesh;
    std::optional<MaterialPtr> material;
    std::optional<SkeletonPtr> skeleton;
    virtual std::vector<IRenderResourcePtr> getRenderResources() override {
      auto &resources = getRenderResources();
      std::vector<IRenderResourcePtr> ret{resources.begin(), resources.end()};
      if (material) {
        auto &resources = getRenderResources();
        ret.insert(ret.end(), resources.begin(), resouces.end());
      }
      if (skeleton) {
        auto &resources = getRenderResources();
        ret.insert(ret.end(), resources.begin(), resouces.end());
      }
      return ret;
    }
  };
  RenderableMesh() = default;
  ~RenderableMesh() = default;

  virtual std::vector<IRenderResourcePtr> getRenderResources() override {
    std::vector<IRenderResourcePtr> ret{objectPC->getRenderResources().begin(),
                                        objectPC->getRenderResources().end()};
    for (auto &subObject : m_subObjects) {
      auto &resources = subObject.getRenderResources();
      ret.insert(ret.end(), resources.begin(), resources.end());
    }
    return ret;
  }

private:
  std::vector<SubObject<VertexType>> m_subObjects;
  ObjectPCPtr objectPC;
};

template <typename VertexType>
using RenderableMeshPtr = std::shared_ptr<RenderableMesh<VertexType>>;
} // namespace LX_core