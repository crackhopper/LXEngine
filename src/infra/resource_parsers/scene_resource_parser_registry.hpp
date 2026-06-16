#pragma once

#include "core/resource/resource_metadata.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace LX_infra {

struct SceneResourceParseContext final {
  LX_core::ResourceUri ownerUri;
  LX_core::TextureContent textureContent = LX_core::TextureContent::Unknown;
};

struct ParsedSceneResource final {
  LX_core::ResourceMetadata metadata;
  LX_core::ResourceIdentityHandle identity;
  std::vector<std::string> diagnostics;
};

using SceneResourceParserFn = std::function<ParsedSceneResource(
    LX_core::SceneResourceTable &, const LX_core::ResourceUri &,
    const SceneResourceParseContext &)>;

class SceneResourceParserRegistry final {
public:
  void registerParser(LX_core::SceneResourceType type, std::string extension,
                      std::string parserName, SceneResourceParserFn parser);

  [[nodiscard]] ParsedSceneResource
  parse(LX_core::SceneResourceTable &table, LX_core::SceneResourceType type,
        const LX_core::ResourceUri &uri,
        const SceneResourceParseContext &context) const;

private:
  struct ParserEntry final {
    std::string parserName;
    SceneResourceParserFn parser;
  };

  std::unordered_map<std::string, ParserEntry> m_parsers;
};

} // namespace LX_infra
