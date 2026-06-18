#include "core/frame_graph/render_work_compiler.hpp"

#include "core/asset/mesh.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/scene_descriptor_resource_resolver.hpp"
#include "core/offline/offline_render_job.hpp"
#include "core/offline/offline_scene_storage_resources.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace LX_core {
namespace {

void noteDiagnostic(RenderInputDesc &desc, RenderInputDiagnosticCode code,
                    std::string message);

struct DrawPreparationFacts final {
  StringID pipelineVariantKey;
  ShaderProgramSet shaderProgram;
  IShaderSharedPtr shaderInfo;
  RenderState renderState;
  VertexLayout vertexLayout;
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
  DescriptorResourceList descriptorResources;
};

struct ComputePreparationFacts final {
  StringID pipelineVariantKey;
  ShaderProgramSet shaderProgram;
  IShaderSharedPtr shaderInfo;
  DescriptorResourceList descriptorResources;
};

[[nodiscard]] StringID shaderUriId(const FramePass &pass) {
  return StringID(pass.shaderUri.string());
}

[[nodiscard]] StringID stablePassDebugId(const FramePass &pass) {
  return pass.name.id != 0 ? pass.name : StringID("<unnamed-pass>");
}

[[nodiscard]] StringID fallbackPipelineVariant(const FramePass &pass,
                                               const RenderInput &input) {
  if (const auto *draw = dynamic_cast<const RenderDrawInput *>(&input)) {
    const std::string source =
        draw->source == RenderDrawInputSource::FullscreenTriangle
            ? "fullscreen-triangle"
            : "scene-renderable";
    return StringID("render-input:" + source +
                    ":shader=" + pass.shaderUri.string());
  }
  if (dynamic_cast<const RenderComputeInput *>(&input) != nullptr) {
    return StringID("render-input:compute-dispatch:shader=" +
                    pass.shaderUri.string());
  }
  return StringID("render-input:unsupported:shader=" + pass.shaderUri.string());
}

[[nodiscard]] PipelineKey
makePipelineKey(const FramePass &pass, StringID pipelineVariantKey,
                const std::vector<ShaderSpecializationConstant>
                    &specializationConstants = {}) {
  return PipelineKey::build(pipelineVariantKey,
                            getFramePassRenderPathNodeSignature(pass),
                            specializationConstants);
}

[[nodiscard]] PipelineKey makePipelineKey(const FramePass &pass,
                                          const RenderInput &input) {
  return makePipelineKey(pass, fallbackPipelineVariant(pass, input));
}

[[nodiscard]] PrimitiveTopology pipelineTopologyFor(const FramePass &pass) {
  if (pass.input.geometry.has_value()) {
    return pass.input.geometry->topology;
  }
  return PrimitiveTopology::TriangleList;
}

[[nodiscard]] StringID shaderVariantKeyFor(const FramePass &pass,
                                           const DrawPreparationFacts &facts) {
  if (facts.shaderInfo) {
    return facts.shaderProgram.getPipelineSignature();
  }
  return shaderUriId(pass);
}

[[nodiscard]] StringID
shaderVariantKeyFor(const FramePass &pass,
                    const ComputePreparationFacts &facts) {
  if (facts.shaderInfo) {
    return facts.shaderProgram.getPipelineSignature();
  }
  return shaderUriId(pass);
}

[[nodiscard]] StringID
reflectionIdentityFor(const IShaderSharedPtr &shaderInfo) {
  if (!shaderInfo) {
    return {};
  }
  return StringID("shader-reflection:" + shaderInfo->getShaderName() + ":" +
                  std::to_string(shaderInfo->getProgramHash()));
}

void appendResourceDependency(std::vector<GpuResourceRef> &out,
                              const GpuResourceRef &resource) {
  if (resource.isValid()) {
    out.push_back(resource);
  }
}

void appendDescriptorResourceDependencies(std::vector<GpuResourceRef> &out,
                                          const DescriptorResourceList &list) {
  for (const DescriptorResourceRef &descriptor : list) {
    if (descriptor.isResource() && descriptor.resource().isValid()) {
      out.push_back(descriptor.resource());
    }
  }
}

void appendDescriptorResources(DescriptorResourceList &out,
                               const DescriptorResourceList &resources) {
  out.insert(out.end(), resources.begin(), resources.end());
}

[[nodiscard]] std::optional<VertexLayout>
vertexLayoutFromResource(const GpuResourceRef &resource) {
  if (!resource.isValid()) {
    return std::nullopt;
  }
  const auto *vertexBuffer =
      dynamic_cast<const IVertexBuffer *>(&resource.get());
  if (vertexBuffer == nullptr) {
    return std::nullopt;
  }
  return vertexBuffer->getLayout();
}

[[nodiscard]] std::optional<PrimitiveTopology>
topologyFromResource(const GpuResourceRef &resource) {
  if (!resource.isValid()) {
    return std::nullopt;
  }
  const auto *indexBuffer = dynamic_cast<const IndexBuffer *>(&resource.get());
  if (indexBuffer == nullptr) {
    return std::nullopt;
  }
  return indexBuffer->getTopology();
}

[[nodiscard]] std::optional<VertexLayout>
filterVertexLayoutToShaderInputs(const VertexLayout &layout,
                                 const IShader &shader) {
  const auto &shaderInputs = shader.getVertexInputs();
  if (shaderInputs.empty()) {
    return layout;
  }

  std::vector<VertexLayoutItem> filteredItems;
  filteredItems.reserve(shaderInputs.size());
  for (const auto &input : shaderInputs) {
    const auto it = std::find_if(
        layout.getItems().begin(), layout.getItems().end(),
        [&input](const VertexLayoutItem &item) {
          return item.location == input.location && item.type == input.type;
        });
    if (it == layout.getItems().end()) {
      return std::nullopt;
    }
    filteredItems.push_back(*it);
  }
  return VertexLayout(std::move(filteredItems), layout.getStride());
}

[[nodiscard]] PipelineBuildDesc makePipelineBuildDesc(
    const FramePass &pass, PipelineKey key, const DrawPreparationFacts &facts,
    std::vector<ShaderSpecializationConstant> specializationConstants) {
  std::vector<ShaderStageCode> stages;
  std::vector<ShaderResourceBinding> bindings;
  if (facts.shaderInfo) {
    stages = facts.shaderInfo->getAllStages();
    bindings = facts.shaderInfo->getReflectionBindings();
  }

  return PipelineBuildDesc::graphics(
      key, shaderVariantKeyFor(pass, facts), pass.target, std::move(stages),
      std::move(bindings), facts.vertexLayout, facts.renderState,
      facts.topology, pass.renderingMode, pass.attachments,
      std::move(specializationConstants));
}

[[nodiscard]] PipelineBuildDesc makePipelineBuildDesc(
    const FramePass &pass, PipelineKey key,
    const ComputePreparationFacts &facts,
    std::vector<ShaderSpecializationConstant> specializationConstants) {
  std::vector<ShaderStageCode> stages;
  std::vector<ShaderResourceBinding> bindings;
  if (facts.shaderInfo) {
    stages = facts.shaderInfo->getAllStages();
    bindings = facts.shaderInfo->getReflectionBindings();
  }
  return PipelineBuildDesc::compute(key, shaderVariantKeyFor(pass, facts),
                                    std::move(stages), std::move(bindings),
                                    std::move(specializationConstants));
}

[[nodiscard]] std::optional<std::string>
featureNameFromGraphResource(StringID resource) {
  if (resource.id == 0) {
    return std::nullopt;
  }
  const std::string resourceName =
      GlobalStringTable::get().toDebugString(resource);
  constexpr std::string_view kPrefix = "feature.";
  if (resourceName.rfind(kPrefix, 0) != 0) {
    return std::nullopt;
  }
  return resourceName.substr(kPrefix.size());
}

[[nodiscard]] std::vector<ShaderSpecializationConstant>
collectPassFeatureSpecializationConstants(
    const FramePass &pass, const RenderWorkBuildContext &context) {
  if (!context.hasRealtimeScene()) {
    return {};
  }

  std::vector<ShaderSpecializationConstant> constants;
  std::vector<std::string> resolvedFeatures;
  const SceneResourceTable &resources = context.realtimeScene().resources();
  for (const FrameGraphRead &read : pass.reads) {
    const auto featureName = featureNameFromGraphResource(read.resource);
    if (!featureName.has_value()) {
      continue;
    }
    if (std::find(resolvedFeatures.begin(), resolvedFeatures.end(),
                  *featureName) != resolvedFeatures.end()) {
      continue;
    }
    const PassFeatureData *data =
        resources.findPassFeatureDataByFeatureName(*featureName);
    if (data == nullptr) {
      continue;
    }
    if (data->shaderUri != pass.shaderUri) {
      continue;
    }
    resolvedFeatures.emplace_back(*featureName);
    for (const PassFeatureSpecializationValue &value :
         data->specializationValues) {
      constants.push_back(ShaderSpecializationConstant{
          .constantId = value.constantId,
          .stage = value.stage,
          .type = value.type,
          .valueU32 = value.valueU32,
      });
    }
  }
  return constants;
}

[[nodiscard]] DescriptorResourceList
collectSceneLevelResourcesForPass(const RenderWorkBuildContext &context,
                                  const FramePass &pass) {
  if (!context.hasRealtimeScene()) {
    return {};
  }

  const Scene &scene = context.realtimeScene();
  const auto &options = context.realtimeOptions();
  if (options.cameraResource.has_value()) {
    return scene.getSceneLevelResources(pass.name, *options.cameraResource);
  }

  const RenderTarget sceneResourceTarget =
      options.sceneResourceTarget.value_or(RenderTarget(pass.target));
  return scene.getSceneLevelResources(pass.name, sceneResourceTarget);
}

void applyPassPreparationFacts(
    DrawPreparationFacts &facts,
    const RenderWorkBuildContext::PassPreparationFacts &passFacts) {
  if (passFacts.pipelineVariantKey.id != 0) {
    facts.pipelineVariantKey = passFacts.pipelineVariantKey;
  }
  facts.shaderProgram = passFacts.shaderProgram;
  facts.shaderInfo = passFacts.shaderInfo ? passFacts.shaderInfo
                                          : passFacts.shaderProgram.shader;
  facts.renderState = passFacts.renderState;
  appendDescriptorResources(facts.descriptorResources,
                            passFacts.descriptorResources);
}

void applyPassPreparationFacts(
    ComputePreparationFacts &facts,
    const RenderWorkBuildContext::PassPreparationFacts &passFacts) {
  if (passFacts.pipelineVariantKey.id != 0) {
    facts.pipelineVariantKey = passFacts.pipelineVariantKey;
  }
  facts.shaderProgram = passFacts.shaderProgram;
  facts.shaderInfo = passFacts.shaderInfo ? passFacts.shaderInfo
                                          : passFacts.shaderProgram.shader;
  appendDescriptorResources(facts.descriptorResources,
                            passFacts.descriptorResources);
}

[[nodiscard]] DrawPreparationFacts collectDrawPreparationFacts(
    const FramePass &pass, const RenderWorkBuildContext &context,
    const RenderDrawInput &draw, RenderInputDesc &desc) {
  DrawPreparationFacts facts;
  facts.pipelineVariantKey = fallbackPipelineVariant(pass, draw);
  facts.renderState = pass.renderState;
  facts.topology = pipelineTopologyFor(pass);

  if (const auto layout = vertexLayoutFromResource(draw.vertexBuffer)) {
    facts.vertexLayout = *layout;
  }
  if (const auto topology = topologyFromResource(draw.indexBuffer)) {
    facts.topology = *topology;
  }

  const auto passFacts = context.findPassPreparationFacts(pass.name);
  if (draw.source == RenderDrawInputSource::FullscreenTriangle) {
    if (passFacts.has_value()) {
      applyPassPreparationFacts(facts, passFacts->get());
    }
    appendDescriptorResources(facts.descriptorResources,
                              collectSceneLevelResourcesForPass(context, pass));
    return facts;
  }

  if (draw.source != RenderDrawInputSource::SceneRenderable ||
      !context.hasRealtimeScene()) {
    return facts;
  }

  const auto &renderables = context.realtimeScene().getRenderables();
  if (draw.sourceRenderableIndex >= renderables.size() ||
      !renderables[draw.sourceRenderableIndex]) {
    return facts;
  }

  const auto validated =
      renderables[draw.sourceRenderableIndex]->getValidatedPassData(pass.name);
  if (!validated.has_value()) {
    return facts;
  }

  const ValidatedRenderablePassData &data = validated->get();
  facts.pipelineVariantKey = data.materialTypeVariant.id != 0
                                 ? data.materialTypeVariant
                                 : data.shaderProgram.getPipelineSignature();
  facts.shaderProgram = data.shaderProgram;
  facts.shaderInfo = data.shaderInfo;
  facts.renderState = data.renderState;

  const GpuResourceRef vertexResource =
      data.vertexBuffer.isValid() ? data.vertexBuffer : draw.vertexBuffer;
  if (const auto layout = vertexLayoutFromResource(vertexResource)) {
    if (facts.shaderInfo) {
      const auto filtered =
          filterVertexLayoutToShaderInputs(*layout, *facts.shaderInfo);
      if (filtered.has_value()) {
        facts.vertexLayout = *filtered;
      } else {
        noteDiagnostic(desc, RenderInputDiagnosticCode::MissingPipelineFacts,
                       "draw input vertex layout is missing a shader input");
      }
    } else {
      facts.vertexLayout = *layout;
    }
  }

  const GpuResourceRef indexResource =
      data.indexBuffer.isValid() ? data.indexBuffer : draw.indexBuffer;
  if (const auto topology = topologyFromResource(indexResource)) {
    facts.topology = *topology;
  } else if (pass.input.geometry.has_value()) {
    facts.topology = pass.input.geometry->topology;
  }

  DescriptorResourceList sceneDescriptorResources =
      buildSceneDescriptorResources(SceneDescriptorResourceContext{
          .scene = context.realtimeScene(),
          .renderable = data,
          .pass = pass.name,
          .target = RenderTarget(pass.target),
          .sceneResources = collectSceneLevelResourcesForPass(context, pass),
      });
  if (passFacts.has_value()) {
    appendDescriptorResources(facts.descriptorResources,
                              passFacts->get().descriptorResources);
  }
  appendDescriptorResources(facts.descriptorResources,
                            sceneDescriptorResources);

  return facts;
}

[[nodiscard]] ComputePreparationFacts
collectComputePreparationFacts(const FramePass &pass,
                               const RenderWorkBuildContext &context,
                               const RenderComputeInput &compute) {
  ComputePreparationFacts facts;
  facts.pipelineVariantKey = fallbackPipelineVariant(pass, compute);
  if (const auto passFacts = context.findPassPreparationFacts(pass.name)) {
    applyPassPreparationFacts(facts, passFacts->get());
  }
  if (context.domain() == RenderDomain::Offline) {
    offline::OfflineRenderJob &job = context.offlineJob();
    if (job.offlineShader) {
      facts.shaderInfo = job.offlineShader;
      facts.shaderProgram.shaderName = job.offlineShader->getShaderName();
      facts.shaderProgram.shader = job.offlineShader;
      facts.pipelineVariantKey =
          StringID("offline-primary-ray:" +
                   std::to_string(job.offlineShader->getProgramHash()));
    }
    offline::OfflineSceneStorageResources storageResources =
        offline::buildOfflineSceneStorageResources(job);
    appendDescriptorResources(facts.descriptorResources,
                              storageResources.descriptorResources);
  }
  return facts;
}

void fillPreparedFacts(const FramePass &pass,
                       const RenderWorkBuildContext &context,
                       const RenderDrawInput &draw, RenderInputDesc &desc) {
  const DrawPreparationFacts facts =
      collectDrawPreparationFacts(pass, context, draw, desc);
  const std::vector<ShaderSpecializationConstant> specializationConstants =
      collectPassFeatureSpecializationConstants(pass, context);
  desc.pipelineKey =
      makePipelineKey(pass, facts.pipelineVariantKey, specializationConstants);
  desc.pipelineBuildDesc = makePipelineBuildDesc(pass, desc.pipelineKey, facts,
                                                 specializationConstants);
  desc.shaderVariantKey = desc.pipelineBuildDesc.shaderVariantKey;
  desc.reflectionIdentity = reflectionIdentityFor(facts.shaderInfo);
  desc.bindingPlan.descriptors = facts.descriptorResources;
  appendResourceDependency(desc.resourceDependencies, draw.vertexBuffer);
  appendResourceDependency(desc.resourceDependencies, draw.indexBuffer);
  appendDescriptorResourceDependencies(desc.resourceDependencies,
                                       desc.bindingPlan.descriptors);
  if (!facts.shaderInfo) {
    noteDiagnostic(desc, RenderInputDiagnosticCode::MissingShaderReflection,
                   "draw input has no shader reflection facts");
  }
  if (desc.pipelineBuildDesc.stages.empty()) {
    noteDiagnostic(desc, RenderInputDiagnosticCode::MissingPipelineFacts,
                   "draw input has no shader stage pipeline facts");
  }
}

void fillPreparedFacts(const FramePass &pass,
                       const RenderWorkBuildContext &context,
                       const RenderComputeInput &compute,
                       RenderInputDesc &desc) {
  (void)compute;
  const ComputePreparationFacts facts =
      collectComputePreparationFacts(pass, context, compute);
  const std::vector<ShaderSpecializationConstant> specializationConstants =
      collectPassFeatureSpecializationConstants(pass, context);
  desc.pipelineKey =
      makePipelineKey(pass, facts.pipelineVariantKey, specializationConstants);
  desc.pipelineBuildDesc = makePipelineBuildDesc(pass, desc.pipelineKey, facts,
                                                 specializationConstants);
  desc.shaderVariantKey = desc.pipelineBuildDesc.shaderVariantKey;
  desc.reflectionIdentity = reflectionIdentityFor(facts.shaderInfo);
  desc.bindingPlan.descriptors = facts.descriptorResources;
  appendDescriptorResourceDependencies(desc.resourceDependencies,
                                       desc.bindingPlan.descriptors);
  if (!facts.shaderInfo) {
    noteDiagnostic(desc, RenderInputDiagnosticCode::MissingShaderReflection,
                   "compute input has no shader reflection facts");
  }
  if (desc.pipelineBuildDesc.stages.empty()) {
    noteDiagnostic(desc, RenderInputDiagnosticCode::MissingPipelineFacts,
                   "compute input has no shader stage pipeline facts");
  }
}

[[nodiscard]] RenderInputDiagnostic
makeDiagnostic(const RenderInputDiagnosticCode code, const StringID pass,
               const StringID debugId, std::string message) {
  return RenderInputDiagnostic{
      .code = code,
      .pass = pass,
      .debugId = debugId,
      .message = std::move(message),
  };
}

void reject(RenderInputDesc &desc, RenderInputDiagnosticCode code,
            std::string message) {
  desc.status = RenderInputStatus::Rejected;
  desc.diagnostics.push_back(
      makeDiagnostic(code, desc.pass, desc.debugId, std::move(message)));
}

void noteDiagnostic(RenderInputDesc &desc, RenderInputDiagnosticCode code,
                    std::string message) {
  desc.diagnostics.push_back(
      makeDiagnostic(code, desc.pass, desc.debugId, std::move(message)));
}

[[nodiscard]] std::optional<StringID>
descriptorBindingName(const DescriptorResourceRef &descriptor) {
  if (descriptor.isTextureArray()) {
    return descriptor.getBindingName();
  }
  if (!descriptor.resource().isValid()) {
    return std::nullopt;
  }
  return descriptor.resource().getBindingName();
}

[[nodiscard]] bool
isValidTextureArrayDescriptor(const DescriptorResourceRef &descriptor) {
  if (!descriptor.isTextureArray() || descriptor.getBindingName().id == 0 ||
      descriptor.textures().empty()) {
    return false;
  }
  return std::all_of(
      descriptor.textures().begin(), descriptor.textures().end(),
      [](const TextureSamplerRef &texture) { return texture.isValid(); });
}

[[nodiscard]] bool
descriptorMatchesBindingType(const DescriptorResourceRef &descriptor,
                             const ShaderResourceBinding &binding) {
  switch (binding.type) {
  case ShaderPropertyType::UniformBuffer:
    return descriptor.isResource() && descriptor.resource().isValid() &&
           descriptor.resource().getType() == ResourceType::UniformBuffer;
  case ShaderPropertyType::StorageBuffer:
    return descriptor.isResource() && descriptor.resource().isValid() &&
           descriptor.resource().getType() == ResourceType::StorageBuffer;
  case ShaderPropertyType::Texture2D:
  case ShaderPropertyType::TextureCube:
    if (isValidTextureArrayDescriptor(descriptor)) {
      return true;
    }
    if (!descriptor.isResource() || !descriptor.resource().isValid()) {
      return false;
    }
    if (descriptor.resource().getType() == ResourceType::CombinedImageSampler) {
      return true;
    }
    return dynamic_cast<const FrameGraphSampledResource *>(
               &descriptor.resource().get()) != nullptr;
  default:
    return true;
  }
}

[[nodiscard]] std::string resourceNameText(StringID id) {
  if (id.id == 0) {
    return {};
  }
  return GlobalStringTable::get().toDebugString(id);
}

[[nodiscard]] bool isBakeGraphSource(std::string_view resourceName) {
  return resourceName.rfind("bake.", 0) == 0;
}

[[nodiscard]] bool
isFrameGraphPlaceholderResource(const DescriptorResourceRef &descriptor) {
  if (!descriptor.isResource() || !descriptor.resource().isValid()) {
    return false;
  }
  return dynamic_cast<const FrameGraphSampledResource *>(
             &descriptor.resource().get()) != nullptr;
}

[[nodiscard]] const DescriptorResourceRef *
findDescriptorForBinding(const DescriptorResourceList &descriptors,
                         StringID bindingName) {
  const auto it = std::find_if(
      descriptors.begin(), descriptors.end(),
      [bindingName](const DescriptorResourceRef &descriptor) {
        const auto descriptorName = descriptorBindingName(descriptor);
        return descriptorName.has_value() && *descriptorName == bindingName;
      });
  return it == descriptors.end() ? nullptr : &*it;
}

void validateBakeSourcePayloads(const FramePass &pass, RenderInputDesc &desc) {
  for (const FrameGraphRead &read : pass.reads) {
    const std::string sourceName = resourceNameText(read.resource);
    if (!isBakeGraphSource(sourceName)) {
      continue;
    }
    if (read.bindingName.id == 0) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "bake source '" + sourceName +
                 "' requires a named typed payload binding");
      continue;
    }
    const DescriptorResourceRef *descriptor = findDescriptorForBinding(
        desc.bindingPlan.descriptors, read.bindingName);
    if (descriptor == nullptr || !descriptor->isResource() ||
        !descriptor->resource().isValid() ||
        isFrameGraphPlaceholderResource(*descriptor)) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "bake source '" + sourceName +
                 "' requires a live typed payload resource");
    }
  }
}

