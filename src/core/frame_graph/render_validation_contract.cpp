#include "core/frame_graph/render_validation_contract.hpp"

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
  if (item.raster.drawData) {
    return "raster draw still uses per-draw drawData push constants";
  }
  if (item.raster.indexCount == 0) {
    return "raster draw has zero indexCount";
  }
  if (item.raster.instanceCount == 0) {
    return "raster draw has zero instanceCount";
  }
  return "raster draw was not covered by an indirect bindless batch";
}

} // namespace

BindlessValidationResult
validateBindlessMigratedQueue(const RenderWorkQueue &queue, StringID pass) {
  BindlessValidationResult result;
  const auto &items = queue.getItems();
  const auto batches = queue.compileIndirectBatches();

  std::unordered_set<usize> covered;
  for (const auto &batch : batches) {
    for (const usize index : batch.sourceItemIndices) {
      covered.insert(index);
    }
  }
  result.coveredItemCount = covered.size();

  for (usize i = 0; i < items.size(); ++i) {
    const RenderWorkItem &item = items[i];
    if (covered.find(i) != covered.end()) {
      continue;
    }
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
  if (items.empty()) {
    decision.kind = BindlessSubmissionDecisionKind::Empty;
    decision.validation.ok = true;
    return decision;
  }

  decision.validation = validateBindlessMigratedQueue(queue, pass);
  if (decision.validation.ok &&
      decision.validation.coveredItemCount == items.size()) {
    decision.kind = BindlessSubmissionDecisionKind::BindlessBatch;
    return decision;
  }

  if (strictValidation && migratedPass) {
    decision.kind = BindlessSubmissionDecisionKind::StrictValidationRejected;
    return decision;
  }

  decision.kind = BindlessSubmissionDecisionKind::LegacyPerItem;
  return decision;
}

} // namespace LX_core
