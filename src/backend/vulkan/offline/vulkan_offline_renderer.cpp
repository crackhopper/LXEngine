#include "backend/vulkan/offline/vulkan_offline_renderer.hpp"

#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "core/raytracing/software_bvh.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/scene/scene_resource_table_upload_view.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

namespace LX_core::backend::offline {
namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

struct alignas(16) ShaderMaterialRecord final {
  Vec4f baseColor{1.0f, 1.0f, 1.0f, 1.0f};
  Vec4f params{0.0f, 0.5f, 0.0f, 0.0f};
  Vec4f emissive{0.0f, 0.0f, 0.0f, 0.0f};
};

struct alignas(16) ShaderSceneParams final {
  Vec4f eye{};
  Vec4f cameraRight{};
  Vec4f cameraUp{};
  Vec4f cameraForward{};
  Vec4f lightDirectionIntensity{};
  Vec4f lightColorEnvironment{};
  Vec4f backgroundColor{};
  u32 width = 0;
  u32 height = 0;
  u32 samples = 1;
  u32 seed = 1;
  u32 primitiveCount = 0;
  u32 bvhNodeCount = 0;
  u32 materialCount = 0;
  u32 maxBounce = 1;
  u32 shadowsEnabled = 1;
  u32 compareMode = 0;
  u32 pad1 = 0;
  u32 pad2 = 0;
};

struct DirectionalLightParams final {
  Vec3f direction{-0.35f, -1.0f, -0.25f};
  Vec3f color{1.0f, 0.96f, 0.88f};
  float intensity = 1.0f;
};

static_assert(sizeof(ShaderMaterialRecord) == 48);
static_assert(sizeof(ShaderSceneParams) == 160);

[[nodiscard]] std::vector<char> loadComputeShader() {
  constexpr const char *shaderFile = "offline_primary_ray.comp.spv";
  std::string shaderPath;
  std::filesystem::path probe = std::filesystem::current_path();
  for (int i = 0; i < 8 && shaderPath.empty(); ++i) {
    const std::filesystem::path buildShaderPath =
        probe / "build" / "assets" / "shaders" / "glsl" / shaderFile;
    if (std::filesystem::exists(buildShaderPath)) {
      shaderPath = buildShaderPath.string();
      break;
    }
    const auto parent = probe.parent_path();
    if (parent == probe) {
      break;
    }
    probe = parent;
  }
  if (shaderPath.empty()) {
    shaderPath = getShaderPath("offline_primary_ray", "comp.spv");
  }
  if (shaderPath.empty()) {
    throw std::runtime_error("failed to find offline compute shader SPIR-V");
  }
  return readFile(shaderPath);
}

[[nodiscard]] VkShaderModule createShaderModule(VkDevice device,
                                                const std::vector<char> &code) {
  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = code.size();
  // Vulkan requires pCode as uint32_t-aligned SPIR-V words.
  info.pCode = reinterpret_cast<const u32 *>(code.data());
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
    throw std::runtime_error("failed to create offline compute shader module");
  }
  return module;
}

void uploadVector(VulkanBuffer &buffer, const void *data, VkDeviceSize size) {
  if (size == 0) {
    return;
  }
  buffer.uploadData(data, size);
}

template <typename T>
[[nodiscard]] VkDeviceSize byteSize(const std::span<const T> values) {
  return static_cast<VkDeviceSize>(sizeof(T)) *
         static_cast<VkDeviceSize>(values.size());
}

template <typename T>
[[nodiscard]] VkDeviceSize byteSize(const std::vector<T> &values) {
  return static_cast<VkDeviceSize>(sizeof(T)) *
         static_cast<VkDeviceSize>(values.size());
}

[[nodiscard]] Vec4f vec4(const Vec3f &value, const float w) {
  return Vec4f{value.x, value.y, value.z, w};
}

[[nodiscard]] Vec3f viewRow(const Mat4f &view, const int row) {
  return Vec3f{view(row, 0), view(row, 1), view(row, 2)};
}

[[nodiscard]] std::optional<std::reference_wrapper<const CameraResource>>
findActiveCamera(const SceneResourceTable &scene) {
  const RenderSceneSnapshot snapshot = scene.buildSnapshot();
  for (const CameraHandle handle : snapshot.cameraHandles) {
    auto camera = scene.resolve(handle);
    if (camera.has_value() && camera->get().active) {
      return std::cref(camera->get());
    }
  }
  return std::nullopt;
}

