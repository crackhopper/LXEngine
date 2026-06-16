#include "ibl_bake_renderer.hpp"

#include "core/asset/material_instance.hpp"
#include "core/asset/material_template.hpp"
#include "core/frame_graph/render_input.hpp"
#include "core/math/mat.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "commands/command_buffer_manager.hpp"
#include "device.hpp"
#include "device_resources/buffer.hpp"
#include "device_resources/texture.hpp"
#include "pipelines/graphics_pipeline.hpp"
#include "render_objects/framebuffer.hpp"
#include "render_objects/render_pass.hpp"
#include "resource_manager.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace LX_core::backend {
namespace {

constexpr VkFormat kBakeFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr float kPi = 3.14159265358979323846f;

usize cubemapLayoutKey(StringID resourceName, u32 mipLevel, u32 faceLayer) {
  usize hash = StringID::Hash{}(resourceName);
  hash ^=
      static_cast<usize>(mipLevel) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
  hash ^=
      static_cast<usize>(faceLayer) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
  return hash;
}

StringID makeBakeRenderPathNodeSignature(const std::string &shaderName,
                                         const RenderState &renderState,
                                         const RenderTargetDesc &target,
                                         u32 indexCount) {
  auto &tbl = GlobalStringTable::get();
  StringID fields[] = {
      StringID("pass=PostProcess"),
      StringID("shader=" + shaderName),
      StringID("stage=raster"),
      StringID(indexCount == 36u ? "dispatch=cube-bake"
                                 : "dispatch=fullscreen-bake"),
      renderState.getPipelineSignature(),
      target.getPipelineSignature(),
  };
  return tbl.compose(TypeTag::RenderPathNode, fields);
}

void transitionSubresource(VkCommandBuffer cmd, VkImage image,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           VkAccessFlags srcAccessMask,
                           VkAccessFlags dstAccessMask,
                           VkPipelineStageFlags srcStage,
                           VkPipelineStageFlags dstStage, u32 mipLevel,
                           u32 faceLayer) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcAccessMask = srcAccessMask;
  barrier.dstAccessMask = dstAccessMask;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = mipLevel;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = faceLayer;
  barrier.subresourceRange.layerCount = 1;
  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);
}

void transitionTexture2D(VkCommandBuffer cmd, VkImage image,
                         VkImageLayout oldLayout, VkImageLayout newLayout,
                         VkAccessFlags srcAccessMask,
                         VkAccessFlags dstAccessMask,
                         VkPipelineStageFlags srcStage,
                         VkPipelineStageFlags dstStage) {
  transitionSubresource(cmd, image, oldLayout, newLayout, srcAccessMask,
                        dstAccessMask, srcStage, dstStage, 0, 0);
}

bool bufferHasNonZeroByte(const void *mapped, VkDeviceSize byteSize) {
  const auto *bytes = static_cast<const unsigned char *>(mapped);
  return std::any_of(bytes, bytes + static_cast<std::size_t>(byteSize),
                     [](unsigned char value) { return value != 0; });
}

u32 mipExtent(u32 baseSize, u32 mipLevel) {
  return std::max(baseSize >> mipLevel, 1u);
}

std::vector<u32> loadSpirvWords(const std::string &shaderName,
                                const char *stageSuffix) {
  const auto path = getShaderPath(shaderName, stageSuffix);
  if (path.empty()) {
    throw std::runtime_error("Failed to find shader file: " + shaderName);
  }
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open shader file: " + path);
  }
  const auto byteSize = static_cast<std::size_t>(file.tellg());
  if (byteSize == 0 || byteSize % sizeof(u32) != 0) {
    throw std::runtime_error("Invalid SPIR-V byte size for shader: " + path);
  }
  std::vector<u32> words(byteSize / sizeof(u32));
  file.seekg(0);
  file.read(reinterpret_cast<char *>(words.data()),
            static_cast<std::streamsize>(byteSize));
  return words;
}

std::vector<ShaderStageCode> loadBakeShaderStages(const std::string &name) {
  return {
      ShaderStageCode{ShaderStage::Vertex, loadSpirvWords(name, "vert.spv")},
      ShaderStageCode{ShaderStage::Fragment, loadSpirvWords(name, "frag.spv")},
  };
}

ShaderResourceBinding textureBinding(const char *name, u32 binding,
                                     ShaderPropertyType type) {
  return ShaderResourceBinding{
      name, 0, binding, type, 1, 0, 0, ShaderStage::Fragment, {}};
}

ShaderResourceBinding captureViewBinding() {
  return ShaderResourceBinding{
      "CaptureViewUBO",
      0,
      1,
      ShaderPropertyType::UniformBuffer,
      1,
      sizeof(Mat4f),
      0,
      ShaderStage::Vertex,
      {StructMemberInfo{"viewProj", ShaderPropertyType::Mat4, 0, 64}}};
}

