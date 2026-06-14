#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "backend/vulkan/details/device_resources/texture.hpp"
#include "backend/vulkan/details/render_objects/framebuffer.hpp"
#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/asset/texture.hpp"
#include "core/debug_draw/debug_draw.hpp"
#include "core/frame_graph/render_input.hpp"
#include "core/frame_graph/render_upload_plan.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"

#include "scene_test_helpers.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

static_assert(
    std::variant_size_v<
        decltype(std::declval<LX_core::backend::VulkanResourceManager &>()
                     .getOrCreatePipeline(
                         std::declval<
                             const LX_core::RenderInputDesc &>()))> ==
        2,
    "VulkanResourceManager must expose RenderInputDesc-based pipeline "
    "resolution for graphics and compute pipelines");

namespace {

bool isKnownEnvironmentSetupFailure(const std::string_view message) {
  return message.find("No available video device") != std::string_view::npos ||
         message.find("Failed to find GPUs with Vulkan support") !=
             std::string_view::npos ||
         message.find("Failed to find a suitable GPU") !=
             std::string_view::npos ||
         message.find("Failed to create Vulkan surface handle") !=
             std::string_view::npos;
}

struct TestUniformResource final : public LX_core::IGpuResource {
  explicit TestUniformResource(u32 value) : value(value) {}

  LX_core::ResourceType getType() const override {
    return LX_core::ResourceType::UniformBuffer;
  }
  const void *getRawData() const override { return &value; }
  u32 getByteSize() const override { return sizeof(value); }

  u32 value = 0;
};

template <typename T, typename... Args>
std::shared_ptr<T> makePlacementShared(void *storage, Args &&...args) {
  auto *ptr = new (storage) T(std::forward<Args>(args)...);
  return std::shared_ptr<T>(ptr, [](T *p) { p->~T(); });
}

LX_core::IShaderSharedPtr makeDebugOverlayShader() {
  constexpr const char *kShaderName = "debug_overlay";
  std::vector<LX_core::ShaderStageCode> stages{
      LX_test::loadTestShaderStage(kShaderName, "vert.spv",
                                   LX_core::ShaderStage::Vertex),
      LX_test::loadTestShaderStage(kShaderName, "frag.spv",
                                   LX_core::ShaderStage::Fragment),
  };
  return std::make_shared<LX_infra::CompiledShader>(
      stages, LX_infra::ShaderReflector::reflect(stages),
      LX_infra::ShaderReflector::reflectVertexInputs(stages), kShaderName);
}

LX_core::MaterialInstanceSharedPtr makeDebugOverlayMaterial() {
  constexpr const char *kShaderName = "debug_overlay";
  auto shader = makeDebugOverlayShader();

  LX_core::ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = kShaderName;
  shaderProgram.shader = std::move(shader);

  LX_core::MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = std::move(shaderProgram);
  passDefinition.renderState.cullMode = LX_core::CullMode::None;
  passDefinition.renderState.depthTestEnable = false;
  passDefinition.renderState.depthWriteEnable = false;
  passDefinition.renderState.blendEnable = false;

  auto tmpl = LX_core::MaterialTemplate::create("debug_overlay_test");
  tmpl->setPassDefinition(LX_core::Pass_DebugOverlay,
                          std::move(passDefinition));
  tmpl->rebuildMaterialInterface();

  auto material = LX_core::MaterialInstance::create(tmpl);
  material->syncGpuData();
  return material;
}

LX_core::SceneSharedPtr makeOverlayScene() {
  auto scene = LX_core::Scene::create("vulkan_debugdraw_growth");
  auto cameraNode = LX_test::makeDefaultCameraNodeWithTarget();
  const auto camera = cameraNode->getComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    throw std::runtime_error("DebugDraw test camera component missing");
  }
  camera->get().setCullingMask(LX_core::Layer_All);
  camera->get().updateMatrices();
  scene->addCamera(cameraNode);
  LX_core::DebugDraw::reset();
  LX_core::DebugDraw::setMaterialProvider(
      [] { return makeDebugOverlayMaterial(); });
  LX_core::DebugDraw::attachScene(scene);
  return scene;
}

