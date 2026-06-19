#include "backend/vulkan/vulkan_post_process_builder.hpp"
#include "backend/vulkan/vulkan_realtime_renderer.hpp"
#include "core/frame_graph/pass.hpp"

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

void testDeferredLightingMaterialUsesSharedIblRuntimeAbi() {
  const LX_core::backend::VulkanPostProcessSettings settings;
  LX_core::backend::VulkanPostProcessBuilder builder(settings);
  const auto material = builder.createDeferredLightingMaterial();
  const auto shader = material->getPassShader(LX_core::Pass_DeferredLighting);
  expect(shader != nullptr, "DeferredLighting material should expose a shader");

  const auto hasBinding = [&](const char *name,
                              LX_core::ShaderPropertyType type, u32 set,
                              u32 binding) {
    const auto found = shader->findBinding(name);
    return found.has_value() && found->get().type == type &&
           found->get().set == set && found->get().binding == binding;
  };

  expect(hasBinding("IrradianceMap", LX_core::ShaderPropertyType::TextureCube,
                    3, 0),
         "DeferredLighting runtime ABI should bind IrradianceMap");
  expect(hasBinding("PrefilteredEnvMap",
                    LX_core::ShaderPropertyType::TextureCube, 3, 1),
         "DeferredLighting runtime ABI should bind PrefilteredEnvMap");
  expect(hasBinding("BrdfLut", LX_core::ShaderPropertyType::Texture2D, 3, 2),
         "DeferredLighting runtime ABI should bind BrdfLut");
  expect(hasBinding("ToneMappingUBO",
                    LX_core::ShaderPropertyType::UniformBuffer, 4, 0),
         "DeferredLighting runtime ABI should bind ToneMappingUBO");
  expect(hasBinding("SurfaceLightingUBO",
                    LX_core::ShaderPropertyType::UniformBuffer, 4, 2),
         "DeferredLighting runtime ABI should bind SurfaceLightingUBO");
  expect(!shader->findBinding("EnvironmentUBO").has_value(),
         "DeferredLighting runtime ABI should not bind retired EnvironmentUBO");
}

LX_core::backend::PreparedRenderStateKey makePreparedKey(
    u64 graphGeneration, u64 resourceGeneration, u64 featureGeneration,
    LX_core::RenderTargetDesc target, u64 sceneNodeGeneration = 4) {
  return LX_core::backend::PreparedRenderStateKey{
      .graphGeneration = graphGeneration,
      .resourceGeneration = resourceGeneration,
      .featureGeneration = featureGeneration,
      .sceneNodeGeneration = sceneNodeGeneration,
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

  const auto first =
      evaluatePreparedRenderStateCache(snapshot, key, 30, 10, 20);
  expect(first.rebuildFrameGraph,
         "first frame should compile the frame graph");
  expect(first.rebuildRenderInputs,
         "first frame should build and prepare render inputs");
  expect(first.rebuildDescriptorUploadPlans,
         "first frame should build descriptor upload plans");
  expect(first.syncUploadPlans, "first frame should sync upload plans");
  expect(!first.syncVolatileResources,
         "first frame full upload sync should cover volatile resources");
  expect(!first.touchCachedUploadResources,
         "first frame has no cached upload resources to refresh");

  snapshot = first.nextSnapshot;
  const auto repeat =
      evaluatePreparedRenderStateCache(snapshot, key, 30, 10, 20);
  expect(!repeat.rebuildFrameGraph,
         "static repeated frame should not compile the frame graph");
  expect(!repeat.rebuildRenderInputs,
         "static repeated frame should not rebuild render inputs");
  expect(!repeat.rebuildDescriptorUploadPlans,
         "static repeated frame should not rebuild descriptor upload plans");
  expect(!repeat.syncUploadPlans,
         "static repeated frame should not sync unchanged upload plans");
  expect(!repeat.syncVolatileResources,
         "static repeated frame should not sync clean volatile resources");
  expect(repeat.touchCachedUploadResources,
         "static repeated frame should refresh cached upload resource liveness");

  const auto descriptorDirty =
      evaluatePreparedRenderStateCache(snapshot, key, 30, 11, 20);
  expect(!descriptorDirty.rebuildFrameGraph,
         "descriptor-only dirty frame should reuse the compiled frame graph");
  expect(!descriptorDirty.rebuildRenderInputs,
         "descriptor-only dirty frame should reuse prepared render inputs");
  expect(descriptorDirty.rebuildDescriptorUploadPlans,
         "descriptor upload generation change should rebuild descriptor upload plans");
  expect(descriptorDirty.syncUploadPlans,
         "descriptor upload generation change should sync upload plans");
  expect(!descriptorDirty.syncVolatileResources,
         "descriptor upload sync should cover volatile resources");
  expect(!descriptorDirty.touchCachedUploadResources,
         "descriptor upload sync should refresh resource liveness itself");

  snapshot = descriptorDirty.nextSnapshot;
  key.resourceGeneration += 1;
  const auto resourceDirty =
      evaluatePreparedRenderStateCache(snapshot, key, 30, 11, 20);
  expect(!resourceDirty.rebuildFrameGraph,
         "resource generation change should reuse the compiled frame graph");
  expect(resourceDirty.rebuildRenderInputs,
         "resource generation change should rebuild render inputs");
  expect(resourceDirty.rebuildDescriptorUploadPlans,
         "resource generation change should rebuild descriptor upload plans");
  expect(resourceDirty.syncUploadPlans,
         "resource generation change should sync upload plans");
  expect(!resourceDirty.syncVolatileResources,
         "resource generation change full upload sync should cover volatile resources");
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
      snapshot, makePreparedKey(1, 2, 3, srgbTarget), 30, 10, 20);
  snapshot = first.nextSnapshot;

  const auto targetDirty = evaluatePreparedRenderStateCache(
      snapshot, makePreparedKey(1, 2, 3, unormTarget), 30, 10, 20);
  expect(targetDirty.rebuildFrameGraph,
         "swapchain target shape change should compile the frame graph");
  expect(targetDirty.rebuildRenderInputs,
         "swapchain target shape change should rebuild render inputs");
  expect(targetDirty.rebuildDescriptorUploadPlans,
         "swapchain target shape change should rebuild descriptor upload plans");
  expect(targetDirty.syncUploadPlans,
         "swapchain target shape change should sync upload plans");
}

