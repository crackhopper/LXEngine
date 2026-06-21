#include "core/frame_graph/render_path_feature_validation.hpp"

#include "core/asset/render_effect.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/rhi/image_format.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace LX_core {
namespace {

bool isTruthyBoolValue(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value == "true" || value == "1" || value == "yes" || value == "on";
}

bool forwardPassManualGammaEnabled(const RenderPathGraph &graph,
                                   const SceneResourceTable &resources) {
  const auto dependency =
      std::find_if(graph.features.begin(), graph.features.end(),
                   [](const RenderPathFeatureDependency &candidate) {
                     return candidate.slot == "forwardPass";
                   });
  if (dependency == graph.features.end()) {
    return false;
  }

  const auto featureHandle = resources.findRenderFeatureByUri(dependency->uri);
  if (!featureHandle.has_value()) {
    return false;
  }

  const auto feature = resources.resolve(*featureHandle);
  if (!feature.has_value()) {
    return false;
  }
  if (feature->get().feature != "forwardPass") {
    return false;
  }

  const auto parameter = feature->get().parameters.find("enable_gamma");
  if (parameter == feature->get().parameters.end()) {
    return false;
  }
  return isTruthyBoolValue(parameter->second.value);
}

bool frameGraphContainsForwardWrite(const FrameGraph &frameGraph,
                                    const std::string &target,
                                    bool requireSrgbAttachment) {
  const StringID forwardPass("Forward");
  const StringID targetId(target);
  const auto &declaredPasses = frameGraph.getPasses();
  return std::any_of(declaredPasses.begin(), declaredPasses.end(),
                     [&](const FramePass &pass) {
                       if (pass.name != forwardPass) {
                         return false;
                       }
                       return std::any_of(
                           pass.writes.begin(), pass.writes.end(),
                           [&](const FrameGraphWrite &write) {
                             if (write.resource.name != targetId) {
                               return false;
                             }
                             if (!requireSrgbAttachment) {
                               return true;
                             }
                             return std::any_of(
                                 pass.attachments.begin(),
                                 pass.attachments.end(),
                                 [&](const RenderPathAttachmentContract
                                         &attachment) {
                                   return !attachment.depth &&
                                          attachment.target == target &&
                                          isSrgbImageFormat(attachment.format);
                                 });
                           });
                     });
}

bool forwardWritesSrgbTarget(const RenderPathGraph &graph,
                             const FrameGraph &frameGraph) {
  if (graph.renderPath != RenderPath::Forward) {
    return false;
  }

  const auto forwardPass =
      std::find_if(graph.passes.begin(), graph.passes.end(),
                   [](const RenderPassNode &pass) {
                     return pass.id == "Forward";
                   });
  if (forwardPass == graph.passes.end()) {
    return false;
  }

  const bool graphDeclaresForwardSrgbWrite = std::any_of(
      forwardPass->attachments.begin(), forwardPass->attachments.end(),
      [&](const RenderPathAttachmentContract &attachment) {
        return !attachment.depth && isSrgbImageFormat(attachment.format) &&
               frameGraphContainsForwardWrite(frameGraph, attachment.target,
                                              false);
      });
  if (graphDeclaresForwardSrgbWrite) {
    return true;
  }

  return std::any_of(
      forwardPass->attachments.begin(), forwardPass->attachments.end(),
      [&](const RenderPathAttachmentContract &attachment) {
        return !attachment.depth &&
               frameGraphContainsForwardWrite(frameGraph, attachment.target,
                                              true);
      });
}

} // namespace

std::vector<RenderPathFeatureValidationDiagnostic>
validateRenderPathFeatureCombination(const RenderPathGraph &graph,
                                     const FrameGraph &frameGraph,
                                     const SceneResourceTable &resources) {
  std::vector<RenderPathFeatureValidationDiagnostic> diagnostics;

  if (forwardWritesSrgbTarget(graph, frameGraph) &&
      forwardPassManualGammaEnabled(graph, resources)) {
    diagnostics.push_back(RenderPathFeatureValidationDiagnostic{
        .message = "FATAL: sRGB target must not use manual gamma",
        .fatal = true,
    });
  }

  return diagnostics;
}

} // namespace LX_core