[[nodiscard]] CameraPose poseFromViewMatrix(const Mat4f &view) {
  const Vec3f right = viewRow(view, 0).normalized();
  const Vec3f up = viewRow(view, 1).normalized();
  const Vec3f backward = viewRow(view, 2).normalized();
  const float tx = view(0, 3);
  const float ty = view(1, 3);
  const float tz = view(2, 3);
  const Vec3f eye = right * -tx + up * -ty + backward * -tz;
  return makeCameraPose(eye, backward * -1.0f, up);
}

[[nodiscard]] CameraRayFrame
makeRayFrameFromCameraResource(const CameraResource &camera,
                               const LX_core::offline::OutputProfile &output) {
  const CameraPose pose = poseFromViewMatrix(camera.view);
  const CameraPose resolvedPose = makeCameraPose(pose.eye, pose.forward, pose.up);
  const Vec3f rightAxis =
      resolvedPose.forward.cross(resolvedPose.up).normalized();

  float verticalScale = 1.0f;
  if (output.cameraOverrides.fovY.has_value()) {
    verticalScale =
        std::tan(*output.cameraOverrides.fovY * kDegToRad * 0.5f);
  } else if (std::abs(camera.proj(1, 1)) > 1.0e-6f) {
    verticalScale = 1.0f / std::abs(camera.proj(1, 1));
  }
  const float outputAspect =
      output.height == 0
          ? 1.0f
          : static_cast<float>(output.width) / static_cast<float>(output.height);
  const float aspect = output.cameraOverrides.aspect.value_or(outputAspect);
  const float horizontalScale = verticalScale * std::max(aspect, 0.0001f);

  return CameraRayFrame{
      .eye = resolvedPose.eye,
      .right = rightAxis * horizontalScale,
      .up = resolvedPose.up * verticalScale,
      .forward = resolvedPose.forward,
  };
}

[[nodiscard]] DirectionalLightParams
findFirstDirectionalLight(const SceneResourceTable &scene) {
  const RenderSceneSnapshot snapshot = scene.buildSnapshot();
  for (const LightHandle handle : snapshot.lightHandles) {
    auto light = scene.resolve(handle);
    if (!light.has_value()) {
      continue;
    }
    try {
      const auto &directional =
          dynamic_cast<const DirectionalLight &>(light->get());
      return DirectionalLightParams{
          .direction = directional.getDirection().normalized(),
          .color = directional.getColor(),
          .intensity = directional.getIntensity(),
      };
    } catch (const std::bad_cast &) {
    }
  }
  return {};
}

[[nodiscard]] std::vector<ShaderMaterialRecord> makeShaderMaterials(
    const SceneResourceTableUploadView &uploadView) {
  std::vector<ShaderMaterialRecord> materials;
  materials.reserve(uploadView.materials.size());
  for (const SceneGpuMaterialRecord &material : uploadView.materials) {
    materials.push_back(ShaderMaterialRecord{
        .baseColor = material.baseColor,
        .params = material.pbrParams,
        .emissive = material.emissive,
    });
  }
  return materials;
}

[[nodiscard]] std::vector<u32>
makeShaderLocalIndices(const SceneResourceTableUploadView &uploadView) {
  std::vector<u32> indices(uploadView.indices.begin(), uploadView.indices.end());
  for (const SceneGpuMeshRecord &mesh : uploadView.meshes) {
    if (mesh.indexOffset > indices.size() ||
        static_cast<usize>(mesh.indexCount) > indices.size() - mesh.indexOffset) {
      throw std::runtime_error("offline upload mesh references invalid indices");
    }
    for (u32 i = 0; i < mesh.indexCount; ++i) {
      u32 &index = indices[mesh.indexOffset + i];
      if (index < mesh.vertexOffset) {
        throw std::runtime_error(
            "offline upload index precedes mesh vertex offset");
      }
      index -= mesh.vertexOffset;
    }
  }
  return indices;
}

[[nodiscard]] std::vector<SceneGpuPrimitiveRecord> makeShaderPrimitives(
    const SceneResourceTableUploadView &uploadView,
    const SceneSoftwareBvh &bvh) {
  std::vector<SceneGpuPrimitiveRecord> primitives;
  primitives.reserve(bvh.primitives().size());
  for (const SceneSoftwareBvhPrimitive &primitive : bvh.primitives()) {
    if (primitive.primitiveIndex >= uploadView.primitives.size()) {
      throw std::runtime_error(
          "software BVH primitive references invalid upload primitive");
    }
    primitives.push_back(uploadView.primitives[primitive.primitiveIndex]);
  }
  return primitives;
}

