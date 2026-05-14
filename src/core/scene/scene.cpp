#include "scene.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"

#include <algorithm>
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
    const std::vector<IRenderableSharedPtr> &renderables, const SceneNode *node) {
  if (!node) {
    return nullptr;
  }
  for (const auto &renderable : renderables) {
    const auto renderableNode = std::dynamic_pointer_cast<SceneNode>(renderable);
    if (renderableNode && renderableNode.get() == node) {
      return renderableNode;
    }
  }
  return nullptr;
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

/*
@source_analysis.section revalidateNodesUsing：shared material 的结构性传播
多个 SceneNode 可以共享同一个 `MaterialInstance`。当材质本身的 pass 启用集合
（`setPassEnabled`）改变时，每个引用它的节点都需要重建 validated cache，因为
`supportsPass` 的结果会变。这条信号节点自己感知不到 — 节点不订阅材质事件，
所以由 Scene 在材质回调里集中遍历，按指针相等而不是 by-name 比较来匹配，
避免误伤同名不同实例的材质。

普通参数写入（`setFloat` / `setTexture`）走 GPU 资源 dirty 路径，结构没变，
不会触发这条传播。换句话说：这里只处理"pass 拓扑改变"这一件结构性事件。
*/
void Scene::revalidateNodesUsing(const MaterialInstanceSharedPtr &materialInstance) {
  if (!materialInstance)
    return;
  for (const auto &renderable : m_renderables) {
    auto node = std::dynamic_pointer_cast<SceneNode>(renderable);
    if (!node)
      continue;
    const auto materialComponent = node->getComponent<MaterialComponent>();
    if (!materialComponent ||
        materialComponent->get().getMaterialInstance() != materialInstance)
      continue;
    node->rebuildValidatedCache();
  }
}

/*
@source_analysis.section getSceneLevelResources：camera×target 与 light×pass 两轴筛选
REQ-009 的核心设计：camera 按 target 选，light 按 pass 选 — 两条规则有意拆开，
不合并成"同时过 pass 和 target"。原因来自身份的不同：

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
std::vector<IGpuResourceSharedPtr>
Scene::getSceneLevelResources(StringID pass, const RenderTarget &target) const {
  std::vector<IGpuResourceSharedPtr> out;

  // Cameras filter by target only. A camera draws to one target; whether a
  // pass draws to that target is orthogonal to the camera's identity.
  for (const auto &cam : m_cameras) {
    if (!cam)
      continue;
    const auto cameraComponent = cam->getComponent<CameraComponent>();
    if (!cameraComponent || !cameraComponent->get().isActive())
      continue;
    if (!cameraComponent->get().matchesTarget(target))
      continue;
    if (auto camUbo = cameraComponent->get().getUBO()) {
      out.push_back(std::dynamic_pointer_cast<IGpuResource>(camUbo));
    }
  }

  // Lights filter by pass only. A light's target scope is transitive — it
  // illuminates any surface being drawn in a pass it participates in.
  for (const auto &light : m_lights) {
    if (!light)
      continue;
    if (!light->supportsPass(pass))
      continue;
    if (auto lightUbo = light->getUBO()) {
      out.push_back(lightUbo);
    }
  }

  return out;
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
  for (const auto &cam : m_cameras) {
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
  return light->getDebugLocalBounds().transformed(node.getWorldTransform());
}

std::optional<Scene::PickHit>
Scene::pick(const Ray &ray, VisibilityLayerMask layerMask) const {
  std::optional<PickHit> bestHit;
  for (const auto &renderable : m_renderables) {
    const auto node = std::dynamic_pointer_cast<SceneNode>(renderable);
    if (!node) {
      continue;
    }
    if ((node->getVisibilityLayerMask() & layerMask) == 0) {
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
    if (*hitDistance <= 1e-4f && getLight(*node) &&
        !node->getWorldBounds().isValid()) {
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

  m_cameras.erase(
      std::remove_if(m_cameras.begin(), m_cameras.end(),
                     [&removedNodeIds](const SceneNodeSharedPtr &candidate) {
                       return candidate &&
                              removedNodeIds.find(candidate.get()) !=
                                  removedNodeIds.end();
                     }),
      m_cameras.end());

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

  std::vector<LightBaseSharedPtr> removedLights;
  for (auto lightIt = m_lightsByNode.begin(); lightIt != m_lightsByNode.end();) {
    if (removedNodeIds.find(lightIt->first) == removedNodeIds.end()) {
      ++lightIt;
      continue;
    }
    if (lightIt->second) {
      if (const auto directionalLight =
              std::dynamic_pointer_cast<DirectionalLight>(lightIt->second)) {
        directionalLight->detachFromSceneNode();
      }
      removedLights.push_back(lightIt->second);
    }
    lightIt = m_lightsByNode.erase(lightIt);
  }
  for (const auto &light : removedLights) {
    removeLight(light);
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

  const auto exists = std::find_if(
      m_cameras.begin(), m_cameras.end(),
      [&cameraNode](const SceneNodeSharedPtr &candidate) {
        return candidate.get() == cameraNode.get();
      });
  if (exists != m_cameras.end()) {
    return;
  }

  const auto renderableExists = std::find_if(
      m_renderables.begin(), m_renderables.end(),
      [&cameraNode](const IRenderableSharedPtr &candidate) {
        return candidate.get() == cameraNode.get();
      });
  if (renderableExists == m_renderables.end()) {
    addRenderable(cameraNode);
  }

  m_cameras.push_back(cameraNode);
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

void Scene::attachLight(const SceneNodeSharedPtr &node,
                        const LightBaseSharedPtr &light) {
  if (!node || !light) {
    return;
  }

  const auto attachedScene = node->getAttachedScene();
  if (!attachedScene || attachedScene.get() != this) {
    return;
  }

  if (std::find(m_lights.begin(), m_lights.end(), light) == m_lights.end()) {
    addLight(light);
  }
  m_lightsByNode[node.get()] = light;
  if (const auto directionalLight =
          std::dynamic_pointer_cast<DirectionalLight>(light)) {
    directionalLight->attachToSceneNode(weak_from_this(), node);
  }
  node->emitRuntimeNodeChanged(SceneNodeAspect::RenderableStructure);
}

LightBaseSharedPtr Scene::getLight(const SceneNode &node) const {
  const auto lightIt = m_lightsByNode.find(&node);
  if (lightIt == m_lightsByNode.end()) {
    return nullptr;
  }
  return lightIt->second;
}

DirectionalLightSharedPtr Scene::getDirectionalLight(const SceneNode &node) const {
  return std::dynamic_pointer_cast<DirectionalLight>(getLight(node));
}

LightBaseSharedPtr Scene::detachLight(const SceneNodeSharedPtr &node) {
  if (!node) {
    return nullptr;
  }

  const auto lightIt = m_lightsByNode.find(node.get());
  if (lightIt == m_lightsByNode.end()) {
    return nullptr;
  }

  LightBaseSharedPtr light = lightIt->second;
  m_lightsByNode.erase(lightIt);
  if (const auto directionalLight = std::dynamic_pointer_cast<DirectionalLight>(light)) {
    directionalLight->detachFromSceneNode();
  }
  removeLight(light);
  node->emitRuntimeNodeChanged(SceneNodeAspect::RenderableStructure);
  return light;
}

void Scene::removeLight(const LightBaseSharedPtr &light) {
  if (!light) {
    return;
  }

  std::vector<SceneNodeSharedPtr> affectedNodes;
  for (auto lightIt = m_lightsByNode.begin(); lightIt != m_lightsByNode.end();) {
    if (lightIt->second != light) {
      ++lightIt;
      continue;
    }
    if (const auto node = findRenderableNodeByAddress(m_renderables, lightIt->first)) {
      affectedNodes.push_back(node);
    }
    lightIt = m_lightsByNode.erase(lightIt);
  }

  if (const auto directionalLight = std::dynamic_pointer_cast<DirectionalLight>(light)) {
    directionalLight->detachFromSceneNode();
  }

  m_lights.erase(
      std::remove_if(m_lights.begin(), m_lights.end(),
                     [&light](const LightBaseSharedPtr &candidate) {
                       return candidate == light;
                     }),
      m_lights.end());

  for (const auto &node : affectedNodes) {
    node->emitRuntimeNodeChanged(SceneNodeAspect::RenderableStructure);
  }
}

} // namespace LX_core
