#pragma once
#include "core/asset/shader.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/math/ray.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/ibl_environment.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "core/scene/scene_events.hpp"
#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace LX_core {

namespace detail {

[[noreturn]] inline void throwProgrammerLogicError(const std::string &message) {
  throw std::logic_error(message);
}

} // namespace detail

using ShaderPtr = IShaderSharedPtr;

enum class RenderDomain {
  Realtime,
  Offline,
};

enum class RenderWorkKind {
  RasterDraw,
  RasterBatch,
  ComputeDispatch,
  RayTracingDispatch,
};

struct RasterDrawWorkPayload final {
  PerDrawDataSharedPtr drawData;
  IGpuResourceSharedPtr vertexBuffer;
  IGpuResourceSharedPtr indexBuffer;
  u32 indexCount = 0;
  u32 firstIndex = 0;
  i32 vertexOffset = 0;
  u32 instanceCount = 1;
};

struct ComputeDispatchWorkPayload final {
  u32 groupCountX = 1;
  u32 groupCountY = 1;
  u32 groupCountZ = 1;
};

/*
@source_analysis.section RenderWorkItem：一次 pipeline work 的最小稳定记录
这个结构体定义在 scene.hpp 而不是 queue.hpp，是因为它描述的是 backend
真正消费的契约，而不是 queue 的内部状态。任何把"一个 pass 内需要执行的一份
GPU work"翻译成 backend 提交单元的代码路径，都收口到这个结构体上。

字段拆分体现两个边界：

- `domain / kind / shaderInfo / pipelineKey / pass / target`：决定走哪条
  pipeline，以及这份 work 是 raster draw、compute dispatch 还是后续 RT work
- `descriptorResources`：决定 pipeline-visible 资源，顺序固定但 backend 按
  binding name 命中，不依赖位置
- `raster / compute`：按 work kind 存放特化 payload，避免把 raster-only
  vertex/index/drawData 当成所有 render work 的公共字段
- `material`：保留材质句柄是为了 `PipelineBuildDesc::fromRenderWorkItem`
  反查 render state 和 owned binding 表，而不是 backend 直接读它
*/
struct RenderWorkItem final {
  RenderDomain domain = RenderDomain::Realtime;
  RenderWorkKind kind = RenderWorkKind::RasterDraw;
  ShaderPtr shaderInfo;
  MaterialInstanceSharedPtr
      material; // 材质句柄 — 用于 PipelineBuildDesc::fromRenderWorkItem

  RasterDrawWorkPayload raster;
  ComputeDispatchWorkPayload compute;

  std::vector<IGpuResourceSharedPtr>
      descriptorResources; // 材质 + skeleton 等资源

  StringID pass;
  RenderTargetDesc target;
  StringID debugId;
  StringID objectSignature;
  StringID materialSignature;
  PipelineKey pipelineKey;
};

/*
@source_analysis.section Scene：扁平容器
Scene 是一层薄壳：三个平铺 vector（renderables / cameras / lights）+ 一个
sceneName。 它不维护层级（节点之间的 parent/child 关系挂在 SceneNode 上）、不做
z-sort、不持有 render state。这种扁平 ownership
让"哪些对象属于这一帧"是可枚举的事实，而不是 需要遍历某种隐式树才能复原的状态。

Scene 本身不隐式创建 camera 或 light；测试和 demo 需要显式注册带组件的
SceneNode。

`enable_shared_from_this` 的存在是为了在 `addRenderable` 里给挂进来的 SceneNode
写 弱反向引用 `weak_from_this()`，让 shared material 重验证传播能从 node 找回
scene。
*/
class Scene : public std::enable_shared_from_this<Scene> {
public:
  using SharedPtr = std::shared_ptr<Scene>;

  explicit Scene(std::string sceneName)
      : m_sceneName(std::move(sceneName)),
        m_rootNode(SceneNode::createPathRoot()) {
    if (m_sceneName.empty()) {
      m_sceneName = "Scene";
    }
  }
  ~Scene();

  static auto create(std::string sceneName,
                     IRenderableSharedPtr mesh = nullptr) {
    auto scene = std::make_shared<Scene>(std::move(sceneName));
    if (mesh) {
      scene->addRenderable(std::move(mesh));
    }
    return scene;
  }

