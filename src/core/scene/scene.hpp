#pragma once
#include "core/resources/pipeline_key.hpp"
#include "core/resources/shader.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/pass.hpp"

namespace LX_core {

using ShaderPtr = IShaderPtr;

// 简化 RenderingItem
struct RenderingItem {
  ShaderPtr shaderInfo;
  MaterialPtr material; // 材质句柄 — 用于 PipelineBuildInfo::fromRenderingItem

  ObjectPCPtr objectInfo;
  IRenderResourcePtr vertexBuffer;
  IRenderResourcePtr indexBuffer;

  std::vector<IRenderResourcePtr> descriptorResources; // 材质 + skeleton 等资源

  ResourcePassFlag passMask;
  StringID pass;
  PipelineKey pipelineKey;
};

// Scene 层简化示例
class Scene {
public:
  using Ptr = std::shared_ptr<Scene>;

  CameraPtr camera;
  DirectionalLightPtr directionalLight;

  Scene(IRenderablePtr mesh) {
    if (mesh)
      m_renderables.push_back(std::move(mesh));
    camera = std::make_shared<Camera>(ResourcePassFlag::Forward);
    directionalLight =
        std::make_shared<DirectionalLight>(ResourcePassFlag::Forward);
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

  /// 返回场景级 descriptor 资源：camera UBO、directional light UBO。
  /// 由 RenderQueue::buildFromScene 在构造每个 RenderingItem 时追加合并到
  /// item.descriptorResources 末尾，替代 VulkanRenderer::initScene 里原来的
  /// 手工 push_back。顺序：camera 先、light 后，保持 descriptor binding 稳定。
  ///
  /// 本版本**无参**：单 camera / 单 light 假设下，所有资源一视同仁地合并到
  /// 所有 item。REQ-009 会扩展为 getSceneLevelResources(pass, target) 的过滤
  /// 版本。
  std::vector<IRenderResourcePtr> getSceneLevelResources() const;

private:
  std::vector<IRenderablePtr> m_renderables;
};

using ScenePtr = Scene::Ptr;
} // namespace LX_core
