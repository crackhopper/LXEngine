#include "core/pipeline/pipeline_build_desc.hpp"

#include "core/asset/mesh.hpp"
#include "core/scene/scene.hpp"

#include <cassert>

namespace LX_core {

PipelineBuildDesc
PipelineBuildDesc::fromRenderingItem(const RenderingItem &item) {
  assert(item.shaderInfo &&
         "PipelineBuildDesc::fromRenderingItem: shaderInfo required");
  assert(item.vertexBuffer &&
         "PipelineBuildDesc::fromRenderingItem: vertexBuffer required");
  assert(item.indexBuffer &&
         "PipelineBuildDesc::fromRenderingItem: indexBuffer required");
  assert(item.material &&
         "PipelineBuildDesc::fromRenderingItem: material required");

  PipelineBuildDesc info;
  info.key = item.pipelineKey;
  info.stages = item.shaderInfo->getAllStages();
  info.bindings = item.shaderInfo->getReflectionBindings();

  auto vb = std::dynamic_pointer_cast<IVertexBuffer>(item.vertexBuffer);
  assert(vb && "PipelineBuildDesc::fromRenderingItem: vertex buffer is not "
               "IVertexBuffer");
  info.vertexLayout = vb->getLayout();

  auto ib = std::dynamic_pointer_cast<IndexBuffer>(item.indexBuffer);
  assert(
      ib &&
      "PipelineBuildDesc::fromRenderingItem: index buffer is not IndexBuffer");
  info.topology = ib->getTopology();

  info.renderState = item.material->getRenderState(item.pass);

  // Engine-wide push constant convention until shader-declared ranges arrive.
  info.pushConstant = PushConstantRange{};
  return info;
}

} // namespace LX_core