void validateBakeOutputPayloads(const FramePass &pass, const RenderInput &input,
                                RenderInputDesc &desc) {
  if (pass.payloads.empty()) {
    return;
  }
  if (pass.input.kind != RenderPassInputKind::ComputeDispatch) {
    return;
  }
  const auto *compute = dynamic_cast<const RenderComputeInput *>(&input);
  if (compute == nullptr || !compute->readbackResource.has_value() ||
      compute->readbackResource->id == 0) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "bake compute payload requires a typed readback payload");
  }
}

void validateBindingPlanCompleteness(RenderInputDesc &desc) {
  for (const ShaderResourceBinding &binding : desc.pipelineBuildDesc.bindings) {
    const StringID bindingName(binding.name);
    const auto descriptorIt = std::find_if(
        desc.bindingPlan.descriptors.begin(),
        desc.bindingPlan.descriptors.end(),
        [bindingName](const DescriptorResourceRef &descriptor) {
          const auto descriptorName = descriptorBindingName(descriptor);
          return descriptorName.has_value() && *descriptorName == bindingName;
        });

    if (descriptorIt == desc.bindingPlan.descriptors.end()) {
      reject(desc, RenderInputDiagnosticCode::MissingBinding,
             "reflected shader binding '" + binding.name +
                 "' has no prepared descriptor resource");
      continue;
    }

    if (!descriptorMatchesBindingType(*descriptorIt, binding)) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "prepared descriptor resource for reflected shader binding '" +
                 binding.name + "' is missing or has incompatible type");
    }
  }
}