ShaderResourceBinding prefilterBinding() {
  return ShaderResourceBinding{
      "PrefilterUBO",
      0,
      2,
      ShaderPropertyType::UniformBuffer,
      1,
      16,
      0,
      ShaderStage::Fragment,
      {StructMemberInfo{"roughness", ShaderPropertyType::Float, 0, 4},
       StructMemberInfo{"sourceMipCount", ShaderPropertyType::Float, 4, 4},
       StructMemberInfo{"sampleCount", ShaderPropertyType::Float, 8, 4},
       StructMemberInfo{"padding", ShaderPropertyType::Float, 12, 4}}};
}

std::vector<ShaderResourceBinding>
bakeShaderBindings(const std::string &shaderName) {
  if (shaderName == "equirect_to_cubemap") {
    return {
        textureBinding("EquirectangularMap", 0, ShaderPropertyType::Texture2D),
        captureViewBinding()};
  }
  if (shaderName == "ibl_irradiance_convolve") {
    return {textureBinding("SkyboxMap", 0, ShaderPropertyType::TextureCube),
            captureViewBinding()};
  }
  if (shaderName == "ibl_prefilter_env") {
    return {textureBinding("SkyboxMap", 0, ShaderPropertyType::TextureCube),
            captureViewBinding(), prefilterBinding()};
  }
  return {};
}

class BakeShader final : public IShader {
public:
  BakeShader(std::string shaderName,
             std::vector<ShaderResourceBinding> bindings)
      : m_shaderName(std::move(shaderName)),
        m_stages(loadBakeShaderStages(m_shaderName)),
        m_bindings(std::move(bindings)) {}

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }
  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const auto &b : m_bindings) {
      if (b.set == set && b.binding == binding) {
        return b;
      }
    }
    return std::nullopt;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const auto &b : m_bindings) {
      if (b.name == name) {
        return b;
      }
    }
    return std::nullopt;
  }
  usize getProgramHash() const override {
    return std::hash<std::string>{}(m_shaderName);
  }
  std::string getShaderName() const override { return m_shaderName; }

private:
  std::string m_shaderName;
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

class CaptureViewResource final : public IGpuResource {
public:
  explicit CaptureViewResource(Mat4f value) : m_value(value) { setDirty(); }

  void setViewProj(const Mat4f &value) {
    m_value = value;
    setDirty();
  }

  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return &m_value; }
  u32 getByteSize() const override { return sizeof(Mat4f); }
  StringID getBindingName() const override {
    return StringID("CaptureViewUBO");
  }

private:
  Mat4f m_value = Mat4f::identity();
};

struct alignas(16) PrefilterParams {
  float roughness = 0.0f;
  float sourceMipCount = 1.0f;
  float sampleCount = 64.0f;
  float padding = 0.0f;
};

class PrefilterResource final : public IGpuResource {
public:
  explicit PrefilterResource(PrefilterParams value) : m_value(value) {
    setDirty();
  }

  void setParams(PrefilterParams value) {
    m_value = value;
    setDirty();
  }

  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return &m_value; }
  u32 getByteSize() const override { return sizeof(PrefilterParams); }
  StringID getBindingName() const override { return StringID("PrefilterUBO"); }

private:
  PrefilterParams m_value{};
};

CombinedTextureSamplerSharedPtr createDefaultEquirectangularMap() {
  TextureDesc desc;
  desc.width = 4;
  desc.height = 2;
  desc.format = TextureFormat::RGBA32Float;
  desc.content = TextureContent::Environment;
  std::vector<u8> bytes(desc.width * desc.height * 4u * sizeof(float), 0);
  auto *pixels = reinterpret_cast<float *>(bytes.data());
  for (u32 y = 0; y < desc.height; ++y) {
    for (u32 x = 0; x < desc.width; ++x) {
      const usize base = static_cast<usize>(y * desc.width + x) * 4u;
      pixels[base + 0u] = static_cast<float>(x + 1u) / desc.width;
      pixels[base + 1u] = static_cast<float>(y + 1u) / desc.height;
      pixels[base + 2u] = 1.0f - pixels[base + 0u] * 0.5f;
      pixels[base + 3u] = 1.0f;
    }
  }
  auto sampler = std::make_shared<CombinedTextureSampler>(
      std::make_shared<Texture>(desc, std::move(bytes)));
  sampler->setBindingName(StringID("EquirectangularMap"));
  sampler->setDirty();
  return sampler;
}

