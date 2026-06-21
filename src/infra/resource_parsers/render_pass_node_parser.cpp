#include "infra/resource_parsers/render_pass_node_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <string>

namespace LX_infra {
namespace {

void addDiagnostic(RenderPassNodeParseResult &result, const std::string &field,
                   const std::string &message) {
  result.diagnostics.push_back(field + ": " + message);
}

bool requireField(const YAML::Node &node, RenderPassNodeParseResult &result,
                  const std::string &field) {
  if (node) {
    return true;
  }
  addDiagnostic(result, field, "missing required field");
  return false;
}

std::optional<LX_core::RenderPassStage> parseStage(const std::string &value) {
  if (value == "raster") {
    return LX_core::RenderPassStage::Raster;
  }
  if (value == "compute") {
    return LX_core::RenderPassStage::Compute;
  }
  return std::nullopt;
}

std::optional<LX_core::RenderPassDispatch>
parseDispatch(const std::string &value) {
  if (value == "draw") {
    return LX_core::RenderPassDispatch::Draw;
  }
  if (value == "fullscreen") {
    return LX_core::RenderPassDispatch::Fullscreen;
  }
  if (value == "compute") {
    return LX_core::RenderPassDispatch::Compute;
  }
  return std::nullopt;
}

std::optional<LX_core::CullMode> parseCullMode(const YAML::Node &node) {
  const auto value = node.as<std::string>();
  if (value == "None") {
    return LX_core::CullMode::None;
  }
  if (value == "Front") {
    return LX_core::CullMode::Front;
  }
  if (value == "Back") {
    return LX_core::CullMode::Back;
  }
  return std::nullopt;
}

std::optional<LX_core::BlendFactor> parseBlendFactor(const YAML::Node &node) {
  const auto value = node.as<std::string>();
  if (value == "Zero") {
    return LX_core::BlendFactor::Zero;
  }
  if (value == "One") {
    return LX_core::BlendFactor::One;
  }
  if (value == "SrcAlpha") {
    return LX_core::BlendFactor::SrcAlpha;
  }
  if (value == "OneMinusSrcAlpha") {
    return LX_core::BlendFactor::OneMinusSrcAlpha;
  }
  return std::nullopt;
}

std::optional<LX_core::CompareOp> parseCompareOp(const YAML::Node &node) {
  const auto value = node.as<std::string>();
  if (value == "Less") {
    return LX_core::CompareOp::Less;
  }
  if (value == "LessEqual") {
    return LX_core::CompareOp::LessEqual;
  }
  if (value == "Greater") {
    return LX_core::CompareOp::Greater;
  }
  if (value == "Equal") {
    return LX_core::CompareOp::Equal;
  }
  if (value == "Always") {
    return LX_core::CompareOp::Always;
  }
  return std::nullopt;
}

std::vector<std::string> parseStringList(const YAML::Node &node) {
  std::vector<std::string> values;
  values.reserve(node.size());
  for (const auto &entry : node) {
    values.push_back(entry.as<std::string>());
  }
  return values;
}

std::optional<std::vector<std::string>>
parseStringSequence(const YAML::Node &node, RenderPassNodeParseResult &result,
                    const std::string &field) {
  if (!node) {
    return std::vector<std::string>{};
  }
  if (!node.IsSequence()) {
    addDiagnostic(result, field, "must be a sequence of strings");
    return std::nullopt;
  }
  std::vector<std::string> values;
  values.reserve(node.size());
  for (std::size_t i = 0; i < node.size(); ++i) {
    if (!node[i].IsScalar()) {
      addDiagnostic(result, field + "[" + std::to_string(i) + "]",
                    "must be a string");
      return std::nullopt;
    }
    values.push_back(node[i].as<std::string>());
  }
  return values;
}

std::optional<bool> parseBoolScalar(const YAML::Node &node,
                                    RenderPassNodeParseResult &result,
                                    const std::string &field) {
  if (!node || !node.IsScalar()) {
    addDiagnostic(result, field, "must be a boolean");
    return std::nullopt;
  }
  const std::string value = node.as<std::string>();
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  addDiagnostic(result, field, "expected true or false");
  return std::nullopt;
}

bool isKnownRenderPathResourceName(const std::string &name) {
  constexpr std::array knownResources{
      "geometry.vertex",
      "geometry.index",
      "material.bsdf",
      "scene.camera",
      "scene.lights",
      "scene.geometry",
      "scene.materials",
      "scene.textures",
      "shadow.main",
      "hdr.color",
      "depth.main",
      "swapchain.color",
      "debug.overlay",
      "debug.ldr.linear",
      "debug.final.srgb",
      "debug.final.unorm_manual_srgb",
      "debug.ramp.srgb",
      "debug.ramp.unorm_manual_srgb",
      "offline.output",
      "bake.environment.source",
      "bake.environment.cubemap",
      "bake.environment.diffuse_sh9",
      "bake.environment.specular_prefilter",
      "bake.material.source",
      "bake.material.brdf_lut",
      "gbuffer.albedoAlpha",
      "gbuffer.albedo",
      "gbuffer.normalRoughness",
      "gbuffer.normal",
      "gbuffer.material",
      "feature.forwardPass",
      "feature.skybox",
      "feature.toneMapping",
      "feature.environmentLighting",
      "feature.surfaceLighting",
      "feature.offlineRayTracer",
      "feature.bloom",
      "bloom.threshold",
      "bloom.blur_h",
      "bloom.blur_v",
      "bloom.blurH",
      "bloom.blur",
  };
  for (const std::string_view known : knownResources) {
    if (name == known) {
      return true;
    }
  }
  return false;
}

std::optional<LX_core::RenderPathAttachmentUsage>
parseAttachmentUsage(const YAML::Node &node, RenderPassNodeParseResult &result,
                     const std::string &field) {
  const std::string value = node.as<std::string>();
  if (value == "color-attachment-write") {
    return LX_core::RenderPathAttachmentUsage::ColorAttachmentWrite;
  }
  if (value == "depth-attachment-read-only") {
    return LX_core::RenderPathAttachmentUsage::DepthAttachmentReadOnly;
  }
  if (value == "depth-attachment-write") {
    return LX_core::RenderPathAttachmentUsage::DepthAttachmentWrite;
  }
  if (value == "depth-attachment-read-write") {
    return LX_core::RenderPathAttachmentUsage::DepthAttachmentReadWrite;
  }
  addDiagnostic(result, field, "unknown attachment usage");
  return std::nullopt;
}

bool attachmentUsageIsDepth(LX_core::RenderPathAttachmentUsage usage) {
  return usage == LX_core::RenderPathAttachmentUsage::DepthAttachmentReadOnly ||
         usage == LX_core::RenderPathAttachmentUsage::DepthAttachmentWrite ||
         usage == LX_core::RenderPathAttachmentUsage::DepthAttachmentReadWrite;
}

bool rejectUnsupportedFields(const YAML::Node &node,
                             RenderPassNodeParseResult &result,
                             const std::string &fieldPrefix) {
  bool ok = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, fieldPrefix,
                    "pass field names must be scalar strings");
      ok = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "id" || key == "shader" || key == "stage" || key == "dispatch" ||
        key == "compute" || key == "rendering" || key == "input" ||
        key == "sources" || key == "targets" || key == "readbacks" ||
        key == "renderState" || key == "writeMode") {
      continue;
    }
    if (key == "payloads") {
      addDiagnostic(result, fieldPrefix + ".payloads",
                    "legacy payloads field is removed; use readbacks");
    } else if (key == "enginePass") {
      addDiagnostic(result, fieldPrefix + ".enginePass",
                    "legacy enginePass bridge is removed; pass name is the "
                    "runtime pass identity");
    } else if (key == "variants" || key == "parameters" || key == "resources" ||
               key == "technique" || key == "pass" || key == "material") {
      addDiagnostic(result, fieldPrefix + "." + key,
                    "legacy pass-node field is removed; render structure must "
                    "come from RenderPathGraph pass contracts");
    } else {
      addDiagnostic(result, fieldPrefix + "." + key,
                    "unsupported pass contract field");
    }
    ok = false;
  }
  return ok;
}

