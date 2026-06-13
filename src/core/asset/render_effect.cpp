#include "core/asset/render_effect.hpp"

namespace LX_core {
namespace {

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
  return GlobalStringTable::get().compose(TypeTag::ObjectRender, fields);
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

StringID getRenderPathNodeSignature(const RenderPassNode &node) {
  std::vector<StringID> fields;
  fields.reserve(8 + node.sources.size() + node.targets.size() +
                 node.attachments.size());
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
  if (node.geometry.has_value()) {
    fields.push_back(geometrySignature(*node.geometry));
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
  return GlobalStringTable::get().compose(TypeTag::PipelineKey, fields);
}

} // namespace LX_core
