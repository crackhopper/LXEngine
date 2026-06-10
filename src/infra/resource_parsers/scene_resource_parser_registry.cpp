#include "infra/resource_parsers/scene_resource_parser_registry.hpp"

#include <filesystem>

namespace LX_infra {
namespace {

std::string keyFor(LX_core::SceneResourceType type, const std::string &ext) {
  return std::to_string(static_cast<int>(type)) + "|" + ext;
}

std::string extensionOf(const LX_core::ResourceUri &uri) {
  return std::filesystem::path(uri.string()).extension().generic_string();
}

void qualifyDiagnostics(ParsedSceneResource &parsed,
                        const LX_core::ResourceUri &ownerUri,
                        const LX_core::ResourceUri &resourceUri,
                        const std::string &parserName) {
  for (std::string &diagnostic : parsed.diagnostics) {
    diagnostic = "owner=" + ownerUri.string() +
                 " resource=" + resourceUri.string() +
                 " parser=" + parserName + ": " + diagnostic;
  }
}

} // namespace

void SceneResourceParserRegistry::registerParser(
    LX_core::SceneResourceType type, std::string extension,
    std::string parserName, SceneResourceParserFn parser) {
  m_parsers[keyFor(type, extension)] =
      ParserEntry{std::move(parserName), std::move(parser)};
}

ParsedSceneResource SceneResourceParserRegistry::parse(
    LX_core::SceneResourceTable &table, LX_core::SceneResourceType type,
    const LX_core::ResourceUri &uri,
    const SceneResourceParseContext &context) const {
  const auto it = m_parsers.find(keyFor(type, extensionOf(uri)));
  if (it == m_parsers.end()) {
    ParsedSceneResource parsed;
    parsed.metadata.type = type;
    parsed.metadata.uri = uri;
    parsed.diagnostics.push_back("owner=" + context.ownerUri.string() +
                                 " resource=" + uri.string() +
                                 " parser=<none>: no parser registered");
    return parsed;
  }

  ParsedSceneResource parsed = it->second.parser(table, uri, context);
  if (!parsed.identity.isValid()) {
    parsed.identity = table.internResourceMetadata(parsed.metadata);
  }
  qualifyDiagnostics(parsed, context.ownerUri, uri, it->second.parserName);
  return parsed;
}

} // namespace LX_infra
