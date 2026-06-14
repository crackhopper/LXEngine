#include "core/frame_graph/render_work_build_context.hpp"

#include "core/offline/offline_render_job.hpp"

#include <stdexcept>

namespace LX_core {

RenderWorkBuildContext RenderWorkBuildContext::realtimeEmpty() {
  return RenderWorkBuildContext();
}

RenderWorkBuildContext RenderWorkBuildContext::realtime(const Scene &scene) {
  return RenderWorkBuildContext(std::cref(scene));
}

RenderWorkBuildContext
RenderWorkBuildContext::realtime(const Scene &scene, RealtimeOptions options) {
  return RenderWorkBuildContext(std::cref(scene), std::move(options));
}

RenderWorkBuildContext
RenderWorkBuildContext::offline(offline::OfflineRenderJob &job) {
  return RenderWorkBuildContext(std::ref(job));
}

RenderDomain RenderWorkBuildContext::domain() const {
  return std::holds_alternative<OfflineSource>(m_source)
             ? RenderDomain::Offline
             : RenderDomain::Realtime;
}

bool RenderWorkBuildContext::hasRealtimeScene() const {
  return std::holds_alternative<RealtimeSource>(m_source);
}

const Scene &RenderWorkBuildContext::realtimeScene() const {
  if (const auto *scene = std::get_if<RealtimeSource>(&m_source)) {
    return scene->get();
  }
  throw std::logic_error(
      "RenderWorkBuildContext does not hold a realtime scene");
}

const RenderWorkBuildContext::RealtimeOptions &
RenderWorkBuildContext::realtimeOptions() const {
  if (domain() != RenderDomain::Realtime) {
    throw std::logic_error(
        "RenderWorkBuildContext does not hold realtime options");
  }
  return m_realtimeOptions;
}

offline::OfflineRenderJob &RenderWorkBuildContext::offlineJob() const {
  if (const auto *job = std::get_if<OfflineSource>(&m_source)) {
    return job->get();
  }
  throw std::logic_error("RenderWorkBuildContext does not hold an offline job");
}

RenderWorkBuildContext::RenderWorkBuildContext()
    : m_source(EmptyRealtimeSource{}) {}

RenderWorkBuildContext::RenderWorkBuildContext(RealtimeSource scene)
    : m_source(scene) {}

RenderWorkBuildContext::RenderWorkBuildContext(RealtimeSource scene,
                                               RealtimeOptions options)
    : m_source(scene), m_realtimeOptions(std::move(options)) {}

RenderWorkBuildContext::RenderWorkBuildContext(OfflineSource job)
    : m_source(job) {}

} // namespace LX_core