[[nodiscard]] bool passUsesFeature(const FramePass &pass,
                                   std::string_view featureSource) {
  return std::any_of(
      pass.reads.begin(), pass.reads.end(), [&](const FrameGraphRead &read) {
        return GlobalStringTable::get().toDebugString(read.resource) ==
               featureSource;
      });
}

[[nodiscard]] bool passUsesSource(const FramePass &pass,
                                  std::string_view sourceName) {
  return std::any_of(
      pass.reads.begin(), pass.reads.end(), [&](const FrameGraphRead &read) {
        return GlobalStringTable::get().toDebugString(read.resource) ==
               sourceName;
      });
}

[[nodiscard]] const ShaderResourceBinding *
findReflectedBinding(const std::vector<ShaderResourceBinding> &bindings,
                     std::string_view name) {
  const auto it = std::find_if(bindings.begin(), bindings.end(),
                               [&](const ShaderResourceBinding &binding) {
                                 return binding.name == name;
                               });
  return it == bindings.end() ? nullptr : &*it;
}

[[nodiscard]] const RenderFeatureParameter *
findParameterForBindingMember(const RenderFeature &feature,
                              std::string_view bindingName,
                              std::string_view memberName) {
  for (const auto &[_, parameter] : feature.parameters) {
    if (parameter.binding == bindingName && parameter.member == memberName) {
      return &parameter;
    }
  }
  return nullptr;
}

