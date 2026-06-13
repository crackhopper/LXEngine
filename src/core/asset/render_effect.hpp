#pragma once

#include "core/asset/render_pass_contract.hpp"
#include "core/resource/resource_uri.hpp"
#include "core/rhi/image_format.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/utils/string_table.hpp"

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

enum class RenderPathNodeRenderingMode {
  Dynamic,
  Traditional,
};

enum class RenderPathGeometryVertexContract {
  PositionOnly,
  PositionNormalUvTangent,
};

struct RenderPathGeometryContract final {
  RenderPathGeometryVertexContract vertex =
      RenderPathGeometryVertexContract::PositionOnly;
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
};

struct RenderPathAttachmentContract final {
  std::string target;
  ImageFormat format = ImageFormat::BGRA8;
  u32 samples = 1;
  u32 layers = 1;
  bool depth = false;
};

struct RenderPassNode final {
  std::string id;
  ResourceUri shaderUri;
  RenderPassStage stage = RenderPassStage::Raster;
  RenderPassDispatch dispatch = RenderPassDispatch::Draw;
  RenderPassNodeFilters filters;
  std::optional<RenderPathNodeRenderingMode> renderingMode;
  std::optional<RenderPathGeometryContract> geometry;
  std::vector<RenderPathAttachmentContract> attachments;
  std::vector<std::string> sources;
  std::vector<std::string> targets;
  RenderState renderState;
  std::optional<std::string> writeMode;
};

[[nodiscard]] StringID getRenderPathNodeSignature(const RenderPassNode &node);

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
