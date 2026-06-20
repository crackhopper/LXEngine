#include "backend/vulkan/offline/vulkan_offline_renderer.hpp"

#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "backend/vulkan/vulkan_frame_graph_executor.hpp"
#include "core/frame_graph/frame_graph_build_plan.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
#include "core/frame_graph/render_work_build_context.hpp"
#include "core/frame_graph/render_work_compiler.hpp"
#include "core/offline/offline_render_validation.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/resource_parsers/render_resource_scene_parser_adapters.hpp"
#include "infra/resource_parsers/material_source_variant_resolver.hpp"
#include "infra/resource_parsers/scene_resource_parser_registry.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace LX_core::backend::offline {
namespace {

[[nodiscard]] std::string debugString(StringID value) {
  return value.id == 0 ? std::string("<none>")
                       : GlobalStringTable::get().toDebugString(value);
}

[[nodiscard]] std::string joinDiagnostics(const std::vector<std::string> &items) {
  std::string out;
  for (const std::string &item : items) {
    if (!out.empty()) {
      out += "\n- ";
    }
    out += item;
  }
  return out;
}

void validatePreparedOfflineRenderDescs(
    const std::vector<PreparedFramePassWork> &preparedPasses) {
  std::ostringstream details;
  bool failed = false;

  for (const PreparedFramePassWork &work : preparedPasses) {
    if (work.inputs.empty()) {
      failed = true;
      details << "\n- pass=" << debugString(work.passName)
              << " produced no render inputs";
    }
    if (work.descs.empty()) {
      failed = true;
      details << "\n- pass=" << debugString(work.passName)
              << " produced no prepared render input descs";
    }
    const RenderInputValidationResult validation =
        validatePreparedRenderInputs(work.descs);
    if (validation.ok) {
      continue;
    }
    failed = true;
    for (const RenderInputDiagnostic &diagnostic :
         validation.diagnostics) {
      details << "\n- pass=" << debugString(diagnostic.pass)
              << " debugId=" << debugString(diagnostic.debugId)
              << " message=" << diagnostic.message;
    }
    for (const RenderInputDesc &desc : work.descs) {
      if (desc.accepted() || !desc.diagnostics.empty()) {
        continue;
      }
      details << "\n- rejected prepared offline render desc pass="
              << debugString(desc.pass)
              << " debugId=" << debugString(desc.debugId);
    }
  }

  if (failed) {
    throw std::runtime_error("offline prepared render input validation failed" +
                             details.str());
  }
}

[[nodiscard]] RenderPathGraphHandle loadOfflineRenderPathGraph(
    SceneResourceTable &scene, const ResourceUri &graphUri) {
  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  const LX_infra::ParsedSceneResource parsed = registry.parse(
      scene, SceneResourceType::RenderPathGraph, graphUri,
      LX_infra::SceneResourceParseContext{
          .ownerUri = ResourceUri("offline-render://frame-graph")});
  if (!parsed.identity.isValid() || parsed.metadata.state == ResourceState::Failed ||
      !parsed.diagnostics.empty()) {
    std::string message =
        "failed to load offline RenderPathGraph '" + graphUri.string() + "'";
    if (!parsed.diagnostics.empty()) {
      message += ":\n- " + joinDiagnostics(parsed.diagnostics);
    }
    throw std::runtime_error(message);
  }
  const auto handle = scene.findRenderPathGraphByMetadataHandle(parsed.identity);
  if (!handle.has_value()) {
    throw std::runtime_error("offline RenderPathGraph did not register payload for '" +
                             graphUri.string() + "'");
  }
  return *handle;
}

[[nodiscard]] IShaderSharedPtr shaderForPass(const SceneResourceTable &scene,
                                             const FramePass &pass) {
  const auto shaderHandle = scene.findShader(pass.shaderUri);
  if (!shaderHandle.has_value()) {
    throw std::runtime_error("offline pass '" + debugString(pass.name) +
                             "' shader '" + pass.shaderUri.string() +
                             "' was not registered");
  }
  const auto shader = scene.resolve(*shaderHandle);
  if (!shader.has_value() || !shader->get().payload) {
    if (!shader.has_value() ||
        !shader->get().requiresMaterialSourceVariant) {
      throw std::runtime_error("offline pass '" + debugString(pass.name) +
                               "' shader '" + pass.shaderUri.string() +
                               "' has no compiled payload");
    }
  }
  if (shader->get().requiresMaterialSourceVariant) {
    const auto &variants = shader->get().materialSourceVariants;
    const auto it = std::find_if(
        variants.begin(), variants.end(),
        [&](const ShaderResourceMetadata::MaterialSourceVariant &variant) {
          return variant.renderPathNodeSignature ==
                 pass.renderPathNodeSignature;
        });
    if (it == variants.end() || !it->shaderProgram.getShader()) {
      throw std::runtime_error(
          "offline pass '" + debugString(pass.name) + "' shader '" +
          pass.shaderUri.string() +
          "' has no material source variant for render path node");
    }
    return it->shaderProgram.getShader();
  }
  return shader->get().payload;
}

[[nodiscard]] RenderWorkBuildContext::Options
makeOfflineWorkOptions(const SceneResourceTable &scene,
                       const FrameGraph &frameGraph,
                       const LX_core::offline::OutputProfile &output,
                       const LX_core::offline::OfflineRenderSettings &offline) {
  RenderWorkBuildContext::Options options;
  const RenderSceneSnapshot snapshot = scene.buildSnapshot();
  for (const CameraHandle cameraHandle : snapshot.cameraHandles) {
    const auto camera = scene.resolve(cameraHandle);
    if (camera.has_value() && camera->get().active) {
      CameraResource outputCamera = camera->get();
      outputCamera.projection =
          LX_core::offline::resolveOutputCameraProjection(
              outputCamera.projection, output);
      outputCamera.proj = makeCameraProjectionMatrix(outputCamera.projection);
      options.cameraResource = outputCamera;
      break;
    }
  }
  options.outputBackgroundColor = output.backgroundColor;
  options.runtimeExtents.push_back(RenderWorkBuildContext::RuntimeExtent{
      .key = StringID("offline.output.resolution"),
      .extent = Vec3u{output.width, output.height, 1u},
  });
  options.featureValues.push_back(RenderFeatureVolatileValue{
      .key = StringID("feature.offlineRayTracer.samples"),
      .value = std::to_string(offline.samples),
  });
  options.featureValues.push_back(RenderFeatureVolatileValue{
      .key = StringID("feature.offlineRayTracer.maxBounce"),
      .value = std::to_string(offline.maxBounce),
  });
  options.featureValues.push_back(RenderFeatureVolatileValue{
      .key = StringID("feature.offlineRayTracer.seed"),
      .value = std::to_string(offline.seed),
  });
  options.featureValues.push_back(RenderFeatureVolatileValue{
      .key = StringID("feature.offlineRayTracer.compareMode"),
      .value = offline.compareMode,
  });

  for (const FramePass &pass : frameGraph.getPasses()) {
    const IShaderSharedPtr shader = shaderForPass(scene, pass);
    options.passPreparationFacts.push_back(
        RenderWorkBuildContext::PassPreparationFacts{
            .pass = pass.name,
            .pipelineVariantKey = StringID("offline:" + debugString(pass.name)),
            .shaderProgram = ShaderProgramSet{.shader = shader},
            .shaderInfo = shader,
            .renderState = pass.renderState,
        });
  }
  return options;
}

[[nodiscard]] std::vector<PreparedFramePassWork>
prepareOfflineFramePassWork(const Scene &scene, const FrameGraph &frameGraph,
                            const LX_core::offline::OutputProfile &output,
                            const LX_core::offline::OfflineRenderSettings &offline) {
  RenderWorkCompiler compiler;
  RenderWorkBuildContext::Options options =
      makeOfflineWorkOptions(scene.resources(), frameGraph, output, offline);
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::forScene(RenderDomain::Offline, scene,
                                       std::move(options));

  std::vector<PreparedFramePassWork> prepared;
  prepared.reserve(frameGraph.getPasses().size());
  for (const FramePass &pass : frameGraph.getPasses()) {
    PreparedFramePassWork work;
    work.passName = pass.name;
    compiler.buildInputs(pass, context, work.inputs);
    work.descs = compiler.prepare(pass, context, work.inputs);
    prepared.push_back(std::move(work));
  }
  validatePreparedOfflineRenderDescs(prepared);
  return prepared;
}

[[nodiscard]] std::vector<PipelineBuildDesc>
collectUniquePipelines(const std::vector<PreparedFramePassWork> &prepared) {
  std::unordered_set<PipelineKey, PipelineKey::Hash> seenPipelines;
  std::vector<PipelineBuildDesc> pipelineDescs;
  for (const PreparedFramePassWork &work : prepared) {
    for (const RenderInputDesc &desc : work.descs) {
      if (desc.accepted() &&
          seenPipelines.insert(desc.pipelineBuildDesc.key).second) {
        pipelineDescs.push_back(desc.pipelineBuildDesc);
      }
    }
  }
  return pipelineDescs;
}

[[nodiscard]] FrameGraphExecutionPayload takeOfflineOutputPayload(
    FrameGraphExecutionResult result) {
  if (!result.ok) {
    std::string message = "offline FrameGraphExecutor failed";
    if (!result.diagnostics.empty()) {
      message += ":\n- " + joinDiagnostics(result.diagnostics);
    }
    throw std::runtime_error(message);
  }
  for (FrameGraphExecutionPayload &payload : result.outputs) {
    if (payload.name == "offline.output" ||
        payload.target == "offline.output") {
      return std::move(payload);
    }
  }
  throw std::runtime_error("offline FrameGraphExecutor produced no offline.output payload");
}

float halfToFloat(u16 value) {
  const u16 sign = static_cast<u16>((value >> 15u) & 0x1u);
  const u16 exponent = static_cast<u16>((value >> 10u) & 0x1fu);
  const u16 mantissa = static_cast<u16>(value & 0x03ffu);
  const float signScale = sign == 0 ? 1.0f : -1.0f;
  if (exponent == 0) {
    if (mantissa == 0) {
      return signScale * 0.0f;
    }
    return signScale * std::ldexp(static_cast<float>(mantissa), -24);
  }
  if (exponent == 31) {
    return mantissa == 0 ? signScale * std::numeric_limits<float>::infinity()
                         : std::numeric_limits<float>::quiet_NaN();
  }
  return signScale * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                                static_cast<int>(exponent) - 15);
}