[[nodiscard]] bool environmentParameterKindMatches(std::string_view memberName,
                                                   std::string_view kind) {
  if (memberName == "color") {
    return kind == "vec3";
  }
  if (memberName == "intensity" || memberName == "rotation") {
    return kind == "float";
  }
  return true;
}

void validateEnvironmentLightingFeatureBindings(
    const FramePass &pass, const RenderWorkBuildContext &context,
    RenderInputDesc &desc) {
  if (!passUsesFeature(pass, "feature.environmentLighting")) {
    return;
  }
  if (!context.hasRealtimeScene()) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.environmentLighting requires a realtime scene resource "
           "table");
    return;
  }

  const SceneResourceTable &resources = context.realtimeScene().resources();
  const auto environmentState = resources.environmentRuntimeState();
  if (!environmentState.has_value() || !environmentState->nodePresent) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.environmentLighting requires a scene environment node");
    return;
  }

  const auto resolvedFeature = resources.resolve(environmentState->feature);
  if (!resolvedFeature.has_value()) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.environmentLighting RenderFeature payload is unresolved");
    return;
  }
  const RenderFeature &feature = resolvedFeature->get();
  if (feature.feature != "environmentLighting") {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "scene environment node RenderFeature payload is not "
           "environmentLighting");
    return;
  }

  const ShaderResourceBinding *skyboxMap =
      findReflectedBinding(desc.pipelineBuildDesc.bindings, "SkyboxMap");
  if (skyboxMap != nullptr) {
    const auto environmentMap = feature.parameters.find("environmentMap");
    if (environmentMap == feature.parameters.end() ||
        environmentMap->second.kind != "textureCube" ||
        environmentMap->second.binding != "SkyboxMap" ||
        environmentMap->second.uri.empty()) {
      reject(desc, RenderInputDiagnosticCode::MissingBinding,
             "feature.environmentLighting parameter environmentMap does not "
             "satisfy reflected binding SkyboxMap");
    }
  }

  const ShaderResourceBinding *ubo = findReflectedBinding(
      desc.pipelineBuildDesc.bindings, "EnvironmentLightingUBO");
  if (ubo == nullptr) {
    return;
  }
  for (const StructMemberInfo &member : ubo->members) {
    const RenderFeatureParameter *parameter = findParameterForBindingMember(
        feature, "EnvironmentLightingUBO", member.name);
    if (parameter == nullptr) {
      reject(desc, RenderInputDiagnosticCode::MissingBinding,
             "feature.environmentLighting is missing EnvironmentLightingUBO." +
                 member.name);
      continue;
    }
    if (!environmentParameterKindMatches(member.name, parameter->kind)) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "feature.environmentLighting parameter for "
             "EnvironmentLightingUBO." +
                 member.name + " has incompatible kind " + parameter->kind);
    }
  }
}

