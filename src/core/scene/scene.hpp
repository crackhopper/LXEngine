#pragma once
#include "core/pipeline/pipeline_key.hpp"
#include "core/asset/shader.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/frame_graph/pass.hpp"
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace LX_core {

namespace detail {

[[noreturn]] inline void throwProgrammerLogicError(const std::string &message) {
  throw std::logic_error(message);
}

} // namespace detail

using ShaderPtr = IShaderSharedPtr;

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

class Scene : public std::enable_shared_from_this<Scene> {
public:
  using SharedPtr = std::shared_ptr<Scene>;

  explicit Scene(std::string sceneName) : m_sceneName(std::move(sceneName)) {
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
      }
    }
    m_renderables.push_back(std::move(r));
  }

  void addCamera(CameraSharedPtr cam) { m_cameras.push_back(std::move(cam)); }
  const std::vector<CameraSharedPtr> &getCameras() const { return m_cameras; }

  void addLight(LightBaseSharedPtr light) { m_lights.push_back(std::move(light)); }
  const std::vector<LightBaseSharedPtr> &getLights() const { return m_lights; }
  const std::string &getSceneName() const { return m_sceneName; }
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
  std::string m_sceneName;
  std::vector<IRenderableSharedPtr> m_renderables;
  std::vector<CameraSharedPtr> m_cameras;
  std::vector<LightBaseSharedPtr> m_lights;
};

using SceneSharedPtr = Scene::SharedPtr;
} // namespace LX_core
