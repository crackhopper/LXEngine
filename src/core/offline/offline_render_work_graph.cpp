#include "core/offline/offline_render_work_graph.hpp"

#include "core/frame_graph/pass.hpp"

namespace LX_core::offline {

FrameGraph buildOfflineRenderWorkGraph(const OfflineRenderJob &job) {
  FrameGraph graph;

  FramePass pass;
  pass.name = Pass_OfflineRayTrace;
  pass.target = RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float);

  RenderWorkItem item;
  item.domain = RenderDomain::Offline;
  item.kind = RenderWorkKind::ComputeDispatch;
  item.pass = Pass_OfflineRayTrace;
  item.target = pass.target;
  item.compute.groupCountX = (job.output.width + 7u) / 8u;
  item.compute.groupCountY = (job.output.height + 7u) / 8u;
  item.compute.groupCountZ = 1u;
  item.debugId = StringID("OfflineRayTraceDispatch");

  pass.queue.addItem(std::move(item));
  graph.addPass(std::move(pass));
  return graph;
}

} // namespace LX_core::offline