void validateSurfaceLightingFeatureRead(const FramePass &pass,
                                        RenderInputDesc &desc) {
  if (findReflectedBinding(desc.pipelineBuildDesc.bindings,
                           "SurfaceLightingUBO") == nullptr) {
    return;
  }
  if (passUsesFeature(pass, "feature.surfaceLighting")) {
    return;
  }
  reject(desc, RenderInputDiagnosticCode::MissingResource,
         "reflected SurfaceLightingUBO requires feature.surfaceLighting read");
}

[[nodiscard]] bool featureBoolValue(const RenderFeatureParameter &parameter) {
  return parameter.value == "true" || parameter.value == "1";
}

[[nodiscard]] bool surfaceLightingIblEnabled(
    const RenderWorkBuildContext &context) {
  if (!context.hasRealtimeScene()) {
    return false;
  }
  const SceneResourceTable &resources = context.realtimeScene().resources();
  const auto featureHandle =
      resources.findRenderFeatureByFeatureName("surfaceLighting");
  if (!featureHandle.has_value()) {
    return false;
  }
  const auto feature = resources.resolve(*featureHandle);
  if (!feature.has_value()) {
    return false;
  }
  const auto enableIblLighting =
      feature->get().parameters.find("enableIblLighting");
  if (enableIblLighting == feature->get().parameters.end()) {
    return false;
  }
  return featureBoolValue(enableIblLighting->second);
}

void validateSurfaceLightingIblBakeSources(
    const FramePass &pass, const RenderWorkBuildContext &context,
    RenderInputDesc &desc) {
  if (findReflectedBinding(desc.pipelineBuildDesc.bindings,
                           "SurfaceLightingUBO") == nullptr) {
    return;
  }
  if (!passUsesFeature(pass, "feature.surfaceLighting")) {
    return;
  }
  if (!surfaceLightingIblEnabled(context)) {
    return;
  }
  if (!passUsesSource(pass, "scene.environmentBake")) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.surfaceLighting enableIblLighting=true requires "
           "scene.environmentBake source");
  }
  if (!passUsesSource(pass, "scene.materialIblBake")) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.surfaceLighting enableIblLighting=true requires "
           "scene.materialIblBake source for standard-pbr");
  }
}

