#include "generic_material_loader.hpp"

#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/material_resource_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>

namespace LX_infra {

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fatalLoader(const std::string &reason) {
  throw std::logic_error("GenericMaterialLoader " + reason);
}

[[nodiscard]] bool isMaterialV2Contract(const YAML::Node &root) {
  const YAML::Node schemaNode = root["schema"];
  return schemaNode && schemaNode.IsScalar() &&
         schemaNode.as<std::string>() == "lxe.material.v2";
}

ParsedMaterialResource
parseMaterialV2Contract(const YAML::Node &root, const LX_core::ResourceUri &uri,
                        LX_core::SceneResourceTable &table) {
  MaterialResourceParser parser;
  return parser.parse(table, uri, YAML::Dump(root));
}

void validateParsedMaterialV2Envelope(const ParsedMaterialResource &parsed,
                                      const LX_core::ResourceUri &uri) {
  if (!parsed.diagnostics.empty() || !parsed.instance) {
    std::ostringstream message;
    message << uri.string() << ": invalid material v2 contract";
    for (const std::string &diagnostic : parsed.diagnostics) {
      message << "\n  " << diagnostic;
    }
    fatalLoader(message.str());
  }
}

LX_core::MaterialInstanceSharedPtr
loadMaterialV2EnvelopeContract(const YAML::Node &root,
                               const LX_core::ResourceUri &uri,
                               LX_core::SceneResourceTable &table) {
  ParsedMaterialResource parsed = parseMaterialV2Contract(root, uri, table);
  validateParsedMaterialV2Envelope(parsed, uri);
  return parsed.instance->cloneInstanceData();
}

} // namespace

LX_core::MaterialInstanceSharedPtr
loadGenericMaterial(const fs::path &materialPath,
                    const GenericMaterialLoadOptions &options) {
  LX_core::SceneResourceTable table;
  return loadGenericMaterial(materialPath, table, options);
}

LX_core::MaterialInstanceSharedPtr
loadGenericMaterial(const fs::path &materialPath,
                    LX_core::SceneResourceTable &resourceTable,
                    const GenericMaterialLoadOptions &options) {
  (void)options;

  const fs::path resolvedMaterialPath = materialPath.is_absolute()
                                            ? materialPath
                                            : resolveRuntimePath(materialPath);
  if (!fs::exists(resolvedMaterialPath)) {
    fatalLoader("material file not found: " + materialPath.string());
  }

  YAML::Node root;
  try {
    root = YAML::LoadFile(resolvedMaterialPath.string());
  } catch (const YAML::Exception &e) {
    fatalLoader("failed to parse material file: " + std::string(e.what()));
  }

  if (!root.IsMap()) {
    fatalLoader("material file root is not a YAML map: " +
                resolvedMaterialPath.string());
  }

  const LX_core::ResourceUri materialUri(
      fs::relative(resolvedMaterialPath).string());
  if (!isMaterialV2Contract(root)) {
    fatalLoader(materialUri.string() +
                ": only schema lxe.material.v2 material files are accepted");
  }

  return loadMaterialV2EnvelopeContract(root, materialUri, resourceTable);
}

} // namespace LX_infra
