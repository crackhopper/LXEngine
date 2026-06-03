#include "backend/vulkan/offline/offline_integrator.hpp"
#include "core/offline/offline_render_profile.hpp"

#include <iostream>

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
}

void testPrimaryRayNameIsRejected() {
  EXPECT(!backend::offline::isOfflineIntegratorSupported("primary-ray"),
         "primary-ray should not remain as the public integrator name");
}

void testDefaultIntegratorName() {
  const auto settings = offline::makeDefaultOfflineRenderSettings();
  EXPECT(settings.integrator == "software-compute",
         "default offline integrator should be software-compute");
}

} // namespace

int main() {
  testSoftwareComputeNameIsSupported();
  testPrimaryRayNameIsRejected();
  testDefaultIntegratorName();
  if (failures != 0) {
    return 1;
  }
  std::cout << "test_offline_integrator_selection passed\n";
  return 0;
}