std::optional<LX_core::RenderState>
parseRenderState(const YAML::Node &node, RenderPassNodeParseResult &result,
                 const std::string &field) {
  if (!node || !node.IsMap()) {
    addDiagnostic(result, field, "missing required field");
    return std::nullopt;
  }

  bool valid = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, field,
                    "renderState field names must be scalar strings");
      valid = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "cullMode" || key == "depthTest" || key == "depthWrite" ||
        key == "depthOp" || key == "blendEnable" || key == "srcBlend" ||
        key == "dstBlend") {
      continue;
    }
    addDiagnostic(result, field + "." + key,
                  "unsupported renderState contract field");
    valid = false;
  }
  valid &= requireField(node["cullMode"], result, field + ".cullMode");
  valid &= requireField(node["depthTest"], result, field + ".depthTest");
  valid &= requireField(node["depthWrite"], result, field + ".depthWrite");
  valid &= requireField(node["depthOp"], result, field + ".depthOp");
  if (!valid) {
    return std::nullopt;
  }

  auto cullMode = parseCullMode(node["cullMode"]);
  if (!cullMode.has_value()) {
    addDiagnostic(result, field + ".cullMode", "unknown cull mode");
    return std::nullopt;
  }
  auto depthOp = parseCompareOp(node["depthOp"]);
  if (!depthOp.has_value()) {
    addDiagnostic(result, field + ".depthOp", "unknown compare op");
    return std::nullopt;
  }

  LX_core::RenderState state;
  state.cullMode = *cullMode;
  state.depthTestEnable = node["depthTest"].as<bool>();
  state.depthWriteEnable = node["depthWrite"].as<bool>();
  state.depthOp = *depthOp;
  if (const auto blend = node["blendEnable"]) {
    state.blendEnable = blend.as<bool>();
  }
  if (const auto srcBlend = node["srcBlend"]) {
    auto parsed = parseBlendFactor(srcBlend);
    if (!parsed.has_value()) {
      addDiagnostic(result, field + ".srcBlend", "unknown blend factor");
      return std::nullopt;
    }
    state.srcBlend = *parsed;
  }
  if (const auto dstBlend = node["dstBlend"]) {
    auto parsed = parseBlendFactor(dstBlend);
    if (!parsed.has_value()) {
      addDiagnostic(result, field + ".dstBlend", "unknown blend factor");
      return std::nullopt;
    }
    state.dstBlend = *parsed;
  }
  return state;
}