std::array<Mat4f, 6> captureViewProjections() {
  const Mat4f projection = Mat4f::perspective(kPi * 0.5f, 1.0f, 0.1f, 10.0f);
  const Vec3f origin{0.0f, 0.0f, 0.0f};
  return {
      projection * Mat4f::lookAt(origin, Vec3f{1.0f, 0.0f, 0.0f},
                                 Vec3f{0.0f, -1.0f, 0.0f}),
      projection * Mat4f::lookAt(origin, Vec3f{-1.0f, 0.0f, 0.0f},
                                 Vec3f{0.0f, -1.0f, 0.0f}),
      projection * Mat4f::lookAt(origin, Vec3f{0.0f, 1.0f, 0.0f},
                                 Vec3f{0.0f, 0.0f, 1.0f}),
      projection * Mat4f::lookAt(origin, Vec3f{0.0f, -1.0f, 0.0f},
                                 Vec3f{0.0f, 0.0f, -1.0f}),
      projection * Mat4f::lookAt(origin, Vec3f{0.0f, 0.0f, 1.0f},
                                 Vec3f{0.0f, -1.0f, 0.0f}),
      projection * Mat4f::lookAt(origin, Vec3f{0.0f, 0.0f, -1.0f},
                                 Vec3f{0.0f, -1.0f, 0.0f}),
  };
}

struct BakeWorkItem final {
  RenderDrawInput input;
  RenderInputDesc desc;
  PipelineBuildDesc pipelineBuildDesc;
  VertexBufferUniquePtr vertexBuffer;
  IndexBufferUniquePtr indexBuffer;
};

BakeWorkItem makeBakeItem(const std::string &shaderName,
                          const RenderTargetDesc &target,
                          std::vector<GpuResourceRef> resources,
                          u32 indexCount) {
  auto shader =
      std::make_shared<BakeShader>(shaderName, bakeShaderBindings(shaderName));
  auto tmpl = MaterialTemplate::create(shaderName);
  ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = shaderName;
  shaderProgram.shader = shader;

  MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = std::move(shaderProgram);
  passDefinition.renderState.cullMode = CullMode::None;
  passDefinition.renderState.depthTestEnable = false;
  passDefinition.renderState.depthWriteEnable = false;
  passDefinition.renderState.blendEnable = false;
  tmpl->setPassDefinition(Pass_PostProcess, std::move(passDefinition));
  tmpl->rebuildMaterialInterface();
  auto material = MaterialInstance::create(std::move(tmpl));
  material->syncGpuData();

  BakeWorkItem work;
  work.vertexBuffer = VertexBuffer<VertexPos>::createUnique(
      std::vector<VertexPos>(std::max(indexCount, 1u)));
  std::vector<u32> indices(indexCount);
  std::iota(indices.begin(), indices.end(), 0u);
  work.indexBuffer = IndexBuffer::createUnique(std::move(indices));

  RenderDrawInput &input = work.input;
  input.source = RenderDrawInputSource::SceneRenderable;
  input.pass = Pass_PostProcess;
  input.debugId =
      StringID(indexCount == 36u ? "IblBakeCube" : "IblBakeFullscreen");
  input.inputIndex = 0;
  input.vertexBuffer = GpuResourceRef{*work.vertexBuffer};
  input.indexBuffer = GpuResourceRef{*work.indexBuffer};
  input.drawCommands.push_back(RenderDrawCommand{
      .indexCount = indexCount,
      .instanceCount = 1,
  });
  std::vector<DescriptorResourceRef> descriptors;
  descriptors.reserve(resources.size());
  for (auto &resource : resources) {
    if (resource.isValid()) {
      descriptors.emplace_back(resource.get());
    }
  }
  const auto resolvedShaderProgram =
      material->getPassShaderProgram(Pass_PostProcess);
  if (!resolvedShaderProgram.has_value()) {
    throw std::logic_error("IBL bake material missing shader program");
  }
  const ShaderProgramSet resolvedProgram = resolvedShaderProgram->get();
  const RenderState renderState =
      material->getPassRenderState(Pass_PostProcess);
  const StringID materialTypeVariant =
      material->getMaterialTypeVariantSignature(resolvedProgram);
  const StringID renderPathNodeSignature = makeBakeRenderPathNodeSignature(
      shaderName, renderState, target, indexCount);
  const PipelineKey pipelineKey =
      PipelineKey::build(materialTypeVariant, renderPathNodeSignature);
  work.pipelineBuildDesc = PipelineBuildDesc::graphics(
      pipelineKey, resolvedProgram.getPipelineSignature(), target,
      shader->getAllStages(), shader->getReflectionBindings(),
      work.vertexBuffer->getLayout(), renderState,
      work.indexBuffer->getTopology(), std::nullopt, {});
  work.desc.status = RenderInputStatus::Accepted;
  work.desc.inputIndex = input.inputIndex;
  work.desc.pass = input.pass;
  work.desc.debugId = input.debugId;
  work.desc.pipelineKey = pipelineKey;
  work.desc.pipelineBuildDesc = work.pipelineBuildDesc;
  work.desc.shaderVariantKey = work.pipelineBuildDesc.shaderVariantKey;
  work.desc.bindingPlan.descriptors = std::move(descriptors);
  work.desc.resourceDependencies.push_back(input.vertexBuffer);
  work.desc.resourceDependencies.push_back(input.indexBuffer);
  for (const DescriptorResourceRef &descriptor :
       work.desc.bindingPlan.descriptors) {
    if (descriptor.isResource()) {
      work.desc.resourceDependencies.push_back(descriptor.resource());
    }
  }
  return work;
}

