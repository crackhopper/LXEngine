#include "core/pipeline/pipeline_build_desc.hpp"

#include "core/asset/mesh.hpp"
#include "core/scene/scene.hpp"

#include <cassert>
#include <unordered_map>

namespace LX_core {

namespace {

/*
@source_analysis.section filterVertexLayoutToShaderInputs：让 pipeline 只声明 shader 真正读取的输入
Mesh 的 vertex layout 可能包含 shader 当前 pass 不读取的属性。pipeline 创建时如果把所有
属性都照搬进去，会让同一个 mesh 在不同 shader/pass 下的 vertex input state 过宽，也会增加
“shader 没声明但 pipeline 填了”的噪声。

这里按 shader reflection 得到的 vertex inputs 过滤 layout，只保留当前 shader 需要的
location/type。前置校验已经在 `SceneNode` 做过；这里的 assert 是为了保证
`PipelineBuildDesc::fromRenderingItem` 只消费已经通过验证的 item。
*/
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
