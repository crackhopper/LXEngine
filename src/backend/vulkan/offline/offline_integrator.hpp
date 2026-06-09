#pragma once

#include "core/offline/offline_render_job.hpp"

#include <memory>
#include <string>

namespace LX_core::backend::offline {

class OfflineIntegrator {
public:
  virtual ~OfflineIntegrator() = default;

  [[nodiscard]] virtual LX_core::offline::OfflineReadbackImage
  render(LX_core::offline::OfflineRenderJob &job) = 0;
};

[[nodiscard]] bool isOfflineIntegratorSupported(const std::string &name);

[[nodiscard]] std::unique_ptr<OfflineIntegrator>
createOfflineIntegrator(const std::string &name);

} // namespace LX_core::backend::offline
