#pragma once

#include "core/asset/render_pass_contract.hpp"
#include "core/resource/resource_uri.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace LX_core {

enum class RenderPath {
  Forward,
  Deferred,
  OfflineRT,
};

struct RenderPathFeatureDependency final {
  std::string slot;
  ResourceUri uri;
};

struct RenderPassNodeFilters final {
  std::vector<std::string> renderClasses;
  std::vector<std::string> bsdfTypes;
};

struct RenderPassNode final {
  std::string id;
  ResourceUri shaderUri;
  RenderPassStage stage = RenderPassStage::Raster;
  RenderPassDispatch dispatch = RenderPassDispatch::Draw;
  RenderPassNodeFilters filters;
  std::vector<std::string> sources;
  std::vector<std::string> targets;
  RenderState renderState;
  std::optional<std::string> writeMode;
};

struct RenderPathGraph final {
  std::string name;
  RenderPath renderPath = RenderPath::Forward;
  std::vector<RenderPathFeatureDependency> features;
  std::vector<RenderPassNode> passes;
};

struct RenderFeatureParameter final {
  std::string kind;
  std::string value;
  ResourceUri uri;
  std::string valueType;
};

struct RenderFeature final {
  std::string name;
  std::string feature;
  std::unordered_map<std::string, RenderFeatureParameter> parameters;
};

} // namespace LX_core