[[nodiscard]] ShaderSceneParams makeShaderParams(
    const LX_core::offline::OfflineRenderJob &job,
    const SceneResourceTableUploadView &uploadView,
    const SceneSoftwareBvh &bvh) {
  const auto camera = findActiveCamera(job.scene);
  if (!camera.has_value()) {
    throw std::runtime_error("offline render scene has no active camera");
  }
  const CameraRayFrame rayFrame =
      makeRayFrameFromCameraResource(camera->get(), job.output);
  const DirectionalLightParams light = findFirstDirectionalLight(job.scene);

  ShaderSceneParams params;
  params.eye = vec4(rayFrame.eye, 0.0f);
  params.cameraRight = vec4(rayFrame.right, 0.0f);
  params.cameraUp = vec4(rayFrame.up, 0.0f);
  params.cameraForward = vec4(rayFrame.forward, 0.0f);
  params.lightDirectionIntensity = vec4(light.direction.normalized(),
                                        light.intensity);
  params.lightColorEnvironment =
      Vec4f{light.color.x, light.color.y, light.color.z, 0.35f};
  params.backgroundColor =
      Vec4f{job.output.backgroundColor.x, job.output.backgroundColor.y,
            job.output.backgroundColor.z, 1.0f};
  params.width = job.output.width;
  params.height = job.output.height;
  params.samples = job.offline.samples;
  params.seed = job.offline.seed;
  params.primitiveCount = static_cast<u32>(bvh.primitiveCount());
  params.bvhNodeCount = static_cast<u32>(bvh.nodes().size());
  params.materialCount = static_cast<u32>(uploadView.materials.size());
  params.maxBounce = job.offline.maxBounce;
  params.shadowsEnabled = job.offline.shadows ? 1u : 0u;
  params.compareMode = job.offline.compareMode == "albedo" ? 1u : 0u;
  return params;
}

void validateUploadView(const SceneResourceTableUploadView &uploadView) {
  if (uploadView.vertices.empty() || uploadView.indices.empty() ||
      uploadView.meshes.empty() || uploadView.primitives.empty() ||
      uploadView.objects.empty() || uploadView.materials.empty()) {
    throw std::runtime_error(
        "offline render scene upload view is missing renderable records");
  }
}

[[nodiscard]] std::vector<u32> toSpirvWords(const std::vector<char> &code) {
  if (code.size() % sizeof(u32) != 0) {
    throw std::runtime_error("offline compute shader SPIR-V has invalid size");
  }
  std::vector<u32> words(code.size() / sizeof(u32));
  std::memcpy(words.data(), code.data(), code.size());
  return words;
}

[[nodiscard]] const LX_core::ShaderResourceBinding &
findReflectedBinding(
    const std::vector<LX_core::ShaderResourceBinding> &bindings,
    const u32 set, const u32 binding) {
  const auto it = std::find_if(
      bindings.begin(), bindings.end(),
      [set, binding](const LX_core::ShaderResourceBinding &reflected) {
        return reflected.set == set && reflected.binding == binding;
      });
  if (it == bindings.end()) {
    std::ostringstream msg;
    msg << "offline shader missing descriptor set " << set << " binding "
        << binding;
    throw std::runtime_error(msg.str());
  }
  return *it;
}

