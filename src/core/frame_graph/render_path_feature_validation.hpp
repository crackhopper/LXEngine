#pragma once

#include <string>
#include <vector>

namespace LX_core {

class FrameGraph;
class SceneResourceTable;
struct RenderPathGraph;

struct RenderPathFeatureValidationDiagnostic final {
  std::string message;
  bool fatal = true;
};

[[nodiscard]] std::vector<RenderPathFeatureValidationDiagnostic>
validateRenderPathFeatureCombination(const RenderPathGraph &graph,
                                     const FrameGraph &frameGraph,
                                     const SceneResourceTable &resources);

} // namespace LX_core
