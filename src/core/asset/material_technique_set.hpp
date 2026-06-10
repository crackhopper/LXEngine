#pragma once

#include "core/asset/material_instance.hpp"
#include "core/asset/material_pass_definition.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace LX_core {

enum class MaterialPassStage {
  Raster,
  Compute,
};

enum class MaterialPassDispatch {
  Draw,
  Fullscreen,
  Compute,
};

struct MaterialPassContract final {
  std::string name;
  ResourceUri shaderUri;
  MaterialPassStage stage = MaterialPassStage::Raster;
  MaterialPassDispatch dispatch = MaterialPassDispatch::Draw;
  std::vector<std::string> sources;
  std::vector<std::string> targets;
  RenderState renderState;
  std::optional<std::string> writeMode;
};

struct MaterialTechnique final {
  std::string name;
  std::vector<MaterialPassContract> passes;
};

class MaterialTechniqueSet final {
public:
  std::string defaultTechnique;
  std::unordered_map<std::string, MaterialTechnique> techniques;
};

} // namespace LX_core