BakeWorkItem makeFullscreenBakeItem(const std::string &shaderName,
                                    const RenderTargetDesc &target) {
  return makeBakeItem(shaderName, target, {}, 3u);
}

void syncBakeItemResources(VulkanResourceManager &resourceManager,
                           VulkanCommandBufferManager &cmdBufferManager,
                           const BakeWorkItem &work) {
  resourceManager.syncResource(cmdBufferManager, work.input.vertexBuffer);
  resourceManager.syncResource(cmdBufferManager, work.input.indexBuffer);
  for (const DescriptorResourceRef &resource :
       work.desc.bindingPlan.descriptors) {
    if (resource.isTextureArray()) {
      for (const TextureSamplerRef &texture : resource.textures()) {
        if (texture.isValid()) {
          resourceManager.syncResource(cmdBufferManager,
                                       GpuResourceRef{texture.get()});
        }
      }
      continue;
    }
    if (resource.resource().isValid()) {
      resourceManager.syncResource(cmdBufferManager, resource.resource());
    }
  }
}

} // namespace

IblBakeRenderer::IblBakeRenderer(VulkanDevice &device,
                                 VulkanResourceManager &resourceManager,
                                 VulkanCommandBufferManager &cmdBufferManager)
    : m_device(device), m_resourceManager(resourceManager),
      m_cmdBufferManager(cmdBufferManager) {}

IblBakeResult
IblBakeRenderer::bakeStaticEnvironment(const IblBakeSettings &settings) {
  const VkImageUsageFlags cubeUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                      VK_IMAGE_USAGE_SAMPLED_BIT;
  m_resourceManager.createOrGetCubemapBakeAttachment(
      StringID("SkyboxMap"),
      VkExtent2D{settings.skyboxSize, settings.skyboxSize}, kBakeFormat, 1,
      cubeUsage);
  m_resourceManager.createOrGetCubemapBakeAttachment(
      StringID("IrradianceMap"),
      VkExtent2D{settings.irradianceSize, settings.irradianceSize}, kBakeFormat,
      1, cubeUsage);
  m_resourceManager.createOrGetCubemapBakeAttachment(
      StringID("PrefilteredEnvMap"),
      VkExtent2D{settings.prefilterSize, settings.prefilterSize}, kBakeFormat,
      settings.prefilterMipCount, cubeUsage);
  m_resourceManager.createOrGetFrameGraphAttachment(
      StringID("BrdfLut"),
      VkExtent2D{settings.brdfLutSize, settings.brdfLutSize}, kBakeFormat,
      VK_IMAGE_ASPECT_COLOR_BIT,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
          VK_IMAGE_USAGE_SAMPLED_BIT);

  const auto environmentMap = settings.equirectangularMap
                                  ? settings.equirectangularMap
                                  : createDefaultEquirectangularMap();
  renderEquirectToCubemap(environmentMap, settings.skyboxSize);
  transitionCubemapToShaderRead(StringID("SkyboxMap"), 1);
  renderIrradianceCubemap(settings.irradianceSize);
  transitionCubemapToShaderRead(StringID("IrradianceMap"), 1);
  renderPrefilterCubemap(settings.prefilterSize, settings.prefilterMipCount);
  transitionCubemapToShaderRead(StringID("PrefilteredEnvMap"),
                                settings.prefilterMipCount);
  clearBrdfLut(settings.brdfLutSize);
  transitionBrdfLutToShaderRead();

  IblBakeResult result{
      .skybox = std::make_unique<BakedTextureResource>(StringID("SkyboxMap"),
                                                       StringID("SkyboxMap")),
      .irradiance = std::make_unique<BakedTextureResource>(
          StringID("IrradianceMap"), StringID("IrradianceMap")),
      .prefiltered = std::make_unique<BakedTextureResource>(
          StringID("PrefilteredEnvMap"), StringID("PrefilteredEnvMap")),
      .brdfLut = std::make_unique<BakedTextureResource>(StringID("BrdfLut"),
                                                        StringID("BrdfLut")),
  };
  m_resourceManager.aliasCubemapBakeTextureResource(
      GpuResourceRef{*result.skybox}, StringID("SkyboxMap"));
  m_resourceManager.aliasCubemapBakeTextureResource(
      GpuResourceRef{*result.irradiance}, StringID("IrradianceMap"));
  m_resourceManager.aliasCubemapBakeTextureResource(
      GpuResourceRef{*result.prefiltered}, StringID("PrefilteredEnvMap"));
  m_resourceManager.aliasFrameGraphTextureResource(
      GpuResourceRef{*result.brdfLut}, StringID("BrdfLut"));
  return result;
}

