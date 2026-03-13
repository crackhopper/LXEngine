#pragma once
#include "camera.hpp"
#include "light.hpp"
#include "object.hpp"

namespace LX_core {

// 简化 RenderItem
struct RenderItem {
  IRenderResourcePtr vertexBuffer;
  VertexFormat vertexFormat;
  IRenderResourcePtr indexBuffer;
  IRenderResourcePtr pushConstant;
  std::vector<IRenderResourcePtr> descriptorResources; // 材质 + skeleton 等资源
  ShaderPtr vertexShader;
  ShaderPtr fragmentShader;
  ResourcePassFlag passMask;
};

// Scene 层简化示例
class Scene {
public:
  // 暂时只支持一个 RenderableMeshPtr
  IRenderablePtr mesh;
  CameraPtr camera;
  DirectionalLightPtr directionalLight;

  Scene() = default;

  // 构建 RenderItem 的接口
  RenderItem buildRenderItem() {
    RenderItem item;
    item.vertexBuffer = mesh->getVertexBuffer();
    item.vertexFormat = mesh->getVertexFormat();
    item.indexBuffer = mesh->getIndexBuffer();
    item.pushConstant = mesh->getPushConstant();
    item.descriptorResources = mesh->getDescriptorResources();
    item.vertexShader = mesh->getVertexShader();
    item.fragmentShader = mesh->getFragmentShader();
    item.passMask = mesh->getPassMask();
    return item;
  }
};

using ScenePtr = std::shared_ptr<Scene>;
} // namespace LX_core