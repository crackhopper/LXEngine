#pragma once

#include "core/asset/render_pass_contract.hpp"
#include "core/math/vec.hpp"
#include "core/platform/types.hpp"
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

enum class RenderPassBatchingMode {
  None,
  All,
  Material,
};

struct RenderPassBatchingContract final {
  RenderPassBatchingMode mode = RenderPassBatchingMode::None;
};

struct RenderPassInputContract final {
  RenderPassInputKind kind = RenderPassInputKind::SceneRenderables;
  RenderPassObjectInputFilter object;
  RenderPassMaterialInputFilter material;
  RenderPassBatchingContract batching;
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

enum class RenderPathOutputKind {
  Buffer,
  Image2D,
  Cubemap,
  Sh9,
  Texture2D,
};

struct RenderPathReadbackContract final {
  std::string name;
  std::string target;
  std::string extentFrom;
  std::string binding;
  std::string format;
  RenderPathOutputKind kind = RenderPathOutputKind::Buffer;
  std::string mediaType;
};

struct RenderPassComputeContract final {
  std::string dispatchFrom;
  Vec3u localSize{1u, 1u, 1u};
};

struct EnvironmentIblBakeConfig final {
  u32 cubemapResolution = 0;
  u32 cubemapFaces = 0;
  std::string diffuseKind;
  u32 diffuseOrder = 0;
  u32 specularResolution = 0;
  std::string specularMips;
  u32 specularFaces = 0;
};

struct StandardPbrBrdfLutBakeConfig final {
  u32 resolution = 0;
};

struct RenderPathBakeConfig final {
  std::string kind;
  std::optional<EnvironmentIblBakeConfig> environment;
  std::optional<StandardPbrBrdfLutBakeConfig> standardPbrBrdfLut;
};

struct RenderPassNode final {
  std::string id;
  ResourceUri shaderUri;
  RenderPassStage stage = RenderPassStage::Raster;
  RenderPassDispatch dispatch = RenderPassDispatch::Draw;
  std::optional<RenderPassComputeContract> compute;
  RenderPassInputContract input;
  std::optional<RenderPathNodeRenderingMode> renderingMode;
  std::vector<RenderPathAttachmentContract> attachments;
  std::vector<std::string> sources;
  std::vector<std::string> targets;
  std::vector<RenderPathReadbackContract> readbacks;
  RenderState renderState;
  std::optional<std::string> writeMode;
};

[[nodiscard]] StringID getRenderPathNodeSignature(const RenderPassNode &node);
[[nodiscard]] std::optional<std::string>
validateRenderPassInputContract(const RenderPassNode &node);

struct RenderPathGraph final {
  std::string name;
  RenderPath renderPath = RenderPath::Forward;
  std::optional<RenderPathBakeConfig> bake;
  std::vector<RenderPathFeatureDependency> features;
  std::vector<RenderPassNode> passes;
};

struct RenderFeatureParameter final {
  std::string kind;
  std::string value;
  ResourceUri uri;
  std::string sourceHash;
  std::string valueType;
  std::string binding;
  std::string member;
  bool required = false;
  bool volatileRuntime = false;
  std::vector<std::string> allowedValues;
  std::string requiredWhenParameter;
  std::string requiredWhenEquals;
};

struct RenderFeatureVolatileValue final {
  StringID key;
  std::string value;
};

enum class RenderFeatureLevel {
  Unknown,
  Shader,
  Pass,
};

struct RenderFeatureShaderContract final {
  ResourceUri uri;
};

enum class RenderFeatureResourceApi {
  Unknown,
  SceneAcceleration,
};

enum class RenderFeatureResourceImplementation {
  Unknown,
  SoftwareBvh,
  HardwareRayTracing,
};

struct RenderFeatureResourceOutput final {
  std::string kind;
  std::string binding;
  std::string layout;
  std::string elementType;
};

struct RenderFeatureResourceRequirement final {
  RenderFeatureResourceApi api = RenderFeatureResourceApi::Unknown;
  std::string function;
  RenderFeatureResourceImplementation implementation =
      RenderFeatureResourceImplementation::Unknown;
  bool derived = true;
  bool volatileRuntime = true;
  std::string source;
  RenderFeatureResourceOutput output;
  bool required = false;
};

struct RenderFeatureHitShaderTableEntry final {
  u32 index = 0;
  std::string materialType;
  ResourceUri uri;
  std::string function;
};

struct RenderFeatureHitShaderTable final {
  std::string payload;
  std::string dispatchFunction;
  std::vector<RenderFeatureHitShaderTableEntry> entries;
};

struct RenderFeature final {
  std::string name;
  std::string feature;
  RenderFeatureLevel level = RenderFeatureLevel::Unknown;
  std::optional<RenderFeatureShaderContract> shader;
  std::unordered_map<std::string, RenderFeatureParameter> parameters;
  std::unordered_map<std::string, RenderFeatureResourceRequirement> resources;
  std::optional<RenderFeatureHitShaderTable> hitShaderTable;
};

} // namespace LX_core