std::optional<LX_core::RenderPathNodeRenderingMode>
parseRenderingMode(const YAML::Node &node, RenderPassNodeParseResult &result,
                   const std::string &field) {
  if (!node || !node.IsMap()) {
    addDiagnostic(result, field, "missing required field");
    return std::nullopt;
  }
  bool valid = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, field,
                    "rendering field names must be scalar strings");
      valid = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "mode" || key == "attachments") {
      continue;
    }
    addDiagnostic(result, field + "." + key,
                  "unsupported rendering contract field");
    valid = false;
  }
  valid &= requireField(node["mode"], result, field + ".mode");
  if (!valid) {
    return std::nullopt;
  }
  const std::string mode = node["mode"].as<std::string>();
  if (mode == "dynamic") {
    return LX_core::RenderPathNodeRenderingMode::Dynamic;
  }
  if (mode == "traditional") {
    return LX_core::RenderPathNodeRenderingMode::Traditional;
  }
  addDiagnostic(result, field + ".mode",
                "expected dynamic or traditional rendering mode");
  return std::nullopt;
}

std::optional<LX_core::RenderPathGeometryVertexContract>
parseGeometryVertexContract(const YAML::Node &node,
                            RenderPassNodeParseResult &result,
                            const std::string &field) {
  const std::string value = node.as<std::string>();
  if (value == "position-only") {
    return LX_core::RenderPathGeometryVertexContract::PositionOnly;
  }
  if (value == "position-normal-uv-tangent") {
    return LX_core::RenderPathGeometryVertexContract::PositionNormalUvTangent;
  }
  addDiagnostic(result, field,
                "expected position-only or position-normal-uv-tangent");
  return std::nullopt;
}

std::optional<LX_core::PrimitiveTopology>
parseGeometryTopology(const YAML::Node &node, RenderPassNodeParseResult &result,
                      const std::string &field) {
  const std::string value = node.as<std::string>();
  if (value == "point-list") {
    return LX_core::PrimitiveTopology::PointList;
  }
  if (value == "line-list") {
    return LX_core::PrimitiveTopology::LineList;
  }
  if (value == "line-strip") {
    return LX_core::PrimitiveTopology::LineStrip;
  }
  if (value == "triangle-list") {
    return LX_core::PrimitiveTopology::TriangleList;
  }
  if (value == "triangle-strip") {
    return LX_core::PrimitiveTopology::TriangleStrip;
  }
  if (value == "triangle-fan") {
    return LX_core::PrimitiveTopology::TriangleFan;
  }
  addDiagnostic(result, field, "unknown primitive topology");
  return std::nullopt;
}

std::optional<LX_core::RenderPathGeometryContract>
parseGeometryContract(const YAML::Node &node, RenderPassNodeParseResult &result,
                      const std::string &field) {
  if (!node || !node.IsMap()) {
    addDiagnostic(result, field, "missing required field");
    return std::nullopt;
  }
  bool valid = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, field,
                    "geometry field names must be scalar strings");
      valid = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "vertex" || key == "topology") {
      continue;
    }
    addDiagnostic(result, field + "." + key,
                  "unsupported geometry contract field");
    valid = false;
  }
  valid &= requireField(node["vertex"], result, field + ".vertex");
  valid &= requireField(node["topology"], result, field + ".topology");
  if (!valid) {
    return std::nullopt;
  }
  auto vertex =
      parseGeometryVertexContract(node["vertex"], result, field + ".vertex");
  auto topology =
      parseGeometryTopology(node["topology"], result, field + ".topology");
  if (!vertex.has_value() || !topology.has_value()) {
    return std::nullopt;
  }
  LX_core::RenderPathGeometryContract geometry;
  geometry.vertex = *vertex;
  geometry.topology = *topology;
  return geometry;
}

std::optional<LX_core::RenderPassInputKind>
parseInputKind(const YAML::Node &node, RenderPassNodeParseResult &result,
               const std::string &field) {
  if (!node || !node.IsScalar()) {
    addDiagnostic(result, field, "missing required field");
    return std::nullopt;
  }
  const std::string value = node.as<std::string>();
  if (value == "scene-renderables") {
    return LX_core::RenderPassInputKind::SceneRenderables;
  }
  if (value == "fullscreen-triangle") {
    return LX_core::RenderPassInputKind::FullscreenTriangle;
  }
  if (value == "compute-dispatch") {
    return LX_core::RenderPassInputKind::ComputeDispatch;
  }
  addDiagnostic(result, field,
                "expected scene-renderables, fullscreen-triangle, or "
                "compute-dispatch");
  return std::nullopt;
}

std::optional<LX_core::RenderPassObjectInputFilter>
parseObjectInputFilter(const YAML::Node &node,
                       RenderPassNodeParseResult &result,
                       const std::string &field) {
  LX_core::RenderPassObjectInputFilter object;
  if (!node) {
    return object;
  }
  if (!node.IsMap()) {
    addDiagnostic(result, field, "object input filter must be a map");
    return std::nullopt;
  }
  bool valid = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, field,
                    "object input filter field names must be scalar strings");
      valid = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "renderClass") {
      continue;
    }
    addDiagnostic(result, field + "." + key,
                  "unsupported object input filter field");
    valid = false;
  }
  auto renderClasses =
      parseStringSequence(node["renderClass"], result, field + ".renderClass");
  if (!valid || !renderClasses.has_value()) {
    return std::nullopt;
  }
  object.renderClasses = std::move(*renderClasses);
  return object;
}

