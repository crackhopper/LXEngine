#pragma once

#include "backend/vulkan/offline/offline_integrator.hpp"
#include "core/frame_graph/render_input.hpp"

#include <memory>
#include <vector>

namespace LX_core::backend::offline {

void validatePreparedOfflineRenderDescs(
    const std::vector<std::vector<RenderInputDesc>> &passDescs);

class SoftwareComputeOfflineIntegrator final : public OfflineIntegrator {
public:
  SoftwareComputeOfflineIntegrator();
  ~SoftwareComputeOfflineIntegrator() override;

  SoftwareComputeOfflineIntegrator(const SoftwareComputeOfflineIntegrator &) =
      delete;
  SoftwareComputeOfflineIntegrator &
  operator=(const SoftwareComputeOfflineIntegrator &) = delete;

  [[nodiscard]] LX_core::offline::OfflineReadbackImage
  render(LX_core::offline::OfflineRenderJob &job) override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace LX_core::backend::offline
