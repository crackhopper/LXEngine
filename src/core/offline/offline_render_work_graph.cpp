#include "core/offline/offline_render_work_graph.hpp"

#include "core/frame_graph/pass.hpp"

namespace LX_core::offline {

FrameGraph createOfflineRenderFrameGraph(const OutputProfile &output) {
  (void)output;
  FrameGraph graph;

  FramePass pass;
  pass.name = Pass_OfflineRayTrace;
  pass.target = RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float);

  graph.addPass(std::move(pass));
  return graph;
}

} // namespace LX_core::offline