  static auto create(IRenderableSharedPtr mesh) {
    auto scene = std::make_shared<Scene>("Scene");
    if (mesh) {
      scene->addRenderable(std::move(mesh));
    }
    return scene;
  }

  static auto create(std::nullptr_t) {
    return std::make_shared<Scene>("Scene");
  }

  const std::vector<IRenderableSharedPtr> &getRenderables() const {
    return m_renderables;
  }

  /*
  @source_analysis.section addRenderable：nodeName 唯一与命名注入
  这个方法承担了三件 SceneNode 自己做不了的事：

  1. 在 scene 命名空间内强制 nodeName 唯一。线性扫描存量 renderable，重名即抛
     `logic_error` — 因为 nodeName 是 scene-level debug id 的主键，duplicate 会
     让错误日志里的对象引用语义崩塌。
  2. 把 `<sceneName>/<nodeName>` 写回 SceneNode 作为 `sceneDebugId`，让跨 scene
     的日志和断言能拿到一个稳定的 StringID 对象引用。
  3. 调用 `attachToScene(weak_from_this())`，给 node 写一个弱反向句柄。这条句柄
     是 shared MaterialInstance 反向传播 `revalidateNodesUsing` 的前提。

  非 SceneNode 类型的 renderable 仍然走 nodeName 唯一性检查，但跳过 scene 反向
  绑定 — 它们没有需要从 scene 读回的状态。
  */
  void addRenderable(IRenderableSharedPtr r) {
    auto node = std::dynamic_pointer_cast<SceneNode>(r);
    if (r) {
      for (const auto &existing : m_renderables) {
        if (!existing)
          continue;
        if (existing->getNodeName() == r->getNodeName()) {
          detail::throwProgrammerLogicError(
              "Scene duplicate nodeName in scene '" + m_sceneName +
              "': " + r->getNodeName());
        }
      }
      if (node) {
        node->attachToScene(weak_from_this());
        if (!node->getParent()) {
          node->setParentInternal(m_rootNode, false);
        }
        node->setSceneDebugId(
            StringID(m_sceneName + "/" + node->getNodeName()));
        registerNodeResources(*node);
        node->warnIfSiblingNameIsDuplicated();
      }
    }
    m_renderables.push_back(std::move(r));
    if (node) {
      node->emitRuntimeNodeLifecycle(SceneEventType::SceneNodeAdded);
    }
  }

  void addRuntimeRenderable(IRenderableSharedPtr r) {
    auto node = std::dynamic_pointer_cast<SceneNode>(r);
    if (r) {
      for (const auto &existing : m_renderables) {
        if (!existing)
          continue;
        if (existing->getNodeName() == r->getNodeName()) {
          detail::throwProgrammerLogicError(
              "Scene duplicate nodeName in scene '" + m_sceneName +
              "': " + r->getNodeName());
        }
      }
      if (node) {
        node->attachToScene(weak_from_this());
        node->setSceneDebugId(
            StringID(m_sceneName + "/" + node->getNodeName()));
        registerNodeResources(*node);
      }
    }
    m_renderables.push_back(std::move(r));
  }

  void removeRenderable(const SceneNodeSharedPtr &node);

  void addCamera(const SceneNodeSharedPtr &cameraNode);
  void removeCamera(const SceneNodeSharedPtr &cameraNode);
  const std::vector<SceneNodeSharedPtr> &getCameras() const {
    return m_cameras;
  }
  [[nodiscard]] SceneNodeSharedPtr getActiveCamera() const;
  void setActiveCamera(const SceneNodeSharedPtr &cameraNode);

