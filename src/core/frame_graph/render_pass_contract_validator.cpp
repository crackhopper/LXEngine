#include "core/frame_graph/render_pass_contract_validator.hpp"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LX_core {
namespace {

bool allowsMultipleProducers(const RenderPassNode &pass,
                             const GraphResourceRegistry &registry,
                             const std::string &target) {
  return pass.writeMode.has_value() &&
         registry.allowsWriteMode(target, *pass.writeMode);
}

const char *stageName(const RenderPassStage stage) {
  switch (stage) {
  case RenderPassStage::Raster:
    return "raster";
  case RenderPassStage::Compute:
    return "compute";
  }
  return "unknown";
}

const char *dispatchName(const RenderPassDispatch dispatch) {
  switch (dispatch) {
  case RenderPassDispatch::Draw:
    return "draw";
  case RenderPassDispatch::Fullscreen:
    return "fullscreen";
  case RenderPassDispatch::Compute:
    return "compute";
  }
  return "unknown";
}

bool supportsStageDispatch(const RenderPassNode &pass) {
  switch (pass.stage) {
  case RenderPassStage::Raster:
    return pass.dispatch == RenderPassDispatch::Draw ||
           pass.dispatch == RenderPassDispatch::Fullscreen;
  case RenderPassStage::Compute:
    return pass.dispatch == RenderPassDispatch::Compute;
  }
  return false;
}

struct TargetProducer final {
  std::string passName;
  std::optional<std::string> writeMode;
};

} // namespace

RenderPassContractValidationReport
validateRenderPassContractResources(const RenderPathGraph &graph,
                                    const GraphResourceRegistry &registry) {
  RenderPassContractValidationReport report;
  std::unordered_map<std::string, std::vector<TargetProducer>>
      producersByTarget;

  for (const RenderPassNode &pass : graph.passes) {
    if (pass.shaderUri.empty()) {
      report.diagnostics.push_back("RenderPathGraph '" + graph.name +
                                   "' pass '" + pass.id +
                                   "' missing shaderUri");
    }
    if (!supportsStageDispatch(pass)) {
      report.diagnostics.push_back(
          "RenderPathGraph '" + graph.name + "' pass '" + pass.id +
          "' unsupported dispatch '" + dispatchName(pass.dispatch) +
          "' for stage '" + stageName(pass.stage) + "'");
    }

    std::unordered_set<std::string> targetsInPass;
    for (const std::string &target : pass.targets) {
      if (!registry.contains(target)) {
        report.diagnostics.push_back("RenderPathGraph '" + graph.name +
                                     "' pass '" + pass.id +
                                     "' unknown target '" + target + "'");
        continue;
      }
      if (registry.isImported(target)) {
        report.diagnostics.push_back(
            "RenderPathGraph '" + graph.name + "' pass '" + pass.id +
            "' imported target '" + target + "' is source-only");
        continue;
      }
      if (!targetsInPass.insert(target).second) {
        report.diagnostics.push_back("RenderPathGraph '" + graph.name +
                                     "' pass '" + pass.id +
                                     "' duplicate target '" + target + "'");
        continue;
      }

      const bool writeModeAllowed =
          !pass.writeMode.has_value() ||
          registry.allowsWriteMode(target, *pass.writeMode);
      auto &producers = producersByTarget[target];
      if (!writeModeAllowed) {
        std::string diagnostic = "RenderPathGraph '" + graph.name + "' pass '" +
                                 pass.id + "' target '" + target +
                                 "' writeMode '" + *pass.writeMode +
                                 "' is not allowed by registry";
        if (!producers.empty()) {
          diagnostic +=
              "; already produced by pass '" + producers.back().passName + "'";
        }
        report.diagnostics.push_back(std::move(diagnostic));
        continue;
      }

      if (!producers.empty()) {
        const TargetProducer &existing = producers.back();
        if (!existing.writeMode.has_value()) {
          report.diagnostics.push_back(
              "RenderPathGraph '" + graph.name + "' pass '" + pass.id +
              "' target '" + target + "' already produced by pass '" +
              existing.passName + "' with missing writeMode");
        } else if (!pass.writeMode.has_value()) {
          report.diagnostics.push_back("RenderPathGraph '" + graph.name +
                                       "' pass '" + pass.id + "' target '" +
                                       target + "' already produced by pass '" +
                                       existing.passName +
                                       "'; duplicate producer missing "
                                       "writeMode");
        } else if (!allowsMultipleProducers(pass, registry, target)) {
          report.diagnostics.push_back(
              "RenderPathGraph '" + graph.name + "' pass '" + pass.id +
              "' target '" + target + "' writeMode '" + *pass.writeMode +
              "' is not allowed by registry");
        } else if (*pass.writeMode != *existing.writeMode) {
          report.diagnostics.push_back(
              "RenderPathGraph '" + graph.name + "' pass '" + pass.id +
              "' target '" + target + "' writeMode '" + *pass.writeMode +
              "' does not match earlier producer pass '" + existing.passName +
              "' writeMode '" + *existing.writeMode + "'");
        } else {
          producers.push_back(TargetProducer{pass.id, pass.writeMode});
        }
      } else {
        producers.push_back(TargetProducer{pass.id, pass.writeMode});
      }
    }
  }

  for (const RenderPassNode &pass : graph.passes) {
    for (const std::string &source : pass.sources) {
      if (!registry.contains(source)) {
        report.diagnostics.push_back("RenderPathGraph '" + graph.name +
                                     "' pass '" + pass.id +
                                     "' unknown source '" + source + "'");
        continue;
      }
      if (registry.isImported(source)) {
        continue;
      }

      const auto producersIt = producersByTarget.find(source);
      bool hasExternalProducer = false;
      if (producersIt != producersByTarget.end()) {
        for (const TargetProducer &producer : producersIt->second) {
          if (producer.passName != pass.id) {
            hasExternalProducer = true;
            break;
          }
        }
      }
      if (!hasExternalProducer) {
        report.diagnostics.push_back("RenderPathGraph '" + graph.name +
                                     "' pass '" + pass.id + "' source '" +
                                     source + "' has no producer");
      }
    }
  }

  return report;
}

} // namespace LX_core
