#include "core/pipeline/pipeline_build_desc.hpp"

#include "core/asset/mesh.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace LX_core {

namespace {

/*
@source_analysis.section filterVertexLayoutToShaderInputs：让 pipeline 只声明
shader 真正读取的输入 Mesh 的 vertex layout 可能包含 shader 当前 pass
不读取的属性。pipeline 创建时如果把所有 属性都照搬进去，会让同一个 mesh 在不同
shader/pass 下的 vertex input state 过宽，也会增加 “shader 没声明但 pipeline
填了”的噪声。

这里按 shader reflection 得到的 vertex inputs 过滤 layout，只保留当前 shader
需要的 location/type。前置校验已经在调用路径做过；这里的 assert 是为了保证
`PipelineBuildDesc::fromRenderWorkItem` 只消费已经通过验证的 direct non-material
raster pass / compute work item。realtime material-source geometry 的 pipeline
lookup 由 RenderBatch + RenderPathNode context 派生，不走这个 direct payload。
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
           "PipelineBuildDesc::fromRenderWorkItem: shader input type mismatch");
    filteredItems.push_back(item);
  }

  assert(filteredItems.size() == shaderInputs.size() &&
         "PipelineBuildDesc::fromRenderWorkItem: filtered layout missing "
         "required shader inputs");
  return VertexLayout(std::move(filteredItems), layout.getStride());
}

[[nodiscard]] bool isMaterialSourceGeometryBinding(
    const ShaderResourceBinding &binding) {
  constexpr std::string_view kMaterialSourceGeometryBindings[] = {
      "SceneObjects", "SceneDraws", "SceneMaterials", "SceneMaterialRefs",
      "SceneTextures"};
  for (const std::string_view name : kMaterialSourceGeometryBindings) {
    if (binding.name == name) {
      return true;
    }
  }
  return false;
}

void validateDirectRasterHelperOnly(const RenderWorkItem &item) {
  if (item.directRaster.purpose == DirectRasterPassPurpose::Unspecified) {
    throw std::logic_error("PipelineBuildDesc::fromRenderWorkItem: "
                           "DirectRasterPass requires an explicit "
                           "non-material helper purpose");
  }

  if (item.shaderProgram.hasEnabledVariant("LX_MATERIAL_CONTRACT_SOURCE")) {
    throw std::logic_error("PipelineBuildDesc::fromRenderWorkItem: "
                           "material-source geometry must use RenderBatch, "
                           "not DirectRasterPass");
  }

  if (!item.shaderInfo) {
    return;
  }
  for (const ShaderResourceBinding &binding :
       item.shaderInfo->getReflectionBindings()) {
    if (isMaterialSourceGeometryBinding(binding)) {
      throw std::logic_error("PipelineBuildDesc::fromRenderWorkItem: "
                             "material-source geometry binding '" +
                             binding.name +
                             "' must use RenderBatch, not DirectRasterPass");
    }
  }
}

} // namespace

PipelineBuildDesc
PipelineBuildDesc::fromRenderWorkItem(const RenderWorkItem &item) {
  if (item.kind == RenderWorkKind::ComputeDispatch) {
    assert(item.shaderInfo &&
           "PipelineBuildDesc::fromRenderWorkItem: compute shaderInfo "
           "required");
    PipelineBuildDesc info;
    info.type = PipelineBuildType::Compute;
    info.key = item.pipelineKey;
    info.shaderVariantKey = item.shaderProgram.getPipelineSignature();
    info.target = item.target;
    info.renderingMode = item.renderingMode;
    info.attachments = item.attachments;
    info.stages = item.shaderInfo->getAllStages();
    info.bindings = item.shaderInfo->getReflectionBindings();
    info.pushConstant = PushConstantRange{};
    info.pushConstant.size = 0;
    info.pushConstant.stageFlagsMask =
        static_cast<ShaderStageMask32>(ShaderStage::Compute);
    return info;
  }

  if (item.kind != RenderWorkKind::DirectRasterPass) {
    throw std::logic_error("PipelineBuildDesc::fromRenderWorkItem: "
                           "unsupported helper work item kind");
  }
  validateDirectRasterHelperOnly(item);

  const auto &directRaster = item.directRaster;
  assert(item.shaderInfo &&
         "PipelineBuildDesc::fromRenderWorkItem: shaderInfo required");
  assert(directRaster.vertexBuffer.isValid() &&
         "PipelineBuildDesc::fromRenderWorkItem: vertexBuffer required");
  assert(directRaster.indexBuffer.isValid() &&
         "PipelineBuildDesc::fromRenderWorkItem: indexBuffer required");
  PipelineBuildDesc info;
  info.type = PipelineBuildType::Graphics;
  info.key = item.pipelineKey;
  info.shaderVariantKey = item.shaderProgram.getPipelineSignature();
  info.target = item.target;
  info.renderingMode = item.renderingMode;
  info.attachments = item.attachments;
  info.stages = item.shaderInfo->getAllStages();
  info.bindings = item.shaderInfo->getReflectionBindings();

  const auto *vb =
      dynamic_cast<const IVertexBuffer *>(&directRaster.vertexBuffer.get());
  assert(vb && "PipelineBuildDesc::fromRenderWorkItem: vertex buffer is not "
               "IVertexBuffer");
  info.vertexLayout =
      filterVertexLayoutToShaderInputs(vb->getLayout(), *item.shaderInfo);

  const auto *ib =
      dynamic_cast<const IndexBuffer *>(&directRaster.indexBuffer.get());
  assert(
      ib &&
      "PipelineBuildDesc::fromRenderWorkItem: index buffer is not IndexBuffer");
  info.topology = ib->getTopology();

  info.renderState = item.renderState;

  // Engine-wide push constant convention until shader-declared ranges arrive.
  info.pushConstant = PushConstantRange{};
  return info;
}

PipelineBuildDesc PipelineBuildDesc::fromRenderBatch(
    const RenderBatch &batch, const RenderPathNodeContext &context) {
  const auto factsIt = std::find_if(
      context.pipelineFacts.begin(), context.pipelineFacts.end(),
      [&batch](const RenderBatchPipelineFacts &facts) {
        return facts.materialTypeSignature == batch.materialTypeSignature;
      });
  if (factsIt == context.pipelineFacts.end()) {
    throw std::logic_error(
        "PipelineBuildDesc::fromRenderBatch: missing pipeline facts for "
        "material type signature");
  }
  if (!factsIt->shaderInfo) {
    throw std::logic_error(
        "PipelineBuildDesc::fromRenderBatch: shaderInfo required");
  }

  PipelineBuildDesc info;
  info.type = PipelineBuildType::Graphics;
  info.key = batch.derivedPipelineKey.id.id == 0
                 ? PipelineKey::build(batch.materialTypeSignature,
                                      context.renderPathNodeSignature)
                 : batch.derivedPipelineKey;
  info.shaderVariantKey = factsIt->shaderProgram.getPipelineSignature();
  info.target = context.target;
  info.renderingMode = context.renderingMode;
  info.attachments = context.attachments;
  info.stages = factsIt->shaderInfo->getAllStages();
  info.bindings = factsIt->shaderInfo->getReflectionBindings();
  info.vertexLayout = factsIt->vertexLayout;
  info.renderState = factsIt->renderState;
  info.topology = factsIt->topology;
  info.pushConstant = PushConstantRange{};
  return info;
}

} // namespace LX_core
