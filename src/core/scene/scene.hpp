#pragma once
#include "core/pipeline/pipeline_key.hpp"
#include "core/asset/shader.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/frame_graph/pass.hpp"
#include <exception>
#include <iostream>
#include <string>

namespace LX_core {

using ShaderPtr = IShaderPtr;

struct RenderingItem {
  ShaderPtr shaderInfo;
  MaterialInstancePtr material; // 材质句柄 — 用于 PipelineBuildDesc::fromRenderingItem

  PerDrawDataPtr drawData;
  IGpuResourcePtr vertexBuffer;
  IGpuResourcePtr indexBuffer;

  std::vector<IGpuResourcePtr> descriptorResources; // 材质 + skeleton 等资源

  StringID pass;
  PipelineKey pipelineKey;
};

class Scene {
public:
  using Ptr = std::shared_ptr<Scene>;

  Scene(std::string sceneName, IRenderablePtr mesh = nullptr)
      : m_sceneName(std::move(sceneName)) {
    if (m_sceneName.empty()) {
      m_sceneName = "Scene";
    }
    if (mesh)
      addRenderable(std::move(mesh));
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

  static auto create(std::string sceneName, IRenderablePtr mesh = nullptr) {
    return std::make_shared<Scene>(std::move(sceneName), std::move(mesh));
  }

  static auto create(IRenderablePtr mesh) {
    return std::make_shared<Scene>("Scene", std::move(mesh));
  }

  static auto create(std::nullptr_t) {
    return std::make_shared<Scene>("Scene", nullptr);
  }

  const std::vector<IRenderablePtr> &getRenderables() const {
    return m_renderables;
  }

  void addRenderable(IRenderablePtr r) {
    if (r) {
      for (const auto &existing : m_renderables) {
        if (!existing)
          continue;
        if (existing->getNodeName() == r->getNodeName()) {
          std::cerr << "FATAL [Scene] duplicate nodeName in scene '"
                    << m_sceneName << "': " << r->getNodeName() << std::endl;
          std::terminate();
        }
      }
      if (auto node = std::dynamic_pointer_cast<SceneNode>(r)) {
        node->attachToScene(this);
        node->setSceneDebugId(
            StringID(m_sceneName + "/" + node->getNodeName()));
      }
    }
    m_renderables.push_back(std::move(r));
  }

  void addCamera(CameraPtr cam) { m_cameras.push_back(std::move(cam)); }
  const std::vector<CameraPtr> &getCameras() const { return m_cameras; }

  void addLight(LightBasePtr light) { m_lights.push_back(std::move(light)); }
  const std::vector<LightBasePtr> &getLights() const { return m_lights; }
  const std::string &getSceneName() const { return m_sceneName; }
  void revalidateNodesUsing(const MaterialInstancePtr &materialInstance);

  /// REQ-009 two-axis filter form: camera by matchesTarget(target), light by
  /// supportsPass(pass). Returns camera data resources first, then light data
  /// resources; both in their respective container insertion order. Empty
  /// return is valid.
  std::vector<IGpuResourcePtr>
  getSceneLevelResources(StringID pass, const RenderTarget &target) const;

private:
  std::string m_sceneName;
  std::vector<IRenderablePtr> m_renderables;
  std::vector<CameraPtr> m_cameras;
  std::vector<LightBasePtr> m_lights;
};

using ScenePtr = Scene::Ptr;
} // namespace LX_core