[[nodiscard]] bool hasDiagnosticCode(const RenderInputDesc &desc,
                                     RenderInputDiagnosticCode code) {
  return std::any_of(desc.diagnostics.begin(), desc.diagnostics.end(),
                     [code](const RenderInputDiagnostic &diagnostic) {
                       return diagnostic.code == code;
                     });
}

[[nodiscard]] bool hasFatalPipelineDiagnostic(const RenderInputDesc &desc) {
  return hasDiagnosticCode(
             desc, RenderInputDiagnosticCode::MissingShaderReflection) ||
         hasDiagnosticCode(desc,
                           RenderInputDiagnosticCode::MissingPipelineFacts);
}

void rejectFatalPipelineFacts(RenderInputDesc &desc) {
  if (desc.accepted() && hasFatalPipelineDiagnostic(desc)) {
    desc.status = RenderInputStatus::Rejected;
  }
}

[[nodiscard]] bool hasSubmittableDrawCommand(const RenderDrawInput &draw) {
  return std::any_of(draw.drawCommands.begin(), draw.drawCommands.end(),
                     [](const RenderDrawCommand &command) {
                       return command.indexCount > 0 &&
                              command.instanceCount > 0;
                     });
}

[[nodiscard]] usize countSubmittableDrawCommands(const RenderDrawInput &draw) {
  return static_cast<usize>(std::count_if(
      draw.drawCommands.begin(), draw.drawCommands.end(),
      [](const RenderDrawCommand &command) {
        return command.indexCount > 0 && command.instanceCount > 0;
      }));
}

[[nodiscard]] bool materialTypeMatches(StringID materialTypeSignature,
                                       std::string_view requestedType) {
  if (materialTypeSignature.id == 0) {
    return false;
  }
  const std::string signature =
      GlobalStringTable::get().toDebugString(materialTypeSignature);
  return signature == requestedType ||
         signature.rfind(std::string(requestedType) + "-", 0) == 0;
}

[[nodiscard]] bool materialTypeAllowed(const RenderDrawInput &draw,
                                       const FramePass &pass) {
  if (pass.input.material.types.empty()) {
    return true;
  }
  if (draw.materialTypeSignature.id == 0) {
    return false;
  }
  return std::any_of(
      pass.input.material.types.begin(), pass.input.material.types.end(),
      [&draw](const std::string &type) {
        return materialTypeMatches(draw.materialTypeSignature, type);
      });
}

[[nodiscard]] std::string stringIdText(StringID id) {
  if (id.id == 0) {
    return {};
  }
  return GlobalStringTable::get().toDebugString(id);
}

[[nodiscard]] bool isDebugObjectClass(std::string_view objectClass) {
  return objectClass == "debug" || objectClass == "debug-only" ||
         objectClass == "debug.mesh";
}

[[nodiscard]] bool objectClassAllowed(const RenderDrawInput &draw,
                                      const FramePass &pass) {
  const std::vector<std::string> &classes = pass.input.object.renderClasses;
  if (classes.empty()) {
    return true;
  }

  const std::string renderType = stringIdText(draw.objectRenderType);
  return std::any_of(classes.begin(), classes.end(),
                     [&draw, &renderType](const std::string &renderClass) {
                       if (isDebugObjectClass(renderClass)) {
                         return draw.debugOnly ||
                                isDebugObjectClass(renderType);
                       }
                       if (!renderType.empty()) {
                         return renderType == renderClass;
                       }
                       return false;
                     });
}

[[nodiscard]] VisibilityLayerMask
resolveVisibleMask(const Scene &scene, const FramePass &pass,
                   const RenderWorkBuildContext::RealtimeOptions &options) {
  VisibilityLayerMask visibleMask = 0;
  if (options.cameraResource.has_value()) {
    visibleMask = options.cameraResource->cullingMask;
  } else {
    const RenderTarget sceneResourceTarget =
        options.sceneResourceTarget.value_or(RenderTarget(pass.target));
    visibleMask = scene.getCombinedCameraCullingMask(sceneResourceTarget);
  }
  if (options.visibleMask.has_value()) {
    visibleMask = *options.visibleMask;
  }
  return visibleMask;
}

[[nodiscard]] MeshHandle resolveObjectMesh(const Scene &scene,
                                           ObjectHandle object) {
  if (!object.isValid()) {
    return {};
  }
  const auto objectResource = scene.resources().resolve(object);
  if (!objectResource.has_value()) {
    return {};
  }
  return objectResource->get().mesh;
}

void fillSceneDrawCommand(const Scene &scene,
                          const ValidatedRenderablePassData &validatedData,
                          RenderDrawInput &draw) {
  if (draw.indexBuffer.isValid()) {
    const auto *indexBuffer =
        dynamic_cast<const IndexBuffer *>(&draw.indexBuffer.get());
    if (indexBuffer != nullptr && indexBuffer->indexCount() > 0) {
      draw.drawCommands.push_back(RenderDrawCommand{
          .indexCount = static_cast<u32>(indexBuffer->indexCount()),
          .instanceCount = 1,
          .firstInstance = 0,
      });
      return;
    }
  }

  if (draw.mesh.isValid()) {
    const auto mesh = scene.resources().resolve(draw.mesh);
    if (mesh.has_value() && mesh->get().getIndexCount() > 0) {
      draw.drawCommands.push_back(RenderDrawCommand{
          .indexCount = mesh->get().getIndexCount(),
          .instanceCount = 1,
          .firstIndex = mesh->get().getIndexOffset(),
          .firstInstance = 0,
      });
      return;
    }
  }
}

void fillRenderType(const IRenderable &renderable, RenderDrawInput &draw) {
  draw.debugOnly = renderable.isDebugOnlyRenderable();
  if (const auto renderType = renderable.getRenderType()) {
    draw.objectRenderType = *renderType;
  } else if (draw.debugOnly) {
    draw.objectRenderType = StringID("debug.mesh");
  }
}

