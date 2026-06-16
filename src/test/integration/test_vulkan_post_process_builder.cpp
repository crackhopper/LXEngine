#include "backend/vulkan/vulkan_post_process_builder.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
  }
}

void expectNear(float actual, float expected, const char *message) {
  if (std::abs(actual - expected) > 1.0e-5f) {
    std::cerr << "[FAIL] " << message << " actual=" << actual
              << " expected=" << expected << '\n';
    std::exit(1);
  }
}

void testStandardPostProcessUsesConfiguredExposure() {
  LX_core::backend::VulkanPostProcessSettings settings;
  settings.exposure = 1.35f;
  LX_core::backend::VulkanPostProcessBuilder builder(settings);

  const auto material = builder.createStandardPostProcessMaterial(
      LX_core::backend::VulkanPostProcessOutputEncoding::Srgb);
  const auto value = material->readShaderBindingParameterValue(
      LX_core::StringID("PostProcessUBO"), LX_core::StringID("exposure"));

  expect(value.has_value(), "PostProcessUBO.exposure should be readable");
  expect(value->type == LX_core::MaterialParameterValueType::Float,
         "PostProcessUBO.exposure should be a float");
  expectNear(value->floatValue, 1.35f,
             "PostProcessUBO.exposure should come from settings");
}

void testStandardPostProcessKeepsOutputEncodingGamma() {
  const LX_core::backend::VulkanPostProcessSettings settings;
  LX_core::backend::VulkanPostProcessBuilder builder(settings);

  const auto material = builder.createStandardPostProcessMaterial(
      LX_core::backend::VulkanPostProcessOutputEncoding::Linear);
  const auto value = material->readShaderBindingParameterValue(
      LX_core::StringID("PostProcessUBO"), LX_core::StringID("gamma"));

  expect(value.has_value(), "PostProcessUBO.gamma should be readable");
  expect(value->type == LX_core::MaterialParameterValueType::Float,
         "PostProcessUBO.gamma should be a float");
  expectNear(value->floatValue, 1.0f,
             "linear output encoding should keep gamma at 1.0");
}

} // namespace

int main() {
  testStandardPostProcessUsesConfiguredExposure();
  testStandardPostProcessKeepsOutputEncodingGamma();
  return 0;
}
