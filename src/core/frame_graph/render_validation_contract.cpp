#include "core/frame_graph/render_validation_contract.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace LX_core {
namespace {

[[nodiscard]] std::string reasonForUncoveredItem(const RenderWorkItem &item) {
  if (item.kind != RenderWorkKind::RasterDraw) {
    return "item is not a raster draw";
  }
  if (!item.raster.vertexBuffer.isValid()) {
    return "raster draw has no vertex buffer";
  }
  if (!item.raster.indexBuffer.isValid()) {
    return "raster draw has no index buffer";
  }
  if (item.raster.indexCount == 0) {
    return "raster draw has zero indexCount";
  }
  if (item.raster.instanceCount == 0) {
    return "raster draw has zero instanceCount";
  }
  return "raster draw was not covered by an indirect bindless batch";
}

[[nodiscard]] bool isLegacyMaterialBinding(StringID bindingName) {
  const std::string name =
      GlobalStringTable::get().toDebugString(bindingName);
  const std::string legacyMaterialUbo = std::string("Material") + "UBO";
  return name == legacyMaterialUbo || name == "MaterialParams" ||
         name == "albedoMap" || name == "normalMap" ||
         name == "metallicRoughnessMap" || name == "aoMap" ||
         name == "emissiveMap";
}

[[nodiscard]] bool shaderConsumesBinding(const IShaderSharedPtr &shader,
                                         std::string_view name) {
  if (!shader) {
    return false;
  }
  for (const auto &binding : shader->getReflectionBindings()) {
    if (binding.name == name) {
      return true;
    }
  }
  return false;
}

void addMaterialV2Diagnostic(MaterialV2ValidationResult &result, usize itemIndex,
                             const RenderWorkItem &item,
                             StringID pass, StringID bindingName,
                             std::string reason) {
  MaterialV2ValidationDiagnostic diagnostic;
  diagnostic.itemIndex = itemIndex;
  diagnostic.pass = pass;
  diagnostic.debugId = item.debugId;
  diagnostic.objectSignature = item.objectSignature;
  diagnostic.materialSignature = item.materialSignature;
  diagnostic.bindingName = bindingName;
  diagnostic.reason = std::move(reason);
  result.diagnostics.push_back(std::move(diagnostic));
}

} // namespace

BindlessValidationResult
validateBindlessMigratedQueue(const RenderWorkQueue &queue, StringID pass) {
  BindlessValidationResult result;
  const auto &items = queue.getItems();
  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();

  std::unordered_set<usize> covered;
  for (const RenderBatch &batch : analysis.batches) {
    for (const usize candidateIndex : batch.candidateIndices) {
      if (candidateIndex < analysis.candidates.size()) {
        covered.insert(analysis.candidates[candidateIndex].inputIndex);
      }
    }
  }
  result.coveredItemCount = covered.size();

  for (const RenderBatchDiagnostic &batchDiagnostic : analysis.diagnostics) {
    BindlessValidationDiagnostic diagnostic;
    diagnostic.itemIndex = batchDiagnostic.inputIndex;
    diagnostic.pass = batchDiagnostic.pass.id == 0 ? pass : batchDiagnostic.pass;
    diagnostic.debugId = batchDiagnostic.debugId;
    diagnostic.objectSignature = batchDiagnostic.objectDataSignature;
    diagnostic.materialSignature = batchDiagnostic.materialTypeSignature;
    diagnostic.reason = "render batch analysis rejected draw input";
    result.diagnostics.push_back(std::move(diagnostic));
  }

  const auto &drawInputs = queue.nodeData().drawInputs;
  for (usize i = 0; i < drawInputs.size(); ++i) {
    const usize inputIndex = drawInputs[i].inputIndex;
    if (covered.find(inputIndex) != covered.end()) {
      continue;
    }
    const bool alreadyDiagnosed =
        std::any_of(analysis.diagnostics.begin(), analysis.diagnostics.end(),
                    [inputIndex](const RenderBatchDiagnostic &diagnostic) {
                      return diagnostic.inputIndex == inputIndex;
                    });
    if (alreadyDiagnosed) {
      continue;
    }
    BindlessValidationDiagnostic diagnostic;
    diagnostic.itemIndex = inputIndex;
    diagnostic.pass =
        analysis.context.pass.id == 0 ? pass : analysis.context.pass;
    diagnostic.debugId = drawInputs[i].debugId;
    diagnostic.objectSignature = analysis.context.objectDataSignature;
    diagnostic.materialSignature = drawInputs[i].materialTypeSignature;
    diagnostic.reason = "draw input was not covered by render batch analysis";
    result.diagnostics.push_back(std::move(diagnostic));
  }

  for (usize i = 0; i < items.size(); ++i) {
    const RenderWorkItem &item = items[i];
    BindlessValidationDiagnostic diagnostic;
    diagnostic.itemIndex = i;
    diagnostic.pass = pass;
    diagnostic.debugId = item.debugId;
    diagnostic.objectSignature = item.objectSignature;
    diagnostic.materialSignature = item.materialSignature;
    diagnostic.reason = reasonForUncoveredItem(item);
    result.diagnostics.push_back(std::move(diagnostic));
  }

  result.ok = result.diagnostics.empty();
  return result;
}