std::optional<LX_core::RenderPassMaterialInputFilter>
parseMaterialInputFilter(const YAML::Node &node,
                         RenderPassNodeParseResult &result,
                         const std::string &field) {
  LX_core::RenderPassMaterialInputFilter material;
  if (!node) {
    return material;
  }
  if (!node.IsMap()) {
    addDiagnostic(result, field, "material input filter must be a map");
    return std::nullopt;
  }
  bool valid = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, field,
                    "material input filter field names must be scalar strings");
      valid = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "type" || key == "required") {
      continue;
    }
    addDiagnostic(result, field + "." + key,
                  "unsupported material input filter field");
    valid = false;
  }
  auto types = parseStringSequence(node["type"], result, field + ".type");
  if (!types.has_value()) {
    return std::nullopt;
  }
  material.types = std::move(*types);
  if (const auto required = node["required"]) {
    auto parsedRequired =
        parseBoolScalar(required, result, field + ".required");
    if (!parsedRequired.has_value()) {
      return std::nullopt;
    }
    material.required = *parsedRequired;
  }
  if (!valid) {
    return std::nullopt;
  }
  return material;
}

std::optional<LX_core::RenderPassBatchingContract>
parseBatchingContract(const YAML::Node &node,
                      RenderPassNodeParseResult &result,
                      const std::string &field) {
  LX_core::RenderPassBatchingContract batching;
  if (!node) {
    return batching;
  }
  if (!node.IsMap()) {
    addDiagnostic(result, field, "batching contract must be a map");
    return std::nullopt;
  }
  bool valid = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, field,
                    "batching contract field names must be scalar strings");
      valid = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "mode") {
      continue;
    }
    addDiagnostic(result, field + "." + key,
                  "unsupported batching contract field");
    valid = false;
  }
  if (!node["mode"] || !node["mode"].IsScalar()) {
    addDiagnostic(result, field + ".mode", "missing scalar batching mode");
    return std::nullopt;
  }
  const std::string mode = node["mode"].as<std::string>();
  if (mode == "none") {
    batching.mode = LX_core::RenderPassBatchingMode::None;
  } else if (mode == "all") {
    batching.mode = LX_core::RenderPassBatchingMode::All;
  } else if (mode == "material") {
    batching.mode = LX_core::RenderPassBatchingMode::Material;
  } else {
    addDiagnostic(result, field + ".mode",
                  "unknown batching mode; expected none, all, or material");
    return std::nullopt;
  }
  if (!valid) {
    return std::nullopt;
  }
  return batching;
}

std::optional<LX_core::RenderPassInputContract>
parseInputContract(const YAML::Node &node, RenderPassNodeParseResult &result,
                   const std::string &field, LX_core::RenderPassStage stage,
                   LX_core::RenderPassDispatch dispatch) {
  if (!node || !node.IsMap()) {
    addDiagnostic(result, field, "missing required field");
    return std::nullopt;
  }

  bool valid = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, field, "input field names must be scalar strings");
      valid = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "kind" || key == "object" || key == "material" ||
        key == "batching" || key == "geometry") {
      continue;
    }
    addDiagnostic(result, field + "." + key,
                  "unsupported input contract field");
    valid = false;
  }

  auto kind = parseInputKind(node["kind"], result, field + ".kind");
  auto batching =
      parseBatchingContract(node["batching"], result, field + ".batching");
  if (!valid || !kind.has_value()) {
    return std::nullopt;
  }

  LX_core::RenderPassInputContract input;
  input.kind = *kind;
  if (!batching.has_value()) {
    return std::nullopt;
  }
  input.batching = *batching;

  if (input.kind == LX_core::RenderPassInputKind::FullscreenTriangle) {
    if (stage != LX_core::RenderPassStage::Raster ||
        dispatch != LX_core::RenderPassDispatch::Fullscreen) {
      addDiagnostic(result, field + ".kind",
                    "fullscreen-triangle input requires raster fullscreen");
      return std::nullopt;
    }
    bool rejectedField = false;
    if (node["object"]) {
      addDiagnostic(result, field + ".object",
                    "fullscreen-triangle input does not accept object filter");
      rejectedField = true;
    }
    if (node["material"]) {
      addDiagnostic(
          result, field + ".material",
          "fullscreen-triangle input does not accept material filter");
      rejectedField = true;
    }
    if (node["geometry"]) {
      addDiagnostic(result, field + ".geometry",
                    "fullscreen-triangle input does not accept geometry");
      rejectedField = true;
    }
    if (input.batching.mode != LX_core::RenderPassBatchingMode::None) {
      addDiagnostic(result, field + ".batching",
                    "fullscreen-triangle input only accepts batching.mode none");
      rejectedField = true;
    }
    return rejectedField ? std::nullopt
                         : std::optional<LX_core::RenderPassInputContract>(
                               std::move(input));
  }

  if (input.kind == LX_core::RenderPassInputKind::ComputeDispatch) {
    if (stage != LX_core::RenderPassStage::Compute ||
        dispatch != LX_core::RenderPassDispatch::Compute) {
      addDiagnostic(result, field + ".kind",
                    "compute-dispatch input requires compute dispatch");
      return std::nullopt;
    }
    auto object =
        parseObjectInputFilter(node["object"], result, field + ".object");
    auto material =
        parseMaterialInputFilter(node["material"], result, field + ".material");
    std::optional<LX_core::RenderPathGeometryContract> geometry;
    bool geometryValid = true;
    if (node["geometry"]) {
      auto parsedGeometry =
          parseGeometryContract(node["geometry"], result, field + ".geometry");
      if (parsedGeometry.has_value()) {
        geometry = *parsedGeometry;
      } else {
        geometryValid = false;
      }
    }
    if (!valid || !object.has_value() || !material.has_value() ||
        !geometryValid) {
      return std::nullopt;
    }
    if (!node["batching"]) {
      input.batching.mode = LX_core::RenderPassBatchingMode::All;
    }
    if (input.batching.mode != LX_core::RenderPassBatchingMode::All) {
      addDiagnostic(result, field + ".batching",
                    "compute-dispatch input only accepts batching.mode all");
      return std::nullopt;
    }
    input.object = std::move(*object);
    input.material = std::move(*material);
    input.geometry = geometry;
    return input;
  }

  auto object =
      parseObjectInputFilter(node["object"], result, field + ".object");
  auto material =
      parseMaterialInputFilter(node["material"], result, field + ".material");
  std::optional<LX_core::RenderPathGeometryContract> geometry;
  bool geometryValid = true;
  if (node["geometry"]) {
    auto parsedGeometry =
        parseGeometryContract(node["geometry"], result, field + ".geometry");
    if (parsedGeometry.has_value()) {
      geometry = *parsedGeometry;
    } else {
      geometryValid = false;
    }
  }
  if (!valid || !kind.has_value() || !object.has_value() ||
      !material.has_value() || !geometryValid) {
    return std::nullopt;
  }

  input.object = std::move(*object);
  input.material = std::move(*material);
  input.geometry = geometry;

  if (input.batching.mode == LX_core::RenderPassBatchingMode::All) {
    addDiagnostic(result, field + ".batching",
                  "batching.mode all is reserved for scene-wide compute-dispatch input");
    return std::nullopt;
  }
  if (!input.geometry.has_value()) {
    addDiagnostic(result, field + ".geometry",
                  "scene-renderables input requires geometry");
    return std::nullopt;
  }
  if (stage != LX_core::RenderPassStage::Raster ||
      dispatch != LX_core::RenderPassDispatch::Draw) {
    addDiagnostic(result, field + ".kind",
                  "scene-renderables input requires raster draw");
    return std::nullopt;
  }

  return input;
}