void validateOfflineDescriptorContract(const std::vector<char> &shaderCode) {
  struct ExpectedBinding {
    u32 binding = 0;
    const char *name = "";
    u32 blockSize = 0;
  };

  constexpr u32 kSet = 0;
  const std::array<ExpectedBinding, 9> expected{{
      {0, "Vertices", 0},
      {1, "Indices", 0},
      {2, "Meshes", 0},
      {3, "Primitives", 0},
      {4, "Objects", 0},
      {5, "Materials", 0},
      {6, "BvhNodes", 0},
      {7, "ParamsBuffer", static_cast<u32>(sizeof(ShaderSceneParams))},
      {8, "OutputBuffer", 0},
  }};

  LX_core::ShaderStageCode stageCode{};
  stageCode.stage = LX_core::ShaderStage::Compute;
  stageCode.bytecode = toSpirvWords(shaderCode);
  const auto reflected = LX_infra::ShaderReflector::reflect({stageCode});
  if (reflected.size() != expected.size()) {
    std::ostringstream msg;
    msg << "offline shader descriptor count mismatch: expected "
        << expected.size() << ", reflected " << reflected.size();
    throw std::runtime_error(msg.str());
  }

  for (const ExpectedBinding &expectedBinding : expected) {
    const auto &binding =
        findReflectedBinding(reflected, kSet, expectedBinding.binding);
    if (binding.name != expectedBinding.name) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " name mismatch: expected " << expectedBinding.name
          << ", reflected " << binding.name;
      throw std::runtime_error(msg.str());
    }
    if (binding.type != LX_core::ShaderPropertyType::StorageBuffer) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " must be a storage buffer";
      throw std::runtime_error(msg.str());
    }
    if (binding.descriptorCount != 1) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " descriptorCount mismatch";
      throw std::runtime_error(msg.str());
    }
    if ((static_cast<u32>(binding.stageFlags) &
         static_cast<u32>(LX_core::ShaderStage::Compute)) == 0) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " is not visible to compute stage";
      throw std::runtime_error(msg.str());
    }
    if (expectedBinding.blockSize != 0 && binding.size != 0 &&
        binding.size != expectedBinding.blockSize) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " block size mismatch: expected " << expectedBinding.blockSize
          << ", reflected " << binding.size;
      throw std::runtime_error(msg.str());
    }
  }
}

} // namespace

struct VulkanOfflineRenderer::Impl final {
  Impl() {
    expSetEnvVK();
    if (!initializeRuntimeAssetRoot()) {
      throw std::runtime_error("failed to initialize runtime asset root");
    }
    device = VulkanDevice::create();
    device->initializeHeadless("lxe_offline_render");
    commandManager = VulkanCommandBufferManager::create(
        *device, 1, device->getGraphicsQueueFamilyIndex());
  }

  ~Impl() {
    if (device) {
      device->waitIdle();
    }
    cleanupPipeline();
    commandManager.reset();
    device.reset();
  }