void fillRawRenderableResources(const IRenderable &renderable,
                                RenderDrawInput &draw) {
  draw.vertexBuffer = renderable.getVertexBuffer();
  draw.indexBuffer = renderable.getIndexBuffer();
  if (!draw.drawCommands.empty() || !draw.indexBuffer.isValid()) {
    return;
  }
  const auto *indexBuffer =
      dynamic_cast<const IndexBuffer *>(&draw.indexBuffer.get());
  if (indexBuffer != nullptr && indexBuffer->indexCount() > 0) {
    draw.drawCommands.push_back(RenderDrawCommand{
        .indexCount = static_cast<u32>(indexBuffer->indexCount()),
        .instanceCount = 1,
        .firstInstance = 0,
    });
  }
}

void buildFullscreenInput(const FramePass &pass,
                          std::vector<std::unique_ptr<RenderInput>> &out) {
  auto draw = std::make_unique<RenderDrawInput>();
  draw->source = RenderDrawInputSource::FullscreenTriangle;
  draw->pass = pass.name;
  draw->debugId = stablePassDebugId(pass);
  draw->inputIndex = out.size();
  draw->drawCommands.push_back(
      RenderDrawCommand{.indexCount = 3, .instanceCount = 1});
  out.push_back(std::move(draw));
}

void buildSceneRenderableInputs(
    const FramePass &pass, const RenderWorkBuildContext &context,
    std::vector<std::unique_ptr<RenderInput>> &out) {
  if (context.domain() != RenderDomain::Realtime ||
      !context.hasRealtimeScene()) {
    throw std::logic_error(
        "RenderWorkCompiler scene-renderables input requires a realtime scene");
  }

  const Scene &scene = context.realtimeScene();
  const VisibilityLayerMask visibleMask =
      resolveVisibleMask(scene, pass, context.realtimeOptions());

  const usize firstPassInput = out.size();
  const auto &renderables = scene.getRenderables();
  for (usize renderableIndex = 0; renderableIndex < renderables.size();
       ++renderableIndex) {
    const auto &renderable = renderables[renderableIndex];
    if (!renderable) {
      continue;
    }
    if ((renderable->getVisibilityLayerMask() & visibleMask) == 0) {
      continue;
    }

    auto draw = std::make_unique<RenderDrawInput>();
    draw->source = RenderDrawInputSource::SceneRenderable;
    draw->sourceRenderableIndex = static_cast<u32>(renderableIndex);
    draw->pass = pass.name;
    draw->debugId = renderable->getDebugId().id != 0
                        ? renderable->getDebugId()
                        : StringID(renderable->getNodeName());
    draw->inputIndex = out.size();
    draw->objectDataSignature = StringID("BindlessObjectData.v1");
    fillRenderType(*renderable, *draw);
    fillRawRenderableResources(*renderable, *draw);

    const auto validated = renderable->getValidatedPassData(pass.name);
    if (validated.has_value()) {
      const ValidatedRenderablePassData &data = validated->get();
      draw->object = data.objectHandle;
      draw->mesh = resolveObjectMesh(scene, data.objectHandle);
      draw->material = data.materialHandle;
      draw->vertexBuffer = data.vertexBuffer;
      draw->indexBuffer = data.indexBuffer;
      draw->primitiveIndex = 0;
      draw->sortCenter = data.sortCenter;
      draw->materialTypeSignature = data.materialTypeSignature;
      draw->drawCommands.clear();
      fillSceneDrawCommand(scene, data, *draw);
    }

    out.push_back(std::move(draw));
  }

  for (usize inputIndex = firstPassInput; inputIndex < out.size();
       ++inputIndex) {
    out[inputIndex]->inputIndex = inputIndex;
  }
}

void validateFullscreenDesc(const FramePass &pass, const RenderDrawInput &draw,
                            RenderInputDesc &desc) {
  if (pass.stage != RenderPassStage::Raster ||
      pass.dispatch != RenderPassDispatch::Fullscreen) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "fullscreen-triangle input requires raster fullscreen pass");
    return;
  }
  if (draw.source != RenderDrawInputSource::FullscreenTriangle) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "fullscreen pass received non-fullscreen draw input");
    return;
  }
  if (!hasSubmittableDrawCommand(draw)) {
    reject(desc, RenderInputDiagnosticCode::ZeroDrawCount,
           "fullscreen-triangle input has no drawable command");
    return;
  }
  desc.status = RenderInputStatus::Accepted;
}

[[nodiscard]] bool
geometryContractMatches(const FramePass &pass,
                        const RenderWorkBuildContext &context,
                        const RenderDrawInput &draw) {
  if (!pass.input.geometry.has_value()) {
    return true;
  }
  if (draw.indexBuffer.isValid()) {
    const auto *indexBuffer =
        dynamic_cast<const IndexBuffer *>(&draw.indexBuffer.get());
    if (indexBuffer == nullptr) {
      return true;
    }
    return indexBuffer->getTopology() == pass.input.geometry->topology;
  }
  if (draw.mesh.isValid() && context.hasRealtimeScene()) {
    const Scene &scene = context.realtimeScene();
    const auto mesh = scene.resources().resolve(draw.mesh);
    if (mesh.has_value()) {
      return mesh->get().getIndexBuffer().getTopology() ==
             pass.input.geometry->topology;
    }
  }
  return true;
}

void validateSceneRenderableDesc(const FramePass &pass,
                                 const RenderWorkBuildContext &context,
                                 const RenderDrawInput &draw,
                                 RenderInputDesc &desc) {
  if (pass.stage != RenderPassStage::Raster ||
      pass.dispatch != RenderPassDispatch::Draw) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "scene-renderables input requires raster draw pass");
    return;
  }
  if (draw.source != RenderDrawInputSource::SceneRenderable) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "scene-renderables pass received non-scene draw input");
    return;
  }
  if (!objectClassAllowed(draw, pass)) {
    reject(desc, RenderInputDiagnosticCode::ObjectClassRejected,
           "renderable object type does not match input contract");
    return;
  }
  if (pass.input.material.required && !draw.material.isValid()) {
    reject(desc, RenderInputDiagnosticCode::MaterialRequired,
           "scene renderable is missing required validated material data");
    return;
  }
  if (!materialTypeAllowed(draw, pass)) {
    reject(desc, RenderInputDiagnosticCode::MaterialTypeRejected,
           "scene renderable material type does not match input contract");
    return;
  }
  if (draw.material.isValid() && !draw.mesh.isValid()) {
    reject(desc, RenderInputDiagnosticCode::MissingMesh,
           "scene renderable material pass has no resolved mesh");
    return;
  }
  if (!geometryContractMatches(pass, context, draw)) {
    reject(desc, RenderInputDiagnosticCode::GeometryContractMismatch,
           "scene renderable mesh topology does not match geometry contract");
    return;
  }
  if (!hasSubmittableDrawCommand(draw)) {
    reject(desc, RenderInputDiagnosticCode::ZeroDrawCount,
           "scene renderable input has no drawable command");
    return;
  }
  desc.status = RenderInputStatus::Accepted;
}