std::optional<LX_core::ImageFormat>
parseImageFormat(const YAML::Node &node, RenderPassNodeParseResult &result,
                 const std::string &field) {
  const std::string value = node.as<std::string>();
  if (value == "RGBA8") {
    return LX_core::ImageFormat::RGBA8;
  }
  if (value == "RGBA8Srgb" || value == "RGBA8_SRGB") {
    return LX_core::ImageFormat::RGBA8Srgb;
  }
  if (value == "RG16F" || value == "RG16Float") {
    return LX_core::ImageFormat::RG16Float;
  }
  if (value == "RGBA16F" || value == "RGBA16Float") {
    return LX_core::ImageFormat::RGBA16Float;
  }
  if (value == "BGRA8") {
    return LX_core::ImageFormat::BGRA8;
  }
  if (value == "BGRA8Srgb" || value == "BGRA8_SRGB") {
    return LX_core::ImageFormat::BGRA8Srgb;
  }
  if (value == "R8") {
    return LX_core::ImageFormat::R8;
  }
  if (value == "D32Float") {
    return LX_core::ImageFormat::D32Float;
  }
  if (value == "D24UnormS8") {
    return LX_core::ImageFormat::D24UnormS8;
  }
  if (value == "D32FloatS8") {
    return LX_core::ImageFormat::D32FloatS8;
  }
  addDiagnostic(result, field, "unknown image format");
  return std::nullopt;
}