void IblBakeRenderer::clearCubemap(StringID resourceName, u32 baseSize,
                                   u32 mipLevels, float seed) {
  auto attachmentOpt = m_resourceManager.getCubemapBakeAttachment(resourceName);
  if (!attachmentOpt.has_value()) {
    throw std::runtime_error("missing cubemap bake attachment");
  }
  auto &attachment = attachmentOpt->get();
  auto renderPass = VulkanRenderPass::create(
      m_device, std::optional<VkFormat>{attachment.format}, std::nullopt,
      false);

  auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
  for (u32 mip = 0; mip < mipLevels; ++mip) {
    const u32 extentValue = mipExtent(baseSize, mip);
    const VkExtent2D extent{extentValue, extentValue};
    for (u32 face = 0; face < 6u; ++face) {
      auto &view = m_resourceManager.getOrCreateCubemapBakeSubresourceView(
          resourceName, mip, face);
      auto framebuffer = VulkanFrameBuffer::create(
          m_device, renderPass->getHandle(),
          std::vector<VkImageView>{view.getHandle()}, extent);

      transitionSubresource(
          cmd->getHandle(), attachment.texture->getHandle(),
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, mip, face);
      auto clearValues = renderPass->getClearValues();
      const float faceValue = static_cast<float>(face + 1u) / 6.0f;
      const float mipValue =
          static_cast<float>(mip + 1u) / static_cast<float>(mipLevels);
      clearValues[0].color = {seed, faceValue, mipValue, 1.0f};
      cmd->beginRenderPass(renderPass->getHandle(), framebuffer->getHandle(),
                           extent, clearValues);
      cmd->endRenderPass();
    }
  }
  m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                           m_device.getGraphicsQueue());
}

VkImageLayout IblBakeRenderer::getTrackedCubemapLayout(StringID resourceName,
                                                       u32 mipLevel,
                                                       u32 faceLayer) const {
  const auto it = m_cubemapLayouts.find(
      cubemapLayoutKey(resourceName, mipLevel, faceLayer));
  if (it == m_cubemapLayouts.end()) {
    return VK_IMAGE_LAYOUT_UNDEFINED;
  }
  return it->second;
}

void IblBakeRenderer::setTrackedCubemapLayout(StringID resourceName,
                                              u32 mipLevel, u32 faceLayer,
                                              VkImageLayout layout) {
  m_cubemapLayouts[cubemapLayoutKey(resourceName, mipLevel, faceLayer)] =
      layout;
}

void IblBakeRenderer::transitionCubemapToShaderRead(StringID resourceName,
                                                    u32 mipLevels) {
  auto attachmentOpt = m_resourceManager.getCubemapBakeAttachment(resourceName);
  if (!attachmentOpt.has_value()) {
    throw std::runtime_error("missing cubemap bake attachment");
  }
  auto &attachment = attachmentOpt->get();
  auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
  for (u32 mip = 0; mip < mipLevels; ++mip) {
    for (u32 face = 0; face < 6u; ++face) {
      const VkImageLayout oldLayout =
          getTrackedCubemapLayout(resourceName, mip, face);
      if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        continue;
      }
      transitionSubresource(cmd->getHandle(), attachment.texture->getHandle(),
                            oldLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, mip, face);
      setTrackedCubemapLayout(resourceName, mip, face,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  }
  m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                           m_device.getGraphicsQueue());
}

