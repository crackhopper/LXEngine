#include "core/frame_graph/technique_validator.hpp"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LX_core {
namespace {

bool allowsMultipleProducers(const MaterialPassContract &pass,
                             const GraphResourceRegistry &registry,
                             const std::string &target) {
  return pass.writeMode.has_value() &&
         registry.allowsWriteMode(target, *pass.writeMode);
}

const char *stageName(const MaterialPassStage stage) {
  switch (stage) {
  case MaterialPassStage::Raster:
    return "raster";
  case MaterialPassStage::Compute:
    return "compute";
  }
  return "unknown";
}

const char *dispatchName(const MaterialPassDispatch dispatch) {
  switch (dispatch) {
  case MaterialPassDispatch::Draw:
    return "draw";
  case MaterialPassDispatch::Fullscreen:
    return "fullscreen";
  case MaterialPassDispatch::Compute:
    return "compute";
  }
  return "unknown";
}

bool supportsStageDispatch(const MaterialPassContract &pass) {
  switch (pass.stage) {
  case MaterialPassStage::Raster:
    return pass.dispatch == MaterialPassDispatch::Draw ||
           pass.dispatch == MaterialPassDispatch::Fullscreen;
  case MaterialPassStage::Compute:
    return pass.dispatch == MaterialPassDispatch::Compute;
  }
  return false;
}

struct TargetProducer final {
  std::string passName;
  std::optional<std::string> writeMode;
};

} // namespace

TechniqueValidationReport
validateTechniqueResources(const MaterialTechnique &technique,
                           const GraphResourceRegistry &registry) {
  TechniqueValidationReport report;
  std::unordered_map<std::string, std::vector<TargetProducer>>
      producersByTarget;

  for (const MaterialPassContract &pass : technique.passes) {
    if (pass.shaderUri.empty()) {
      report.diagnostics.push_back("technique '" + technique.name + "' pass '" +
                                   pass.name + "' missing shaderUri");
    }
    if (!supportsStageDispatch(pass)) {
      report.diagnostics.push_back(
          "technique '" + technique.name + "' pass '" + pass.name +
          "' unsupported dispatch '" + dispatchName(pass.dispatch) +
          "' for stage '" + stageName(pass.stage) + "'");
    }

    std::unordered_set<std::string> targetsInPass;
    for (const std::string &target : pass.targets) {
      if (!registry.contains(target)) {
        report.diagnostics.push_back("technique '" + technique.name +
                                     "' pass '" + pass.name +
                                     "' unknown target '" + target + "'");
        continue;
      }
      if (registry.isImported(target)) {
        report.diagnostics.push_back("technique '" + technique.name +
                                     "' pass '" + pass.name +
                                     "' imported target '" + target +
                                     "' is source-only");
        continue;
      }
      if (!targetsInPass.insert(target).second) {
        report.diagnostics.push_back("technique '" + technique.name +
                                     "' pass '" + pass.name +
                                     "' duplicate target '" + target + "'");
        continue;
      }

      const bool writeModeAllowed =
          !pass.writeMode.has_value() ||
          registry.allowsWriteMode(target, *pass.writeMode);
      auto &producers = producersByTarget[target];
      if (!writeModeAllowed) {
        std::string diagnostic = "technique '" + technique.name + "' pass '" +
                                 pass.name + "' target '" + target +
                                 "' writeMode '" + *pass.writeMode +
                                 "' is not allowed by registry";
        if (!producers.empty()) {
          diagnostic += "; already produced by pass '" +
                        producers.back().passName + "'";
        }
        report.diagnostics.push_back(std::move(diagnostic));
        continue;
      }

      if (!producers.empty()) {
        const TargetProducer &existing = producers.back();
        if (!existing.writeMode.has_value()) {
          report.diagnostics.push_back(
              "technique '" + technique.name + "' pass '" + pass.name +
              "' target '" + target + "' already produced by pass '" +
              existing.passName +
              "' with missing writeMode");
        } else if (!pass.writeMode.has_value()) {
          report.diagnostics.push_back("technique '" + technique.name +
                                       "' pass '" + pass.name + "' target '" +
                                       target + "' already produced by pass '" +
                                       existing.passName +
                                       "'; duplicate producer missing "
                                       "writeMode");
        } else if (!allowsMultipleProducers(pass, registry, target)) {
          report.diagnostics.push_back("technique '" + technique.name +
                                       "' pass '" + pass.name + "' target '" +
                                       target + "' writeMode '" +
                                       *pass.writeMode +
                                       "' is not allowed by registry");
        } else if (*pass.writeMode != *existing.writeMode) {
          report.diagnostics.push_back(
              "technique '" + technique.name + "' pass '" + pass.name +
              "' target '" + target + "' writeMode '" + *pass.writeMode +
              "' does not match earlier producer pass '" + existing.passName +
              "' writeMode '" + *existing.writeMode + "'");
        } else {
          producers.push_back(TargetProducer{pass.name, pass.writeMode});
        }
      } else {
        producers.push_back(TargetProducer{pass.name, pass.writeMode});
      }
    }
  }

  for (const MaterialPassContract &pass : technique.passes) {
    for (const std::string &source : pass.sources) {
      if (!registry.contains(source)) {
        report.diagnostics.push_back("technique '" + technique.name +
                                     "' pass '" + pass.name +
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
          if (producer.passName != pass.name) {
            hasExternalProducer = true;
            break;
          }
        }
      }
      if (!hasExternalProducer) {
        report.diagnostics.push_back("technique '" + technique.name +
                                     "' pass '" + pass.name + "' source '" +
                                     source + "' has no producer");
      }
    }
  }

  return report;
}

} // namespace LX_core