std::vector<LX_core::RenderPathAttachmentContract>
parseAttachmentContracts(const YAML::Node &node,
                         RenderPassNodeParseResult &result,
                         const std::string &field) {
  std::vector<LX_core::RenderPathAttachmentContract> attachments;
  if (!node) {
    return attachments;
  }
  if (!node.IsSequence()) {
    addDiagnostic(result, field, "attachments must be a sequence");
    return attachments;
  }
  attachments.reserve(node.size());
  for (std::size_t i = 0; i < node.size(); ++i) {
    const YAML::Node attachmentNode = node[i];
    const std::string prefix = field + "[" + std::to_string(i) + "]";
    if (!attachmentNode || !attachmentNode.IsMap()) {
      addDiagnostic(result, prefix, "attachment must be a map");
      continue;
    }
    bool valid = true;
    for (auto it = attachmentNode.begin(); it != attachmentNode.end(); ++it) {
      if (!it->first.IsScalar()) {
        addDiagnostic(result, prefix,
                      "attachment field names must be scalar strings");
        valid = false;
        continue;
      }
      const std::string key = it->first.as<std::string>();
      if (key == "target" || key == "format" || key == "samples" ||
          key == "layers" || key == "depth" || key == "attachmentUsage") {
        continue;
      }
      addDiagnostic(result, prefix + "." + key,
                    "unsupported attachment contract field");
      valid = false;
    }
    valid &= requireField(attachmentNode["target"], result, prefix + ".target");
    valid &= requireField(attachmentNode["format"], result, prefix + ".format");
    valid &=
        requireField(attachmentNode["samples"], result, prefix + ".samples");
    valid &= requireField(attachmentNode["layers"], result, prefix + ".layers");
    if (!valid) {
      continue;
    }
    auto format =
        parseImageFormat(attachmentNode["format"], result, prefix + ".format");
    if (!format.has_value()) {
      continue;
    }
    LX_core::RenderPathAttachmentContract attachment;
    attachment.target = attachmentNode["target"].as<std::string>();
    attachment.format = *format;
    attachment.samples = attachmentNode["samples"].as<u32>();
    attachment.layers = attachmentNode["layers"].as<u32>();
    if (const auto depth = attachmentNode["depth"]) {
      attachment.depth = depth.as<bool>();
    }
    attachment.attachmentUsage =
        attachment.depth
            ? LX_core::RenderPathAttachmentUsage::DepthAttachmentWrite
            : LX_core::RenderPathAttachmentUsage::ColorAttachmentWrite;
    if (const auto usage = attachmentNode["attachmentUsage"]) {
      auto parsedUsage =
          parseAttachmentUsage(usage, result, prefix + ".attachmentUsage");
      if (!parsedUsage.has_value()) {
        continue;
      }
      attachment.attachmentUsage = *parsedUsage;
    }
    const bool depthUsage = attachmentUsageIsDepth(attachment.attachmentUsage);
    if (attachment.depth && !depthUsage) {
      addDiagnostic(result, prefix + ".attachmentUsage",
                    "depth attachment requires depth attachment usage");
      continue;
    }
    if (!attachment.depth && depthUsage) {
      addDiagnostic(result, prefix + ".attachmentUsage",
                    "color attachment cannot use depth attachment usage");
      continue;
    }
    attachments.push_back(std::move(attachment));
  }
  return attachments;
}

void validateAttachmentUsageAgainstTargets(
    const std::vector<LX_core::RenderPathAttachmentContract> &attachments,
    const std::vector<std::string> &targets, RenderPassNodeParseResult &result,
    const std::string &fieldPrefix) {
  for (const auto &attachment : attachments) {
    const bool inTargets = std::find(targets.begin(), targets.end(),
                                     attachment.target) != targets.end();
    if (attachment.attachmentUsage ==
            LX_core::RenderPathAttachmentUsage::DepthAttachmentReadOnly &&
        inTargets) {
      addDiagnostic(result,
                    fieldPrefix + ".rendering.attachments." + attachment.target,
                    "read-only depth attachment must not be listed in "
                    "targets");
    }
  }
}

void validateResourceVocabulary(const std::vector<std::string> &resources,
                                RenderPassNodeParseResult &result,
                                const std::string &field) {
  for (const std::string &resource : resources) {
    if (!isKnownRenderPathResourceName(resource)) {
      addDiagnostic(result, field + "." + resource,
                    "unknown RenderPath resource name");
    }
  }
}

[[nodiscard]] bool isBakeResourceName(const std::string &name) {
  return name.rfind("bake.", 0) == 0;
}

[[nodiscard]] bool isBakeReadbackTarget(const std::string &name) {
  return name == "bake.environment.diffuse_sh9" ||
         name == "bake.environment.specular_prefilter" ||
         name == "bake.material.brdf_lut";
}

[[nodiscard]] bool passTargetsBakeReadbackResource(
    const std::vector<std::string> &targets) {
  return std::any_of(targets.begin(), targets.end(), isBakeReadbackTarget);
}

[[nodiscard]] bool isKnownBakeReadbackFormat(const std::string &format) {
  return format == "RGBA16Float" || format == "RGBA32Float" ||
         format == "RG16Float" || format == "SH9RgbFloat";
}

std::optional<LX_core::RenderPathOutputKind>
parseReadbackKind(const std::string &kind) {
  if (kind == "buffer") {
    return LX_core::RenderPathOutputKind::Buffer;
  }
  if (kind == "image2d") {
    return LX_core::RenderPathOutputKind::Image2D;
  }
  if (kind == "cubemap") {
    return LX_core::RenderPathOutputKind::Cubemap;
  }
  if (kind == "sh9") {
    return LX_core::RenderPathOutputKind::Sh9;
  }
  if (kind == "texture2d") {
    return LX_core::RenderPathOutputKind::Texture2D;
  }
  return std::nullopt;
}

[[nodiscard]] bool
bakeReadbackMatchesTarget(const LX_core::RenderPathReadbackContract &readback) {
  if (readback.target == "bake.environment.cubemap" ||
      readback.target == "bake.environment.specular_prefilter") {
    return readback.format == "RGBA16Float" &&
           readback.kind == LX_core::RenderPathOutputKind::Cubemap;
  }
  if (readback.target == "bake.environment.diffuse_sh9") {
    return readback.format == "SH9RgbFloat" &&
           readback.kind == LX_core::RenderPathOutputKind::Sh9;
  }
  if (readback.target == "bake.material.brdf_lut") {
    return readback.format == "RG16Float" &&
           readback.kind == LX_core::RenderPathOutputKind::Texture2D;
  }
  return true;
}

