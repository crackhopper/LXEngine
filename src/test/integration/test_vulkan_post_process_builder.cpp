#include "backend/vulkan/vulkan_post_process_builder.hpp"
#include "backend/vulkan/vulkan_realtime_renderer.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

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

LX_core::backend::PreparedRenderStateKey makePreparedKey(
    u64 graphGeneration, u64 resourceGeneration, u64 featureGeneration,
    LX_core::RenderTargetDesc target) {
  return LX_core::backend::PreparedRenderStateKey{
      .graphGeneration = graphGeneration,
      .resourceGeneration = resourceGeneration,
      .featureGeneration = featureGeneration,
      .target = std::move(target),
  };
}

void testPreparedRenderStateCacheSkipsStaticFrameWork() {
  using LX_core::backend::PreparedRenderStateCacheSnapshot;
  using LX_core::backend::evaluatePreparedRenderStateCache;

  PreparedRenderStateCacheSnapshot snapshot;
  const auto target = LX_core::RenderTargetDesc::swapchain(
      LX_core::ImageFormat::BGRA8Srgb, LX_core::ImageFormat::D32Float);
  auto key = makePreparedKey(1, 2, 3, target);

  const auto first = evaluatePreparedRenderStateCache(snapshot, key, 10);
  expect(first.rebuildFrameGraph,
         "first frame should compile the frame graph");
  expect(first.rebuildRenderInputs,
         "first frame should build and prepare render inputs");
  expect(first.rebuildDescriptorUploadPlans,
         "first frame should build descriptor upload plans");
  expect(first.syncUploadPlans, "first frame should sync upload plans");

  snapshot = first.nextSnapshot;
  const auto repeat = evaluatePreparedRenderStateCache(snapshot, key, 10);
  expect(!repeat.rebuildFrameGraph,
         "static repeated frame should not compile the frame graph");
  expect(!repeat.rebuildRenderInputs,
         "static repeated frame should not rebuild render inputs");
  expect(!repeat.rebuildDescriptorUploadPlans,
         "static repeated frame should not rebuild descriptor upload plans");
  expect(!repeat.syncUploadPlans,
         "static repeated frame should not sync unchanged upload plans");

  const auto uploadDirty =
      evaluatePreparedRenderStateCache(snapshot, key, 11);
  expect(!uploadDirty.rebuildFrameGraph,
         "upload-only dirty frame should reuse the compiled frame graph");
  expect(!uploadDirty.rebuildRenderInputs,
         "upload-only dirty frame should reuse prepared render inputs");
  expect(uploadDirty.rebuildDescriptorUploadPlans,
         "upload generation change should rebuild descriptor upload plans");
  expect(uploadDirty.syncUploadPlans,
         "upload generation change should sync upload plans");

  snapshot = uploadDirty.nextSnapshot;
  key.resourceGeneration += 1;
  const auto resourceDirty =
      evaluatePreparedRenderStateCache(snapshot, key, 11);
  expect(resourceDirty.rebuildFrameGraph,
         "resource generation change should compile the frame graph");
  expect(resourceDirty.rebuildRenderInputs,
         "resource generation change should rebuild render inputs");
  expect(resourceDirty.rebuildDescriptorUploadPlans,
         "resource generation change should rebuild descriptor upload plans");
  expect(resourceDirty.syncUploadPlans,
         "resource generation change should sync upload plans");
}

void testPreparedRenderStateCacheInvalidatesOnTargetShapeChange() {
  using LX_core::backend::PreparedRenderStateCacheSnapshot;
  using LX_core::backend::evaluatePreparedRenderStateCache;

  PreparedRenderStateCacheSnapshot snapshot;
  const auto srgbTarget = LX_core::RenderTargetDesc::swapchain(
      LX_core::ImageFormat::BGRA8Srgb, LX_core::ImageFormat::D32Float);
  const auto unormTarget = LX_core::RenderTargetDesc::swapchain(
      LX_core::ImageFormat::BGRA8, LX_core::ImageFormat::D32Float);
  const auto first = evaluatePreparedRenderStateCache(
      snapshot, makePreparedKey(1, 2, 3, srgbTarget), 10);
  snapshot = first.nextSnapshot;

  const auto targetDirty = evaluatePreparedRenderStateCache(
      snapshot, makePreparedKey(1, 2, 3, unormTarget), 10);
  expect(targetDirty.rebuildFrameGraph,
         "swapchain target shape change should compile the frame graph");
  expect(targetDirty.rebuildRenderInputs,
         "swapchain target shape change should rebuild render inputs");
  expect(targetDirty.rebuildDescriptorUploadPlans,
         "swapchain target shape change should rebuild descriptor upload plans");
  expect(targetDirty.syncUploadPlans,
         "swapchain target shape change should sync upload plans");
}

} // namespace

int main() {
  testStandardPostProcessUsesConfiguredExposure();
  testStandardPostProcessKeepsOutputEncodingGamma();
  testPreparedRenderStateCacheSkipsStaticFrameWork();
  testPreparedRenderStateCacheInvalidatesOnTargetShapeChange();
  return 0;
}