  void addLight(LightBaseSharedPtr light) {
    if (!light) {
      return;
    }
    if (m_lightHandles.find(light.get()) == m_lightHandles.end()) {
      m_lightHandles[light.get()] = m_resources.registerLight(light);
    }
    if (std::find(m_lights.begin(), m_lights.end(), light) == m_lights.end()) {
      m_lights.push_back(std::move(light));
    }
  }
  void attachLight(const SceneNodeSharedPtr &node,
                   const LightBaseSharedPtr &light);
  [[nodiscard]] LightBaseSharedPtr getLight(const SceneNode &node) const;
  [[nodiscard]] DirectionalLightSharedPtr
  getDirectionalLight(const SceneNode &node) const;
  [[nodiscard]] PointLightSharedPtr getPointLight(const SceneNode &node) const;
  [[nodiscard]] SpotLightSharedPtr getSpotLight(const SceneNode &node) const;
  [[nodiscard]] LightBaseSharedPtr detachLight(const SceneNodeSharedPtr &node);
  void removeLight(const LightBaseSharedPtr &light);
  const std::vector<LightBaseSharedPtr> &getLights() const { return m_lights; }
  [[nodiscard]] SceneLightsDataSharedPtr getSceneLightsUBO() const {
    return m_sceneLightsUbo;
  }
  void setIblEnvironmentResources(IblEnvironmentResources resources);
  [[nodiscard]] IblEnvironmentResources getIblEnvironmentResourceSet() const;
  [[nodiscard]] std::vector<IGpuResourceSharedPtr>
  getIblEnvironmentResources() const;
  const std::string &getSceneName() const { return m_sceneName; }
  struct PickHit {
    SceneNodeSharedPtr node;
    float distance = 0.0f;
  };
  SceneNode *findByPath(const std::string &path) const;
  [[nodiscard]] std::vector<std::string> listAllPaths() const;
  std::string dumpTree() const;
  void revalidateNodesUsing(const MaterialInstanceSharedPtr &materialInstance);
  [[nodiscard]] SceneEventHub &events() { return m_events; }
  [[nodiscard]] const SceneEventHub &events() const { return m_events; }
  [[nodiscard]] SceneResourceTable &resources() { return m_resources; }
  [[nodiscard]] const SceneResourceTable &resources() const {
    return m_resources;
  }
  [[nodiscard]] RenderSceneSnapshot buildRenderSceneSnapshot() const;

  /// REQ-009 two-axis filter form: camera by matchesTarget(target), light by
  /// supportsPass(pass). Returns camera data resources first, then light data
  /// resources; both in their respective container insertion order. Empty
  /// return is valid.
  std::vector<IGpuResourceSharedPtr>
  getSceneLevelResources(StringID pass, const RenderTarget &target) const;
  VisibilityLayerMask
  getCombinedCameraCullingMask(const RenderTarget &target) const;
  [[nodiscard]] BoundingBox getPickBounds(const SceneNode &node) const;
  std::optional<PickHit>
  pick(const Ray &ray,
       VisibilityLayerMask layerMask = VisibilityMask_All) const;
  [[nodiscard]] const SceneNodeSharedPtr &getRootNode() const {
    return m_rootNode;
  }
  [[nodiscard]] std::vector<SceneNodeSharedPtr> getRootNodes() const;

private:
  static std::vector<std::string> splitPathSegments(const std::string &path);
  static bool matchesPathSegment(const SceneNode &node,
                                 const std::string &pathSegment);
  static void appendPaths(const SceneNode &node, std::vector<std::string> &out);
  static void appendTreeLines(const SceneNode &node, std::string prefix,
                              bool isLast, std::string &out);
  void registerNodeResources(SceneNode &node);
  void releaseNodeResources(SceneNode &node);
  void syncNodeResourceState(SceneNode &node) const;
  std::string m_sceneName;
  SceneNodeSharedPtr m_rootNode;
  std::vector<IRenderableSharedPtr> m_renderables;
  std::vector<SceneNodeSharedPtr> m_cameras;
  std::vector<LightBaseSharedPtr> m_lights;
  std::unordered_map<const SceneNode *, LightBaseSharedPtr> m_lightsByNode;
  std::unordered_map<const LightBase *, LightHandle> m_lightHandles;
  mutable SceneLightsDataSharedPtr m_sceneLightsUbo =
      std::make_shared<SceneLightsData>();
  mutable std::optional<IblEnvironmentResources> m_iblEnvironmentResources;
  mutable SceneResourceTable m_resources;
  SceneEventHub m_events;
};

using SceneSharedPtr = Scene::SharedPtr;
} // namespace LX_core