std::optional<LX_core::Vec3u>
parseLocalSize(const YAML::Node &node, RenderPassNodeParseResult &result,
               const std::string &field) {
  if (!node || !node.IsSequence() || node.size() != 3) {
    addDiagnostic(result, field, "must be a sequence of three integers");
    return std::nullopt;
  }
  return LX_core::Vec3u{node[0].as<u32>(), node[1].as<u32>(),
                        node[2].as<u32>()};
}

std::optional<LX_core::RenderPassComputeContract>
parseComputeContract(const YAML::Node &node, RenderPassNodeParseResult &result,
                     const std::string &field) {
  if (!node || !node.IsMap()) {
    addDiagnostic(result, field, "missing required compute contract");
    return std::nullopt;
  }
  bool valid = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, field, "compute field names must be scalar strings");
      valid = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "dispatchFrom" || key == "localSize") {
      continue;
    }
    addDiagnostic(result, field + "." + key,
                  "unsupported compute contract field");
    valid = false;
  }
  valid &= requireField(node["dispatchFrom"], result, field + ".dispatchFrom");
  valid &= requireField(node["localSize"], result, field + ".localSize");
  auto localSize = parseLocalSize(node["localSize"], result, field + ".localSize");
  if (!valid || !localSize.has_value()) {
    return std::nullopt;
  }
  LX_core::RenderPassComputeContract compute;
  compute.dispatchFrom = node["dispatchFrom"].as<std::string>();
  compute.localSize = *localSize;
  if (compute.dispatchFrom.empty()) {
    addDiagnostic(result, field + ".dispatchFrom", "must not be empty");
    return std::nullopt;
  }
  if (compute.localSize.x == 0u || compute.localSize.y == 0u ||
      compute.localSize.z == 0u) {
    addDiagnostic(result, field + ".localSize", "must be non-zero");
    return std::nullopt;
  }
  return compute;
}

std::vector<LX_core::RenderPathReadbackContract> parseReadbackContracts(
    const YAML::Node &node, const std::vector<std::string> &targets,
    RenderPassNodeParseResult &result, const std::string &field) {
  std::vector<LX_core::RenderPathReadbackContract> readbacks;
  if (!node) {
    return readbacks;
  }
  if (!node.IsSequence()) {
    addDiagnostic(result, field, "readbacks must be a sequence");
    return readbacks;
  }
  readbacks.reserve(node.size());
  for (std::size_t i = 0; i < node.size(); ++i) {
    const YAML::Node readbackNode = node[i];
    const std::string prefix = field + "[" + std::to_string(i) + "]";
    if (!readbackNode || !readbackNode.IsMap()) {
      addDiagnostic(result, prefix, "readback must be a map");
      continue;
    }
    bool valid = true;
    for (auto it = readbackNode.begin(); it != readbackNode.end(); ++it) {
      if (!it->first.IsScalar()) {
        addDiagnostic(result, prefix,
                      "readback field names must be scalar strings");
        valid = false;
        continue;
      }
      const std::string key = it->first.as<std::string>();
      if (key == "name" || key == "target" || key == "extentFrom" ||
          key == "binding" || key == "format" || key == "kind" ||
          key == "mediaType") {
        continue;
      }
      addDiagnostic(result, prefix + "." + key,
                    "unsupported readback contract field");
      valid = false;
    }
    valid &= requireField(readbackNode["name"], result, prefix + ".name");
    valid &= requireField(readbackNode["target"], result, prefix + ".target");
    valid &=
        requireField(readbackNode["extentFrom"], result, prefix + ".extentFrom");
    valid &= requireField(readbackNode["format"], result, prefix + ".format");
    valid &= requireField(readbackNode["kind"], result, prefix + ".kind");
    valid &=
        requireField(readbackNode["mediaType"], result, prefix + ".mediaType");
    if (!valid) {
      continue;
    }
    LX_core::RenderPathReadbackContract readback;
    readback.name = readbackNode["name"].as<std::string>();
    readback.target = readbackNode["target"].as<std::string>();
    readback.extentFrom = readbackNode["extentFrom"].as<std::string>();
    if (readbackNode["binding"]) {
      readback.binding = readbackNode["binding"].as<std::string>();
    }
    readback.format = readbackNode["format"].as<std::string>();
    readback.mediaType = readbackNode["mediaType"].as<std::string>();
    auto kind = parseReadbackKind(readbackNode["kind"].as<std::string>());
    if (!kind.has_value()) {
      addDiagnostic(result, prefix + ".kind", "unknown readback kind");
      valid = false;
    } else {
      readback.kind = *kind;
    }
    if (readback.name.empty()) {
      addDiagnostic(result, prefix + ".name", "must not be empty");
      valid = false;
    }
    if (readback.target.empty()) {
      addDiagnostic(result, prefix + ".target", "must not be empty");
      valid = false;
    }
    if (readback.extentFrom.empty()) {
      addDiagnostic(result, prefix + ".extentFrom", "must not be empty");
      valid = false;
    }
    if (readback.mediaType.empty()) {
      addDiagnostic(result, prefix + ".mediaType", "must not be empty");
      valid = false;
    }
    if (std::find(targets.begin(), targets.end(), readback.target) ==
        targets.end()) {
      addDiagnostic(result, prefix + ".target",
                    "readback target must be listed in targets");
      valid = false;
    }
    if (!isKnownBakeReadbackFormat(readback.format)) {
      addDiagnostic(result, prefix + ".format", "unknown readback format");
      valid = false;
    }
    if (valid && !bakeReadbackMatchesTarget(readback)) {
      addDiagnostic(result, prefix,
                    "readback format/kind does not match target");
      valid = false;
    }
    if (valid) {
      readbacks.push_back(std::move(readback));
    }
  }
  return readbacks;
}

} // namespace

