#include "core/offline/offline_render_work_graph.hpp"

#include "core/frame_graph/pass.hpp"

namespace LX_core::offline {
namespace {

[[nodiscard]] StringID offlineComputePassName() {
  return StringID("OfflineCompute");
}

} // namespace

FrameGraph createOfflineRenderFrameGraph(const OutputProfile &output) {
  (void)output;
  FrameGraph graph;

  FramePass pass;
  pass.name = offlineComputePassName();
  pass.target = RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float);
  pass.stage = RenderPassStage::Compute;
  pass.dispatch = RenderPassDispatch::Compute;
  pass.input.kind = RenderPassInputKind::ComputeDispatch;

  graph.addPass(std::move(pass));
  return graph;
}

} // namespace LX_core::offline
