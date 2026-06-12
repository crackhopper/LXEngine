#pragma once

#include "core/frame_graph/render_queue.hpp"

#include <string>
#include <vector>

namespace LX_core {

struct BindlessValidationDiagnostic final {
  usize itemIndex = 0;
  StringID pass;
  StringID debugId;
  StringID objectSignature;
  StringID materialSignature;
  std::string reason;
};

struct BindlessValidationResult final {
  bool ok = false;
  usize coveredItemCount = 0;
  std::vector<BindlessValidationDiagnostic> diagnostics;
};

enum class BindlessSubmissionDecisionKind {
  Empty,
  BindlessBatch,
  StrictValidationRejected,
};

struct BindlessSubmissionDecision final {
  BindlessSubmissionDecisionKind kind = BindlessSubmissionDecisionKind::Empty;
  BindlessValidationResult validation;
};

struct MaterialV2ValidationDiagnostic final {
  usize itemIndex = 0;
  StringID pass;
  StringID debugId;
  StringID objectSignature;
  StringID materialSignature;
  StringID bindingName;
  std::string reason;
};

struct MaterialV2ValidationResult final {
  bool ok = false;
  std::vector<MaterialV2ValidationDiagnostic> diagnostics;
};

[[nodiscard]] BindlessValidationResult
validateBindlessMigratedQueue(const RenderWorkQueue &queue, StringID pass);

[[nodiscard]] BindlessSubmissionDecision decideBindlessSubmission(
    const RenderWorkQueue &queue, StringID pass, bool strictValidation,
    bool migratedPass);

[[nodiscard]] MaterialV2ValidationResult
validateMaterialV2StrictQueue(const RenderWorkQueue &queue, StringID pass);

} // namespace LX_core