[[nodiscard]] LX_core::offline::OfflineReadbackImage
makeReadbackImage(const FrameGraphExecutionPayload &payload) {
  if (payload.format != "RGBA32Float" && payload.format != "RGBA16Float") {
    throw std::runtime_error(
        "offline output payload expected RGBA32Float or RGBA16Float");
  }
  if (payload.kind != RenderPathOutputKind::Image2D) {
    throw std::runtime_error("offline output payload expected image2d");
  }
  if (payload.extent.x == 0 || payload.extent.y == 0) {
    throw std::runtime_error("offline output payload has empty extent");
  }
  const usize pixelCount =
      static_cast<usize>(payload.extent.x) * static_cast<usize>(payload.extent.y);
  const usize expectedBytes =
      payload.format == "RGBA32Float" ? pixelCount * 4u * sizeof(float)
                                      : pixelCount * 4u * sizeof(u16);
  if (payload.bytes.size() != expectedBytes) {
    throw std::runtime_error("offline output payload expected " +
                             std::to_string(expectedBytes) + " bytes, got " +
                             std::to_string(payload.bytes.size()));
  }

  LX_core::offline::OfflineReadbackImage image;
  image.width = payload.extent.x;
  image.height = payload.extent.y;
  image.rgba.resize(pixelCount * 4u);
  if (payload.format == "RGBA32Float") {
    std::memcpy(image.rgba.data(), payload.bytes.data(), payload.bytes.size());
  } else {
    const auto *halfPixels =
        reinterpret_cast<const u16 *>(payload.bytes.data());
    for (usize i = 0; i < image.rgba.size(); ++i) {
      image.rgba[i] = halfToFloat(halfPixels[i]);
    }
  }
  for (const float value : image.rgba) {
    if (!std::isfinite(value)) {
      throw std::runtime_error("offline render readback contained non-finite values");
    }
  }
  return image;
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
    resourceManager = VulkanResourceManager::create(*device);
  }

  ~Impl() {
    if (device) {
      device->waitIdle();
    }
    resourceManager.reset();
    commandManager.reset();
    device.reset();
  }

  [[nodiscard]] VulkanOfflineRenderResult render(VulkanOfflineRenderRequest request) {
    LX_core::offline::validateOfflineRenderInputs(request.scene, request.output);

    const RenderPathGraphHandle graphHandle =
        loadOfflineRenderPathGraph(request.scene, request.renderPathGraphUri);
    Scene scene("OfflineRT", std::move(request.scene));
    scene.setRenderSettings(request.renderSettings);
    auto resolvedGraph = scene.resources().resolve(graphHandle);
    if (!resolvedGraph.has_value()) {
      throw std::runtime_error("offline RenderPathGraph payload disappeared");
    }

    const RenderPathGraph &renderPathGraph = resolvedGraph->get();
    const LX_infra::MaterialSourceVariantResolverResult variants =
        LX_infra::resolveMaterialSourceVariants(
            scene.resources(), renderPathGraph, request.renderPathGraphUri);
    if (!variants.success) {
      throw std::runtime_error("offline material source variant resolution failed:\n- " +
                               joinDiagnostics(variants.diagnostics));
    }
    FrameGraph frameGraph = buildFrameGraphFromRenderPathGraph(
        renderPathGraph, GraphResourceRegistry::makeDefault());
    const CompiledFrameGraph compiledGraph = frameGraph.compile();
    if (!compiledGraph.isValid()) {
      throw std::runtime_error(compiledGraph.errorText());
    }

    std::vector<PreparedFramePassWork> prepared =
        prepareOfflineFramePassWork(scene, frameGraph, request.output,
                                    request.offline);
    resourceManager->preloadPipelines(collectUniquePipelines(prepared));

    VulkanFrameGraphExecutor executor(VulkanFrameGraphExecutionTarget{
        .mode = VulkanFrameGraphExecutionMode::ImmediateSubmitReadback,
        .device = device.get(),
        .commandManager = commandManager.get(),
        .resourceManager = resourceManager.get(),
    });
    FrameGraphExecutionPayload payload = takeOfflineOutputPayload(
        executor.execute(FrameGraphExecutionRequest{
            .graph = &frameGraph,
            .compiled = &compiledGraph,
            .preparedPasses = prepared,
        }));
    LX_core::offline::OfflineReadbackImage image = makeReadbackImage(payload);
    return VulkanOfflineRenderResult{.payload = std::move(payload),
                                     .image = std::move(image)};
  }

  VulkanDeviceUniquePtr device;
  VulkanCommandBufferManagerUniquePtr commandManager;
  VulkanResourceManagerUniquePtr resourceManager;
};

VulkanOfflineRenderer::VulkanOfflineRenderer()
    : m_impl(std::make_unique<Impl>()) {}

VulkanOfflineRenderer::~VulkanOfflineRenderer() = default;

VulkanOfflineRenderResult
VulkanOfflineRenderer::render(VulkanOfflineRenderRequest request) {
  return m_impl->render(std::move(request));
}

} // namespace LX_core::backend::offline
