#include "scene.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace LX_core {

namespace {

struct RemovedNodeSnapshot {
  SceneNodeSharedPtr node;
  std::string lastAttachedPath;
  std::string stableNodeName;
};

void collectSubtreeSnapshots(const SceneNodeSharedPtr &node,
                             std::vector<RemovedNodeSnapshot> &out) {
  if (!node) {
    return;
  }

  out.push_back(RemovedNodeSnapshot{
      .node = node,
      .lastAttachedPath = node->getPath(),
      .stableNodeName = node->getNodeName(),
  });
  for (const auto &child : node->getChildren()) {
    collectSubtreeSnapshots(child, out);
  }
}

[[nodiscard]] SceneNodeSharedPtr findRenderableNodeByAddress(
    const std::vector<IRenderableSharedPtr> &renderables,
    const SceneNode *node) {
  if (!node) {
    return nullptr;
  }
  for (const auto &renderable : renderables) {
    const auto renderableNode =
        std::dynamic_pointer_cast<SceneNode>(renderable);
    if (renderableNode && renderableNode.get() == node) {
      return renderableNode;
    }
  }
  return nullptr;
}

[[nodiscard]] CameraResource
makeCameraResourceFromSnapshot(const CameraSnapshot &snapshot) {
  return CameraResource{
      .pose = snapshot.pose,
      .projection = snapshot.projection,
      .view = makeCameraViewMatrix(snapshot.pose),
      .proj = makeCameraProjectionMatrix(snapshot.projection),
      .cullingMask = snapshot.cullingMask,
      .active = snapshot.active,
  };
}

[[nodiscard]] CameraResource
makeCameraResource(const CameraComponent &cameraComponent) {
  return makeCameraResourceFromSnapshot(cameraComponent.getSnapshot());
}

[[nodiscard]] Mat4f inverseAffine(const Mat4f &m) {
  const f32 a00 = m(0, 0);
  const f32 a01 = m(0, 1);
  const f32 a02 = m(0, 2);
  const f32 a10 = m(1, 0);
  const f32 a11 = m(1, 1);
  const f32 a12 = m(1, 2);
  const f32 a20 = m(2, 0);
  const f32 a21 = m(2, 1);
  const f32 a22 = m(2, 2);

  const f32 c00 = a11 * a22 - a12 * a21;
  const f32 c01 = -(a10 * a22 - a12 * a20);
  const f32 c02 = a10 * a21 - a11 * a20;
  const f32 c10 = -(a01 * a22 - a02 * a21);
  const f32 c11 = a00 * a22 - a02 * a20;
  const f32 c12 = -(a00 * a21 - a01 * a20);
  const f32 c20 = a01 * a12 - a02 * a11;
  const f32 c21 = -(a00 * a12 - a02 * a10);
  const f32 c22 = a00 * a11 - a01 * a10;
  const f32 det = a00 * c00 + a01 * c01 + a02 * c02;
  if (std::abs(det) < 1.0e-8f) {
    return Mat4f::identity();
  }
  const f32 invDet = 1.0f / det;

  Mat4f inverse = Mat4f::identity();
  inverse(0, 0) = c00 * invDet;
  inverse(0, 1) = c10 * invDet;
  inverse(0, 2) = c20 * invDet;
  inverse(1, 0) = c01 * invDet;
  inverse(1, 1) = c11 * invDet;
  inverse(1, 2) = c21 * invDet;
  inverse(2, 0) = c02 * invDet;
  inverse(2, 1) = c12 * invDet;
  inverse(2, 2) = c22 * invDet;

  const Vec3f t{m(0, 3), m(1, 3), m(2, 3)};
  inverse(0, 3) =
      -(inverse(0, 0) * t.x + inverse(0, 1) * t.y + inverse(0, 2) * t.z);
  inverse(1, 3) =
      -(inverse(1, 0) * t.x + inverse(1, 1) * t.y + inverse(1, 2) * t.z);
  inverse(2, 3) =
      -(inverse(2, 0) * t.x + inverse(2, 1) * t.y + inverse(2, 2) * t.z);
  return inverse;
}

[[nodiscard]] ObjectResource makeObjectResource(const SceneNode &node,
                                                MeshHandle meshHandle,
                                                MaterialHandle materialHandle) {
  ObjectResource object;
  object.mesh = meshHandle;
  object.material = materialHandle;
  object.objectToWorld = node.getWorldTransform();
  object.worldToObject = inverseAffine(object.objectToWorld);
  object.worldBounds = node.getWorldBounds();
  object.visibilityMask = node.getVisibilityLayerMask();
  object.debugId = node.getDebugId();
  object.visible = true;
  object.debugOnly = node.isDebugOnlyRenderable();
  return object;
}

} // namespace

