#include "backend/vulkan/offline/offline_integrator.hpp"
#include "backend/vulkan/offline/vulkan_offline_renderer.hpp"
#include "core/offline/offline_render_job.hpp"
#include "core/offline/offline_render_profile.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace LX_core;

namespace {
int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL: " << msg << '\n';                                    \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testSoftwareComputeNameIsSupported() {
  EXPECT(backend::offline::isOfflineIntegratorSupported("software-compute"),
         "software-compute integrator should be supported");
  auto integrator =
      backend::offline::createOfflineIntegrator("software-compute");
  EXPECT(integrator != nullptr,
         "software-compute integrator factory should create an integrator");
}

void testPrimaryRayNameIsRejected() {
  EXPECT(!backend::offline::isOfflineIntegratorSupported("primary-ray"),
         "primary-ray should not remain as the public integrator name");
  bool rejected = false;
  try {
    (void)backend::offline::createOfflineIntegrator("primary-ray");
  } catch (const std::exception &error) {
    rejected =
        std::string(error.what()).find("unsupported offline integrator") !=
        std::string::npos;
  }
  EXPECT(rejected, "primary-ray factory path should be rejected clearly");
}

void testDefaultIntegratorName() {
  const auto settings = offline::makeDefaultOfflineRenderSettings();
  EXPECT(settings.integrator == "software-compute",
         "default offline integrator should be software-compute");
}

void testRendererRejectsUnsupportedIntegratorBeforeSceneValidation() {
  offline::OfflineRenderJob job;
  job.offline.integrator = "path-tracing";

  backend::offline::VulkanOfflineRenderer renderer;
  bool rejected = false;
  try {
    (void)renderer.render(job);
  } catch (const std::exception &error) {
    rejected =
        std::string(error.what()).find("unsupported offline integrator: "
                                      "path-tracing") != std::string::npos;
  }
  EXPECT(rejected,
         "renderer should reject unsupported integrator before scene validation");
}

} // namespace

int main() {
  testSoftwareComputeNameIsSupported();
  testPrimaryRayNameIsRejected();
  testDefaultIntegratorName();
  testRendererRejectsUnsupportedIntegratorBeforeSceneValidation();
  if (failures != 0) {
    return 1;
  }
  std::cout << "test_offline_integrator_selection passed\n";
  return 0;
}
