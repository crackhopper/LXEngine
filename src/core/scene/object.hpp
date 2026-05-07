#pragma once
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/skeleton.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/math/mat.hpp"
#include "core/math/transform.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/scene/component.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/visibility_mask.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace LX_core {

class Scene;

struct PerDrawData {
  using SharedPtr = std::shared_ptr<PerDrawData>;

  alignas(16) u8 data[128] = {0};
  u32 activeSize = sizeof(PerDrawLayoutBase);

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

  void updateModelMatrix(const Mat4f &model) {
    std::memcpy(data, &model, sizeof(model));
    if (activeSize < sizeof(PerDrawLayoutBase)) {
      activeSize = sizeof(PerDrawLayoutBase);
    }
  }

  const void *rawData() const { return data; }
  u32 byteSize() const { return activeSize; }
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
  virtual StringID getPipelineSignature(StringID pass) const = 0;
  virtual bool supportsPass(StringID pass) const = 0;
  virtual VisibilityLayerMask getVisibilityLayerMask() const = 0;
  virtual std::string getNodeName() const = 0;
  virtual StringID getDebugId() const { return StringID{}; }

  virtual std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
  getValidatedPassData(StringID pass) const = 0;
};

using IRenderableSharedPtr = std::shared_ptr<IRenderable>;

