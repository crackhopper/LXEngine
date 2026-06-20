#include "core/frame_graph/render_work_build_context.hpp"

#include <stdexcept>

namespace LX_core {

RenderWorkBuildContext
RenderWorkBuildContext::empty(RenderDomain domain) {
  return RenderWorkBuildContext(domain, nullptr, Options{});
}

RenderWorkBuildContext
RenderWorkBuildContext::forScene(RenderDomain domain, const Scene &scene) {
  return RenderWorkBuildContext(domain, &scene, Options{});
}

RenderWorkBuildContext RenderWorkBuildContext::forScene(RenderDomain domain,
                                                        const Scene &scene,
                                                        Options options) {
  return RenderWorkBuildContext(domain, &scene, std::move(options));
}

RenderDomain RenderWorkBuildContext::domain() const { return m_domain; }

bool RenderWorkBuildContext::hasScene() const { return m_scene != nullptr; }

const Scene &RenderWorkBuildContext::scene() const {
  if (m_scene == nullptr) {
    throw std::logic_error("RenderWorkBuildContext does not hold a scene");
  }
  return *m_scene;
}

const SceneResourceTable &RenderWorkBuildContext::resourceTable() const {
  return scene().resources();
}

const RenderWorkBuildContext::Options &RenderWorkBuildContext::options() const {
  return m_options;
}

std::optional<Vec3u>
RenderWorkBuildContext::findRuntimeExtent(StringID key) const {
  for (const RuntimeExtent &extent : m_options.runtimeExtents) {
    if (extent.key == key) {
      return extent.extent;
    }
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<const RenderFeatureVolatileValue>>
RenderWorkBuildContext::findFeatureValue(StringID key) const {
  for (const RenderFeatureVolatileValue &value : m_options.featureValues) {
    if (value.key == key) {
      return std::cref(value);
    }
  }
  return std::nullopt;
}

std::optional<std::reference_wrapper<
    const RenderWorkBuildContext::PassPreparationFacts>>
RenderWorkBuildContext::findPassPreparationFacts(StringID pass) const {
  for (const PassPreparationFacts &facts : m_options.passPreparationFacts) {
    if (facts.pass == pass) {
      return std::cref(facts);
    }
  }
  return std::nullopt;
}

RenderWorkBuildContext::RenderWorkBuildContext(RenderDomain domain,
                                               const Scene *scene,
                                               Options options)
    : m_domain(domain), m_scene(scene), m_options(std::move(options)) {}

} // namespace LX_core
