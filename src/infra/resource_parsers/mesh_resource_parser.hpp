#pragma once

#include "infra/resource_parsers/scene_resource_parser_registry.hpp"

namespace LX_infra {

class MeshResourceParser final {
public:
  [[nodiscard]] ParsedSceneResource
  parse(LX_core::SceneResourceTable &table, const LX_core::ResourceUri &uri,
        const SceneResourceParseContext &context) const;
};

} // namespace LX_infra