  void cleanupPipeline() {
    if (!device) {
      return;
    }
    const VkDevice vkDevice = device->getLogicalDevice();
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(vkDevice, pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
      pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
      descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (descriptorPool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
      descriptorPool = VK_NULL_HANDLE;
    }
  }

  void ensurePipeline() {
    if (pipeline != VK_NULL_HANDLE) {
      return;
    }
    const VkDevice vkDevice = device->getLogicalDevice();
    const auto shaderCode = loadComputeShader();
    validateOfflineDescriptorContract(shaderCode);

    std::array<VkDescriptorSetLayoutBinding, 9> bindings{};
    for (u32 i = 0; i < bindings.size(); ++i) {
      bindings[i].binding = i;
      bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].descriptorCount = 1;
      bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr,
                                    &descriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create offline descriptor set layout");
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    if (vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr,
                               &pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create offline compute pipeline layout");
    }

    const VkShaderModule shaderModule = createShaderModule(vkDevice, shaderCode);
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;
    const VkResult result = vkCreateComputePipelines(
        vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(vkDevice, shaderModule, nullptr);
    if (result != VK_SUCCESS) {
      throw std::runtime_error("failed to create offline compute pipeline");
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = static_cast<u32>(bindings.size());
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create offline descriptor pool");
    }
  }

  [[nodiscard]] LX_core::offline::OfflineReadbackImage
  render(const LX_core::offline::OfflineRenderJob &job) {
    ensurePipeline();

    const SceneResourceTableUploadView uploadView = job.scene.buildUploadView();
    validateUploadView(uploadView);
    const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(uploadView);
    const std::vector<u32> shaderIndices = makeShaderLocalIndices(uploadView);
    const std::vector<SceneGpuPrimitiveRecord> shaderPrimitives =
        makeShaderPrimitives(uploadView, bvh);
    const std::vector<ShaderMaterialRecord> shaderMaterials =
        makeShaderMaterials(uploadView);
    const ShaderSceneParams params = makeShaderParams(job, uploadView, bvh);

    const VkBufferUsageFlags storageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkMemoryPropertyFlags hostMemory =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    auto vertexBuffer = VulkanBuffer::create(
        *device, byteSize(uploadView.vertices), storageUsage, hostMemory);
    auto indexBuffer = VulkanBuffer::create(
        *device, byteSize(shaderIndices), storageUsage,
        hostMemory);
    auto meshBuffer = VulkanBuffer::create(
        *device, byteSize(uploadView.meshes), storageUsage, hostMemory);
    auto primitiveBuffer = VulkanBuffer::create(
        *device, byteSize(shaderPrimitives), storageUsage, hostMemory);
    auto objectBuffer = VulkanBuffer::create(
        *device, byteSize(uploadView.objects),
        storageUsage,
        hostMemory);
    auto materialBuffer = VulkanBuffer::create(
        *device, byteSize(shaderMaterials),
        storageUsage,
        hostMemory);
    auto bvhBuffer = VulkanBuffer::create(
        *device, byteSize(bvh.nodes()),
        storageUsage, hostMemory);
    auto paramsBuffer =
        VulkanBuffer::create(*device,
                             sizeof(ShaderSceneParams),
                             storageUsage, hostMemory);
    const VkDeviceSize outputSize =
        static_cast<VkDeviceSize>(job.output.width) *
        static_cast<VkDeviceSize>(job.output.height) * sizeof(Vec4f);
    auto outputBuffer =
        VulkanBuffer::create(*device, outputSize, storageUsage, hostMemory);

    uploadVector(*vertexBuffer, uploadView.vertices.data(),
                 byteSize(uploadView.vertices));
    uploadVector(*indexBuffer, shaderIndices.data(), byteSize(shaderIndices));
    uploadVector(*meshBuffer, uploadView.meshes.data(),
                 byteSize(uploadView.meshes));
    uploadVector(*primitiveBuffer, shaderPrimitives.data(),
                 byteSize(shaderPrimitives));
    uploadVector(*objectBuffer, uploadView.objects.data(),
                 byteSize(uploadView.objects));
    uploadVector(*materialBuffer, shaderMaterials.data(),
                 byteSize(shaderMaterials));
    uploadVector(*bvhBuffer, bvh.nodes().data(), byteSize(bvh.nodes()));
    uploadVector(*paramsBuffer, &params, sizeof(ShaderSceneParams));

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(device->getLogicalDevice(), &allocInfo,
                                 &descriptorSet) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate offline descriptor set");
    }

    std::array<VkDescriptorBufferInfo, 9> bufferInfos{{
        {vertexBuffer->getHandle(), 0, vertexBuffer->getSize()},
        {indexBuffer->getHandle(), 0, indexBuffer->getSize()},
        {meshBuffer->getHandle(), 0, meshBuffer->getSize()},
        {primitiveBuffer->getHandle(), 0, primitiveBuffer->getSize()},
        {objectBuffer->getHandle(), 0, objectBuffer->getSize()},
        {materialBuffer->getHandle(), 0, materialBuffer->getSize()},
        {bvhBuffer->getHandle(), 0, bvhBuffer->getSize()},
        {paramsBuffer->getHandle(), 0, paramsBuffer->getSize()},
        {outputBuffer->getHandle(), 0, outputBuffer->getSize()},
    }};
    std::array<VkWriteDescriptorSet, 9> writes{};
    for (u32 i = 0; i < writes.size(); ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = descriptorSet;
      writes[i].dstBinding = i;
      writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[i].descriptorCount = 1;
      writes[i].pBufferInfo = &bufferInfos[i];
    }
    vkUpdateDescriptorSets(device->getLogicalDevice(),
                           static_cast<u32>(writes.size()), writes.data(), 0,
                           nullptr);

    auto cmd = commandManager->beginSingleTimeCommands();
    vkCmdBindPipeline(cmd->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(cmd->getHandle(), (job.output.width + 7) / 8,
                  (job.output.height + 7) / 8, 1);
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd->getHandle(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                         0, nullptr);
    commandManager->endSingleTimeCommands(std::move(cmd),
                                          device->getGraphicsQueue());

    LX_core::offline::OfflineReadbackImage image;
    image.width = job.output.width;
    image.height = job.output.height;
    image.rgba.resize(image.pixelCount() * 4);
    void *mapped = outputBuffer->map();
    std::memcpy(image.rgba.data(), mapped,
                static_cast<usize>(image.rgba.size() * sizeof(float)));
    outputBuffer->unmap();

    vkResetDescriptorPool(device->getLogicalDevice(), descriptorPool, 0);
    return image;
  }

  VulkanDeviceUniquePtr device;
  VulkanCommandBufferManagerUniquePtr commandManager;
  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
};

VulkanOfflineRenderer::VulkanOfflineRenderer()
    : m_impl(std::make_unique<Impl>()) {}

VulkanOfflineRenderer::~VulkanOfflineRenderer() = default;

LX_core::offline::OfflineReadbackImage
VulkanOfflineRenderer::render(const LX_core::offline::OfflineRenderJob &job) {
  return m_impl->render(job);
}

} // namespace LX_core::backend::offline
