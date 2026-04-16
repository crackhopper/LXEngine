#pragma once
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/skeleton.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/math/mat.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/rhi/render_resource.hpp"
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace LX_core {

class Scene;

struct ObjectPC : public IRenderResource {
  using Ptr = std::shared_ptr<ObjectPC>;

  alignas(16) uint8_t data[128] = {0};
  uint32_t activeSize = sizeof(PC_Base);

  explicit ObjectPC(ResourcePassFlag passFlag = ResourcePassFlag::Forward)
      : passFlag(passFlag) {
    PC_Base base;
    std::memcpy(data, &base, sizeof(base));
  }

  template <typename T>
  void update(const T &params) {
    static_assert(sizeof(T) <= 128, "PushConstant block too large!");
    std::memcpy(data, &params, sizeof(T));
    activeSize = sizeof(T);
  }

  ResourcePassFlag getPassFlag() const override { return passFlag; }
  ResourceType getType() const override { return ResourceType::PushConstant; }
  const void *getRawData() const override { return data; }
  u32 getByteSize() const override { return activeSize; }

private:
  ResourcePassFlag passFlag;
};

using ObjectPCPtr = ObjectPC::Ptr;

struct ValidatedRenderablePassData {
  StringID pass;
  MaterialInstancePtr material;
  IShaderPtr shaderInfo;
  ObjectPCPtr objectInfo;
  IRenderResourcePtr vertexBuffer;
  IRenderResourcePtr indexBuffer;
  std::vector<IRenderResourcePtr> descriptorResources;
  ResourcePassFlag passMask = ResourcePassFlag::Forward;
  StringID objectSignature;
  PipelineKey pipelineKey;
};

class IRenderable {
public:
  virtual ~IRenderable() = default;

  virtual IRenderResourcePtr getVertexBuffer() const = 0;
  virtual IRenderResourcePtr getIndexBuffer() const = 0;
  virtual std::vector<IRenderResourcePtr> getDescriptorResources() const = 0;
  virtual ResourcePassFlag getPassMask() const = 0;
  virtual IShaderPtr getShaderInfo() const = 0;
  virtual ObjectPCPtr getObjectInfo() const { return nullptr; }
  virtual StringID getRenderSignature(StringID pass) const = 0;
  virtual bool supportsPass(StringID pass) const = 0;
  virtual std::string getNodeName() const = 0;
  virtual StringID getDebugId() const { return StringID{}; }

  virtual std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
  getValidatedPassData(StringID pass) const = 0;
};

using IRenderablePtr = std::shared_ptr<IRenderable>;

class SceneNode final : public IRenderable {
public:
  using Ptr = std::shared_ptr<SceneNode>;

  SceneNode(std::string nodeName, MeshPtr mesh, MaterialInstancePtr material,
            SkeletonPtr skeleton = nullptr);
  ~SceneNode() override;

  SceneNode(const SceneNode &) = delete;
  SceneNode &operator=(const SceneNode &) = delete;
  SceneNode(SceneNode &&) = delete;
  SceneNode &operator=(SceneNode &&) = delete;

  static Ptr create(std::string nodeName, MeshPtr mesh,
                    MaterialInstancePtr material,
                    SkeletonPtr skeleton = nullptr) {
    return std::make_shared<SceneNode>(std::move(nodeName), std::move(mesh),
                                       std::move(material),
                                       std::move(skeleton));
  }

  const MeshPtr &getMesh() const { return m_mesh; }
  const MaterialInstancePtr &getMaterialInstance() const {
    return m_materialInstance;
  }
  const std::optional<SkeletonPtr> &getSkeleton() const { return m_skeleton; }

  void setMesh(MeshPtr mesh);
  void setMaterialInstance(MaterialInstancePtr material);
  void setSkeleton(SkeletonPtr skeleton);

  IRenderResourcePtr getVertexBuffer() const override;
  IRenderResourcePtr getIndexBuffer() const override;
  std::vector<IRenderResourcePtr> getDescriptorResources() const override;
  ResourcePassFlag getPassMask() const override;
  IShaderPtr getShaderInfo() const override;
  ObjectPCPtr getObjectInfo() const override { return m_objectPC; }
  StringID getRenderSignature(StringID pass) const override;
  bool supportsPass(StringID pass) const override;
  std::string getNodeName() const override { return m_nodeName; }
  StringID getDebugId() const override { return m_debugId; }
  void setSceneDebugId(StringID debugId) { m_debugId = debugId; }
  void attachToScene(Scene *scene) { m_scene = scene; }

  std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
  getValidatedPassData(StringID pass) const override;

private:
  friend class Scene;
  void rebuildValidatedCache();
  void registerMaterialPassListener();
  void unregisterMaterialPassListener();

  std::string m_nodeName;
  MeshPtr m_mesh;
  MaterialInstancePtr m_materialInstance;
  std::optional<SkeletonPtr> m_skeleton;
  ObjectPCPtr m_objectPC;
  StringID m_debugId;
  std::unordered_map<StringID, ValidatedRenderablePassData, StringID::Hash>
      m_validatedPasses;
  uint64_t m_materialPassListenerId = 0;
  Scene *m_scene = nullptr;
};

struct RenderableSubMesh final : public IRenderable {
public:
  MeshPtr mesh;
  MaterialInstancePtr material;
  std::optional<SkeletonPtr> skeleton;
  ObjectPCPtr objectPC;
  std::string nodeName = "RenderableSubMesh";

  RenderableSubMesh(MeshPtr mesh_, MaterialInstancePtr material_,
                    SkeletonPtr skeleton_ = nullptr,
                    std::string nodeName_ = "RenderableSubMesh");

  IRenderResourcePtr getVertexBuffer() const override;
  IRenderResourcePtr getIndexBuffer() const override;
  std::vector<IRenderResourcePtr> getDescriptorResources() const override;
  ResourcePassFlag getPassMask() const override;
  IShaderPtr getShaderInfo() const override;
  ObjectPCPtr getObjectInfo() const override { return objectPC; }
  StringID getRenderSignature(StringID pass) const override;
  bool supportsPass(StringID pass) const override;
  std::string getNodeName() const override { return nodeName; }
  StringID getDebugId() const override { return StringID(nodeName); }
  std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
  getValidatedPassData(StringID pass) const override;

private:
  mutable std::optional<ValidatedRenderablePassData> m_lastValidatedData;
};

using SceneNodePtr = SceneNode::Ptr;

} // namespace LX_core