void syncDebugOverlayResources(
    LX_core::backend::VulkanResourceManager &resourceManager,
    LX_core::backend::VulkanCommandBufferManager &cmdBufferMgr,
    const LX_core::RenderDrawInput &input,
    const LX_core::RenderInputDesc &desc) {
  std::vector<std::unique_ptr<LX_core::RenderInput>> inputs;
  inputs.push_back(std::make_unique<LX_core::RenderDrawInput>(input));
  std::vector<LX_core::RenderInputDesc> descs{desc};
  const LX_core::RenderUploadPlan uploadPlan =
      LX_core::buildRenderUploadPlan(inputs, descs);
  for (const auto &resource : uploadPlan.resources) {
    resourceManager.syncResource(cmdBufferMgr, resource);
  }
  resourceManager.collectGarbage();
}

LX_core::RenderDrawInput makeDebugOverlayInputFromScene(
    const LX_core::Scene &scene) {
  for (const auto &renderable : scene.getRenderables()) {
    if (!renderable || !renderable->isDebugOnlyRenderable() ||
        !renderable->supportsPass(LX_core::Pass_DebugOverlay)) {
      continue;
    }
    LX_core::RenderDrawInput input;
    input.source = LX_core::RenderDrawInputSource::SceneRenderable;
    input.pass = LX_core::Pass_DebugOverlay;
    input.debugId = renderable->getDebugId();
    input.vertexBuffer = renderable->getVertexBuffer();
    input.indexBuffer = renderable->getIndexBuffer();
    const auto *indexBuffer =
        input.indexBuffer.isValid()
            ? dynamic_cast<const LX_core::IndexBuffer *>(
                  &input.indexBuffer.get())
            : nullptr;
    if (!input.vertexBuffer.isValid() || indexBuffer == nullptr ||
        indexBuffer->indexCount() == 0) {
      continue;
    }
    input.drawCommands.push_back(LX_core::RenderDrawCommand{
        .indexCount = static_cast<u32>(indexBuffer->indexCount()),
        .instanceCount = 1,
    });
    return input;
  }
  throw std::runtime_error("debug overlay scene produced no draw input");
}

LX_core::RenderInputDesc makeDebugOverlayDesc(
    const LX_core::Scene &scene, const LX_core::RenderDrawInput &input) {
  auto shader = makeDebugOverlayShader();
  LX_core::ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = "debug_overlay";
  shaderProgram.shader = shader;

  LX_core::RenderState renderState;
  renderState.cullMode = LX_core::CullMode::None;
  renderState.depthTestEnable = false;
  renderState.depthWriteEnable = false;

  const auto *vertexBuffer =
      dynamic_cast<const LX_core::IVertexBuffer *>(&input.vertexBuffer.get());
  const auto *indexBuffer =
      dynamic_cast<const LX_core::IndexBuffer *>(&input.indexBuffer.get());
  if (vertexBuffer == nullptr || indexBuffer == nullptr) {
    throw std::runtime_error("debug overlay draw input missing typed geometry");
  }

  LX_core::FramePass pass;
  pass.name = LX_core::Pass_DebugOverlay;
  pass.stage = LX_core::RenderPassStage::Raster;
  pass.dispatch = LX_core::RenderPassDispatch::Draw;
  pass.input.kind = LX_core::RenderPassInputKind::SceneRenderables;
  pass.input.material.required = false;
  pass.input.geometry = LX_core::RenderPathGeometryContract{
      .vertex = LX_core::RenderPathGeometryVertexContract::PositionOnly,
      .topology = LX_core::PrimitiveTopology::LineList,
  };
  pass.shaderUri = LX_core::ResourceUri("debug_overlay");

  const LX_core::PipelineKey pipelineKey = LX_core::PipelineKey::build(
      shaderProgram.getPipelineSignature(),
      LX_core::getFramePassRenderPathNodeSignature(pass));

  LX_core::RenderInputDesc desc;
  desc.status = LX_core::RenderInputStatus::Accepted;
  desc.inputIndex = input.inputIndex;
  desc.pass = input.pass;
  desc.debugId = input.debugId;
  desc.pipelineKey = pipelineKey;
  desc.pipelineBuildDesc = LX_core::PipelineBuildDesc::graphics(
      pipelineKey, shaderProgram.getPipelineSignature(), pass.target,
      shader->getAllStages(), shader->getReflectionBindings(),
      vertexBuffer->getLayout(), renderState, indexBuffer->getTopology(),
      pass.renderingMode, pass.attachments);
  desc.shaderVariantKey = desc.pipelineBuildDesc.shaderVariantKey;
  desc.bindingPlan.descriptors =
      scene.getSceneLevelResources(LX_core::Pass_DebugOverlay,
                                   LX_core::RenderTarget{});
  desc.resourceDependencies.push_back(input.vertexBuffer);
  desc.resourceDependencies.push_back(input.indexBuffer);
  for (const LX_core::DescriptorResourceRef &resource :
       desc.bindingPlan.descriptors) {
    if (resource.isResource()) {
      desc.resourceDependencies.push_back(resource.resource());
    }
  }
  return desc;
}

