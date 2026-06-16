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

enum class RenderPathAttachmentUsage {
  ColorAttachmentWrite,
  DepthAttachmentReadOnly,
  DepthAttachmentWrite,
  DepthAttachmentReadWrite,
};

enum class RenderPassInputKind {
  SceneRenderables,
  FullscreenTriangle,
  ComputeDispatch,
};

struct RenderPassObjectInputFilter final {
  std::vector<std::string> renderClasses;
};

struct RenderPassMaterialInputFilter final {
  std::vector<std::string> types;
  bool required = true;
};

struct RenderPassInputContract final {
  RenderPassInputKind kind = RenderPassInputKind::SceneRenderables;
  RenderPassObjectInputFilter object;
  RenderPassMaterialInputFilter material;
  std::optional<RenderPathGeometryContract> geometry;
};

struct RenderPathAttachmentContract final {
  std::string target;
  ImageFormat format = ImageFormat::BGRA8;
  u32 samples = 1;
  u32 layers = 1;
  bool depth = false;
  RenderPathAttachmentUsage attachmentUsage =
      RenderPathAttachmentUsage::ColorAttachmentWrite;
};

struct RenderPassNode final {
  std::string id;
  ResourceUri shaderUri;
  RenderPassStage stage = RenderPassStage::Raster;
  RenderPassDispatch dispatch = RenderPassDispatch::Draw;
  RenderPassInputContract input;
  std::optional<RenderPathNodeRenderingMode> renderingMode;
  std::vector<RenderPathAttachmentContract> attachments;
  std::vector<std::string> sources;
  std::vector<std::string> targets;
  RenderState renderState;
  std::optional<std::string> writeMode;
};

[[nodiscard]] StringID getRenderPathNodeSignature(const RenderPassNode &node);
[[nodiscard]] std::optional<std::string>
validateRenderPassInputContract(const RenderPassNode &node);

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
  std::string binding;
  std::string member;
  bool required = false;
  std::vector<std::string> allowedValues;
  std::string requiredWhenParameter;
  std::string requiredWhenEquals;
};

enum class RenderFeatureLevel {
  Unknown,
  Shader,
  Pass,
};

struct RenderFeatureShaderContract final {
  ResourceUri uri;
};

struct RenderFeature final {
  std::string name;
  std::string feature;
  RenderFeatureLevel level = RenderFeatureLevel::Unknown;
  std::optional<RenderFeatureShaderContract> shader;
  std::unordered_map<std::string, RenderFeatureParameter> parameters;
};

} // namespace LX_core