/*
@source_analysis.section ~Scene：weak detach 协议
析构时显式遍历 renderables 并对每个 SceneNode 调 `detachFromScene()`，把 node 内
的 `m_scene` weak_ptr 清空。看起来冗余 — Scene 析构后，weak_ptr 本来就锁不回去。
但显式 reset 的目的不是断引用，而是让 SceneNode 后续的判断 "我现在还挂在某个
scene 上吗" 用 `m_scene.lock() != nullptr` 就能给出确定答案，不会出现 "持有的
是 expired weak，曾经挂过但 scene 已经销毁" 这种二义状态。
*/
Scene::~Scene() {
  for (const auto &renderable : m_renderables) {
    auto node = std::dynamic_pointer_cast<SceneNode>(renderable);
    if (!node)
      continue;
    node->detachFromScene();
  }
}

SceneNode *Scene::findByPath(const std::string &path) const {
  const auto segments = splitPathSegments(path);
  if (segments.empty()) {
    return m_rootNode.get();
  }

  SceneNode *current = m_rootNode.get();
  std::vector<SceneNodeSharedPtr> candidates = getRootNodes();
  for (const auto &segment : segments) {
    SceneNode *next = nullptr;
    for (const auto &candidate : candidates) {
      if (!candidate || !matchesPathSegment(*candidate, segment)) {
        continue;
      }
      next = candidate.get();
      break;
    }
    if (!next) {
      return nullptr;
    }

    current = next;
    candidates.clear();
    for (const auto &childWeak : current->m_children) {
      if (const auto child = childWeak.lock()) {
        candidates.push_back(child);
      }
    }
  }

  return current;
}

