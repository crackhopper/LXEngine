#include "core/frame_graph/render_work_build_context.hpp"

#include "core/offline/offline_render_job.hpp"

#include <stdexcept>

namespace LX_core {

RenderWorkBuildContext
RenderWorkBuildContext::realtime(const Scene &scene) {
  return RenderWorkBuildContext(std::cref(scene));
}

RenderWorkBuildContext RenderWorkBuildContext::realtime(
    const Scene &scene, std::vector<IGpuResourceSharedPtr> sceneResources,
    VisibilityLayerMask visibleMask) {
  return RenderWorkBuildContext(std::cref(scene), std::move(sceneResources),
                                visibleMask);
}

RenderWorkBuildContext
RenderWorkBuildContext::offline(const offline::OfflineRenderJob &job) {
  return RenderWorkBuildContext(std::cref(job));
}

RenderDomain RenderWorkBuildContext::domain() const {
  if (std::holds_alternative<RealtimeSource>(m_source)) {
    return RenderDomain::Realtime;
  }
  return RenderDomain::Offline;
}

const Scene &RenderWorkBuildContext::realtimeScene() const {
  if (const auto *scene = std::get_if<RealtimeSource>(&m_source)) {
    return scene->get();
  }
  throw std::logic_error("RenderWorkBuildContext does not hold a realtime scene");
}

bool RenderWorkBuildContext::hasRealtimeOverrides() const {
  return m_sceneResources.has_value() && m_visibleMask.has_value();
}

const std::vector<IGpuResourceSharedPtr> &
RenderWorkBuildContext::realtimeSceneResources() const {
  if (!m_sceneResources.has_value()) {
    throw std::logic_error("RenderWorkBuildContext has no realtime resources");
  }
  return *m_sceneResources;
}

VisibilityLayerMask RenderWorkBuildContext::realtimeVisibleMask() const {
  if (!m_visibleMask.has_value()) {
    throw std::logic_error("RenderWorkBuildContext has no visibility mask");
  }
  return *m_visibleMask;
}

const offline::OfflineRenderJob &RenderWorkBuildContext::offlineJob() const {
  if (const auto *job = std::get_if<OfflineSource>(&m_source)) {
    return job->get();
  }
  throw std::logic_error("RenderWorkBuildContext does not hold an offline job");
}

RenderWorkBuildContext::RenderWorkBuildContext(RealtimeSource scene)
    : m_source(scene) {}

RenderWorkBuildContext::RenderWorkBuildContext(
    RealtimeSource scene, std::vector<IGpuResourceSharedPtr> sceneResources,
    VisibilityLayerMask visibleMask)
    : m_source(scene), m_sceneResources(std::move(sceneResources)),
      m_visibleMask(visibleMask) {}

RenderWorkBuildContext::RenderWorkBuildContext(OfflineSource job)
    : m_source(job) {}

} // namespace LX_core