class SceneNode final : public IRenderable,
                        public std::enable_shared_from_this<SceneNode> {
public:
  using SharedPtr = std::shared_ptr<SceneNode>;

  explicit SceneNode(std::string nodeName);
  ~SceneNode() override;

  SceneNode(const SceneNode &) = delete;
  SceneNode &operator=(const SceneNode &) = delete;
  SceneNode(SceneNode &&) = delete;
  SceneNode &operator=(SceneNode &&) = delete;

  static SharedPtr create(std::string nodeName) {
    return std::make_shared<SceneNode>(std::move(nodeName));
  }

  template <typename T, typename... Args>
  std::optional<std::reference_wrapper<T>> addComponent(Args &&...args) {
    const ComponentTypeId typeId = componentTypeId<T>();
    const auto exists =
        std::find_if(m_components.begin(), m_components.end(),
                     [typeId](const std::unique_ptr<IComponent> &component) {
                       return component->getTypeId() == typeId;
                     });
    assert(exists == m_components.end() &&
           "SceneNode duplicate component type is not allowed");
    if (exists != m_components.end()) {
      return std::nullopt;
    }

    auto component = std::make_unique<T>(std::forward<Args>(args)...);
    auto &componentRef = *component;
    const bool affectsRenderableStructure =
        componentRef.affectsRenderableStructure();
    componentRef.attachTo(*this);
    m_components.push_back(std::move(component));
    if (affectsRenderableStructure) {
      rebuildValidatedCache();
    }
    return std::ref(componentRef);
  }

  template <typename T>
  std::optional<std::reference_wrapper<T>> getComponent() {
    const ComponentTypeId typeId = componentTypeId<T>();
    for (auto &component : m_components) {
      if (component->getTypeId() == typeId) {
        return std::ref(static_cast<T &>(*component));
      }
    }
    return std::nullopt;
  }

  template <typename T>
  std::optional<std::reference_wrapper<const T>> getComponent() const {
    const ComponentTypeId typeId = componentTypeId<T>();
    for (const auto &component : m_components) {
      if (component->getTypeId() == typeId) {
        return std::cref(static_cast<const T &>(*component));
      }
    }
    return std::nullopt;
  }

  template <typename T>
  bool removeComponent() {
    const ComponentTypeId typeId = componentTypeId<T>();
    const auto it =
        std::find_if(m_components.begin(), m_components.end(),
                     [typeId](const std::unique_ptr<IComponent> &component) {
                       return component->getTypeId() == typeId;
                     });
    if (it == m_components.end()) {
      return false;
    }

    const bool affectsRenderableStructure = (*it)->affectsRenderableStructure();
    (*it)->detachFromOwner();
    m_components.erase(it);
    if (affectsRenderableStructure) {
      rebuildValidatedCache();
    }
    return true;
  }

  std::vector<std::reference_wrapper<IComponent>> listComponents();
  std::vector<std::reference_wrapper<const IComponent>> listComponents() const;

  void setLocalTransform(const Transform &transform);
  const Transform &getLocalTransform() const { return m_localTransform; }
  void setTranslation(const Vec3f &translation);
  void setRotation(const Quatf &rotation);
  void setScale(const Vec3f &scale);
  void setName(std::string name);
  const std::string &getName() const { return m_name; }
  std::string getPath() const;
  Vec3f getTranslation() const { return m_localTransform.translation; }
  Quatf getRotation() const { return m_localTransform.rotation; }
  Vec3f getScale() const { return m_localTransform.scale; }
  const Mat4f &getWorldTransform() const;
  BoundingBox getLocalBounds() const;
  BoundingBox getWorldBounds() const;
  void setParent(const SharedPtr &parent);
  void clearParent();
  SharedPtr getParent() const { return m_parent.lock(); }
  [[nodiscard]] std::vector<SharedPtr> getChildren() const;

  IGpuResourceSharedPtr getVertexBuffer() const override;
  IGpuResourceSharedPtr getIndexBuffer() const override;
  std::vector<IGpuResourceSharedPtr>
  getDescriptorResources(StringID pass) const override;
  IShaderSharedPtr getShaderInfo() const override;
  PerDrawDataSharedPtr getPerDrawData() const override;
  StringID getPipelineSignature(StringID pass) const override;
  bool supportsPass(StringID pass) const override;
  std::string getNodeName() const override { return m_nodeName; }
  StringID getDebugId() const override { return m_debugId; }
  void setSceneDebugId(StringID debugId) { m_debugId = debugId; }
  void attachToScene(const std::weak_ptr<Scene> &scene) { m_scene = scene; }
  void detachFromScene() { m_scene.reset(); }
  std::shared_ptr<Scene> getAttachedScene() const { return m_scene.lock(); }
  VisibilityLayerMask getVisibilityLayerMask() const override {
    return m_visibilityLayerMask;
  }
  void setVisibilityLayerMask(VisibilityLayerMask mask) {
    m_visibilityLayerMask = mask;
  }

  std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
  getValidatedPassData(StringID pass) const override;

private:
  friend class Scene;
  friend class IComponent;
  struct PathRootTag {};
  explicit SceneNode(PathRootTag);
  [[nodiscard]] static SharedPtr createPathRoot();
  void markWorldTransformDirty();
  void updateWorldTransformIfNeeded() const;
  void syncPerDrawModelMatrix() const;
  void removeFromParentChildrenList();
  void pruneExpiredChildren();
  void rebuildValidatedCache();
  void clearComponents();
  void warnIfSiblingNameIsDuplicated() const;
  [[nodiscard]] std::string getPathSegment() const;
  [[nodiscard]] static std::string sanitizeName(std::string name);

  std::string m_nodeName;
  std::string m_name;
  std::vector<std::unique_ptr<IComponent>> m_components;
  PerDrawDataSharedPtr m_perDrawData;
  StringID m_debugId;
  std::unordered_map<StringID, ValidatedRenderablePassData, StringID::Hash>
      m_validatedPasses;
  std::weak_ptr<Scene> m_scene;
  std::weak_ptr<SceneNode> m_parent;
  std::vector<std::weak_ptr<SceneNode>> m_children;
  Transform m_localTransform = Transform::identity();
  mutable Mat4f m_worldTransform = Mat4f::identity();
  mutable bool m_worldTransformDirty = false;
  mutable bool m_worldTransformHasParent = false;
  VisibilityLayerMask m_visibilityLayerMask = VisibilityMask_All;
  bool m_isPathRoot = false;
};

using SceneNodeSharedPtr = SceneNode::SharedPtr;

} // namespace LX_core
