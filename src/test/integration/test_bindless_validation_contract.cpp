#include "core/frame_graph/render_input.hpp"
#include "core/frame_graph/render_validation_contract.hpp"

#include <iostream>
#include <vector>

using namespace LX_core;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

void testRejectedDescFailsValidationWithDiagnostic() {
  RenderInputDesc desc;
  desc.status = RenderInputStatus::Rejected;
  desc.inputIndex = 0;
  desc.pass = StringID("Forward");
  desc.debugId = StringID("draw.input.rejected");
  desc.diagnostics.push_back(RenderInputDiagnostic{
      .code = RenderInputDiagnosticCode::MissingBinding,
      .pass = desc.pass,
      .debugId = desc.debugId,
      .message = "reflected shader binding has no prepared descriptor"});
  desc.stats.compilerInputCount = 1;
  desc.stats.rejectedInputCount = 1;

  const std::vector<RenderInputDesc> descs{desc};
  const RenderInputValidationResult validation =
      validatePreparedRenderInputs(descs);

  EXPECT(descs.size() == 1, "validation should inspect one desc");
  EXPECT(!descs.front().accepted(), "desc should be rejected");
  EXPECT(!validation.ok, "rejected desc should fail validation");
  EXPECT(validation.diagnostics.size() == 1,
         "validation should surface desc diagnostics");
  EXPECT(validation.diagnostics.front().code ==
             RenderInputDiagnosticCode::MissingBinding,
         "validation should preserve diagnostic code");
  EXPECT(descs.front().stats.acceptedInputCount == 0,
         "stats should not count rejected desc as accepted");
  EXPECT(descs.front().stats.rejectedInputCount == 1,
         "stats should count rejected desc");
  EXPECT(descs.front().stats.submittedDrawCount == 0,
         "rejected desc should not count submitted draws");
  EXPECT(descs.front().stats.fallbackObservedCount == 0,
         "validation should not use fallback");
}

void testAcceptedDescWithDiagnosticFailsValidation() {
  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;
  desc.inputIndex = 0;
  desc.pass = StringID("Forward");
  desc.debugId = StringID("draw.input.accepted.with.diagnostic");
  desc.diagnostics.push_back(RenderInputDiagnostic{
      .code = RenderInputDiagnosticCode::MissingResource,
      .pass = desc.pass,
      .debugId = desc.debugId,
      .message = "accepted input still carried a binding diagnostic"});
  desc.stats.compilerInputCount = 1;
  desc.stats.acceptedInputCount = 1;

  const std::vector<RenderInputDesc> descs{desc};
  const RenderInputValidationResult validation =
      validatePreparedRenderInputs(descs);

  EXPECT(descs.front().accepted(), "desc setup should be accepted");
  EXPECT(!validation.ok,
         "any desc diagnostic should fail validation even when accepted");
  EXPECT(validation.diagnostics.size() == 1,
         "validation should preserve accepted desc diagnostic");
  EXPECT(validation.diagnostics.front().message ==
             "accepted input still carried a binding diagnostic",
         "validation should preserve diagnostic message");
}

} // namespace

int main() {
  testRejectedDescFailsValidationWithDiagnostic();
  testAcceptedDescWithDiagnosticFailsValidation();
  if (g_failures != 0) {
    std::cerr << g_failures << " bindless validation contract checks failed\n";
    return 1;
  }
  return 0;
}
