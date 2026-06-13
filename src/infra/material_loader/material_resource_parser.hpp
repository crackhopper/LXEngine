#pragma once

#include "core/asset/material_instance.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/material_loader/material_contract_reflector.hpp"

#include <string_view>
#include <vector>

namespace LX_infra {

struct MaterialParseContext final {
  LX_core::ResourceUri materialUri;
  LX_core::ResourceUri baseUri;
};

struct ParsedMaterialResource final {
  LX_core::MaterialInstance::UniquePtr instance;
  std::vector<LX_core::MaterialResourceDependency> dependencies;
  std::vector<std::string> diagnostics;
};

class MaterialResourceParser final {
public:
  explicit MaterialResourceParser(
      MaterialContractReflector reflector = reflectMaterialContractSource,
      MaterialContractSourceLoader sourceLoader =
          loadMaterialContractSourceText);

  [[nodiscard]] ParsedMaterialResource
  parse(LX_core::SceneResourceTable &table, const LX_core::ResourceUri &uri,
        std::string_view yamlText) const;

private:
  MaterialContractReflector m_reflector;
  MaterialContractSourceLoader m_sourceLoader;
};

} // namespace LX_infra
