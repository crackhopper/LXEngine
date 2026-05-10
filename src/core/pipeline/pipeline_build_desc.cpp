#include "core/pipeline/pipeline_build_desc.hpp"

#include "core/asset/mesh.hpp"
#include "core/scene/scene.hpp"

#include <cassert>
#include <unordered_map>

namespace LX_core {

namespace {

VertexLayout filterVertexLayoutToShaderInputs(const VertexLayout &layout,
                                              const IShader &shader) {
  const auto &shaderInputs = shader.getVertexInputs();
  if (shaderInputs.empty()) {
    return layout;
  }

  std::unordered_map<u32, DataType> requiredInputs;
  requiredInputs.reserve(shaderInputs.size());
  for (const auto &input : shaderInputs) {
    requiredInputs.emplace(input.location, input.type);
  }

  std::vector<VertexLayoutItem> filteredItems;
  filteredItems.reserve(shaderInputs.size());
  for (const auto &item : layout.getItems()) {
    auto it = requiredInputs.find(item.location);
    if (it == requiredInputs.end()) {
      continue;
    }
    assert(it->second == item.type &&
           "PipelineBuildDesc::fromRenderingItem: shader input type mismatch");
    filteredItems.push_back(item);
  }

  assert(filteredItems.size() == shaderInputs.size() &&
         "PipelineBuildDesc::fromRenderingItem: filtered layout missing "
         "required shader inputs");
  return VertexLayout(std::move(filteredItems), layout.getStride());
}

} // namespace

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
  info.vertexLayout = filterVertexLayoutToShaderInputs(vb->getLayout(),
                                                       *item.shaderInfo);

  auto ib = std::dynamic_pointer_cast<IndexBuffer>(item.indexBuffer);
  assert(
      ib &&
      "PipelineBuildDesc::fromRenderingItem: index buffer is not IndexBuffer");
  info.topology = ib->getTopology();

  info.renderState = item.material->getPassRenderState(item.pass);

  // Engine-wide push constant convention until shader-declared ranges arrive.
  info.pushConstant = PushConstantRange{};
  return info;
}

} // namespace LX_core