BindlessSubmissionDecision decideBindlessSubmission(
    const RenderWorkQueue &queue, StringID pass, const bool strictValidation,
    const bool migratedPass) {
  BindlessSubmissionDecision decision;
  const auto &items = queue.getItems();
  if (items.empty() && queue.nodeData().drawInputs.empty()) {
    decision.kind = BindlessSubmissionDecisionKind::Empty;
    decision.validation.ok = true;
    return decision;
  }

  decision.validation = validateBindlessMigratedQueue(queue, pass);
  if (decision.validation.ok && items.empty() &&
      decision.validation.coveredItemCount == queue.nodeData().drawInputs.size()) {
    decision.kind = BindlessSubmissionDecisionKind::BindlessBatch;
    return decision;
  }

  if (migratedPass) {
    decision.kind = BindlessSubmissionDecisionKind::StrictValidationRejected;
    return decision;
  }

  (void)strictValidation;
  decision.kind = BindlessSubmissionDecisionKind::StrictValidationRejected;
  return decision;
}

MaterialV2ValidationResult
validateMaterialV2StrictQueue(const RenderWorkQueue &queue, StringID pass) {
  MaterialV2ValidationResult result;
  const auto &items = queue.getItems();
  for (usize i = 0; i < items.size(); ++i) {
    const RenderWorkItem &item = items[i];
    if (item.kind != RenderWorkKind::RasterDraw) {
      continue;
    }

    for (const DescriptorResourceRef &resource : item.descriptorResources) {
      const StringID bindingName = resource.getBindingName();
      if (!isLegacyMaterialBinding(bindingName)) {
        continue;
      }
      addMaterialV2Diagnostic(
          result, i, item, pass, bindingName,
          "Material v2 validation forbids legacy material binding '" +
              GlobalStringTable::get().toDebugString(bindingName) + "'");
    }

    if (!item.shaderInfo) {
      addMaterialV2Diagnostic(
          result, i, item, pass, StringID("FinalShader"),
          "Material v2 validation requires a resolved final shader");
    }
    if (item.materialTypeVariant.id == 0) {
      addMaterialV2Diagnostic(
          result, i, item, pass, StringID("MaterialTypeVariant"),
          "Material v2 validation requires a final material type variant");
    }
    if (item.renderPathNodeSignature.id == 0) {
      addMaterialV2Diagnostic(
          result, i, item, pass, StringID("RenderPathNodeSignature"),
          "Material v2 validation requires a final RenderPath node signature");
    }
    if (item.pipelineKey.id.id == 0) {
      addMaterialV2Diagnostic(
          result, i, item, pass, StringID("PipelineKey"),
          "Material v2 validation requires a final pipeline key");
    }

    if (shaderConsumesBinding(item.shaderInfo, "SceneMaterials") &&
        item.raster.materialIndex == u32_max) {
      addMaterialV2Diagnostic(
          result, i, item, pass, StringID("SceneMaterials"),
          "Material v2 validation requires a typed SceneMaterials index");
    }
    if (shaderConsumesBinding(item.shaderInfo, "SceneMaterialRefs") &&
        item.raster.materialRefIndex == u32_max) {
      addMaterialV2Diagnostic(
          result, i, item, pass, StringID("SceneMaterialRefs"),
          "Material v2 validation requires a typed source material ref index");
    }
    if (item.raster.drawRecordIndex == u32_max) {
      addMaterialV2Diagnostic(
          result, i, item, pass, StringID("SceneDraws"),
          "Material v2 validation requires a typed SceneDraws index");
    }
  }

  result.ok = result.diagnostics.empty();
  return result;
}

} // namespace LX_core
