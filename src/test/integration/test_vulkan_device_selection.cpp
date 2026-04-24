#include "backend/vulkan/details/device.hpp"
#include "core/utils/env.hpp"

#include <iostream>

using namespace LX_core::backend;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testDiscretePreferredOverIntegrated() {
  const int discrete = VulkanDevice::getPhysicalDevicePreferenceScoreForTesting(
      VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
  const int integrated =
      VulkanDevice::getPhysicalDevicePreferenceScoreForTesting(
          VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);

  EXPECT(discrete > integrated,
         "discrete GPU score must rank above integrated GPU score");
}

void testIntegratedStillRanksAboveCpuFallback() {
  const int integrated =
      VulkanDevice::getPhysicalDevicePreferenceScoreForTesting(
          VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
  const int cpu = VulkanDevice::getPhysicalDevicePreferenceScoreForTesting(
      VK_PHYSICAL_DEVICE_TYPE_CPU);

  EXPECT(integrated > cpu,
         "integrated GPU score must still rank above CPU fallback");
}

} // namespace

int main() {
  expSetEnvVK();
  testDiscretePreferredOverIntegrated();
  testIntegratedStillRanksAboveCpuFallback();

  if (failures == 0) {
    std::cout << "[PASS] All Vulkan device selection tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed.\n";
  }
  return failures == 0 ? 0 : 1;
}
