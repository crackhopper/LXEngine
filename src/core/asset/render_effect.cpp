#include "core/asset/render_effect.hpp"

namespace LX_core {
namespace {

[[nodiscard]] const char *inputKindName(RenderPassInputKind kind) {
  switch (kind) {
  case RenderPassInputKind::SceneRenderables:
    return "scene-renderables";
  case RenderPassInputKind::FullscreenTriangle:
    return "fullscreen-triangle";
  case RenderPassInputKind::ComputeDispatch:
    return "compute-dispatch";
  }
  return "unknown";
}

[[nodiscard]] StringID renderingModeSignature(RenderPathNodeRenderingMode mode) {
  return StringID(mode == RenderPathNodeRenderingMode::Dynamic
                      ? "rendering=dynamic"
                      : "rendering=traditional");
}

[[nodiscard]] StringID
geometryVertexSignature(RenderPathGeometryVertexContract vertex) {
  switch (vertex) {
  case RenderPathGeometryVertexContract::PositionOnly:
    return StringID("vertex=position-only");
  case RenderPathGeometryVertexContract::PositionNormalUvTangent:
    return StringID("vertex=position-normal-uv-tangent");
  }
  return StringID("vertex=unknown");
}

[[nodiscard]] StringID geometrySignature(const RenderPathGeometryContract &g) {
  StringID fields[] = {
      geometryVertexSignature(g.vertex),
      topologyPipelineSignature(g.topology),
  };
  return GlobalStringTable::get().compose(TypeTag::RenderPathGeometry, fields);
}

[[nodiscard]] StringID
attachmentSignature(const RenderPathAttachmentContract &attachment) {
  return StringID("attachment:target=" + attachment.target +
                  ";format=" +
                  std::to_string(static_cast<u32>(attachment.format)) +
                  ";samples=" + std::to_string(attachment.samples) +
                  ";layers=" + std::to_string(attachment.layers) +
                  ";depth=" + (attachment.depth ? "true" : "false"));
}

} // namespace

std::optional<std::string>
validateRenderPassInputContract(const RenderPassNode &node) {
  const RenderPassInputContract &input = node.input;
  switch (input.kind) {
  case RenderPassInputKind::SceneRenderables:
    if (node.stage != RenderPassStage::Raster ||
        node.dispatch != RenderPassDispatch::Draw) {
      return std::string("input.kind '") + inputKindName(input.kind) +
             "' requires raster draw";
    }
    if (!input.geometry.has_value()) {
      return "input.geometry is required for scene-renderables input";
    }
    break;
  case RenderPassInputKind::FullscreenTriangle:
    if (node.stage != RenderPassStage::Raster ||
        node.dispatch != RenderPassDispatch::Fullscreen) {
      return std::string("input.kind '") + inputKindName(input.kind) +
             "' requires raster fullscreen";
    }
    if (!input.object.renderClasses.empty()) {
      return "input.object is not accepted for fullscreen-triangle input";
    }
    if (!input.material.types.empty() || !input.material.required) {
      return "input.material is not accepted for fullscreen-triangle input";
    }
    if (input.geometry.has_value()) {
      return "input.geometry is not accepted for fullscreen-triangle input";
    }
    break;
  case RenderPassInputKind::ComputeDispatch:
    if (node.stage != RenderPassStage::Compute ||
        node.dispatch != RenderPassDispatch::Compute) {
      return std::string("input.kind '") + inputKindName(input.kind) +
             "' requires compute dispatch";
    }
    if (!input.object.renderClasses.empty()) {
      return "input.object is not accepted for compute-dispatch input";
    }
    if (!input.material.types.empty() || !input.material.required) {
      return "input.material is not accepted for compute-dispatch input";
    }
    if (input.geometry.has_value()) {
      return "input.geometry is not accepted for compute-dispatch input";
    }
    break;
  }
  return std::nullopt;
}

StringID getRenderPathNodeSignature(const RenderPassNode &node) {
  std::vector<StringID> fields;
  const RenderPassInputContract &input = node.input;
  fields.reserve(12 + input.object.renderClasses.size() +
                 input.material.types.size() + node.sources.size() +
                 node.targets.size() + node.attachments.size());
  fields.push_back(StringID("pass=" + node.id));
  fields.push_back(StringID("shader=" + node.shaderUri.string()));
  fields.push_back(StringID(node.stage == RenderPassStage::Raster
                                ? "stage=raster"
                                : "stage=compute"));
  fields.push_back(StringID(node.dispatch == RenderPassDispatch::Draw
                                ? "dispatch=draw"
                                : node.dispatch == RenderPassDispatch::Fullscreen
                                      ? "dispatch=fullscreen"
                                      : "dispatch=compute"));
  fields.push_back(node.renderState.getPipelineSignature());
  if (node.renderingMode.has_value()) {
    fields.push_back(renderingModeSignature(*node.renderingMode));
  }
  fields.push_back(StringID(input.kind == RenderPassInputKind::SceneRenderables
                                ? "input=scene-renderables"
                                : input.kind ==
                                          RenderPassInputKind::FullscreenTriangle
                                      ? "input=fullscreen-triangle"
                                      : "input=compute-dispatch"));
  for (const std::string &renderClass : input.object.renderClasses) {
    fields.push_back(StringID("object.renderClass=" + renderClass));
  }
  fields.push_back(StringID(input.material.required
                                ? "material.required=true"
                                : "material.required=false"));
  for (const std::string &type : input.material.types) {
    fields.push_back(StringID("material.type=" + type));
  }
  if (input.geometry.has_value()) {
    fields.push_back(geometrySignature(*input.geometry));
  }
  for (const std::string &source : node.sources) {
    fields.push_back(StringID("source=" + source));
  }
  for (const std::string &target : node.targets) {
    fields.push_back(StringID("target=" + target));
  }
  for (const RenderPathAttachmentContract &attachment : node.attachments) {
    fields.push_back(attachmentSignature(attachment));
  }
  return GlobalStringTable::get().compose(TypeTag::RenderPathNode, fields);
}

} // namespace LX_core
