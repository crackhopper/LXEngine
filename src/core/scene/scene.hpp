#pragma once
#include "core/pipeline/pipeline_key.hpp"
#include "core/asset/shader.hpp"
#include "core/scene/camera.hpp"
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

构造时强制 seed 一个 Camera + DirectionalLight，是 REQ-009 的兜底：那些不走完整
`VulkanRenderer::initScene` 的 core/test 路径，仍然能拿到非空的 scene-level 资源。
seed 出来的 Camera 已经 `setTarget(RenderTarget{})`，否则 `matchesTarget` 永远 false，
`getSceneLevelResources` 永远返回空 — 这条隐含约束容易在写测试时被忘掉。

`enable_shared_from_this` 的存在是为了在 `addRenderable` 里给挂进来的 SceneNode 写
弱反向引用 `weak_from_this()`，让 shared material 重验证传播能从 node 找回 scene。
*/
class Scene : public std::enable_shared_from_this<Scene> {
public:
  using SharedPtr = std::shared_ptr<Scene>;

  explicit Scene(std::string sceneName)
      : m_sceneName(std::move(sceneName)), m_pathRoot(SceneNode::createPathRoot()) {
    if (m_sceneName.empty()) {
      m_sceneName = "Scene";
    }
    // REQ-009: the ctor seeds a default Camera + DirectionalLight into the
    // multi-container fields. The seeded camera is created with a default
    // RenderTarget{} so tests that don't run through VulkanRenderer::initScene
    // still see a non-empty scene-level resource list.
    auto cam = std::make_shared<Camera>();
    cam->setTarget(RenderTarget{});
    m_cameras.push_back(std::move(cam));

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
        node->setSceneDebugId(
            StringID(m_sceneName + "/" + node->getNodeName()));
        node->warnIfSiblingNameIsDuplicated();
      }
    }
    m_renderables.push_back(std::move(r));
  }

  void addCamera(CameraSharedPtr cam) { m_cameras.push_back(std::move(cam)); }
  const std::vector<CameraSharedPtr> &getCameras() const { return m_cameras; }

  void addLight(LightBaseSharedPtr light) { m_lights.push_back(std::move(light)); }
  const std::vector<LightBaseSharedPtr> &getLights() const { return m_lights; }
  const std::string &getSceneName() const { return m_sceneName; }
  SceneNode *findByPath(const std::string &path) const;
  std::string dumpTree() const;
  void revalidateNodesUsing(const MaterialInstanceSharedPtr &materialInstance);

  /// REQ-009 two-axis filter form: camera by matchesTarget(target), light by
  /// supportsPass(pass). Returns camera data resources first, then light data
  /// resources; both in their respective container insertion order. Empty
  /// return is valid.
  std::vector<IGpuResourceSharedPtr>
  getSceneLevelResources(StringID pass, const RenderTarget &target) const;
  VisibilityLayerMask getCombinedCameraCullingMask(
      const RenderTarget &target) const;

private:
  [[nodiscard]] std::vector<SceneNodeSharedPtr> getRootNodes() const;
  static std::vector<std::string> splitPathSegments(const std::string &path);
  static bool matchesPathSegment(const SceneNode &node,
                                 const std::string &pathSegment);
  static void appendTreeLines(const SceneNode &node, std::string prefix,
                              bool isLast, std::string &out);
  std::string m_sceneName;
  SceneNodeSharedPtr m_pathRoot;
  std::vector<IRenderableSharedPtr> m_renderables;
  std::vector<CameraSharedPtr> m_cameras;
  std::vector<LightBaseSharedPtr> m_lights;
};

using SceneSharedPtr = Scene::SharedPtr;
} // namespace LX_core
