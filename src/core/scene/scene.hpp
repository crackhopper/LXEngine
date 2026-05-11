#pragma once
#include "core/pipeline/pipeline_key.hpp"
#include "core/asset/shader.hpp"
#include "core/math/ray.hpp"
#include "core/scene/scene_events.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/frame_graph/pass.hpp"
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace LX_core {

namespace detail {

[[noreturn]] inline void throwProgrammerLogicError(const std::string &message) {
  throw std::logic_error(message);
}

} // namespace detail

using ShaderPtr = IShaderSharedPtr;

/*
@source_analysis.section RenderingItem：一帧 draw 的最小稳定记录
这个结构体定义在 scene.hpp 而不是 queue.hpp，是因为它描述的是 backend 真正消费的契约，
而不是 queue 的内部状态。任何把"一个 renderable 在某个 pass 下要画一次"翻译成
"backend 提交单元"的代码路径，都收口到这个结构体上。

字段拆分体现两个边界：

- `shaderInfo / pipelineKey / pass`：决定走哪条 pipeline，是 pipeline cache 的 key 来源
- `vertexBuffer / indexBuffer / drawData / descriptorResources`：决定这次 draw 的数据来源
- `material`：保留材质句柄是为了 `PipelineBuildDesc::fromRenderingItem` 反查 render state
  和 owned binding 表，而不是 backend 直接读它

descriptorResources 的列表已经合并了"renderable 自带"和"scene-level 追加"两段，
顺序固定 — backend 按 binding name 命中，不依赖位置。
*/
struct RenderingItem {
  ShaderPtr shaderInfo;
  MaterialInstanceSharedPtr material; // 材质句柄 — 用于 PipelineBuildDesc::fromRenderingItem

  PerDrawDataSharedPtr drawData;
  IGpuResourceSharedPtr vertexBuffer;
  IGpuResourceSharedPtr indexBuffer;

  std::vector<IGpuResourceSharedPtr> descriptorResources; // 材质 + skeleton 等资源

  StringID pass;
  PipelineKey pipelineKey;
};

/*
@source_analysis.section Scene：扁平容器与默认 seed
Scene 是一层薄壳：三个平铺 vector（renderables / cameras / lights）+ 一个 sceneName。
它不维护层级（节点之间的 parent/child 关系挂在 SceneNode 上）、不做 z-sort、不持有
render state。这种扁平 ownership 让"哪些对象属于这一帧"是可枚举的事实，而不是
需要遍历某种隐式树才能复原的状态。

构造时仍然 seed 一个 DirectionalLight，方便那些不走完整 renderer 初始化的测试路径。
camera 不再单独 seed；测试和 demo 需要显式注册 camera-bearing SceneNode。

`enable_shared_from_this` 的存在是为了在 `addRenderable` 里给挂进来的 SceneNode 写
弱反向引用 `weak_from_this()`，让 shared material 重验证传播能从 node 找回 scene。
*/
class Scene : public std::enable_shared_from_this<Scene> {
public:
  using SharedPtr = std::shared_ptr<Scene>;

  explicit Scene(std::string sceneName)
      : m_sceneName(std::move(sceneName)), m_rootNode(SceneNode::createPathRoot()) {
    if (m_sceneName.empty()) {
      m_sceneName = "Scene";
    }
    m_lights.push_back(std::make_shared<DirectionalLight>());
  }
  ~Scene();

  static auto create(std::string sceneName, IRenderableSharedPtr mesh = nullptr) {
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
    if (r) {
      for (const auto &existing : m_renderables) {
        if (!existing)
          continue;
        if (existing->getNodeName() == r->getNodeName()) {
          detail::throwProgrammerLogicError("Scene duplicate nodeName in scene '" +
                                            m_sceneName + "': " +
                                            r->getNodeName());
        }
      }
      if (auto node = std::dynamic_pointer_cast<SceneNode>(r)) {
        node->attachToScene(weak_from_this());
        if (!node->getParent()) {
          node->setParent(m_rootNode);
        }
        node->setSceneDebugId(
            StringID(m_sceneName + "/" + node->getNodeName()));
        node->warnIfSiblingNameIsDuplicated();
      }
    }
    m_renderables.push_back(std::move(r));
  }

  void removeRenderable(const SceneNodeSharedPtr &node);

  void addCamera(const SceneNodeSharedPtr &cameraNode);
  void removeCamera(const SceneNodeSharedPtr &cameraNode);
  const std::vector<SceneNodeSharedPtr> &getCameras() const { return m_cameras; }

  void addLight(LightBaseSharedPtr light) { m_lights.push_back(std::move(light)); }
  void removeLight(const LightBaseSharedPtr &light);
  const std::vector<LightBaseSharedPtr> &getLights() const { return m_lights; }
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

  /// REQ-009 two-axis filter form: camera by matchesTarget(target), light by
  /// supportsPass(pass). Returns camera data resources first, then light data
  /// resources; both in their respective container insertion order. Empty
  /// return is valid.
  std::vector<IGpuResourceSharedPtr>
  getSceneLevelResources(StringID pass, const RenderTarget &target) const;
  VisibilityLayerMask getCombinedCameraCullingMask(
      const RenderTarget &target) const;
  std::optional<PickHit> pick(
      const Ray &ray,
      VisibilityLayerMask layerMask = VisibilityMask_All) const;
  [[nodiscard]] const SceneNodeSharedPtr &getRootNode() const { return m_rootNode; }
  [[nodiscard]] std::vector<SceneNodeSharedPtr> getRootNodes() const;

private:
  static std::vector<std::string> splitPathSegments(const std::string &path);
  static bool matchesPathSegment(const SceneNode &node,
                                 const std::string &pathSegment);
  static void appendPaths(const SceneNode &node, std::vector<std::string> &out);
  static void appendTreeLines(const SceneNode &node, std::string prefix,
                              bool isLast, std::string &out);
  std::string m_sceneName;
  SceneNodeSharedPtr m_rootNode;
  std::vector<IRenderableSharedPtr> m_renderables;
  std::vector<SceneNodeSharedPtr> m_cameras;
  std::vector<LightBaseSharedPtr> m_lights;
  SceneEventHub m_events;
};

using SceneSharedPtr = Scene::SharedPtr;
} // namespace LX_core
