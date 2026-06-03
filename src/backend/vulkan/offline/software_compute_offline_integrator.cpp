#include "backend/vulkan/offline/software_compute_offline_integrator.hpp"

#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "core/offline/offline_render_work_graph.hpp"
#include "core/offline/offline_render_validation.hpp"
#include "core/raytracing/software_bvh.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/scene/scene_resource_table_upload_view.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <algorithm>
#include <array>
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

struct DirectionalLightParams final {
  Vec3f direction{-0.35f, -1.0f, -0.25f};
  Vec3f color{1.0f, 0.96f, 0.88f};
  float intensity = 1.0f;
};

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

[[nodiscard]] CameraRayFrame
makeRayFrameFromCameraResource(const CameraResource &camera,
                               const LX_core::offline::OutputProfile &output) {
  const CameraProjection projection =
      LX_core::offline::resolveOutputCameraProjection(camera.projection,
                                                      output);
  return makeCameraRayFrame(camera.pose, projection);
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
    } catch (const std::bad_cast &) {}
  }
  return {};
}

[[nodiscard]] std::vector<SceneGpuPrimitiveRecord>
makeShaderPrimitives(const SceneResourceTableUploadView &uploadView,
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

[[nodiscard]] SceneGpuFrameParams
makeShaderParams(const LX_core::offline::OfflineRenderJob &job,
                 const SceneResourceTableUploadView &uploadView,
                 const SceneSoftwareBvh &bvh) {
  const auto camera = findActiveCamera(job.scene);
  if (!camera.has_value()) {
    throw std::runtime_error("offline render scene has no active camera");
  }
  const CameraRayFrame rayFrame =
      makeRayFrameFromCameraResource(camera->get(), job.output);
  const DirectionalLightParams light = findFirstDirectionalLight(job.scene);

  SceneGpuFrameParams params;
  params.eye = vec4(rayFrame.eye, 0.0f);
  params.cameraRight = vec4(rayFrame.right, 0.0f);
  params.cameraUp = vec4(rayFrame.up, 0.0f);
  params.cameraForward = vec4(rayFrame.forward, 0.0f);
  params.lightDirectionIntensity =
      vec4(light.direction.normalized(), light.intensity);
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

[[nodiscard]] const LX_core::ShaderResourceBinding &findReflectedBinding(
    const std::vector<LX_core::ShaderResourceBinding> &bindings, const u32 set,
    const u32 binding) {
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
      {0, "SceneVertices", 0},
      {1, "SceneIndices", 0},
      {2, "SceneMeshes", 0},
      {3, "ScenePrimitives", 0},
      {4, "SceneObjects", 0},
      {5, "SceneMaterials", 0},
      {6, "SceneBvhNodes", 0},
      {7, "SceneFrameParams", static_cast<u32>(sizeof(SceneGpuFrameParams))},
      {8, "OutputPixels", 0},
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

struct SoftwareComputeOfflineIntegrator::Impl final {
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
      throw std::runtime_error(
          "failed to create offline descriptor set layout");
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    if (vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr,
                               &pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error(
          "failed to create offline compute pipeline layout");
    }

    const VkShaderModule shaderModule =
        createShaderModule(vkDevice, shaderCode);
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
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
    LX_core::offline::validateOfflineRenderJob(job);
    const FrameGraph renderGraph =
        LX_core::offline::buildOfflineRenderWorkGraph(job);
    const CompiledFrameGraph compiledGraph = renderGraph.compile();
    if (!compiledGraph.isValid()) {
      throw std::runtime_error(compiledGraph.errorText());
    }
    if (renderGraph.getPasses().empty()) {
      throw std::runtime_error("offline render graph has no passes");
    }
    const FramePass &rayTracePass = renderGraph.getPasses().front();
    if (rayTracePass.queue.getItems().empty()) {
      throw std::runtime_error("offline ray trace pass has no work item");
    }
    const RenderWorkItem &workItem = rayTracePass.queue.getItems().front();
    if (workItem.domain != RenderDomain::Offline ||
        workItem.kind != RenderWorkKind::ComputeDispatch) {
      throw std::runtime_error(
          "offline ray trace pass must produce offline compute work");
    }

    const SceneResourceTableUploadView uploadView = job.scene.buildUploadView();
    validateUploadView(uploadView);
    const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(uploadView);
    const std::vector<SceneGpuPrimitiveRecord> shaderPrimitives =
        makeShaderPrimitives(uploadView, bvh);
    const SceneGpuFrameParams params = makeShaderParams(job, uploadView, bvh);

    ensurePipeline();

    const VkBufferUsageFlags storageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkMemoryPropertyFlags hostMemory =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    auto vertexBuffer = VulkanBuffer::create(
        *device, byteSize(uploadView.vertices), storageUsage, hostMemory);
    auto indexBuffer = VulkanBuffer::create(*device, byteSize(uploadView.indices),
                                            storageUsage, hostMemory);
    auto meshBuffer = VulkanBuffer::create(*device, byteSize(uploadView.meshes),
                                           storageUsage, hostMemory);
    auto primitiveBuffer = VulkanBuffer::create(
        *device, byteSize(shaderPrimitives), storageUsage, hostMemory);
    auto objectBuffer = VulkanBuffer::create(
        *device, byteSize(uploadView.objects), storageUsage, hostMemory);
    auto materialBuffer = VulkanBuffer::create(
        *device, byteSize(uploadView.materials), storageUsage, hostMemory);
    auto bvhBuffer = VulkanBuffer::create(*device, byteSize(bvh.nodes()),
                                          storageUsage, hostMemory);
    auto paramsBuffer = VulkanBuffer::create(*device, sizeof(SceneGpuFrameParams),
                                             storageUsage, hostMemory);
    const VkDeviceSize outputSize =
        static_cast<VkDeviceSize>(job.output.width) *
        static_cast<VkDeviceSize>(job.output.height) * sizeof(Vec4f);
    auto outputBuffer =
        VulkanBuffer::create(*device, outputSize, storageUsage, hostMemory);

    uploadVector(*vertexBuffer, uploadView.vertices.data(),
                 byteSize(uploadView.vertices));
    uploadVector(*indexBuffer, uploadView.indices.data(),
                 byteSize(uploadView.indices));
    uploadVector(*meshBuffer, uploadView.meshes.data(),
                 byteSize(uploadView.meshes));
    uploadVector(*primitiveBuffer, shaderPrimitives.data(),
                 byteSize(shaderPrimitives));
    uploadVector(*objectBuffer, uploadView.objects.data(),
                 byteSize(uploadView.objects));
    uploadVector(*materialBuffer, uploadView.materials.data(),
                 byteSize(uploadView.materials));
    uploadVector(*bvhBuffer, bvh.nodes().data(), byteSize(bvh.nodes()));
    uploadVector(*paramsBuffer, &params, sizeof(SceneGpuFrameParams));

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
    vkCmdBindPipeline(cmd->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE,
                      pipeline);
    vkCmdBindDescriptorSets(cmd->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(cmd->getHandle(), workItem.compute.groupCountX,
                  workItem.compute.groupCountY,
                  workItem.compute.groupCountZ);
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

SoftwareComputeOfflineIntegrator::SoftwareComputeOfflineIntegrator()
    : m_impl(std::make_unique<Impl>()) {}

SoftwareComputeOfflineIntegrator::~SoftwareComputeOfflineIntegrator() = default;

LX_core::offline::OfflineReadbackImage SoftwareComputeOfflineIntegrator::render(
    const LX_core::offline::OfflineRenderJob &job) {
  return m_impl->render(job);
}

bool isOfflineIntegratorSupported(const std::string &name) {
  return name == "software-compute";
}

std::unique_ptr<OfflineIntegrator>
createOfflineIntegrator(const std::string &name) {
  if (name == "software-compute") {
    return std::make_unique<SoftwareComputeOfflineIntegrator>();
  }
  throw std::runtime_error("unsupported offline integrator: " + name);
}

} // namespace LX_core::backend::offline