void IblBakeRenderer::renderEquirectToCubemap(
    const CombinedTextureSamplerSharedPtr &source, u32 skyboxSize) {
  auto attachmentOpt =
      m_resourceManager.getCubemapBakeAttachment(StringID("SkyboxMap"));
  if (!attachmentOpt.has_value()) {
    throw std::runtime_error("missing skybox bake attachment");
  }
  auto &attachment = attachmentOpt->get();
  auto renderPass = VulkanRenderPass::create(
      m_device, std::optional<VkFormat>{attachment.format}, std::nullopt,
      false);
  RenderTargetDesc target =
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float);
  auto captureView =
      std::make_unique<CaptureViewResource>(captureViewProjections()[0]);
  auto work = makeBakeItem(
      "equirect_to_cubemap", target,
      {GpuResourceRef{*source}, GpuResourceRef{*captureView}}, 36u);

  syncBakeItemResources(m_resourceManager, m_cmdBufferManager, work);
  auto pipeline = m_resourceManager.getOrCreatePipeline(work.pipelineBuildDesc);

  const auto viewProjections = captureViewProjections();
  for (u32 face = 0; face < 6u; ++face) {
    captureView->setViewProj(viewProjections[face]);
    m_resourceManager.syncResource(m_cmdBufferManager,
                                   GpuResourceRef{*captureView});
    auto &view = m_resourceManager.getOrCreateCubemapBakeSubresourceView(
        StringID("SkyboxMap"), 0, face);
    auto framebuffer =
        VulkanFrameBuffer::create(m_device, renderPass->getHandle(),
                                  std::vector<VkImageView>{view.getHandle()},
                                  VkExtent2D{skyboxSize, skyboxSize});
    auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
    transitionSubresource(
        cmd->getHandle(), attachment.texture->getHandle(),
        getTrackedCubemapLayout(StringID("SkyboxMap"), 0, face),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, face);
    auto clearValues = renderPass->getClearValues();
    clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    cmd->beginRenderPass(renderPass->getHandle(), framebuffer->getHandle(),
                         VkExtent2D{skyboxSize, skyboxSize}, clearValues);
    cmd->setViewport(skyboxSize, skyboxSize);
    cmd->setScissor(skyboxSize, skyboxSize);
    cmd->bindPipeline(pipeline);
    cmd->bindResources(m_resourceManager, pipeline, work.input, work.desc);
    cmd->executeRenderInput(work.input, work.desc);
    cmd->endRenderPass();
    m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                             m_device.getGraphicsQueue());
    setTrackedCubemapLayout(StringID("SkyboxMap"), 0, face,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  }
}

void IblBakeRenderer::renderIrradianceCubemap(u32 irradianceSize) {
  auto attachmentOpt =
      m_resourceManager.getCubemapBakeAttachment(StringID("IrradianceMap"));
  if (!attachmentOpt.has_value()) {
    throw std::runtime_error("missing irradiance bake attachment");
  }
  auto &attachment = attachmentOpt->get();
  auto renderPass = VulkanRenderPass::create(
      m_device, std::optional<VkFormat>{attachment.format}, std::nullopt,
      false);
  RenderTargetDesc target =
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float);
  auto skybox = std::make_unique<BakedTextureResource>(StringID("SkyboxMap"),
                                                       StringID("SkyboxMap"));
  m_resourceManager.aliasCubemapBakeTextureResource(GpuResourceRef{*skybox},
                                                    StringID("SkyboxMap"));
  auto captureView =
      std::make_unique<CaptureViewResource>(captureViewProjections()[0]);
  auto work = makeBakeItem(
      "ibl_irradiance_convolve", target,
      {GpuResourceRef{*skybox}, GpuResourceRef{*captureView}}, 36u);

  syncBakeItemResources(m_resourceManager, m_cmdBufferManager, work);
  auto pipeline = m_resourceManager.getOrCreatePipeline(work.pipelineBuildDesc);

  const auto viewProjections = captureViewProjections();
  for (u32 face = 0; face < 6u; ++face) {
    captureView->setViewProj(viewProjections[face]);
    m_resourceManager.syncResource(m_cmdBufferManager,
                                   GpuResourceRef{*captureView});
    auto &view = m_resourceManager.getOrCreateCubemapBakeSubresourceView(
        StringID("IrradianceMap"), 0, face);
    auto framebuffer =
        VulkanFrameBuffer::create(m_device, renderPass->getHandle(),
                                  std::vector<VkImageView>{view.getHandle()},
                                  VkExtent2D{irradianceSize, irradianceSize});
    auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
    transitionSubresource(
        cmd->getHandle(), attachment.texture->getHandle(),
        getTrackedCubemapLayout(StringID("IrradianceMap"), 0, face),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, face);
    auto clearValues = renderPass->getClearValues();
    clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    cmd->beginRenderPass(renderPass->getHandle(), framebuffer->getHandle(),
                         VkExtent2D{irradianceSize, irradianceSize},
                         clearValues);
    cmd->setViewport(irradianceSize, irradianceSize);
    cmd->setScissor(irradianceSize, irradianceSize);
    cmd->bindPipeline(pipeline);
    cmd->bindResources(m_resourceManager, pipeline, work.input, work.desc);
    cmd->executeRenderInput(work.input, work.desc);
    cmd->endRenderPass();
    m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                             m_device.getGraphicsQueue());
    setTrackedCubemapLayout(StringID("IrradianceMap"), 0, face,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  }
}

