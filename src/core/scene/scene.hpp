#pragma once
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"

namespace LX_core {

// 简化 RenderItem
struct RenderItem {
  ShaderPtr shaderInfo;

  ObjectPCPtr objectInfo;
  VertexFormat vertexFormat;
  IRenderResourcePtr vertexBuffer;
  IRenderResourcePtr indexBuffer;
  
  std::vector<IRenderResourcePtr> descriptorResources; // 材质 + skeleton 等资源
  
  ResourcePassFlag passMask;
};

// Scene 层简化示例
class Scene {
public:
  using Ptr = std::shared_ptr<Scene>;

  // 暂时只支持一个 RenderableMeshPtr
  IRenderablePtr mesh;
  CameraPtr camera;
  DirectionalLightPtr directionalLight;

  Scene(IRenderablePtr mesh) : mesh(mesh) {
    camera = std::make_shared<Camera>(ResourcePassFlag::Forward);
    directionalLight = std::make_shared<DirectionalLight>(ResourcePassFlag::Forward);
  }

  static auto create(IRenderablePtr mesh) {
    return std::make_shared<Scene>(mesh);
  }

  // 构建 RenderItem 的接口
  RenderItem buildRenderItem() {
    RenderItem item;
    item.vertexBuffer = mesh->getVertexBuffer();
    item.vertexFormat = mesh->getVertexFormat();
    item.indexBuffer = mesh->getIndexBuffer();
    item.objectInfo = mesh->getObjectInfo();
    item.descriptorResources = mesh->getDescriptorResources();
    item.shaderInfo = mesh->getShaderInfo();
    item.passMask = mesh->getPassMask();
    return item;
  }
};

using ScenePtr = Scene::Ptr;
} // namespace LX_core