#include "scene.hpp"
#include "core/resources/mesh.hpp"

namespace LX_core {

RenderingItem Scene::buildRenderingItem() {
  RenderingItem item;
  item.vertexBuffer = mesh->getVertexBuffer();
  item.indexBuffer = mesh->getIndexBuffer();
  item.objectInfo = mesh->getObjectInfo();
  item.descriptorResources = mesh->getDescriptorResources();
  item.shaderInfo = mesh->getShaderInfo();
  item.passMask = mesh->getPassMask();

  auto sub = std::dynamic_pointer_cast<RenderableSubMesh>(mesh);
  if (sub && sub->mesh && sub->material) {
    item.pipelineKey = PipelineKey::build(
        sub->material->getShaderProgramSet(), sub->mesh->getPipelineHash(),
        sub->material->getRenderState(), sub->mesh->getPrimitiveTopology(),
        sub->skeleton.has_value());
  }
  return item;
}

} // namespace LX_core
