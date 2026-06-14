#include "core/frame_graph/render_validation_contract.hpp"

#include <string>
#include <unordered_set>

namespace LX_core {
namespace {

[[nodiscard]] std::string reasonForUncoveredItem(const RenderWorkItem &item) {
  if (item.kind != RenderWorkKind::DirectRasterPass) {
    return "item is not a direct raster pass";
  }
  if (!item.directRaster.vertexBuffer.isValid()) {
    return "direct raster pass has no vertex buffer";
  }
  if (!item.directRaster.indexBuffer.isValid()) {
    return "direct raster pass has no index buffer";
  }
  if (item.directRaster.indexCount == 0) {
    return "direct raster pass has zero indexCount";
  }
  if (item.directRaster.instanceCount == 0) {
    return "direct raster pass has zero instanceCount";
  }
  return "direct raster pass was not covered by an indirect bindless batch";
}

[[nodiscard]] std::string
reasonForBatchDiagnostic(const RenderBatchDiagnosticReason reason) {
  switch (reason) {
  case RenderBatchDiagnosticReason::ObjectDataSignatureMismatch:
    return "object-data-signature-mismatch";
  case RenderBatchDiagnosticReason::MaterialTypeSignatureMismatch:
    return "material-type-signature-mismatch";
  case RenderBatchDiagnosticReason::SourceMaterialRefUnresolved:
    return "source-material-ref-unresolved";
  case RenderBatchDiagnosticReason::ObjectDrawRecordUnresolved:
    return "object-draw-record-unresolved";
  case RenderBatchDiagnosticReason::InvalidSourceMaterialRef:
    return "invalid-source-material-ref";
  case RenderBatchDiagnosticReason::InvalidDrawRecord:
    return "invalid-draw-record";
  case RenderBatchDiagnosticReason::MissingMeshRange:
    return "missing-mesh-range";
  case RenderBatchDiagnosticReason::InvalidMeshRange:
    return "invalid-mesh-range";
  case RenderBatchDiagnosticReason::ZeroIndexCount:
    return "zero-index-count";
  case RenderBatchDiagnosticReason::ZeroInstanceCount:
    return "zero-instance-count";
  case RenderBatchDiagnosticReason::GlobalGeometryTableMissing:
    return "global-geometry-table-missing";
  case RenderBatchDiagnosticReason::BackendIndirectUnsupported:
    return "backend-indirect-unsupported";
  case RenderBatchDiagnosticReason::LegacyInputRejected:
    return "legacy-input-rejected";
  }
  return "unknown-render-batch-diagnostic";
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

void addBindlessDecisionDiagnostic(BindlessSubmissionDecision &decision,
                                   StringID pass,
                                   const RenderWorkQueue &queue,
                                   std::string reason) {
  BindlessValidationDiagnostic diagnostic;
  diagnostic.pass = pass;
  if (!queue.getItems().empty()) {
    const RenderWorkItem &item = queue.getItems().front();
    diagnostic.debugId = item.debugId;
    diagnostic.objectSignature = item.objectSignature;
    diagnostic.materialSignature = item.materialSignature;
  }
  diagnostic.reason = std::move(reason);
  decision.validation.diagnostics.push_back(std::move(diagnostic));
}

} // namespace

bool isAllowedDirectRasterHelperPurpose(const DirectRasterPassPurpose purpose,
                                        const bool allowTestOnly) {
  switch (purpose) {
  case DirectRasterPassPurpose::FullscreenPostProcess:
  case DirectRasterPassPurpose::IblBake:
  case DirectRasterPassPurpose::DebugOverlay:
    return true;
  case DirectRasterPassPurpose::TestOnlyNonMaterial:
    return allowTestOnly;
  case DirectRasterPassPurpose::Unspecified:
    return false;
  }
  return false;
}

bool isAllowedDirectRasterHelperWorkItem(const RenderWorkItem &item,
                                         const bool allowTestOnly) {
  return item.kind == RenderWorkKind::DirectRasterPass &&
         isAllowedDirectRasterHelperPurpose(item.directRaster.purpose,
                                            allowTestOnly);
}

RenderWorkQueueSubmissionClassification
classifyRenderWorkQueueSubmission(const RenderWorkQueue &queue,
                                  const bool allowTestOnly) {
  const auto &items = queue.getItems();
  const bool hasItems = !items.empty();
  const bool hasDrawInputs = !queue.nodeData().drawInputs.empty();
  if (!hasItems && !hasDrawInputs) {
    return {RenderWorkQueueSubmissionClass::Empty, {}};
  }

  if (hasItems && hasDrawInputs) {
    return {RenderWorkQueueSubmissionClass::MixedDirectHelperAndMaterialSource,
            "direct helper items cannot be mixed with material-source draw "
            "inputs"};
  }

  if (hasItems) {
    for (const RenderWorkItem &item : items) {
      if (!isAllowedDirectRasterHelperWorkItem(item, allowTestOnly)) {
        return {RenderWorkQueueSubmissionClass::InvalidDirectHelper,
                "queue contains a non-helper or disallowed direct raster work "
                "item"};
      }
    }
    return {RenderWorkQueueSubmissionClass::DirectHelper, {}};
  }

  return {RenderWorkQueueSubmissionClass::MaterialSourceBatch, {}};
}

BindlessValidationResult
validateBindlessMigratedQueue(const RenderWorkQueue &queue, StringID pass) {
  BindlessValidationResult result;
  const auto &items = queue.getItems();
  const RenderBatchAnalysis analysis = queue.compileIndirectBatches();
  const auto &drawInputs = queue.nodeData().drawInputs;

  std::unordered_set<usize> drawInputIds;
  drawInputIds.reserve(drawInputs.size());
  for (const RenderDrawInput &drawInput : drawInputs) {
    drawInputIds.insert(drawInput.inputIndex);
  }

  std::unordered_set<usize> diagnosedInputIds;
  diagnosedInputIds.reserve(analysis.diagnostics.size());

  for (const RenderBatchDiagnostic &batchDiagnostic : analysis.diagnostics) {
    BindlessValidationDiagnostic diagnostic;
    diagnostic.itemIndex = batchDiagnostic.inputIndex;
    diagnostic.pass = batchDiagnostic.pass.id == 0 ? pass : batchDiagnostic.pass;
    diagnostic.debugId = batchDiagnostic.debugId;
    diagnostic.objectSignature = batchDiagnostic.objectDataSignature;
    diagnostic.materialSignature = batchDiagnostic.materialTypeSignature;
    diagnostic.reason = reasonForBatchDiagnostic(batchDiagnostic.reason);
    result.diagnostics.push_back(std::move(diagnostic));
    diagnosedInputIds.insert(batchDiagnostic.inputIndex);
  }

  const auto addAnalysisDiagnostic =
      [&](const usize itemIndex, const StringID debugId,
          const StringID objectSignature, const StringID materialSignature,
          std::string reason) {
    BindlessValidationDiagnostic diagnostic;
    diagnostic.itemIndex = itemIndex;
    diagnostic.pass =
        analysis.context.pass.id == 0 ? pass : analysis.context.pass;
    diagnostic.debugId = debugId;
    diagnostic.objectSignature = objectSignature;
    diagnostic.materialSignature = materialSignature;
    diagnostic.reason = std::move(reason);
    result.diagnostics.push_back(std::move(diagnostic));
  };

  std::unordered_set<usize> coveredInputIds;
  coveredInputIds.reserve(analysis.candidates.size());
  for (const RenderBatch &batch : analysis.batches) {
    for (const usize candidateIndex : batch.candidateIndices) {
      if (candidateIndex >= analysis.candidates.size()) {
        addAnalysisDiagnostic(
            candidateIndex, StringID{}, batch.objectDataSignature,
            batch.materialTypeSignature,
            "render batch analysis referenced invalid candidate index");
        continue;
      }

      const PreparedRenderDrawCandidate &candidate =
          analysis.candidates[candidateIndex];
      if (drawInputIds.find(candidate.inputIndex) == drawInputIds.end()) {
        addAnalysisDiagnostic(
            candidate.inputIndex, candidate.debugId,
            candidate.objectDataSignature, candidate.materialTypeSignature,
            "render batch analysis covered unknown draw input");
        continue;
      }
      if (diagnosedInputIds.find(candidate.inputIndex) !=
          diagnosedInputIds.end()) {
        addAnalysisDiagnostic(
            candidate.inputIndex, candidate.debugId,
            candidate.objectDataSignature, candidate.materialTypeSignature,
            "render batch analysis covered a diagnosed draw input");
        continue;
      }
      if (!coveredInputIds.insert(candidate.inputIndex).second) {
        addAnalysisDiagnostic(
            candidate.inputIndex, candidate.debugId,
            candidate.objectDataSignature, candidate.materialTypeSignature,
            "render batch analysis covered a draw input more than once");
      }
    }
  }
  result.coveredItemCount = coveredInputIds.size();

  for (const RenderDrawInput &drawInput : drawInputs) {
    if (coveredInputIds.find(drawInput.inputIndex) != coveredInputIds.end()) {
      continue;
    }
    if (diagnosedInputIds.find(drawInput.inputIndex) !=
        diagnosedInputIds.end()) {
      continue;
    }
    addAnalysisDiagnostic(drawInput.inputIndex, drawInput.debugId,
                          analysis.context.objectDataSignature,
                          drawInput.materialTypeSignature,
                          "draw input was not covered by render batch "
                          "analysis");
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

  result.ok = analysis.ok() && result.diagnostics.empty();
  return result;
}

BindlessSubmissionDecision decideBindlessSubmission(
    const RenderWorkQueue &queue, StringID pass, const bool strictValidation,
    const bool migratedPass) {
  BindlessSubmissionDecision decision;
  const RenderWorkQueueSubmissionClassification classification =
      classifyRenderWorkQueueSubmission(queue);
  switch (classification.kind) {
  case RenderWorkQueueSubmissionClass::Empty:
    decision.kind = BindlessSubmissionDecisionKind::Empty;
    decision.validation.ok = true;
    return decision;
  case RenderWorkQueueSubmissionClass::DirectHelper:
    decision.kind = BindlessSubmissionDecisionKind::DirectHelper;
    decision.validation.ok = true;
    return decision;
  case RenderWorkQueueSubmissionClass::MixedDirectHelperAndMaterialSource:
  case RenderWorkQueueSubmissionClass::InvalidDirectHelper:
    decision.kind =
        BindlessSubmissionDecisionKind::StrictValidationRejected;
    decision.validation.ok = false;
    addBindlessDecisionDiagnostic(decision, pass, queue,
                                  classification.reason);
    return decision;
  case RenderWorkQueueSubmissionClass::MaterialSourceBatch:
    break;
  }

  decision.validation = validateBindlessMigratedQueue(queue, pass);
  if (decision.validation.ok && queue.getItems().empty() &&
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
    if (item.kind != RenderWorkKind::DirectRasterPass) {
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
        item.directRaster.materialIndex == u32_max) {
      addMaterialV2Diagnostic(
          result, i, item, pass, StringID("SceneMaterials"),
          "Material v2 validation requires a typed SceneMaterials index");
    }
    if (shaderConsumesBinding(item.shaderInfo, "SceneMaterialRefs") &&
        item.directRaster.materialRefIndex == u32_max) {
      addMaterialV2Diagnostic(
          result, i, item, pass, StringID("SceneMaterialRefs"),
          "Material v2 validation requires a typed source material ref index");
    }
    if (item.directRaster.drawRecordIndex == u32_max) {
      addMaterialV2Diagnostic(
          result, i, item, pass, StringID("SceneDraws"),
          "Material v2 validation requires a typed SceneDraws index");
    }
  }

  result.ok = result.diagnostics.empty();
  return result;
}

} // namespace LX_core
