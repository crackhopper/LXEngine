#include "infra/resource_parsers/texture_resource_parser.hpp"

namespace LX_infra {

ParsedSceneResource TextureResourceParser::parse(
    LX_core::SceneResourceTable &table, const LX_core::ResourceUri &uri,
    const SceneResourceParseContext &context) const {
  (void)context;
  ParsedSceneResource parsed;
  parsed.metadata.type = LX_core::SceneResourceType::Texture;
  parsed.metadata.uri = uri;
  parsed.identity = table.internResourceMetadata(parsed.metadata);
  return parsed;
}

} // namespace LX_infra