void IblBakeRenderer::renderPrefilterCubemap(u32 prefilterSize, u32 mipLevels) {
  auto attachmentOpt =
      m_resourceManager.getCubemapBakeAttachment(StringID("PrefilteredEnvMap"));
  if (!attachmentOpt.has_value()) {
    throw std::runtime_error("missing prefilter bake attachment");
  }
  auto &attachment = attachmentOpt->get();
  auto renderPass = VulkanRenderPass::create(
      m_device, std::optional<VkFormat>{attachment.format}, std::nullopt,
      false);
  RenderTargetDesc target =
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float);
  auto skybox = std::make_unique<BakedTextureResource>(StringID("SkyboxMap"),
                                                       StringID("SkyboxMap"));
  m_resourceManager.aliasCubemapBakeTextureResource(GpuResourceRef{*skybox},
                                                    StringID("SkyboxMap"));
  auto captureView =
      std::make_unique<CaptureViewResource>(captureViewProjections()[0]);
  auto prefilter = std::make_unique<PrefilterResource>(
      PrefilterParams{0.0f, static_cast<float>(mipLevels), 64.0f, 0.0f});
  auto work =
      makeBakeItem("ibl_prefilter_env", target,
                   {GpuResourceRef{*skybox}, GpuResourceRef{*captureView},
                    GpuResourceRef{*prefilter}},
                   36u);

  syncBakeItemResources(m_resourceManager, m_cmdBufferManager, work);
  auto pipeline = m_resourceManager.getOrCreatePipeline(work.pipelineBuildDesc);

  const auto viewProjections = captureViewProjections();
  for (u32 mip = 0; mip < mipLevels; ++mip) {
    const u32 extentValue = mipExtent(prefilterSize, mip);
    const float roughness =
        mipLevels <= 1u
            ? 0.0f
            : static_cast<float>(mip) / static_cast<float>(mipLevels - 1u);
    prefilter->setParams(
        PrefilterParams{roughness, static_cast<float>(mipLevels), 64.0f, 0.0f});
    m_resourceManager.syncResource(m_cmdBufferManager,
                                   GpuResourceRef{*prefilter});
    for (u32 face = 0; face < 6u; ++face) {
      captureView->setViewProj(viewProjections[face]);
      m_resourceManager.syncResource(m_cmdBufferManager,
                                     GpuResourceRef{*captureView});
      auto &view = m_resourceManager.getOrCreateCubemapBakeSubresourceView(
          StringID("PrefilteredEnvMap"), mip, face);
      auto framebuffer =
          VulkanFrameBuffer::create(m_device, renderPass->getHandle(),
                                    std::vector<VkImageView>{view.getHandle()},
                                    VkExtent2D{extentValue, extentValue});
      auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
      transitionSubresource(
          cmd->getHandle(), attachment.texture->getHandle(),
          getTrackedCubemapLayout(StringID("PrefilteredEnvMap"), mip, face),
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, mip, face);
      auto clearValues = renderPass->getClearValues();
      clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
      cmd->beginRenderPass(renderPass->getHandle(), framebuffer->getHandle(),
                           VkExtent2D{extentValue, extentValue}, clearValues);
      cmd->setViewport(extentValue, extentValue);
      cmd->setScissor(extentValue, extentValue);
      cmd->bindPipeline(pipeline);
      cmd->bindResources(m_resourceManager, pipeline, work.input, work.desc);
      cmd->executeRenderInput(work.input, work.desc);
      cmd->endRenderPass();
      m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                               m_device.getGraphicsQueue());
      setTrackedCubemapLayout(StringID("PrefilteredEnvMap"), mip, face,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
  }
}