RenderPassNodeParseResult
parseRenderPassNodeContract(const std::string &passName, const YAML::Node &node,
                            const std::string &fieldPrefix) {
  RenderPassNodeParseResult result;
  if (!node || !node.IsMap()) {
    addDiagnostic(result, fieldPrefix, "pass must be a map");
    return result;
  }

  rejectUnsupportedFields(node, result, fieldPrefix);
  requireField(node["shader"], result, fieldPrefix + ".shader");
  requireField(node["stage"], result, fieldPrefix + ".stage");
  requireField(node["dispatch"], result, fieldPrefix + ".dispatch");
  requireField(node["input"], result, fieldPrefix + ".input");
  requireField(node["sources"], result, fieldPrefix + ".sources");
  requireField(node["targets"], result, fieldPrefix + ".targets");
  auto renderState = parseRenderState(node["renderState"], result,
                                      fieldPrefix + ".renderState");
  if (!result.diagnostics.empty()) {
    return result;
  }

  auto stage = parseStage(node["stage"].as<std::string>());
  if (!stage.has_value()) {
    addDiagnostic(result, fieldPrefix + ".stage", "unknown stage");
    return result;
  }
  auto dispatch = parseDispatch(node["dispatch"].as<std::string>());
  if (!dispatch.has_value()) {
    addDiagnostic(result, fieldPrefix + ".dispatch", "unknown dispatch");
    return result;
  }
  std::optional<LX_core::RenderPathNodeRenderingMode> renderingMode;
  if (*stage == LX_core::RenderPassStage::Raster) {
    requireField(node["rendering"], result, fieldPrefix + ".rendering");
    renderingMode = parseRenderingMode(node["rendering"], result,
                                       fieldPrefix + ".rendering");
  }
  auto input = parseInputContract(node["input"], result, fieldPrefix + ".input",
                                  *stage, *dispatch);
  if (!result.diagnostics.empty()) {
    return result;
  }
  if (!node["sources"].IsSequence()) {
    addDiagnostic(result, fieldPrefix + ".sources", "must be a sequence");
    return result;
  }
  if (!node["targets"].IsSequence()) {
    addDiagnostic(result, fieldPrefix + ".targets", "must be a sequence");
    return result;
  }
  if (node["shader"].as<std::string>().empty()) {
    addDiagnostic(result, fieldPrefix + ".shader", "must not be empty");
    return result;
  }
  if (node["sources"].size() == 0) {
    addDiagnostic(result, fieldPrefix + ".sources", "must not be empty");
    return result;
  }
  if (node["targets"].size() == 0) {
    addDiagnostic(result, fieldPrefix + ".targets", "must not be empty");
    return result;
  }
  std::vector<std::string> sources = parseStringList(node["sources"]);
  std::vector<std::string> targets = parseStringList(node["targets"]);
  validateResourceVocabulary(sources, result, fieldPrefix + ".sources");
  validateResourceVocabulary(targets, result, fieldPrefix + ".targets");
  const bool bakePass = passTargetsBakeReadbackResource(targets);
  if (bakePass && (!node["readbacks"] || node["readbacks"].size() == 0)) {
    addDiagnostic(result, fieldPrefix + ".readbacks",
                  "bake pass requires readback declaration");
  }
  std::optional<LX_core::RenderPassComputeContract> compute;
  if (*stage == LX_core::RenderPassStage::Compute ||
      *dispatch == LX_core::RenderPassDispatch::Compute ||
      input->kind == LX_core::RenderPassInputKind::ComputeDispatch) {
    compute =
        parseComputeContract(node["compute"], result, fieldPrefix + ".compute");
  } else if (node["compute"]) {
    addDiagnostic(result, fieldPrefix + ".compute",
                  "compute contract is only valid for compute dispatch passes");
  }
  auto readbacks = parseReadbackContracts(node["readbacks"], targets, result,
                                          fieldPrefix + ".readbacks");
  const YAML::Node rendering = node["rendering"];
  std::vector<LX_core::RenderPathAttachmentContract> attachments;
  if (rendering) {
    attachments =
        parseAttachmentContracts(rendering["attachments"], result,
                                 fieldPrefix + ".rendering.attachments");
  }
  validateAttachmentUsageAgainstTargets(attachments, targets, result,
                                        fieldPrefix);
  if (!result.diagnostics.empty()) {
    return result;
  }

  LX_core::RenderPassNode pass;
  pass.id = passName;
  pass.shaderUri = node["shader"].as<std::string>();
  pass.stage = *stage;
  pass.dispatch = *dispatch;
  pass.compute = compute;
  pass.input = std::move(*input);
  pass.renderingMode = renderingMode;
  pass.attachments = std::move(attachments);
  pass.sources = std::move(sources);
  pass.targets = std::move(targets);
  pass.readbacks = std::move(readbacks);
  pass.renderState = *renderState;
  if (const auto writeMode = node["writeMode"]) {
    pass.writeMode = writeMode.as<std::string>();
  }
  result.pass = std::move(pass);
  return result;
}

} // namespace LX_infra
