#include "scene.hpp"

#include <sstream>
#include <utility>

namespace LX_core {

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
    return m_pathRoot.get();
  }

  SceneNode *current = m_pathRoot.get();
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
    if (node->getMaterialInstance() != materialInstance)
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
    if (!cam->matchesTarget(target))
      continue;
    if (auto camUbo = cam->getUBO()) {
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
    if (!cam->matchesTarget(target))
      continue;
    mask |= cam->getCullingMask();
  }
  return mask;
}

std::vector<SceneNodeSharedPtr> Scene::getRootNodes() const {
  std::vector<SceneNodeSharedPtr> roots;
  for (const auto &renderable : m_renderables) {
    const auto node = std::dynamic_pointer_cast<SceneNode>(renderable);
    if (!node || node->getParent()) {
      continue;
    }
    roots.push_back(node);
  }
  return roots;
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

} // namespace LX_core