void testPreparedRenderStateCacheReusesFrameGraphOnSceneNodeGeneration() {
  using LX_core::backend::PreparedRenderStateCacheSnapshot;
  using LX_core::backend::evaluatePreparedRenderStateCache;

  PreparedRenderStateCacheSnapshot snapshot;
  const auto target = LX_core::RenderTargetDesc::swapchain(
      LX_core::ImageFormat::BGRA8Srgb, LX_core::ImageFormat::D32Float);
  const auto first = evaluatePreparedRenderStateCache(
      snapshot, makePreparedKey(1, 2, 3, target, 4), 30, 10, 20);
  snapshot = first.nextSnapshot;

  const auto nodeDirty = evaluatePreparedRenderStateCache(
      snapshot, makePreparedKey(1, 2, 3, target, 5), 30, 10, 20);
  expect(!nodeDirty.rebuildFrameGraph,
         "scene-node generation change should reuse the compiled frame graph");
  expect(nodeDirty.rebuildRenderInputs,
         "scene-node generation change should rebuild render inputs");
  expect(nodeDirty.rebuildDescriptorUploadPlans,
         "scene-node generation change should rebuild descriptor upload plans");
  expect(nodeDirty.syncUploadPlans,
         "scene-node generation change should sync upload plans");
}

void testPreparedRenderStateCacheSplitsDescriptorAndVolatileUploadDirty() {
  using LX_core::backend::PreparedRenderStateCacheSnapshot;
  using LX_core::backend::evaluatePreparedRenderStateCache;

  PreparedRenderStateCacheSnapshot snapshot;
  const auto target = LX_core::RenderTargetDesc::swapchain(
      LX_core::ImageFormat::BGRA8Srgb, LX_core::ImageFormat::D32Float);
  const auto key = makePreparedKey(1, 2, 3, target, 4);
  const auto first =
      evaluatePreparedRenderStateCache(snapshot, key, 30, 10, 20);
  snapshot = first.nextSnapshot;

  const auto bakedResourceDirty =
      evaluatePreparedRenderStateCache(snapshot, key, 31, 11, 20);
  expect(!bakedResourceDirty.rebuildFrameGraph,
         "baked-resource upload dirty should not compile the frame graph");
  expect(!bakedResourceDirty.rebuildRenderInputs,
         "baked-resource upload dirty should reuse prepared render inputs");
  expect(bakedResourceDirty.rebuildDescriptorUploadPlans,
         "baked-resource upload dirty should rebuild descriptor upload plans");
  expect(bakedResourceDirty.syncUploadPlans,
         "baked-resource upload dirty should sync full upload plans");
  expect(!bakedResourceDirty.syncVolatileResources,
         "baked-resource upload dirty should not use the volatile-only path");

  snapshot = first.nextSnapshot;
  const auto valueOnlyDirty =
      evaluatePreparedRenderStateCache(snapshot, key, 30, 10, 21);
  expect(!valueOnlyDirty.rebuildFrameGraph,
         "value-only volatile dirty should not compile the frame graph");
  expect(!valueOnlyDirty.rebuildRenderInputs,
         "value-only volatile dirty should not rebuild render inputs");
  expect(!valueOnlyDirty.rebuildDescriptorUploadPlans,
         "value-only volatile dirty should not rebuild descriptor upload plans");
  expect(!valueOnlyDirty.syncUploadPlans,
         "value-only volatile dirty should not sync full upload plans");
  expect(valueOnlyDirty.syncVolatileResources,
         "value-only volatile dirty should sync only dirty host buffers");
  expect(valueOnlyDirty.touchCachedUploadResources,
         "value-only volatile dirty should refresh cached static resource liveness");
}

} // namespace

int main() {
  testStandardPostProcessUsesConfiguredExposure();
  testStandardPostProcessKeepsOutputEncodingGamma();
  testDeferredLightingMaterialUsesSharedIblRuntimeAbi();
  testPreparedRenderStateCacheSkipsStaticFrameWork();
  testPreparedRenderStateCacheInvalidatesOnTargetShapeChange();
  testPreparedRenderStateCacheReusesFrameGraphOnSceneNodeGeneration();
  testPreparedRenderStateCacheSplitsDescriptorAndVolatileUploadDirty();
  return 0;
}