void validateComputeDesc(const FramePass &pass, const RenderComputeInput &input,
                         RenderInputDesc &desc) {
  if (pass.stage != RenderPassStage::Compute ||
      pass.dispatch != RenderPassDispatch::Compute) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "compute-dispatch input requires compute pass");
    return;
  }
  if (input.groupCountX == 0 || input.groupCountY == 0 ||
      input.groupCountZ == 0) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "compute-dispatch input has zero dispatch group dimension");
    return;
  }
  desc.status = RenderInputStatus::Accepted;
}

void updateStats(std::vector<RenderInputDesc> &descs,
                 const std::vector<std::unique_ptr<RenderInput>> &inputs) {
  RenderInputStats stats;
  stats.compilerInputCount = inputs.size();
  for (const RenderInputDesc &desc : descs) {
    if (desc.accepted()) {
      ++stats.acceptedInputCount;
    } else {
      ++stats.rejectedInputCount;
    }
  }
  for (const RenderInputDesc &desc : descs) {
    if (!desc.accepted() || desc.inputIndex >= inputs.size() ||
        !inputs[desc.inputIndex]) {
      continue;
    }
    const RenderInput &input = *inputs[desc.inputIndex];
    if (const auto *draw = dynamic_cast<const RenderDrawInput *>(&input)) {
      stats.submittedDrawCount += countSubmittableDrawCommands(*draw);
    } else if (dynamic_cast<const RenderComputeInput *>(&input) != nullptr) {
      ++stats.submittedDispatchCount;
    }
  }
  for (RenderInputDesc &desc : descs) {
    desc.stats = stats;
  }
}

} // namespace

void RenderWorkCompiler::buildInputs(
    const FramePass &pass, const RenderWorkBuildContext &context,
    std::vector<std::unique_ptr<RenderInput>> &outInputs) const {
  switch (pass.input.kind) {
  case RenderPassInputKind::FullscreenTriangle:
    buildFullscreenInput(pass, outInputs);
    return;
  case RenderPassInputKind::SceneRenderables:
    buildSceneRenderableInputs(pass, context, outInputs);
    return;
  case RenderPassInputKind::ComputeDispatch: {
    auto compute = std::make_unique<RenderComputeInput>();
    compute->pass = pass.name;
    compute->debugId = stablePassDebugId(pass);
    compute->inputIndex = outInputs.size();
    if (context.domain() == RenderDomain::Offline) {
      offline::OfflineRenderJob &job = context.offlineJob();
      compute->groupCountX = (job.output.width + 7u) / 8u;
      compute->groupCountY = (job.output.height + 7u) / 8u;
      compute->groupCountZ = 1u;
      compute->readbackResource = StringID("OutputPixels");
    }
    outInputs.push_back(std::move(compute));
    return;
  }
  }
}

std::vector<RenderInputDesc> RenderWorkCompiler::prepare(
    const FramePass &pass, const RenderWorkBuildContext &context,
    const std::vector<std::unique_ptr<RenderInput>> &inputs) const {
  std::vector<RenderInputDesc> descs;
  descs.reserve(inputs.size());

  for (usize index = 0; index < inputs.size(); ++index) {
    RenderInputDesc desc;
    desc.inputIndex = index;
    desc.pass = pass.name;
    desc.debugId = stablePassDebugId(pass);
    desc.shaderUri = shaderUriId(pass);

    if (!inputs[index]) {
      reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
             "render input pointer is null");
      descs.push_back(std::move(desc));
      continue;
    }

    const RenderInput &input = *inputs[index];
    desc.inputIndex = index;
    desc.pass = input.pass.id != 0 ? input.pass : pass.name;
    desc.debugId =
        input.debugId.id != 0 ? input.debugId : stablePassDebugId(pass);
    desc.pipelineKey = makePipelineKey(pass, input);

    if (const auto *draw = dynamic_cast<const RenderDrawInput *>(&input)) {
      fillPreparedFacts(pass, context, *draw, desc);
      if (pass.input.kind == RenderPassInputKind::FullscreenTriangle) {
        validateFullscreenDesc(pass, *draw, desc);
      } else if (pass.input.kind == RenderPassInputKind::SceneRenderables) {
        validateSceneRenderableDesc(pass, context, *draw, desc);
      } else {
        reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
               "draw input does not match pass input contract");
      }
    } else if (const auto *compute =
                   dynamic_cast<const RenderComputeInput *>(&input)) {
      fillPreparedFacts(pass, context, *compute, desc);
      if (pass.input.kind == RenderPassInputKind::ComputeDispatch) {
        validateComputeDesc(pass, *compute, desc);
      } else {
        reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
               "compute input does not match pass input contract");
      }
    } else {
      reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
             "unknown render input type");
    }
    if (desc.accepted()) {
      validateEnvironmentLightingFeatureBindings(pass, context, desc);
    }
    if (desc.accepted()) {
      validateSurfaceLightingFeatureRead(pass, desc);
    }
    if (desc.accepted()) {
      validateSurfaceLightingIblBakeSources(pass, context, desc);
    }
    if (desc.accepted()) {
      validateBakeSourcePayloads(pass, desc);
    }
    if (desc.accepted()) {
      validateBakeOutputPayloads(pass, input, desc);
    }
    if (desc.accepted()) {
      validateBindingPlanCompleteness(desc);
    }
    rejectFatalPipelineFacts(desc);

    descs.push_back(std::move(desc));
  }

  updateStats(descs, inputs);
  return descs;
}

} // namespace LX_core