struct SyncedDebugOverlayItem final {
  LX_core::RenderDrawInput input;
  LX_core::RenderInputDesc desc;
};

SyncedDebugOverlayItem
syncDebugOverlayItem(LX_core::backend::VulkanResourceManager &resourceManager,
                     LX_core::backend::VulkanCommandBufferManager &cmdBufferMgr,
                     LX_core::Scene &scene) {
  LX_core::RenderDrawInput input = makeDebugOverlayInputFromScene(scene);
  LX_core::RenderInputDesc desc = makeDebugOverlayDesc(scene, input);
  syncDebugOverlayResources(resourceManager, cmdBufferMgr, input, desc);
  return SyncedDebugOverlayItem{
      .input = std::move(input),
      .desc = std::move(desc),
  };
}

bool drawDebugOverlayItem(
    LX_core::backend::VulkanDevice &device,
    LX_core::backend::VulkanResourceManager &resourceManager,
    LX_core::backend::VulkanCommandBufferManager &cmdBufferMgr,
    const SyncedDebugOverlayItem &work) {
  auto &renderPass = resourceManager.getRenderPass();
  auto pipeline = resourceManager.getOrCreatePipeline(work.desc);
  const VkPipeline pipelineHandle =
      std::visit([](auto ref) { return ref.get().getHandle(); }, pipeline);
  if (pipelineHandle == VK_NULL_HANDLE) {
    std::cerr << "DebugDraw overlay pipeline was not created\n";
    return false;
  }

  const VkExtent2D extent{64, 64};
  auto colorTex = LX_core::backend::VulkanTexture::createForAttachment(
      device, extent.width, extent.height, device.getSurfaceFormat().format,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
  auto depthTex = LX_core::backend::VulkanTexture::createForAttachment(
      device, extent.width, extent.height, device.getDepthFormat(),
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, device.getDepthAspectMask());
  std::vector<VkImageView> attachments = {colorTex->getImageView(),
                                          depthTex->getImageView()};
  auto framebuffer = LX_core::backend::VulkanFrameBuffer::create(
      device, renderPass.getHandle(), attachments, extent);

  cmdBufferMgr.beginFrame(0);
  auto cmd = cmdBufferMgr.allocateBuffer();

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (vkBeginCommandBuffer(cmd->getHandle(), &beginInfo) != VK_SUCCESS) {
    std::cerr << "Failed to begin DebugDraw command buffer\n";
    return false;
  }

  cmd->beginRenderPass(renderPass.getHandle(), framebuffer->getHandle(), extent,
                       renderPass.getClearValues());
  cmd->setViewport(extent.width, extent.height);
  cmd->setScissor(extent.width, extent.height);
  cmd->bindPipeline(pipeline);
  cmd->bindResources(resourceManager, pipeline, work.input, work.desc);
  cmd->executeRenderInput(work.input, work.desc);
  cmd->endRenderPass();

  if (vkEndCommandBuffer(cmd->getHandle()) != VK_SUCCESS) {
    std::cerr << "Failed to end DebugDraw command buffer\n";
    return false;
  }

  return true;
}

bool verifyDebugDrawGrowthSync(
    LX_core::backend::VulkanDevice &device,
    LX_core::backend::VulkanResourceManager &resourceManager,
    LX_core::backend::VulkanCommandBufferManager &cmdBufferMgr) {
  auto scene = makeOverlayScene();

  LX_core::DebugDraw::beginFrame();
  LX_core::DebugDraw::drawLine({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  const bool initialSceneDirty = LX_core::DebugDraw::endFrame();

  auto smallItem = syncDebugOverlayItem(resourceManager, cmdBufferMgr, *scene);

  const auto smallVertexIdentity =
      LX_core::DebugDraw::testing::vertexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  const auto smallIndexIdentity =
      LX_core::DebugDraw::testing::indexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  auto smallVkVertex = resourceManager.getBuffer(smallVertexIdentity);
  auto smallVkIndex = resourceManager.getBuffer(smallIndexIdentity);
  if (!smallVkVertex || !smallVkIndex) {
    std::cerr << "Initial DebugDraw Vulkan buffers were not created\n";
    return false;
  }
  if (smallVkVertex->get().getSize() !=
          smallItem.input.vertexBuffer.get().getByteSize() ||
      smallVkIndex->get().getSize() !=
          smallItem.input.indexBuffer.get().getByteSize()) {
    std::cerr
        << "Initial DebugDraw GPU buffer sizes do not match CPU resources\n";
    return false;
  }
  if (!initialSceneDirty) {
    std::cerr << "Initial DebugDraw frame should mark scene dirty\n";
    return false;
  }
  if (!drawDebugOverlayItem(device, resourceManager, cmdBufferMgr, smallItem)) {
    return false;
  }
  const auto smallVertexHandle = smallVkVertex->get().getHandle();
  const auto smallIndexHandle = smallVkIndex->get().getHandle();
  const auto initialReservedVertexCapacity =
      LX_core::DebugDraw::testing::reservedVertexCapacity(
          LX_core::Layer_EditorOverlay);
  const usize growthLineCount = (initialReservedVertexCapacity / 2) + 1;

  LX_core::DebugDraw::beginFrame();
  for (usize i = 0; i < growthLineCount; ++i) {
    LX_core::DebugDraw::drawLine({0.0f, 0.0f, 0.0f},
                                 {static_cast<float>(i), 1.0f, 0.0f});
  }
  const bool growthSceneDirty = LX_core::DebugDraw::endFrame();

  const auto grownVertexIdentity =
      LX_core::DebugDraw::testing::vertexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  const auto grownIndexIdentity =
      LX_core::DebugDraw::testing::indexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  if (!growthSceneDirty) {
    std::cerr << "Growth DebugDraw frame should mark scene dirty\n";
    return false;
  }
  if (grownVertexIdentity == smallVertexIdentity ||
      grownIndexIdentity == smallIndexIdentity) {
    std::cerr << "DebugDraw growth did not replace CPU buffer identities\n";
    return false;
  }

  auto grownItem = syncDebugOverlayItem(resourceManager, cmdBufferMgr, *scene);
  auto grownVkVertex = resourceManager.getBuffer(grownVertexIdentity);
  auto grownVkIndex = resourceManager.getBuffer(grownIndexIdentity);
  if (!grownVkVertex || !grownVkIndex) {
    std::cerr << "Grown DebugDraw Vulkan buffers were not created\n";
    return false;
  }
  if (grownVkVertex->get().getSize() !=
          grownItem.input.vertexBuffer.get().getByteSize() ||
      grownVkIndex->get().getSize() !=
          grownItem.input.indexBuffer.get().getByteSize()) {
    std::cerr
        << "Grown DebugDraw GPU buffer sizes do not match CPU resources\n";
    return false;
  }
  if (grownVkVertex->get().getHandle() == smallVertexHandle ||
      grownVkIndex->get().getHandle() == smallIndexHandle) {
    std::cerr
        << "Grown DebugDraw buffers reused stale undersized Vulkan handles\n";
    return false;
  }
  if (!drawDebugOverlayItem(device, resourceManager, cmdBufferMgr, grownItem)) {
    return false;
  }
  const auto grownReservedVertexCapacity =
      LX_core::DebugDraw::testing::reservedVertexCapacity(
          LX_core::Layer_EditorOverlay);
  const usize retainedLineCount =
      std::max<usize>(1, (grownReservedVertexCapacity / 2) - 1);

  LX_core::DebugDraw::beginFrame();
  for (usize i = 0; i < retainedLineCount; ++i) {
    LX_core::DebugDraw::drawLine({0.0f, 0.0f, 0.0f},
                                 {static_cast<float>(i), 2.0f, 0.0f});
  }
  const bool retainedSceneDirty = LX_core::DebugDraw::endFrame();

  const auto retainedVertexIdentity =
      LX_core::DebugDraw::testing::vertexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  const auto retainedIndexIdentity =
      LX_core::DebugDraw::testing::indexBufferIdentity(
          LX_core::Layer_EditorOverlay);
  if (retainedVertexIdentity != grownVertexIdentity ||
      retainedIndexIdentity != grownIndexIdentity) {
    std::cerr << "Within-capacity DebugDraw frame unexpectedly replaced CPU "
                 "identities\n";
    return false;
  }
  if (retainedSceneDirty) {
    std::cerr
        << "Within-capacity DebugDraw frame should not mark scene dirty\n";
    return false;
  }

  syncDebugOverlayResources(resourceManager, cmdBufferMgr, grownItem.input,
                            grownItem.desc);
  auto retainedVkVertex = resourceManager.getBuffer(retainedVertexIdentity);
  auto retainedVkIndex = resourceManager.getBuffer(retainedIndexIdentity);
  if (!retainedVkVertex || !retainedVkIndex) {
    std::cerr << "Retained DebugDraw Vulkan buffers were not found\n";
    return false;
  }
  if (retainedVkVertex->get().getSize() !=
          grownItem.input.vertexBuffer.get().getByteSize() ||
      retainedVkIndex->get().getSize() !=
          grownItem.input.indexBuffer.get().getByteSize()) {
    std::cerr << "Within-capacity DebugDraw GPU sizes no longer match retained "
                 "CPU capacity\n";
    return false;
  }
  if (retainedVkVertex->get().getHandle() != grownVkVertex->get().getHandle() ||
      retainedVkIndex->get().getHandle() != grownVkIndex->get().getHandle()) {
    std::cerr << "Within-capacity DebugDraw frame should reuse grown Vulkan "
                 "buffers\n";
    return false;
  }
  if (!drawDebugOverlayItem(device, resourceManager, cmdBufferMgr, grownItem)) {
    return false;
  }

  LX_core::DebugDraw::reset();
  return true;
}

} // namespace

int main() {
  expSetEnvVK();
  bool environmentSetupComplete = false;
  try {
    auto success = initializeRuntimeAssetRoot();
    if (!success) {
      std::cerr << "Failed to find shader files\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>(
        "Test Vulkan ResourceManager", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanResourceManager");

    VkSurfaceFormatKHR surfaceFormat = device->getSurfaceFormat();
    const VkFormat depthFormat = device->getDepthFormat();

    auto cmdBufferMgr = LX_core::backend::VulkanCommandBufferManager::create(
        *device, 3, device->getGraphicsQueueFamilyIndex());
    auto resourceManager =
        LX_core::backend::VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(surfaceFormat,
                                                     depthFormat);
    environmentSetupComplete = true;

    const VkExtent2D frameGraphExtent{64, 64};
    auto &frameGraphDepth = resourceManager->createOrGetFrameGraphAttachment(
        LX_core::StringID("test.depth"), frameGraphExtent, depthFormat,
        device->getDepthAspectMask(),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT);
    if (frameGraphDepth.texture->getImageView() == VK_NULL_HANDLE) {
      std::cerr << "Frame graph attachment image view was not created\n";
      return 1;
    }
    if (frameGraphDepth.texture->getSampler() == VK_NULL_HANDLE) {
      std::cerr << "Frame graph sampled attachment sampler was not created\n";
      return 1;
    }
    if (frameGraphDepth.format != depthFormat ||
        frameGraphDepth.extent.width != frameGraphExtent.width ||
        frameGraphDepth.extent.height != frameGraphExtent.height ||
        frameGraphDepth.currentLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
      std::cerr << "Frame graph attachment metadata mismatch\n";
      return 1;
    }
    auto foundFrameGraphDepth = resourceManager->getFrameGraphAttachment(
        LX_core::StringID("test.depth"));
    if (!foundFrameGraphDepth ||
        &foundFrameGraphDepth->get() != &frameGraphDepth) {
      std::cerr << "Frame graph attachment lookup failed\n";
      return 1;
    }
    if ((frameGraphDepth.usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0) {
      std::cerr
          << "Frame graph attachment usage metadata did not retain SAMPLED\n";
      return 1;
    }

    (void)resourceManager->createOrGetFrameGraphAttachment(
        LX_core::StringID("test.color"), frameGraphExtent, surfaceFormat.format,
        VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    bool usageMismatchRejected = false;
    try {
      (void)resourceManager->createOrGetFrameGraphAttachment(
          LX_core::StringID("test.color"), frameGraphExtent,
          surfaceFormat.format, VK_IMAGE_ASPECT_COLOR_BIT,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    } catch (const std::runtime_error &e) {
      usageMismatchRejected =
          std::string(e.what()).find("usage") != std::string::npos;
    }
    if (!usageMismatchRejected) {
      std::cerr << "Frame graph attachment usage mismatch was not rejected\n";
      return 1;
    }

    const VkExtent2D cubemapBakeExtent{64, 64};
    auto &cubemapBakeAttachment =
        resourceManager->createOrGetCubemapBakeAttachment(
            LX_core::StringID("test.ibl.prefilter"), cubemapBakeExtent,
            VK_FORMAT_R16G16B16A16_SFLOAT, 4,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    if (!cubemapBakeAttachment.texture ||
        cubemapBakeAttachment.texture->getImageView() == VK_NULL_HANDLE ||
        cubemapBakeAttachment.texture->getMipLevels() != 4u ||
        cubemapBakeAttachment.texture->getArrayLayers() != 6u) {
      std::cerr
          << "Cubemap bake attachment texture was not created correctly\n";
      return 1;
    }
    if (cubemapBakeAttachment.baseExtent.width != cubemapBakeExtent.width ||
        cubemapBakeAttachment.baseExtent.height != cubemapBakeExtent.height ||
        cubemapBakeAttachment.format != VK_FORMAT_R16G16B16A16_SFLOAT) {
      std::cerr << "Cubemap bake attachment metadata mismatch\n";
      return 1;
    }
    auto &faceMipView = resourceManager->getOrCreateCubemapBakeSubresourceView(
        LX_core::StringID("test.ibl.prefilter"), 2, 5);
    if (faceMipView.getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Cubemap bake face/mip view was not created\n";
      return 1;
    }
    auto &sameFaceMipView =
        resourceManager->getOrCreateCubemapBakeSubresourceView(
            LX_core::StringID("test.ibl.prefilter"), 2, 5);
    if (&sameFaceMipView != &faceMipView) {
      std::cerr << "Cubemap bake face/mip view was not cached\n";
      return 1;
    }
    bool cubemapMismatchRejected = false;
    try {
      (void)resourceManager->createOrGetCubemapBakeAttachment(
          LX_core::StringID("test.ibl.prefilter"), cubemapBakeExtent,
          VK_FORMAT_R16G16B16A16_SFLOAT, 5,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    } catch (const std::runtime_error &e) {
      cubemapMismatchRejected =
          std::string(e.what()).find("mips") != std::string::npos;
    }
    if (!cubemapMismatchRejected) {
      std::cerr << "Cubemap bake attachment mip mismatch was not rejected\n";
      return 1;
    }

    using V = LX_core::VertexPosNormalUvBone;
    auto vertexBufferPtr = LX_core::VertexBuffer<V>::create({
        V({-5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, -5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
    });

    auto indexBufferPtr = LX_core::IndexBuffer::create({0u, 1u, 2u});

    resourceManager->syncResource(*cmdBufferMgr, *vertexBufferPtr);
    resourceManager->syncResource(*cmdBufferMgr, *indexBufferPtr);
    resourceManager->collectGarbage();

    LX_core::TextureDesc hdrDesc;
    hdrDesc.width = 2;
    hdrDesc.height = 2;
    hdrDesc.format = LX_core::TextureFormat::RGBA32Float;
    auto hdrSampler = std::make_shared<LX_core::CombinedTextureSampler>(
        std::make_shared<LX_core::Texture>(
            hdrDesc,
            std::vector<u8>(LX_core::expectedTextureByteCount(hdrDesc))));
    const auto hdrIdentity = hdrSampler->getBackendCacheIdentity();
    resourceManager->syncResource(*cmdBufferMgr, *hdrSampler);
    auto hdrGpuTexture = resourceManager->getTexture(hdrIdentity);
    if (!hdrGpuTexture ||
        hdrGpuTexture->get().getFormat() != VK_FORMAT_R32G32B32A32_SFLOAT) {
      std::cerr << "HDR sampler did not upload as RGBA32F Vulkan texture\n";
      return 1;
    }

    LX_core::TextureDesc mip2DDesc;
    mip2DDesc.width = 4;
    mip2DDesc.height = 4;
    mip2DDesc.format = LX_core::TextureFormat::RGBA8;
    mip2DDesc.mipLevels = 3;
    auto mip2DSampler = std::make_shared<LX_core::CombinedTextureSampler>(
        std::make_shared<LX_core::Texture>(
            mip2DDesc,
            std::vector<u8>(LX_core::expectedTextureByteCount(mip2DDesc))));
    const auto mip2DIdentity = mip2DSampler->getBackendCacheIdentity();
    resourceManager->syncResource(*cmdBufferMgr, *mip2DSampler);
    auto mip2DGpuTexture = resourceManager->getTexture(mip2DIdentity);
    if (!mip2DGpuTexture || mip2DGpuTexture->get().getArrayLayers() != 1 ||
        mip2DGpuTexture->get().getMipLevels() != 3) {
      std::cerr << "2D sampler did not preserve Vulkan mip shape\n";
      return 1;
    }

    LX_core::TextureDesc cubeDesc;
    cubeDesc.width = 4;
    cubeDesc.height = 4;
    cubeDesc.format = LX_core::TextureFormat::RGBA16Float;
    cubeDesc.dimension = LX_core::TextureDimension::TextureCube;
    cubeDesc.mipLevels = 3;
    cubeDesc.arrayLayers = 6;
    auto cubeSampler = std::make_shared<LX_core::CombinedTextureSampler>(
        std::make_shared<LX_core::Texture>(
            cubeDesc,
            std::vector<u8>(LX_core::expectedTextureByteCount(cubeDesc))));
    const auto cubeIdentity = cubeSampler->getBackendCacheIdentity();
    resourceManager->syncResource(*cmdBufferMgr, *cubeSampler);
    auto cubeGpuTexture = resourceManager->getTexture(cubeIdentity);
    if (!cubeGpuTexture ||
        cubeGpuTexture->get().getFormat() != VK_FORMAT_R16G16B16A16_SFLOAT ||
        cubeGpuTexture->get().getArrayLayers() != 6 ||
        cubeGpuTexture->get().getMipLevels() != 3) {
      std::cerr << "Cubemap sampler did not preserve Vulkan texture shape\n";
      return 1;
    }

    auto input = LX_test::makeMinimalRenderDrawInputForVulkanTests(
        *vertexBufferPtr, *indexBufferPtr);
    auto pipelineDesc =
        LX_test::makeMinimalDirectRasterHelperPipelineBuildDescForVulkanTests(
            *vertexBufferPtr, *indexBufferPtr);
    auto desc =
        LX_test::makeAcceptedRenderInputDescForVulkanTests(pipelineDesc, input);
    auto pipeline = resourceManager->getOrCreatePipeline(desc);
    const VkPipeline pipelineHandle =
        std::visit([](auto ref) { return ref.get().getHandle(); }, pipeline);
    if (pipelineHandle == VK_NULL_HANDLE) {
      std::cerr << "Pipeline not created correctly\n";
      return 1;
    }

    auto vkVertexOpt =
        resourceManager->getBuffer(vertexBufferPtr->getBackendCacheIdentity());
    auto vkIndexOpt =
        resourceManager->getBuffer(indexBufferPtr->getBackendCacheIdentity());
    if (!vkVertexOpt || !vkIndexOpt) {
      std::cerr << "Expected Vulkan buffers were not created\n";
      return 1;
    }

    auto &vkVertex = vkVertexOpt->get();
    auto &vkIndex = vkIndexOpt->get();
    if (vkVertex.getHandle() == VK_NULL_HANDLE ||
        vkIndex.getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Vulkan buffer handles are null\n";
      return 1;
    }

    auto tempResource = std::make_shared<TestUniformResource>(7u);
    const auto tempIdentity = tempResource->getBackendCacheIdentity();
    resourceManager->syncResource(*cmdBufferMgr, *tempResource);
    resourceManager->collectGarbage();
    auto tempBuffer0 = resourceManager->getBuffer(tempIdentity);
    if (!tempBuffer0) {
      std::cerr << "Expected temp uniform GPU buffer after initial sync\n";
      return 1;
    }
    const auto tempHandle0 = tempBuffer0->get().getHandle();

    resourceManager->collectGarbage();
    auto tempBuffer1 = resourceManager->getBuffer(tempIdentity);
    if (!tempBuffer1 || tempBuffer1->get().getHandle() != tempHandle0) {
      std::cerr << "Temporarily unused resource was not retained across grace "
                   "frame\n";
      return 1;
    }

    resourceManager->syncResource(*cmdBufferMgr, *tempResource);
    resourceManager->collectGarbage();
    auto tempBuffer2 = resourceManager->getBuffer(tempIdentity);
    if (!tempBuffer2 || tempBuffer2->get().getHandle() != tempHandle0) {
      std::cerr
          << "Resync after one inactive frame should reuse same GPU buffer\n";
      return 1;
    }

    resourceManager->collectGarbage();
    resourceManager->collectGarbage();
    if (resourceManager->getBuffer(tempIdentity)) {
      std::cerr
          << "Temp resource should be evicted after inactivity grace period\n";
      return 1;
    }

    using ReusedStorage = std::aligned_storage_t<sizeof(TestUniformResource),
                                                 alignof(TestUniformResource)>;
    ReusedStorage reusedStorage;

    auto reusedA =
        makePlacementShared<TestUniformResource>(&reusedStorage, 11u);
    const auto reusedIdentityA = reusedA->getBackendCacheIdentity();
    resourceManager->syncResource(*cmdBufferMgr, *reusedA);
    resourceManager->collectGarbage();
    auto reusedBufferA = resourceManager->getBuffer(reusedIdentityA);
    if (!reusedBufferA) {
      std::cerr << "Expected first placement resource GPU buffer\n";
      return 1;
    }

    auto firstAddress = reusedA.get();
    reusedA.reset();

    auto reusedB =
        makePlacementShared<TestUniformResource>(&reusedStorage, 22u);
    const auto reusedIdentityB = reusedB->getBackendCacheIdentity();
    if (reusedB.get() != firstAddress) {
      std::cerr << "Placement test did not reuse the same CPU address\n";
      return 1;
    }
    if (reusedIdentityA == reusedIdentityB) {
      std::cerr
          << "Stable backend identity unexpectedly reused across objects\n";
      return 1;
    }

    resourceManager->syncResource(*cmdBufferMgr, *reusedB);
    if (resourceManager->getCachedResourceCount() < 2) {
      std::cerr << "Address-reused CPU resource aliased old GPU cache entry\n";
      return 1;
    }

    if (!verifyDebugDrawGrowthSync(*device, *resourceManager, *cmdBufferMgr)) {
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    if (!environmentSetupComplete && isKnownEnvironmentSetupFailure(e.what())) {
      std::cerr << "SKIP VulkanResourceManager test: " << e.what() << "\n";
      return 0;
    }
    std::cerr << "FAIL VulkanResourceManager test: " << e.what() << "\n";
    return 1;
  }
}
