#pragma once

#include "backend/vulkan/offline/offline_integrator.hpp"

#include <memory>

namespace LX_core::backend::offline {

class SoftwareComputeOfflineIntegrator final : public OfflineIntegrator {
public:
  SoftwareComputeOfflineIntegrator();
  ~SoftwareComputeOfflineIntegrator() override;

  SoftwareComputeOfflineIntegrator(const SoftwareComputeOfflineIntegrator &) =
      delete;
  SoftwareComputeOfflineIntegrator &
  operator=(const SoftwareComputeOfflineIntegrator &) = delete;

  [[nodiscard]] LX_core::offline::OfflineReadbackImage
  render(const LX_core::offline::OfflineRenderJob &job) override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace LX_core::backend::offline
