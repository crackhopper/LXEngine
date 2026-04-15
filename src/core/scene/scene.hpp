#pragma once
#include "core/pipeline/pipeline_key.hpp"
#include "core/asset/shader.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/frame_graph/pass.hpp"

namespace LX_core {

using ShaderPtr = IShaderPtr;

struct RenderingItem {
  ShaderPtr shaderInfo;
  MaterialPtr material; // 材质句柄 — 用于 PipelineBuildDesc::fromRenderingItem

  ObjectPCPtr objectInfo;
  IRenderResourcePtr vertexBuffer;
  IRenderResourcePtr indexBuffer;

  std::vector<IRenderResourcePtr> descriptorResources; // 材质 + skeleton 等资源

  ResourcePassFlag passMask;
  StringID pass;
  PipelineKey pipelineKey;
};

class Scene {
public:
  using Ptr = std::shared_ptr<Scene>;

  Scene(IRenderablePtr mesh) {
    if (mesh)
      m_renderables.push_back(std::move(mesh));
    // REQ-009: the ctor seeds a default Camera + DirectionalLight into the
    // multi-container fields. The seeded camera is created with a default
    // RenderTarget{} so tests that don't run through VulkanRenderer::initScene
    // still see a non-empty scene-level resource list.
    auto cam = std::make_shared<Camera>(ResourcePassFlag::Forward);
    cam->setTarget(RenderTarget{});
    m_cameras.push_back(std::move(cam));

    m_lights.push_back(
        std::make_shared<DirectionalLight>(ResourcePassFlag::Forward));
  }

  static auto create(IRenderablePtr mesh) {
    return std::make_shared<Scene>(mesh);
  }

  const std::vector<IRenderablePtr> &getRenderables() const {
    return m_renderables;
  }

  void addRenderable(IRenderablePtr r) {
    m_renderables.push_back(std::move(r));
  }

  void addCamera(CameraPtr cam) { m_cameras.push_back(std::move(cam)); }
  const std::vector<CameraPtr> &getCameras() const { return m_cameras; }

  void addLight(LightBasePtr light) { m_lights.push_back(std::move(light)); }
  const std::vector<LightBasePtr> &getLights() const { return m_lights; }

  /// REQ-009 two-axis filter form: camera by matchesTarget(target), light by
  /// supportsPass(pass). Returns camera UBOs first, then light UBOs; both in
  /// their respective container insertion order. Empty return is valid.
  std::vector<IRenderResourcePtr>
  getSceneLevelResources(StringID pass, const RenderTarget &target) const;

private:
  std::vector<IRenderablePtr> m_renderables;
  std::vector<CameraPtr> m_cameras;
  std::vector<LightBasePtr> m_lights;
};

using ScenePtr = Scene::Ptr;
} // namespace LX_core