std::vector<std::string> Scene::listAllPaths() const {
  std::vector<std::string> out;
  for (const auto &rootNode : getRootNodes()) {
    if (rootNode) {
      appendPaths(*rootNode, out);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string Scene::dumpTree() const {
  std::string out = "/\n";
  const auto rootNodes = getRootNodes();
  for (usize i = 0; i < rootNodes.size(); ++i) {
    appendTreeLines(*rootNodes[i], "", i + 1 == rootNodes.size(), out);
  }
  return out;
}

void Scene::setActiveMaterialTagForRenderables(const std::string &tag) {
  for (const auto &renderable : m_renderables) {
    auto node = std::dynamic_pointer_cast<SceneNode>(renderable);
    if (!node)
      continue;
    const auto materialComponent = node->getComponent<MaterialComponent>();
    if (!materialComponent)
      continue;
    (void)materialComponent->get().setActiveMaterialTag(tag);
    syncNodeResourceState(*node);
  }
}

/*
@source_analysis.section getSceneLevelResources：camera×target 与 light×pass
两轴筛选 REQ-009 的核心设计：camera 按 target 选，light 按 pass 选 —
两条规则有意拆开， 不合并成"同时过 pass 和 target"。原因来自身份的不同：

- camera 的身份是"画到哪个 target"，与 pass 无关。同一个 camera 在 forward、
  depth-prepass、GUI 这三个写入同一 target 的 pass 里都该出现，pipeline 不同
  但相机 UBO 是同一份。
- light 的身份是"参与哪些 pass"，与 target 无关。一个 DirectionalLight 在所有
  写入它支持的 pass 的 RenderTarget 上都该照亮，让 light 也带 target 限制会
  退化成 per-RT 复制 light 实例。

返回顺序固定：先 cameras 再 lights，各自按容器插入序追加。queue 把这一段拼在
per-renderable descriptor 列表末尾 — backend 按 binding name 命中，不依赖位置。
空返回是合法的（pass 没有任何 light 参与时常见），调用方不应该把空当作错误。
*/
DescriptorResourceList
Scene::getSceneLevelResources(StringID pass, const RenderTarget &target) const {
  DescriptorResourceList out;

  // Cameras filter by target only. A camera draws to one target; whether a
  // pass draws to that target is orthogonal to the camera's identity.
  for (const auto &cam : getCameras()) {
    if (!cam)
      continue;
    const auto cameraComponent = cam->getComponent<CameraComponent>();
    if (!cameraComponent || !cameraComponent->get().isActive())
      continue;
    if (!cameraComponent->get().matchesTarget(target))
      continue;
    const CameraHandle cameraHandle = cameraComponent->get().getCameraHandle();
    auto camUbo = m_resources.getCameraUboResource(cameraHandle);
    if (camUbo.isValid()) {
      out.emplace_back(camUbo.get());
    }
  }

  // Lights filter by pass only. A light's target scope is transitive — it
  // illuminates any surface being drawn in a pass it participates in.
  for (const LightHandle lightHandle : m_lightHandles) {
    const auto resolvedLight = m_resources.resolve(lightHandle);
    if (!resolvedLight.has_value())
      continue;
    const LightBase &light = resolvedLight->get();
    if (!light.getSceneNode())
      continue;
    if (!light.supportsPass(pass))
      continue;
    auto lightUbo = light.getUBO();
    if (lightUbo.isValid()) {
      out.emplace_back(lightUbo.get());
    }
  }
  auto sceneLights =
      m_resources.buildSceneLightsUboResource(m_lightHandles, pass);
  if (sceneLights.isValid()) {
    out.emplace_back(sceneLights.get());
  }

  return out;
}

DescriptorResourceList
Scene::getSceneLevelResources(StringID pass,
                              const CameraResource &camera) const {
  DescriptorResourceList out;
  if (camera.active) {
    auto camUbo = m_resources.buildRenderCameraUboResource(camera);
    if (camUbo.isValid()) {
      out.emplace_back(camUbo.get());
    }
  }

  for (const LightHandle lightHandle : m_lightHandles) {
    const auto resolvedLight = m_resources.resolve(lightHandle);
    if (!resolvedLight.has_value()) {
      continue;
    }
    const LightBase &light = resolvedLight->get();
    if (!light.getSceneNode()) {
      continue;
    }
    if (!light.supportsPass(pass)) {
      continue;
    }
    auto lightUbo = light.getUBO();
    if (lightUbo.isValid()) {
      out.emplace_back(lightUbo.get());
    }
  }
  auto sceneLights =
      m_resources.buildSceneLightsUboResource(m_lightHandles, pass);
  if (sceneLights.isValid()) {
    out.emplace_back(sceneLights.get());
  }
  return out;
}

CameraResource Scene::makeCameraResource(const CameraSnapshot &snapshot) {
  return makeCameraResourceFromSnapshot(snapshot);
}

/*
@source_analysis.section getCombinedCameraCullingMask：可见性裁剪与资源筛选解耦
queue 用这个合并 mask 决定 renderable 是否进入当前 queue（按位与 visibilityMask
不为 0）。它和 `getSceneLevelResources` 用的是同一条 target 过滤规则，但作用
维度完全独立：

- 资源筛选：决定 CameraUBO / LightUBO 是否进入 descriptor 表
- mask 合并：决定 renderable 是否参与 draw

两条路径解耦的结果是：即使 mask 把所有 renderable 都裁掉，CameraUBO 还是会被
绑定 — pass 的 fixed-function 阶段仍然依赖它，下一帧重新出现时 backend 不需要
重建 binding。"这一帧没东西画" 不会反向撤销 scene-level 资源契约。

合并使用按位 OR：多 camera 的 visibility 是并集语义（renderable 只要被任何一个
target 相关 camera 接受就保留），不是交集。
*/
VisibilityLayerMask
Scene::getCombinedCameraCullingMask(const RenderTarget &target) const {
  VisibilityLayerMask mask = 0;
  for (const auto &cam : getCameras()) {
    if (!cam)
      continue;
    const auto cameraComponent = cam->getComponent<CameraComponent>();
    if (!cameraComponent || !cameraComponent->get().isActive())
      continue;
    if (!cameraComponent->get().matchesTarget(target))
      continue;
    mask |= cameraComponent->get().getCullingMask();
  }
  return mask;
}

BoundingBox Scene::getPickBounds(const SceneNode &node) const {
  const BoundingBox meshBounds = node.getWorldBounds();
  if (meshBounds.isValid()) {
    return meshBounds;
  }

  const auto camera = node.getComponent<CameraComponent>();
  if (camera.has_value()) {
    return camera->get().getDebugLocalBounds().transformed(
        node.getWorldTransform());
  }

  const auto light = getLight(node);
  if (!light) {
    return {};
  }
  return light->get().getDebugLocalBounds().transformed(
      node.getWorldTransform());
}

std::optional<Scene::PickHit> Scene::pick(const Ray &ray,
                                          VisibilityLayerMask layerMask) const {
  return pick(ray, PickOptions{.layerMask = layerMask});
}

std::optional<Scene::PickHit>
Scene::pick(const Ray &ray, const PickOptions &options) const {
  std::optional<PickHit> bestHit;
  for (const auto &renderable : m_renderables) {
    const auto node = std::dynamic_pointer_cast<SceneNode>(renderable);
    if (!node) {
      continue;
    }
    if (options.excludedNode.has_value() &&
        node.get() == &options.excludedNode->get()) {
      continue;
    }
    if ((node->getVisibilityLayerMask() & options.layerMask) == 0) {
      continue;
    }

    const BoundingBox worldBounds = getPickBounds(*node);
    if (!worldBounds.isValid()) {
      continue;
    }

    const auto hitDistance = intersectRayBox(ray, worldBounds);
    if (!hitDistance.has_value()) {
      continue;
    }
    const bool isDebugOnlyBounds =
        !node->getWorldBounds().isValid() &&
        (getLight(*node) || node->getComponent<CameraComponent>());
    if (isDebugOnlyBounds && worldBounds.contains(ray.origin)) {
      continue;
    }
    if (*hitDistance <= 1e-4f && isDebugOnlyBounds) {
      continue;
    }

    if (!bestHit.has_value() || *hitDistance < bestHit->distance) {
      bestHit = PickHit{node, *hitDistance};
    }
  }
  return bestHit;
}

void Scene::appendPaths(const SceneNode &node, std::vector<std::string> &out) {
  out.push_back(node.getPath());
  for (const auto &child : node.getChildren()) {
    if (child) {
      appendPaths(*child, out);
    }
  }
}

std::vector<SceneNodeSharedPtr> Scene::getRootNodes() const {
  return m_rootNode ? m_rootNode->getChildren()
                    : std::vector<SceneNodeSharedPtr>{};
}

std::vector<std::string> Scene::splitPathSegments(const std::string &path) {
  std::string normalized = path;
  if (normalized.empty()) {
    normalized = "/";
  } else if (normalized.front() != '/') {
    normalized.insert(normalized.begin(), '/');
  }

  std::vector<std::string> segments;
  std::string current;
  for (usize i = 1; i < normalized.size(); ++i) {
    const char c = normalized[i];
    if (c == '/') {
      segments.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  if (normalized.size() > 1) {
    segments.push_back(current);
  }
  return segments;
}

bool Scene::matchesPathSegment(const SceneNode &node,
                               const std::string &pathSegment) {
  return node.getName() == pathSegment || node.getPathSegment() == pathSegment;
}

void Scene::appendTreeLines(const SceneNode &node, std::string prefix,
                            bool isLast, std::string &out) {
  out += prefix;
  out += isLast ? "└── " : "├── ";
  out += node.getPathSegment();
  out += "\n";

  std::vector<SceneNodeSharedPtr> children;
  for (const auto &childWeak : node.m_children) {
    if (const auto child = childWeak.lock()) {
      children.push_back(child);
    }
  }

  const std::string childPrefix =
      std::move(prefix) + (isLast ? "    " : "│   ");
  for (usize i = 0; i < children.size(); ++i) {
    appendTreeLines(*children[i], childPrefix, i + 1 == children.size(), out);
  }
}

void Scene::removeRenderable(const SceneNodeSharedPtr &node) {
  if (!node) {
    return;
  }

  const auto attachedScene = node->getAttachedScene();
  if (!attachedScene || attachedScene.get() != this) {
    return;
  }

  const auto renderableIt =
      std::find_if(m_renderables.begin(), m_renderables.end(),
                   [&node](const IRenderableSharedPtr &candidate) {
                     return candidate.get() == node.get();
                   });
  if (renderableIt == m_renderables.end()) {
    return;
  }

  std::vector<RemovedNodeSnapshot> removedNodes;
  collectSubtreeSnapshots(node, removedNodes);
  if (removedNodes.empty()) {
    return;
  }

  std::unordered_set<const SceneNode *> removedNodeIds;
  for (const auto &removedNode : removedNodes) {
    removedNodeIds.insert(removedNode.node.get());
  }

  m_cameraNodes.erase(
      std::remove_if(m_cameraNodes.begin(), m_cameraNodes.end(),
                     [&removedNodeIds](const std::weak_ptr<SceneNode> &weak) {
                       const auto candidate = weak.lock();
                       return !candidate ||
                              removedNodeIds.find(candidate.get()) !=
                                  removedNodeIds.end();
                     }),
      m_cameraNodes.end());

  m_renderables.erase(
      std::remove_if(m_renderables.begin(), m_renderables.end(),
                     [&removedNodeIds](const IRenderableSharedPtr &candidate) {
                       const auto candidateNode =
                           std::dynamic_pointer_cast<SceneNode>(candidate);
                       return candidateNode &&
                              removedNodeIds.find(candidateNode.get()) !=
                                  removedNodeIds.end();
                     }),
      m_renderables.end());

  std::vector<LightHandle> removedLights;
  for (auto lightIt = m_lightHandlesByNode.begin();
       lightIt != m_lightHandlesByNode.end();) {
    if (removedNodeIds.find(lightIt->first) == removedNodeIds.end()) {
      ++lightIt;
      continue;
    }
    if (auto light = m_resources.resolve(lightIt->second)) {
      light->get().detachFromSceneNode();
      removedLights.push_back(lightIt->second);
    }
    lightIt = m_lightHandlesByNode.erase(lightIt);
  }
  for (const auto &light : removedLights) {
    removeLight(light);
  }

  for (const auto &removedNode : removedNodes) {
    releaseNodeResources(*removedNode.node);
  }

  node->clearParentInternal(false);
  for (const auto &removedNode : removedNodes) {
    removedNode.node->detachFromScene();
    removedNode.node->setSceneDebugId(StringID{});
  }

  for (const auto &removedNode : removedNodes) {
    m_events.emit(SceneEvent{
        .domain = SceneEventDomain::Runtime,
        .type = SceneEventType::SceneNodeRemoved,
        .path = removedNode.lastAttachedPath,
        .stableNodeName = removedNode.stableNodeName,
    });
  }
}

void Scene::addCamera(const SceneNodeSharedPtr &cameraNode) {
  if (!cameraNode) {
    return;
  }

  if (!cameraNode->getComponent<CameraComponent>().has_value()) {
    detail::throwProgrammerLogicError(
        "Scene::addCamera requires a SceneNode with CameraComponent");
  }

  const auto exists =
      std::find_if(m_cameraNodes.begin(), m_cameraNodes.end(),
                   [&cameraNode](const std::weak_ptr<SceneNode> &weak) {
                     const auto candidate = weak.lock();
                     return !candidate || candidate.get() == cameraNode.get();
                   });
  if (exists != m_cameraNodes.end()) {
    return;
  }

  const auto renderableExists =
      std::find_if(m_renderables.begin(), m_renderables.end(),
                   [&cameraNode](const IRenderableSharedPtr &candidate) {
                     return candidate.get() == cameraNode.get();
                   });
  if (renderableExists == m_renderables.end()) {
    addRenderable(cameraNode);
  }

  auto cameraComponent = cameraNode->getComponent<CameraComponent>();
  if (cameraComponent.has_value()) {
    const CameraHandle cameraHandle =
        m_resources.registerCamera(
            LX_core::makeCameraResource(cameraComponent->get()));
    cameraComponent->get().setCameraHandle(cameraHandle);
    m_cameraHandles.push_back(cameraHandle);
  }
  m_cameraNodes.push_back(cameraNode);
}

SceneNodeSharedPtr Scene::getActiveCamera() const {
  for (const auto &cameraNode : getCameras()) {
    if (!cameraNode) {
      continue;
    }
    const auto camera = cameraNode->getComponent<CameraComponent>();
    if (camera.has_value() && camera->get().isActive()) {
      return cameraNode;
    }
  }
  return {};
}

void Scene::setActiveCamera(const SceneNodeSharedPtr &cameraNode) {
  if (cameraNode && !cameraNode->getComponent<CameraComponent>().has_value()) {
    detail::throwProgrammerLogicError(
        "Scene::setActiveCamera requires a SceneNode with CameraComponent");
  }

  for (const auto &candidate : getCameras()) {
    if (!candidate) {
      continue;
    }
    auto camera = candidate->getComponent<CameraComponent>();
    if (!camera.has_value()) {
      continue;
    }
    camera->get().setActive(cameraNode && candidate.get() == cameraNode.get());
  }
}

void Scene::removeCamera(const SceneNodeSharedPtr &cameraNode) {
  if (!cameraNode) {
    return;
  }

  const auto attachedScene = cameraNode->getAttachedScene();
  if (!attachedScene || attachedScene.get() != this) {
    return;
  }

  removeRenderable(cameraNode);
}

std::vector<SceneNodeSharedPtr> Scene::getCameras() const {
  std::vector<SceneNodeSharedPtr> cameras;
  cameras.reserve(m_cameraNodes.size());
  for (const auto &weak : m_cameraNodes) {
    if (auto camera = weak.lock()) {
      cameras.push_back(std::move(camera));
    }
  }
  return cameras;
}

void Scene::attachLight(const SceneNodeSharedPtr &node,
                        const LightBaseSharedPtr &light) {
  if (!node || !light) {
    return;
  }

  const auto attachedScene = node->getAttachedScene();
  if (!attachedScene || attachedScene.get() != this) {
    return;
  }

  LightHandle lightHandle;
  for (const LightHandle candidate : m_lightHandles) {
    const auto existing = m_resources.resolve(candidate);
    if (existing.has_value() && &existing->get() == light.get()) {
      lightHandle = candidate;
      break;
    }
  }
  if (!lightHandle.isValid()) {
    addLight(light);
    if (!m_lightHandles.empty()) {
      lightHandle = m_lightHandles.back();
    }
  }
  m_lightHandlesByNode[node.get()] = lightHandle;
  if (auto tableLight = m_resources.resolve(lightHandle)) {
    tableLight->get().attachToSceneNode(weak_from_this(), node);
  }
  node->emitRuntimeNodeChanged(SceneNodeAspect::RenderableStructure);
}

std::optional<std::reference_wrapper<LightBase>>
Scene::getLight(const SceneNode &node) {
  const auto lightIt = m_lightHandlesByNode.find(&node);
  if (lightIt == m_lightHandlesByNode.end()) {
    return std::nullopt;
  }
  return m_resources.resolve(lightIt->second);
}

std::optional<std::reference_wrapper<const LightBase>>
Scene::getLight(const SceneNode &node) const {
  const auto lightIt = m_lightHandlesByNode.find(&node);
  if (lightIt == m_lightHandlesByNode.end()) {
    return std::nullopt;
  }
  return m_resources.resolve(lightIt->second);
}

std::optional<std::reference_wrapper<DirectionalLight>>
Scene::getDirectionalLight(const SceneNode &node) {
  auto light = getLight(node);
  if (!light.has_value()) {
    return std::nullopt;
  }
  if (auto *directional = dynamic_cast<DirectionalLight *>(&light->get())) {
    return std::ref(*directional);
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<const DirectionalLight>>
Scene::getDirectionalLight(const SceneNode &node) const {
  auto light = getLight(node);
  if (!light.has_value()) {
    return std::nullopt;
  }
  if (const auto *directional =
          dynamic_cast<const DirectionalLight *>(&light->get())) {
    return std::cref(*directional);
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<PointLight>>
Scene::getPointLight(const SceneNode &node) {
  auto light = getLight(node);
  if (!light.has_value()) {
    return std::nullopt;
  }
  if (auto *point = dynamic_cast<PointLight *>(&light->get())) {
    return std::ref(*point);
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<const PointLight>>
Scene::getPointLight(const SceneNode &node) const {
  auto light = getLight(node);
  if (!light.has_value()) {
    return std::nullopt;
  }
  if (const auto *point = dynamic_cast<const PointLight *>(&light->get())) {
    return std::cref(*point);
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<SpotLight>>
Scene::getSpotLight(const SceneNode &node) {
  auto light = getLight(node);
  if (!light.has_value()) {
    return std::nullopt;
  }
  if (auto *spot = dynamic_cast<SpotLight *>(&light->get())) {
    return std::ref(*spot);
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<const SpotLight>>
Scene::getSpotLight(const SceneNode &node) const {
  auto light = getLight(node);
  if (!light.has_value()) {
    return std::nullopt;
  }
  if (const auto *spot = dynamic_cast<const SpotLight *>(&light->get())) {
    return std::cref(*spot);
  }
  return std::nullopt;
}

std::optional<LightHandle> Scene::detachLight(const SceneNodeSharedPtr &node) {
  if (!node) {
    return std::nullopt;
  }

  const auto lightIt = m_lightHandlesByNode.find(node.get());
  if (lightIt == m_lightHandlesByNode.end()) {
    return std::nullopt;
  }

  const LightHandle removedHandle = lightIt->second;
  auto light = m_resources.resolve(removedHandle);
  m_lightHandlesByNode.erase(lightIt);
  if (!light.has_value()) {
    return std::nullopt;
  }
  light->get().detachFromSceneNode();
  removeLight(removedHandle);
  node->emitRuntimeNodeChanged(SceneNodeAspect::RenderableStructure);
  return removedHandle;
}

void Scene::removeLight(const LightBase &light) {
  LightHandle removedHandle;
  for (const LightHandle candidate : m_lightHandles) {
    const auto existing = m_resources.resolve(candidate);
    if (existing.has_value() && &existing->get() == &light) {
      removedHandle = candidate;
      break;
    }
  }
  removeLight(removedHandle);
}

void Scene::removeLight(LightHandle removedHandle) {
  if (!removedHandle.isValid()) {
    return;
  }
  std::vector<SceneNodeSharedPtr> affectedNodes;
  for (auto lightIt = m_lightHandlesByNode.begin();
       lightIt != m_lightHandlesByNode.end();) {
    if (lightIt->second != removedHandle) {
      ++lightIt;
      continue;
    }
    if (const auto node =
            findRenderableNodeByAddress(m_renderables, lightIt->first)) {
      affectedNodes.push_back(node);
    }
    lightIt = m_lightHandlesByNode.erase(lightIt);
  }

  if (auto light = m_resources.resolve(removedHandle)) {
    light->get().detachFromSceneNode();
  }

  m_resources.release(removedHandle);
  m_lightHandles.erase(std::remove_if(m_lightHandles.begin(),
                                      m_lightHandles.end(),
                                      [removedHandle](LightHandle candidate) {
                                        return candidate == removedHandle;
                                      }),
                       m_lightHandles.end());

  for (const auto &node : affectedNodes) {
    node->emitRuntimeNodeChanged(SceneNodeAspect::RenderableStructure);
  }
}

std::vector<std::reference_wrapper<LightBase>> Scene::getLights() {
  std::vector<std::reference_wrapper<LightBase>> lights;
  lights.reserve(m_lightHandles.size());
  for (const LightHandle handle : m_lightHandles) {
    if (auto light = m_resources.resolve(handle)) {
      lights.push_back(std::ref(light->get()));
    }
  }
  return lights;
}

std::vector<std::reference_wrapper<const LightBase>> Scene::getLights() const {
  std::vector<std::reference_wrapper<const LightBase>> lights;
  lights.reserve(m_lightHandles.size());
  for (const LightHandle handle : m_lightHandles) {
    if (auto light = m_resources.resolve(handle)) {
      lights.push_back(std::cref(light->get()));
    }
  }
  return lights;
}

void Scene::registerNodeResources(SceneNode &node) {
  MeshHandle meshHandle;
  if (auto meshComponent = node.getComponent<MeshComponent>()) {
    const auto &mesh = meshComponent->get().getPendingMesh();
    if (mesh) {
      meshHandle = m_resources.registerMesh(mesh->cloneUnique());
      if (const auto tableMesh = m_resources.resolve(meshHandle)) {
        meshComponent->get().setGeometryStorageHandle(
            tableMesh->get().getGeometryStorageHandle());
      }
      meshComponent->get().setMeshHandle(meshHandle);
      meshComponent->get().clearPendingMesh();
    }
  }

  MaterialHandle materialHandle;
  if (auto materialComponent = node.getComponent<MaterialComponent>()) {
    materialComponent->get().forEachPendingMaterial(
        [this, &materialComponent, &materialHandle](
            const std::string &tag, const MaterialInstanceSharedPtr &material) {
          if (!material) {
            return;
          }
          const MaterialHandle handle =
              m_resources.registerMaterial(material->cloneInstanceDataUnique());
          if (tag.empty()) {
            materialComponent->get().setMaterialHandle(handle);
          } else {
            materialComponent->get().setTaggedMaterialHandle(tag, handle);
          }
          if (!materialHandle.isValid() ||
              tag == materialComponent->get().getActiveMaterialTag()) {
            materialHandle = handle;
          }
        });
    materialComponent->get().clearPendingMaterials();
    if (!materialHandle.isValid()) {
      materialHandle = materialComponent->get().getMaterialHandle();
    }
  }

  if (auto skeletonComponent = node.getComponent<SkeletonComponent>()) {
    const auto &skeleton = skeletonComponent->get().getPendingSkeleton();
    if (skeleton) {
      const SkeletonHandle skeletonHandle =
          m_resources.registerSkeleton(skeleton->cloneUnique());
      skeletonComponent->get().setSkeletonHandle(skeletonHandle);
      skeletonComponent->get().clearPendingSkeleton();
    }
  }

  if (meshHandle.isValid() && materialHandle.isValid()) {
    if (auto meshComponent = node.getComponent<MeshComponent>()) {
      meshComponent->get().setObjectHandle(m_resources.registerObject(
          makeObjectResource(node, meshHandle, materialHandle)));
    }
  }
}

void Scene::releaseNodeResources(SceneNode &node) {
  if (auto cameraComponent = node.getComponent<CameraComponent>()) {
    const CameraHandle cameraHandle = cameraComponent->get().getCameraHandle();
    if (cameraHandle.isValid()) {
      m_resources.release(cameraHandle);
      m_cameraHandles.erase(
          std::remove_if(m_cameraHandles.begin(), m_cameraHandles.end(),
                         [cameraHandle](CameraHandle candidate) {
                           return candidate == cameraHandle;
                         }),
          m_cameraHandles.end());
      cameraComponent->get().setCameraHandle({});
    }
  }

  if (auto meshComponent = node.getComponent<MeshComponent>()) {
    const ObjectHandle objectHandle = meshComponent->get().getObjectHandle();
    if (objectHandle.isValid()) {
      m_resources.release(objectHandle);
      meshComponent->get().setObjectHandle({});
    }
    const GeometryStorageHandle geometryHandle =
        meshComponent->get().getGeometryStorageHandle();
    if (geometryHandle.isValid()) {
      m_resources.release(geometryHandle);
      meshComponent->get().setGeometryStorageHandle({});
    }
    const MeshHandle meshHandle = meshComponent->get().getMeshHandle();
    if (meshHandle.isValid()) {
      m_resources.release(meshHandle);
      meshComponent->get().setMeshHandle({});
    }
  }

  if (auto materialComponent = node.getComponent<MaterialComponent>()) {
    materialComponent->get().forEachMaterialHandle(
        [this](const std::string &, MaterialHandle handle) {
          if (handle.isValid()) {
            m_resources.release(handle);
          }
        });
    materialComponent->get().setMaterialHandle({});
  }

  if (auto skeletonComponent = node.getComponent<SkeletonComponent>()) {
    const SkeletonHandle skeletonHandle =
        skeletonComponent->get().getSkeletonHandle();
    if (skeletonHandle.isValid()) {
      m_resources.release(skeletonHandle);
      skeletonComponent->get().setSkeletonHandle({});
    }
  }
}

void Scene::syncNodeResourceState(SceneNode &node) const {
  MeshHandle meshHandle;
  if (auto meshComponent = node.getComponent<MeshComponent>()) {
    meshHandle = meshComponent->get().getMeshHandle();
    const auto &pendingMesh = meshComponent->get().getPendingMesh();
    if (pendingMesh) {
      const ObjectHandle objectHandle = meshComponent->get().getObjectHandle();
      if (objectHandle.isValid()) {
        m_resources.release(objectHandle);
        meshComponent->get().setObjectHandle({});
      }
      const GeometryStorageHandle geometryHandle =
          meshComponent->get().getGeometryStorageHandle();
      if (geometryHandle.isValid()) {
        m_resources.release(geometryHandle);
        meshComponent->get().setGeometryStorageHandle({});
      }
      if (meshHandle.isValid()) {
        m_resources.release(meshHandle);
      }
      meshHandle = m_resources.registerMesh(pendingMesh->cloneUnique());
      if (const auto tableMesh = m_resources.resolve(meshHandle)) {
        meshComponent->get().setGeometryStorageHandle(
            tableMesh->get().getGeometryStorageHandle());
      }
      meshComponent->get().setMeshHandle(meshHandle);
      meshComponent->get().clearPendingMesh();
    }
  }

  if (auto materialComponent = node.getComponent<MaterialComponent>()) {
    MaterialHandle materialHandle =
        materialComponent->get().getMaterialHandle();
    bool registeredPendingMaterial = false;
    materialComponent->get().forEachPendingMaterial(
        [this, &materialComponent, &materialHandle, &registeredPendingMaterial](
            const std::string &tag, const MaterialInstanceSharedPtr &material) {
          if (!material) {
            return;
          }
          MaterialHandle oldHandle =
              tag.empty()
                  ? materialComponent->get().getMaterialHandle()
                  : materialComponent->get().getMaterialHandleForTag(tag);
          if (oldHandle.isValid()) {
            m_resources.release(oldHandle);
          }
          const MaterialHandle newHandle =
              m_resources.registerMaterial(material->cloneInstanceDataUnique());
          if (tag.empty()) {
            materialComponent->get().setMaterialHandle(newHandle);
          } else {
            materialComponent->get().setTaggedMaterialHandle(tag, newHandle);
          }
          if (tag.empty() ||
              tag == materialComponent->get().getActiveMaterialTag()) {
            materialHandle = newHandle;
          }
          registeredPendingMaterial = true;
        });
    if (registeredPendingMaterial) {
      materialComponent->get().clearPendingMaterials();
    }
  }

  if (auto skeletonComponent = node.getComponent<SkeletonComponent>()) {
    const auto &pendingSkeleton =
        skeletonComponent->get().getPendingSkeleton();
    if (pendingSkeleton) {
      const SkeletonHandle oldHandle =
          skeletonComponent->get().getSkeletonHandle();
      if (oldHandle.isValid()) {
        m_resources.release(oldHandle);
      }
      const SkeletonHandle newHandle =
          m_resources.registerSkeleton(pendingSkeleton->cloneUnique());
      skeletonComponent->get().setSkeletonHandle(newHandle);
      skeletonComponent->get().clearPendingSkeleton();
    }
  }

  if (auto meshComponent = node.getComponent<MeshComponent>()) {
    const ObjectHandle objectHandle = meshComponent->get().getObjectHandle();
    const MeshHandle meshHandle = meshComponent->get().getMeshHandle();
    MaterialHandle materialHandle;
    if (auto materialComponent = node.getComponent<MaterialComponent>()) {
      materialHandle = materialComponent->get().getMaterialHandle();
    }
    if (meshHandle.isValid() && materialHandle.isValid()) {
      if (objectHandle.isValid()) {
        m_resources.updateObject(
            objectHandle, makeObjectResource(node, meshHandle, materialHandle));
      } else {
        meshComponent->get().setObjectHandle(m_resources.registerObject(
            makeObjectResource(node, meshHandle, materialHandle)));
      }
    }
  }

  if (auto cameraComponent = node.getComponent<CameraComponent>()) {
    const CameraHandle cameraHandle = cameraComponent->get().getCameraHandle();
    if (cameraHandle.isValid()) {
      m_resources.updateCamera(
          cameraHandle, LX_core::makeCameraResource(cameraComponent->get()));
    }
  }
}

} // namespace LX_core
