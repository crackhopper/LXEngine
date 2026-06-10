#include "core/frame_graph/technique_validator.hpp"

#include <unordered_map>

namespace LX_core {
namespace {

bool allowsMultipleProducers(const MaterialPassContract &pass) {
  return pass.writeMode.has_value() && *pass.writeMode == "append";
}

} // namespace

TechniqueValidationReport
validateTechniqueResources(const MaterialTechnique &technique,
                           const GraphResourceRegistry &registry) {
  TechniqueValidationReport report;
  std::unordered_map<std::string, std::string> producerByTarget;

  for (const MaterialPassContract &pass : technique.passes) {
    if (pass.shaderUri.empty()) {
      report.diagnostics.push_back("technique '" + technique.name + "' pass '" +
                                   pass.name + "' missing shaderUri");
    }
    for (const std::string &source : pass.sources) {
      if (!registry.contains(source)) {
        report.diagnostics.push_back("technique '" + technique.name +
                                     "' pass '" + pass.name +
                                     "' unknown source '" + source + "'");
      }
    }
    for (const std::string &target : pass.targets) {
      if (!registry.contains(target)) {
        report.diagnostics.push_back("technique '" + technique.name +
                                     "' pass '" + pass.name +
                                     "' unknown target '" + target + "'");
        continue;
      }
      auto existing = producerByTarget.find(target);
      if (existing != producerByTarget.end()) {
        if (!allowsMultipleProducers(pass)) {
          report.diagnostics.push_back("technique '" + technique.name +
                                       "' target '" + target +
                                       "' already produced by pass '" +
                                       existing->second + "'");
        }
      } else {
        producerByTarget.emplace(target, pass.name);
      }
    }
  }

  return report;
}

} // namespace LX_core
