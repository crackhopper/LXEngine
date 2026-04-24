#pragma once
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/skeleton.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/math/mat.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/rhi/gpu_resource.hpp"
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

struct PerDrawData {
  using SharedPtr = std::shared_ptr<PerDrawData>;

  alignas(16) uint8_t data[128] = {0};
  PerDrawByteSize32 activeSize = sizeof(PerDrawLayoutBase);

  PerDrawData() {
    PerDrawLayoutBase base;
    std::memcpy(data, &base, sizeof(base));
  }

  template <typename T>
  void update(const T &params) {
    static_assert(sizeof(T) <= 128, "PushConstant block too large!");
    std::memcpy(data, &params, sizeof(T));
    activeSize = sizeof(T);
  }

  const void *rawData() const { return data; }
  PerDrawByteSize32 byteSize() const { return activeSize; }
};

using PerDrawDataSharedPtr = PerDrawData::SharedPtr;

struct ValidatedRenderablePassData {
  StringID pass;
  MaterialInstanceSharedPtr material;
  IShaderSharedPtr shaderInfo;
  PerDrawDataSharedPtr drawData;
  IGpuResourceSharedPtr vertexBuffer;
  IGpuResourceSharedPtr indexBuffer;
  std::vector<IGpuResourceSharedPtr> descriptorResources;
  StringID objectSignature;
  PipelineKey pipelineKey;
};

class IRenderable {
public:
  virtual ~IRenderable() = default;

  virtual IGpuResourceSharedPtr getVertexBuffer() const = 0;
  virtual IGpuResourceSharedPtr getIndexBuffer() const = 0;
  virtual std::vector<IGpuResourceSharedPtr>
  getDescriptorResources(StringID pass) const = 0;
  virtual IShaderSharedPtr getShaderInfo() const = 0;
  virtual PerDrawDataSharedPtr getPerDrawData() const { return nullptr; }
  virtual StringID getRenderSignature(StringID pass) const = 0;
  virtual bool supportsPass(StringID pass) const = 0;
  virtual std::string getNodeName() const = 0;
  virtual StringID getDebugId() const { return StringID{}; }

  virtual std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
  getValidatedPassData(StringID pass) const = 0;
};

using IRenderableSharedPtr = std::shared_ptr<IRenderable>;

class SceneNode final : public IRenderable {
public:
  using SharedPtr = std::shared_ptr<SceneNode>;

  SceneNode(std::string nodeName, MeshSharedPtr mesh, MaterialInstanceSharedPtr material,
            SkeletonSharedPtr skeleton = nullptr);
  ~SceneNode() override;

  SceneNode(const SceneNode &) = delete;
  SceneNode &operator=(const SceneNode &) = delete;
  SceneNode(SceneNode &&) = delete;
  SceneNode &operator=(SceneNode &&) = delete;

  static SharedPtr create(std::string nodeName, MeshSharedPtr mesh,
                          MaterialInstanceSharedPtr material,
                          SkeletonSharedPtr skeleton = nullptr) {
    return std::make_shared<SceneNode>(std::move(nodeName), std::move(mesh),
                                       std::move(material),
                                       std::move(skeleton));
  }

  const MeshSharedPtr &getMesh() const { return m_mesh; }
  const MaterialInstanceSharedPtr &getMaterialInstance() const {
    return m_materialInstance;
  }
  const std::optional<SkeletonSharedPtr> &getSkeleton() const { return m_skeleton; }

  void setMesh(MeshSharedPtr mesh);
  void setMaterialInstance(MaterialInstanceSharedPtr material);
  void setSkeleton(SkeletonSharedPtr skeleton);

  IGpuResourceSharedPtr getVertexBuffer() const override;
  IGpuResourceSharedPtr getIndexBuffer() const override;
  std::vector<IGpuResourceSharedPtr>
  getDescriptorResources(StringID pass) const override;
  IShaderSharedPtr getShaderInfo() const override;
  PerDrawDataSharedPtr getPerDrawData() const override { return m_perDrawData; }
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
  MeshSharedPtr m_mesh;
  MaterialInstanceSharedPtr m_materialInstance;
  std::optional<SkeletonSharedPtr> m_skeleton;
  PerDrawDataSharedPtr m_perDrawData;
  StringID m_debugId;
  std::unordered_map<StringID, ValidatedRenderablePassData, StringID::Hash>
      m_validatedPasses;
  uint64_t m_materialPassListenerId = 0;
  Scene *m_scene = nullptr;
};

struct RenderableSubMesh final : public IRenderable {
public:
  MeshSharedPtr mesh;
  MaterialInstanceSharedPtr material;
  std::optional<SkeletonSharedPtr> skeleton;
  PerDrawDataSharedPtr perDrawData;
  std::string nodeName = "RenderableSubMesh";

  RenderableSubMesh(MeshSharedPtr mesh_, MaterialInstanceSharedPtr material_,
                    SkeletonSharedPtr skeleton_ = nullptr,
                    std::string nodeName_ = "RenderableSubMesh");

  IGpuResourceSharedPtr getVertexBuffer() const override;
  IGpuResourceSharedPtr getIndexBuffer() const override;
  std::vector<IGpuResourceSharedPtr>
  getDescriptorResources(StringID pass) const override;
  IShaderSharedPtr getShaderInfo() const override;
  PerDrawDataSharedPtr getPerDrawData() const override { return perDrawData; }
  StringID getRenderSignature(StringID pass) const override;
  bool supportsPass(StringID pass) const override;
  std::string getNodeName() const override { return nodeName; }
  StringID getDebugId() const override { return StringID(nodeName); }
  std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
  getValidatedPassData(StringID pass) const override;

private:
  mutable std::optional<ValidatedRenderablePassData> m_lastValidatedData;
};

using SceneNodeSharedPtr = SceneNode::SharedPtr;

} // namespace LX_core