void IblBakeRenderer::clearBrdfLut(u32 size) {
  auto attachmentOpt =
      m_resourceManager.getFrameGraphAttachment(StringID("BrdfLut"));
  if (!attachmentOpt.has_value()) {
    throw std::runtime_error("missing BRDF LUT bake attachment");
  }
  auto &attachment = attachmentOpt->get();
  auto renderPass = VulkanRenderPass::create(
      m_device, std::optional<VkFormat>{attachment.format}, std::nullopt,
      false);
  auto framebuffer = VulkanFrameBuffer::create(
      m_device, renderPass->getHandle(),
      std::vector<VkImageView>{attachment.texture->getImageView()},
      VkExtent2D{size, size});

  RenderTargetDesc target =
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float);
  auto work = makeFullscreenBakeItem("ibl_brdf_lut", target);
  syncBakeItemResources(m_resourceManager, m_cmdBufferManager, work);
  auto pipeline = m_resourceManager.getOrCreatePipeline(work.pipelineBuildDesc);

  auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
  transitionTexture2D(
      cmd->getHandle(), attachment.texture->getHandle(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  auto clearValues = renderPass->getClearValues();
  clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
  cmd->beginRenderPass(renderPass->getHandle(), framebuffer->getHandle(),
                       VkExtent2D{size, size}, clearValues);
  cmd->setViewport(size, size);
  cmd->setScissor(size, size);
  cmd->bindPipeline(pipeline);
  cmd->bindResources(m_resourceManager, pipeline, work.input, work.desc);
  cmd->executeRenderInput(work.input, work.desc);
  cmd->endRenderPass();
  m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                           m_device.getGraphicsQueue());
  attachment.currentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

void IblBakeRenderer::transitionBrdfLutToShaderRead() {
  auto attachmentOpt =
      m_resourceManager.getFrameGraphAttachment(StringID("BrdfLut"));
  if (!attachmentOpt.has_value()) {
    throw std::runtime_error("missing BRDF LUT bake attachment");
  }
  auto &attachment = attachmentOpt->get();
  if (attachment.currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    return;
  }

  VkAccessFlags srcAccess = 0;
  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  if (attachment.currentLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else if (attachment.currentLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  }

  auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
  transitionTexture2D(cmd->getHandle(), attachment.texture->getHandle(),
                      attachment.currentLayout,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, srcAccess,
                      VK_ACCESS_SHADER_READ_BIT, srcStage,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                           m_device.getGraphicsQueue());
  attachment.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

bool IblBakeRenderer::debugReadbackCubemapFaceHasData(StringID resourceName,
                                                      u32 mipLevel,
                                                      u32 faceLayer,
                                                      u32 extent) {
  auto attachmentOpt = m_resourceManager.getCubemapBakeAttachment(resourceName);
  if (!attachmentOpt.has_value()) {
    return false;
  }
  auto &attachment = attachmentOpt->get();
  const VkDeviceSize byteSize = static_cast<VkDeviceSize>(extent) *
                                static_cast<VkDeviceSize>(extent) * 8u;
  auto readback =
      VulkanBuffer::create(m_device, byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
  const VkImageLayout oldLayout =
      getTrackedCubemapLayout(resourceName, mipLevel, faceLayer);
  VkAccessFlags srcAccess = 0;
  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    srcAccess = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  }
  transitionSubresource(cmd->getHandle(), attachment.texture->getHandle(),
                        oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        srcAccess, VK_ACCESS_TRANSFER_READ_BIT, srcStage,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, mipLevel, faceLayer);

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = mipLevel;
  region.imageSubresource.baseArrayLayer = faceLayer;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {extent, extent, 1};
  vkCmdCopyImageToBuffer(cmd->getHandle(), attachment.texture->getHandle(),
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         readback->getHandle(), 1, &region);
  m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                           m_device.getGraphicsQueue());
  setTrackedCubemapLayout(resourceName, mipLevel, faceLayer,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  const void *mapped = readback->map();
  const bool hasData = bufferHasNonZeroByte(mapped, byteSize);
  readback->unmap();
  return hasData;
}

bool IblBakeRenderer::debugReadbackBrdfLutHasData(u32 extent) {
  auto attachmentOpt =
      m_resourceManager.getFrameGraphAttachment(StringID("BrdfLut"));
  if (!attachmentOpt.has_value()) {
    return false;
  }
  auto &attachment = attachmentOpt->get();
  const VkDeviceSize byteSize = static_cast<VkDeviceSize>(extent) *
                                static_cast<VkDeviceSize>(extent) * 8u;
  auto readback =
      VulkanBuffer::create(m_device, byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
  VkAccessFlags srcAccess = 0;
  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  if (attachment.currentLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else if (attachment.currentLayout ==
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    srcAccess = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }
  transitionTexture2D(
      cmd->getHandle(), attachment.texture->getHandle(),
      attachment.currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcAccess,
      VK_ACCESS_TRANSFER_READ_BIT, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {extent, extent, 1};
  vkCmdCopyImageToBuffer(cmd->getHandle(), attachment.texture->getHandle(),
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         readback->getHandle(), 1, &region);
  m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                           m_device.getGraphicsQueue());
  attachment.currentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  const void *mapped = readback->map();
  const bool hasData = bufferHasNonZeroByte(mapped, byteSize);
  readback->unmap();
  return hasData;
}

} // namespace LX_core::backend
