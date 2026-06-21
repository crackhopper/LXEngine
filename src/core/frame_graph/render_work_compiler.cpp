#include "core/frame_graph/render_work_compiler.hpp"

#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/texture.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_feature_derived_resource_producer.hpp"
#include "core/frame_graph/scene_descriptor_resource_resolver.hpp"
#include "core/raytracing/software_bvh.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace LX_core {
namespace {

void noteDiagnostic(RenderInputDesc &desc, RenderInputDiagnosticCode code,
                    std::string message);
void reject(RenderInputDesc &desc, RenderInputDiagnosticCode code,
            std::string message);
[[nodiscard]] std::string stringIdText(StringID id);

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

class ComputeStorageBufferResource final : public IGpuResource {
public:
  ComputeStorageBufferResource(StringID bindingName,
                               std::vector<std::byte> bytes)
      : m_bindingName(bindingName), m_bytes(std::move(bytes)) {
    setDirty();
  }

  ResourceType getType() const override { return ResourceType::StorageBuffer; }
  const void *getRawData() const override { return m_bytes.data(); }
  u32 getByteSize() const override { return static_cast<u32>(m_bytes.size()); }
  StringID getBindingName() const override { return m_bindingName; }

private:
  StringID m_bindingName;
  std::vector<std::byte> m_bytes;
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

template <typename T>
std::vector<std::byte> copyBytes(std::span<const T> values) {
  std::vector<std::byte> bytes(sizeof(T) * values.size());
  if (!bytes.empty()) {
    std::memcpy(bytes.data(), values.data(), bytes.size());
  }
  return bytes;
}

std::vector<std::byte> copySourceMaterialRecordBytes(
    std::span<const SourceLocalMaterialRecord> records) {
  usize byteCount = 0;
  for (const SourceLocalMaterialRecord &record : records) {
    byteCount += record.bytes.size();
  }
  std::vector<std::byte> bytes(byteCount);
  usize cursor = 0;
  for (const SourceLocalMaterialRecord &record : records) {
    if (record.bytes.empty()) {
      continue;
    }
    std::memcpy(bytes.data() + cursor, record.bytes.data(),
                record.bytes.size());
    cursor += record.bytes.size();
  }
  return bytes;
}

[[nodiscard]] std::span<const std::byte>
sourceRecordBytes(const SourceLocalMaterialRecord &record) {
  return {reinterpret_cast<const std::byte *>(record.bytes.data()),
          record.bytes.size()};
}

template <typename T>
[[nodiscard]] T readSourceField(std::span<const std::byte> bytes, usize offset,
                                T fallback) {
  if (offset + sizeof(T) > bytes.size()) {
    return fallback;
  }
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

template <typename T>
std::vector<std::byte> copyObjectBytes(const T &value) {
  std::vector<std::byte> bytes(sizeof(T));
  std::memcpy(bytes.data(), &value, sizeof(T));
  return bytes;
}

std::vector<std::byte> zeroBytes(usize byteSize) {
  return std::vector<std::byte>(byteSize);
}

[[nodiscard]] GpuResourceRef
addComputeStorageResource(const SceneResourceTable &resources,
                          StringID bindingName, std::vector<std::byte> bytes) {
  return resources.addRenderGpuResource(
      std::make_unique<ComputeStorageBufferResource>(bindingName,
                                                     std::move(bytes)));
}

[[nodiscard]] u32 ceilDiv(const u32 value, const u32 divisor) {
  return divisor == 0u ? value : (value + divisor - 1u) / divisor;
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

[[nodiscard]] const RenderPathFeatureDependency *
findPassFeatureDependency(const FramePass &pass, std::string_view featureName) {
  const auto it =
      std::find_if(pass.features.begin(), pass.features.end(),
                   [&](const RenderPathFeatureDependency &dependency) {
                     return dependency.slot == featureName;
                   });
  return it == pass.features.end() ? nullptr : &*it;
}

struct ResolvedPassFeature final {
  RenderFeatureHandle handle;
  std::reference_wrapper<const RenderFeature> feature;
};

[[nodiscard]] std::optional<ResolvedPassFeature>
resolvePassFeature(const FramePass &pass, const SceneResourceTable &resources,
                   std::string_view featureName) {
  const RenderPathFeatureDependency *dependency =
      findPassFeatureDependency(pass, featureName);
  if (dependency == nullptr) {
    return std::nullopt;
  }
  const auto handle = resources.findRenderFeatureByUri(dependency->uri);
  if (!handle.has_value()) {
    return std::nullopt;
  }
  const auto feature = resources.resolve(*handle);
  if (!feature.has_value()) {
    return std::nullopt;
  }
  if (feature->get().feature != featureName) {
    return std::nullopt;
  }
  return ResolvedPassFeature{.handle = *handle,
                             .feature = std::cref(feature->get())};
}

void appendValidResources(DescriptorResourceList &out,
                          std::vector<GpuResourceRef> resources) {
  for (const GpuResourceRef &resource : resources) {
    if (resource.isValid()) {
      out.emplace_back(resource.get());
    }
  }
}

void appendPassFeatureSceneResources(DescriptorResourceList &out,
                                     const SceneResourceTable &resources,
                                     const FramePass &pass) {
  for (const FrameGraphRead &read : pass.reads) {
    const auto featureName = featureNameFromGraphResource(read.resource);
    if (!featureName.has_value()) {
      continue;
    }
    if (*featureName == "environmentLighting") {
      appendValidResources(out, resources.getEnvironmentLightingResources());
      appendValidResources(out, resources.getIblEnvironmentResources());
    } else if (*featureName == "skybox") {
      const auto feature = resolvePassFeature(pass, resources, *featureName);
      if (feature.has_value()) {
        std::vector<GpuResourceRef> featureResources =
            resources.getSkyboxResources(feature->handle);
        appendValidResources(out, std::move(featureResources));
      }
    } else if (*featureName == "surfaceLighting") {
      const auto feature = resolvePassFeature(pass, resources, *featureName);
      if (feature.has_value()) {
        std::vector<GpuResourceRef> featureResources =
            resources.getSurfaceLightingResources(feature->handle);
        appendValidResources(out, std::move(featureResources));
      }
    } else if (*featureName == "toneMapping") {
      appendValidResources(out, resources.getToneMappingResources());
    } else if (*featureName == "bloom") {
      appendValidResources(out, resources.getBloomResources());
    }
  }
}

[[nodiscard]] std::vector<ShaderSpecializationConstant>
collectPassFeatureSpecializationConstants(
    const FramePass &pass, const RenderWorkBuildContext &context) {
  if (!context.hasScene()) {
    return {};
  }

  std::vector<ShaderSpecializationConstant> constants;
  std::vector<std::string> resolvedFeatures;
  const SceneResourceTable &resources = context.scene().resources();
  for (const FrameGraphRead &read : pass.reads) {
    const auto featureName = featureNameFromGraphResource(read.resource);
    if (!featureName.has_value()) {
      continue;
    }
    if (std::find(resolvedFeatures.begin(), resolvedFeatures.end(),
                  *featureName) != resolvedFeatures.end()) {
      continue;
    }
    const auto feature = resolvePassFeature(pass, resources, *featureName);
    if (!feature.has_value()) {
      continue;
    }
    const PassFeatureData *data =
        resources.findPassFeatureData(feature->handle);
    if (data == nullptr || data->shaderUri != pass.shaderUri) {
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
  if (!context.hasScene()) {
    return {};
  }

  const Scene &scene = context.scene();
  const SceneResourceTable &resources = scene.resources();
  const auto &options = context.options();
  if (options.cameraResource.has_value()) {
    DescriptorResourceList out =
        scene.getSceneLevelResources(pass.name, *options.cameraResource);
    appendPassFeatureSceneResources(out, resources, pass);
    return out;
  }

  const RenderTarget sceneResourceTarget =
      options.sceneResourceTarget.value_or(RenderTarget(pass.target));
  DescriptorResourceList out =
      scene.getSceneLevelResources(pass.name, sceneResourceTarget);
  appendPassFeatureSceneResources(out, resources, pass);
  return out;
}

void appendDerivedFeatureResourcesForPass(const FramePass &pass,
                                          const RenderWorkBuildContext &context,
                                          RenderInputDesc &desc) {
  if (!context.hasScene()) {
    return;
  }

  const SceneResourceTable &resources = context.resourceTable();
  std::vector<std::string> resolvedFeatures;
  for (const FrameGraphRead &read : pass.reads) {
    const auto featureName = featureNameFromGraphResource(read.resource);
    if (!featureName.has_value()) {
      continue;
    }
    if (std::find(resolvedFeatures.begin(), resolvedFeatures.end(),
                  *featureName) != resolvedFeatures.end()) {
      continue;
    }
    resolvedFeatures.emplace_back(*featureName);

    const auto feature = resolvePassFeature(pass, resources, *featureName);
    if (!feature.has_value()) {
      continue;
    }

    for (const auto &[resourceName, resource] :
         feature->feature.get().resources) {
      std::string diagnostic;
      const auto result = RenderFeatureDerivedResourceProducerRegistry::build(
          RenderFeatureDerivedResourceRequest{
              .feature = &feature->feature.get(),
              .resource = &resource,
              .sceneResources = &resources,
          },
          diagnostic);
      if (!result.has_value()) {
        reject(desc, RenderInputDiagnosticCode::MissingResource,
               "feature." + *featureName + ".resources." + resourceName +
                   " could not build derived resource: " + diagnostic);
        continue;
      }
      if (result->descriptorResources.empty()) {
        reject(desc, RenderInputDiagnosticCode::MissingResource,
               "feature." + *featureName + ".resources." + resourceName +
                   " produced no descriptor resources");
        continue;
      }
      appendDescriptorResources(desc.bindingPlan.descriptors,
                                result->descriptorResources);
      appendDescriptorResourceDependencies(desc.resourceDependencies,
                                           result->descriptorResources);
    }
  }
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
      !context.hasScene()) {
    return facts;
  }

  const auto &renderables = context.scene().getRenderables();
  if (renderables.empty() && context.domain() == RenderDomain::Offline) {
    if (passFacts.has_value()) {
      applyPassPreparationFacts(facts, passFacts->get());
    }
    if (draw.material.isValid()) {
      const auto material = context.resourceTable().resolve(draw.material);
      if (material.has_value()) {
        const auto shaderProgram =
            material->get().getPassShaderProgram(pass.name);
        if (shaderProgram.has_value()) {
          facts.pipelineVariantKey =
              material->get().getMaterialTypeVariantSignature(
                  shaderProgram->get());
          facts.shaderProgram = shaderProgram->get();
          facts.shaderInfo = shaderProgram->get().getShader();
          facts.renderState = material->get().getPassRenderState(pass.name);
        }
      }
    }
    ValidatedRenderablePassData data;
    data.pass = pass.name;
    data.objectHandle = draw.object;
    data.materialHandle = draw.material;
    data.shaderProgram = facts.shaderProgram;
    data.shaderInfo = facts.shaderInfo;
    data.vertexBuffer = draw.vertexBuffer;
    data.indexBuffer = draw.indexBuffer;
    data.renderState = facts.renderState;
    data.sortCenter = draw.sortCenter;
    data.materialTypeSignature = draw.materialTypeSignature;
    DescriptorResourceList sceneDescriptorResources =
        buildSceneDescriptorResources(SceneDescriptorResourceContext{
            .scene = context.scene(),
            .renderable = data,
            .pass = pass.name,
            .target = RenderTarget(pass.target),
            .sceneResources = collectSceneLevelResourcesForPass(context, pass),
        });
    appendDescriptorResources(facts.descriptorResources,
                              sceneDescriptorResources);
    return facts;
  }
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
          .scene = context.scene(),
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
  appendDescriptorResources(facts.descriptorResources,
                            collectSceneLevelResourcesForPass(context, pass));
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
  for (const RenderDrawCommand &command : draw.drawCommands) {
    appendResourceDependency(desc.resourceDependencies, command.vertexBuffer);
    appendResourceDependency(desc.resourceDependencies, command.indexBuffer);
  }
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

[[nodiscard]] const ShaderResourceBinding *
findBindingForName(const std::vector<ShaderResourceBinding> &bindings,
                   StringID bindingName) {
  const std::string name = resourceNameText(bindingName);
  const auto it = std::find_if(bindings.begin(), bindings.end(),
                               [&](const ShaderResourceBinding &binding) {
                                 return binding.name == name;
                               });
  return it == bindings.end() ? nullptr : &*it;
}

[[nodiscard]] bool shaderConsumesBinding(const RenderInputDesc &desc,
                                         StringID bindingName) {
  return findBindingForName(desc.pipelineBuildDesc.bindings, bindingName) !=
         nullptr;
}

[[nodiscard]] bool descAlreadyHasDescriptor(const RenderInputDesc &desc,
                                            StringID bindingName) {
  return findDescriptorForBinding(desc.bindingPlan.descriptors, bindingName) !=
         nullptr;
}

void appendDescriptor(RenderInputDesc &desc,
                      const DescriptorResourceRef &descriptor) {
  desc.bindingPlan.descriptors.push_back(descriptor);
  if (descriptor.isResource() && descriptor.resource().isValid()) {
    desc.resourceDependencies.push_back(descriptor.resource());
  }
}

void appendStorageIfConsumed(RenderInputDesc &desc,
                             const SceneResourceTable &resources,
                             StringID bindingName,
                             std::vector<std::byte> bytes) {
  if (!shaderConsumesBinding(desc, bindingName) ||
      descAlreadyHasDescriptor(desc, bindingName)) {
    return;
  }
  const GpuResourceRef resource =
      addComputeStorageResource(resources, bindingName, std::move(bytes));
  if (resource.isValid()) {
    appendDescriptor(desc, DescriptorResourceRef{resource.get()});
  }
}

void appendFrameGraphSampledResourcesForPass(
    const FramePass &pass, const RenderWorkBuildContext &context,
    RenderInputDesc &desc) {
  if (!context.hasScene()) {
    return;
  }
  const SceneResourceTable &resources = context.resourceTable();
  for (const FrameGraphRead &read : pass.reads) {
    if (read.bindingName.id == 0 ||
        !shaderConsumesBinding(desc, read.bindingName) ||
        descAlreadyHasDescriptor(desc, read.bindingName)) {
      continue;
    }
    const GpuResourceRef resource = resources.addRenderGpuResource(
        std::make_unique<FrameGraphSampledResource>(read.resource,
                                                    read.bindingName));
    if (resource.isValid()) {
      appendDescriptor(desc, DescriptorResourceRef{resource.get()});
    }
  }
}

[[nodiscard]] std::optional<Vec3u>
offlineOutputExtent(const RenderWorkBuildContext &context,
                    RenderInputDesc &desc) {
  const auto extent =
      context.findRuntimeExtent(StringID("offline.output.resolution"));
  if (!extent.has_value()) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "OfflineRT compute requires runtime extent "
           "offline.output.resolution");
    return std::nullopt;
  }
  return extent;
}

[[nodiscard]] u32
parseU32RuntimeFeatureValue(const RenderWorkBuildContext &context, StringID key,
                            u32 fallback) {
  const auto value = context.findFeatureValue(key);
  if (!value.has_value()) {
    return fallback;
  }
  try {
    return static_cast<u32>(std::stoul(value->get().value));
  } catch (const std::exception &) {
    return fallback;
  }
}

[[nodiscard]] u32
parseCompareModeRuntimeFeatureValue(const RenderWorkBuildContext &context) {
  const auto value = context.findFeatureValue(
      StringID("feature.offlineRayTracer.compareMode"));
  if (!value.has_value()) {
    return 0u;
  }
  return value->get().value == "albedo" ? 1u : 0u;
}

struct DirectionalLightParams final {
  Vec3f direction{0.0f, -1.0f, 0.0f};
  Vec3f color{0.0f, 0.0f, 0.0f};
  float intensity = 0.0f;
};

[[nodiscard]] std::optional<std::reference_wrapper<const CameraResource>>
findActiveCamera(const SceneResourceTable &resources) {
  const RenderSceneSnapshot snapshot = resources.buildSnapshot();
  for (const CameraHandle handle : snapshot.cameraHandles) {
    auto camera = resources.resolve(handle);
    if (camera.has_value() && camera->get().active) {
      return std::cref(camera->get());
    }
  }
  return std::nullopt;
}

[[nodiscard]] DirectionalLightParams
findFirstDirectionalLight(const SceneResourceTable &resources) {
  const RenderSceneSnapshot snapshot = resources.buildSnapshot();
  for (const LightHandle handle : snapshot.lightHandles) {
    auto light = resources.resolve(handle);
    if (!light.has_value()) {
      continue;
    }
    const auto *directional =
        dynamic_cast<const DirectionalLight *>(&light->get());
    if (directional == nullptr) {
      continue;
    }
    return DirectionalLightParams{
        .direction = directional->getDirection().normalized(),
        .color = directional->getColor(),
        .intensity = directional->getIntensity(),
    };
  }
  return {};
}

[[nodiscard]] SceneGpuFrameParams
makeOfflineFrameParams(const RenderWorkBuildContext &context,
                       const SceneResourceTableUploadView &uploadView,
                       Vec3u extent, const DescriptorResourceRef *bvhNodes) {
  const SceneResourceTable &resources = context.resourceTable();
  const auto camera = findActiveCamera(resources);
  CameraRayFrame rayFrame;
  if (camera.has_value()) {
    CameraProjection projection = camera->get().projection;
    if (extent.y != 0u) {
      projection.aspect =
          static_cast<float>(extent.x) / static_cast<float>(extent.y);
    }
    rayFrame = makeCameraRayFrame(camera->get().pose, projection);
  }
  const DirectionalLightParams light = findFirstDirectionalLight(resources);

  SceneGpuFrameParams params;
  params.eye = Vec4f{rayFrame.eye.x, rayFrame.eye.y, rayFrame.eye.z, 0.0f};
  params.cameraRight =
      Vec4f{rayFrame.right.x, rayFrame.right.y, rayFrame.right.z, 0.0f};
  params.cameraUp = Vec4f{rayFrame.up.x, rayFrame.up.y, rayFrame.up.z, 0.0f};
  params.cameraForward =
      Vec4f{rayFrame.forward.x, rayFrame.forward.y, rayFrame.forward.z, 0.0f};
  params.lightDirectionIntensity = Vec4f{light.direction.x, light.direction.y,
                                         light.direction.z, light.intensity};
  params.lightColorEnvironment =
      Vec4f{light.color.x, light.color.y, light.color.z, 0.0f};
  const Vec3f background = context.options().outputBackgroundColor;
  params.backgroundColor =
      Vec4f{background.x, background.y, background.z, 1.0f};
  params.width = extent.x;
  params.height = extent.y;
  params.samples = parseU32RuntimeFeatureValue(
      context, StringID("feature.offlineRayTracer.samples"), 1u);
  params.seed = parseU32RuntimeFeatureValue(
      context, StringID("feature.offlineRayTracer.seed"), 1u);
  params.primitiveCount = static_cast<u32>(uploadView.primitives.size());
  if (bvhNodes != nullptr && bvhNodes->isResource() &&
      bvhNodes->resource().isValid()) {
    params.bvhNodeCount = bvhNodes->resource().get().getByteSize() /
                          static_cast<u32>(sizeof(SceneSoftwareBvhNode));
  }
  params.materialCount = uploadView.materialRefs.empty()
                             ? static_cast<u32>(uploadView.materials.size())
                             : static_cast<u32>(uploadView.materialRefs.size());
  params.maxBounce = parseU32RuntimeFeatureValue(
      context, StringID("feature.offlineRayTracer.maxBounce"), 1u);
  params.shadowsEnabled = context.scene().renderSettings().shadows ? 1u : 0u;
  params.compareMode = parseCompareModeRuntimeFeatureValue(context);
  return params;
}

DescriptorResourceRef makeSceneTextureArray(
    const SceneResourceTable &resources,
    std::span<const std::reference_wrapper<const CombinedTextureSampler>>
        uploadTextures) {
  constexpr usize kTextureDescriptorCount = 256;
  if (uploadTextures.size() > kTextureDescriptorCount) {
    throw std::runtime_error("OfflineRT scene texture descriptor array "
                             "supports at most 256 textures");
  }

  std::vector<TextureSamplerRef> textures;
  textures.reserve(kTextureDescriptorCount);
  for (const auto &texture : uploadTextures) {
    textures.emplace_back(texture.get());
  }

  TextureSamplerRef paddingTexture;
  if (!uploadTextures.empty()) {
    paddingTexture = TextureSamplerRef{uploadTextures.front().get()};
  } else {
    paddingTexture = resources.addRenderTextureSampler(
        std::make_unique<CombinedTextureSampler>(createWhiteTexture()));
  }
  if (!paddingTexture.isValid()) {
    throw std::runtime_error("OfflineRT scene texture descriptor padding "
                             "missing");
  }
  while (textures.size() < kTextureDescriptorCount) {
    textures.emplace_back(paddingTexture.get());
  }
  return DescriptorResourceRef::textureArray(StringID("SceneTextures"),
                                             std::move(textures));
}

void appendOfflineComputeSceneResources(const FramePass &pass,
                                        const RenderWorkBuildContext &context,
                                        RenderInputDesc &desc) {
  (void)pass;
  if (!context.hasScene()) {
    return;
  }
  const bool needsOfflineSceneResource =
      std::any_of(desc.pipelineBuildDesc.bindings.begin(),
                  desc.pipelineBuildDesc.bindings.end(),
                  [](const ShaderResourceBinding &binding) {
                    return binding.name == "ScenePositions" ||
                           binding.name == "SceneAttributeStreams" ||
                           binding.name == "SceneAttributeValues" ||
                           binding.name == "SceneIndices" ||
                           binding.name == "SceneMeshes" ||
                           binding.name == "ScenePrimitives" ||
                           binding.name == "SceneObjects" ||
                           binding.name == "SceneMaterials" ||
                           binding.name == "SceneMaterialRefs" ||
                           binding.name == "SceneSourceMaterialRecords" ||
                           binding.name == "SceneFrameParams" ||
                           binding.name == "OutputPixels" ||
                           binding.name == "SceneTextures";
                  });
  if (!needsOfflineSceneResource) {
    return;
  }

  const auto extent = offlineOutputExtent(context, desc);
  if (!extent.has_value()) {
    return;
  }

  const SceneResourceTable &resources = context.resourceTable();
  const SceneResourceTableUploadView uploadView = resources.buildUploadView();
  appendStorageIfConsumed(desc, resources, StringID("ScenePositions"),
                          copyBytes(uploadView.positions));
  appendStorageIfConsumed(desc, resources, StringID("SceneAttributeStreams"),
                          copyBytes(uploadView.attributeStreams));
  appendStorageIfConsumed(desc, resources, StringID("SceneAttributeValues"),
                          copyBytes(uploadView.attributeValues));
  appendStorageIfConsumed(desc, resources, StringID("SceneIndices"),
                          copyBytes(uploadView.indices));
  appendStorageIfConsumed(desc, resources, StringID("SceneMeshes"),
                          copyBytes(uploadView.meshes));
  appendStorageIfConsumed(desc, resources, StringID("ScenePrimitives"),
                          copyBytes(uploadView.primitives));
  appendStorageIfConsumed(desc, resources, StringID("SceneObjects"),
                          copyBytes(uploadView.objects));
  appendStorageIfConsumed(desc, resources, StringID("SceneMaterials"),
                          copyBytes(uploadView.materials));
  appendStorageIfConsumed(desc, resources, StringID("SceneMaterialRefs"),
                          copyBytes(uploadView.materialRefs));
  appendStorageIfConsumed(
      desc, resources, StringID("SceneSourceMaterialRecords"),
      copySourceMaterialRecordBytes(uploadView.sourceMaterialRecords));

  if (shaderConsumesBinding(desc, StringID("SceneFrameParams")) &&
      !descAlreadyHasDescriptor(desc, StringID("SceneFrameParams"))) {
    const DescriptorResourceRef *bvhNodes = findDescriptorForBinding(
        desc.bindingPlan.descriptors, StringID("SceneBvhNodes"));
    const SceneGpuFrameParams params =
        makeOfflineFrameParams(context, uploadView, *extent, bvhNodes);
    appendStorageIfConsumed(desc, resources, StringID("SceneFrameParams"),
                            copyObjectBytes(params));
  }

  if (shaderConsumesBinding(desc, StringID("OutputPixels")) &&
      !descAlreadyHasDescriptor(desc, StringID("OutputPixels"))) {
    const usize outputBytes = static_cast<usize>(extent->x) *
                              static_cast<usize>(extent->y) * sizeof(Vec4f);
    appendStorageIfConsumed(desc, resources, StringID("OutputPixels"),
                            zeroBytes(outputBytes));
  }

  if (shaderConsumesBinding(desc, StringID("SceneTextures")) &&
      !descAlreadyHasDescriptor(desc, StringID("SceneTextures"))) {
    try {
      appendDescriptor(desc,
                       makeSceneTextureArray(resources, uploadView.textures));
    } catch (const std::exception &error) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             std::string("OfflineRT SceneTextures resource failed: ") +
                 error.what());
    }
  }
}

[[nodiscard]] u32
sourceStorageIndexForMaterial(const SceneResourceTableUploadView &uploadView,
                              MaterialHandle handle) {
  const auto it =
      std::find_if(uploadView.materialRefIndexByHandle.begin(),
                   uploadView.materialRefIndexByHandle.end(),
                   [handle](const SceneResourceMaterialRefUploadIndex &entry) {
                     return entry.handle == handle;
                   });
  if (it == uploadView.materialRefIndexByHandle.end() ||
      it->typedIndex >= uploadView.materialRefs.size()) {
    return u32_max;
  }
  return uploadView.materialRefs[it->typedIndex].sourceStorageIndex;
}

[[nodiscard]] u32
primitiveIndexForParticipant(const SceneResourceTableUploadView &uploadView,
                             const RenderSceneParticipant &participant) {
  const auto objectIt =
      std::find_if(uploadView.objectIndexByHandle.begin(),
                   uploadView.objectIndexByHandle.end(),
                   [&participant](const SceneResourceObjectUploadIndex &entry) {
                     return entry.handle == participant.object;
                   });
  if (objectIt == uploadView.objectIndexByHandle.end()) {
    return u32_max;
  }
  const auto meshIt = std::find_if(
      uploadView.meshIndexByHandle.begin(), uploadView.meshIndexByHandle.end(),
      [&participant](const SceneResourceMeshUploadIndex &entry) {
        return entry.handle == participant.mesh;
      });
  if (meshIt == uploadView.meshIndexByHandle.end()) {
    return u32_max;
  }
  const u32 localPrimitiveIndex =
      participant.primitiveIndex == u32_max ? 0u : participant.primitiveIndex;
  u32 primitiveOrdinal = 0;
  for (u32 i = 0; i < uploadView.primitives.size(); ++i) {
    const SceneGpuPrimitiveRecord &primitive = uploadView.primitives[i];
    if (primitive.objectIndex != objectIt->typedIndex ||
        primitive.meshIndex != meshIt->typedIndex) {
      continue;
    }
    if (primitiveOrdinal == localPrimitiveIndex) {
      return i;
    }
    ++primitiveOrdinal;
  }
  return u32_max;
}

[[nodiscard]] u32
materialRecordIndexForHandle(const SceneResourceTableUploadView &uploadView,
                             MaterialHandle handle) {
  const auto refIt =
      std::find_if(uploadView.materialRefIndexByHandle.begin(),
                   uploadView.materialRefIndexByHandle.end(),
                   [handle](const SceneResourceMaterialRefUploadIndex &entry) {
                     return entry.handle == handle;
                   });
  if (refIt != uploadView.materialRefIndexByHandle.end()) {
    return refIt->typedIndex;
  }
  const auto materialIt =
      std::find_if(uploadView.materialIndexByHandle.begin(),
                   uploadView.materialIndexByHandle.end(),
                   [handle](const SceneResourceMaterialUploadIndex &entry) {
                     return entry.handle == handle;
                   });
  return materialIt == uploadView.materialIndexByHandle.end()
             ? u32_max
             : materialIt->typedIndex;
}

[[nodiscard]] std::optional<std::reference_wrapper<const PrimitiveHitShader>>
findPrimitiveHitShader(const RayProgramTable &table, u32 participantIndex,
                       u32 primitiveIndex) {
  const auto it = std::find_if(
      table.primitiveHitShaders.begin(), table.primitiveHitShaders.end(),
      [participantIndex, primitiveIndex](const PrimitiveHitShader &group) {
        return group.participantIndex == participantIndex &&
               group.primitiveIndex == primitiveIndex;
      });
  if (it == table.primitiveHitShaders.end()) {
    return std::nullopt;
  }
  return std::cref(*it);
}

[[nodiscard]] std::vector<RayPrimitiveHitShaderRecord>
buildRayPrimitiveHitShaderRecords(
    const SceneResourceTableUploadView &uploadView,
    const RenderComputeInput &compute, const RayProgramTable &table) {
  std::vector<RayPrimitiveHitShaderRecord> originalRecords(
      uploadView.primitives.size());
  for (u32 participantIndex = 0;
       participantIndex < compute.sceneParticipants.size();
       ++participantIndex) {
    const RenderSceneParticipant &participant =
        compute.sceneParticipants[participantIndex];
    const u32 materialIndex =
        materialRecordIndexForHandle(uploadView, participant.material);
    const auto participantHitShader =
        findPrimitiveHitShader(table, participantIndex, 0u);
    const u32 participantHitShaderIndex =
        participantHitShader.has_value()
            ? participantHitShader->get().hitShaderIndex
            : 0u;
    const u32 participantFlags = !participantHitShader.has_value() ||
                                         participantHitShader->get().castsShadow
                                     ? 1u
                                     : 0u;
    u32 localPrimitiveIndex = 0;
    for (;;) {
      const u32 primitiveIndex = primitiveIndexForParticipant(
          uploadView,
          RenderSceneParticipant{.object = participant.object,
                                 .mesh = participant.mesh,
                                 .material = participant.material,
                                 .primitiveIndex = localPrimitiveIndex});
      if (primitiveIndex == u32_max) {
        break;
      }
      originalRecords[primitiveIndex] = RayPrimitiveHitShaderRecord{
          .hitShaderIndex = participantHitShaderIndex,
          .materialIndex = materialIndex == u32_max ? 0u : materialIndex,
          .flags = participantFlags,
      };
      ++localPrimitiveIndex;
    }
  }
  if (uploadView.primitives.empty()) {
    return originalRecords;
  }

  const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(uploadView);
  std::vector<RayPrimitiveHitShaderRecord> records;
  records.reserve(bvh.primitives().size());
  for (const SceneSoftwareBvhPrimitive &primitive : bvh.primitives()) {
    records.push_back(primitive.primitiveIndex < originalRecords.size()
                          ? originalRecords[primitive.primitiveIndex]
                          : RayPrimitiveHitShaderRecord{});
  }
  return records;
}

[[nodiscard]] u32
textureSlotForMaterialParameter(const SceneResourceTable &resources,
                                const SceneResourceTableUploadView &uploadView,
                                const MaterialInstance &material,
                                std::string_view parameterName, u32 fallback) {
  const StringID parameterId{std::string(parameterName)};
  TextureHandle handle = material.getTextureHandle(parameterId);
  if (!handle.isValid()) {
    for (const MaterialResourceDependency &dependency :
         material.getMaterialDependencies()) {
      if (dependency.kind == MaterialEnvelopeKind::Texture &&
          dependency.parameterName == parameterName) {
        if (const auto texture = resources.findTexture(dependency.uri)) {
          handle = *texture;
          break;
        }
      }
    }
  }
  if (!handle.isValid()) {
    return fallback;
  }
  const auto it =
      std::find_if(uploadView.textureIndexByHandle.begin(),
                   uploadView.textureIndexByHandle.end(),
                   [handle](const SceneResourceTextureUploadIndex &entry) {
                     return entry.handle == handle;
                   });
  return it == uploadView.textureIndexByHandle.end() ? fallback
                                                     : it->typedIndex;
}

[[nodiscard]] std::vector<RayMaterialRecord>
buildRayMaterialRecords(const SceneResourceTable &resources,
                        const SceneResourceTableUploadView &uploadView,
                        const RenderComputeInput &compute,
                        const RayProgramTable &table) {
  std::vector<RayMaterialRecord> records(
      std::max(uploadView.materialRefs.size(), uploadView.materials.size()));
  for (u32 participantIndex = 0;
       participantIndex < compute.sceneParticipants.size();
       ++participantIndex) {
    const RenderSceneParticipant &participant =
        compute.sceneParticipants[participantIndex];
    const u32 materialIndex =
        materialRecordIndexForHandle(uploadView, participant.material);
    if (materialIndex == u32_max || materialIndex >= records.size()) {
      continue;
    }
    const auto material = resources.resolve(participant.material);
    if (!material.has_value()) {
      continue;
    }
    const auto hitShader = findPrimitiveHitShader(table, participantIndex, 0u);
    const u32 hitShaderIndex =
        hitShader.has_value() ? hitShader->get().hitShaderIndex : 0u;
    RayMaterialRecord record;
    record.hitShaderIndex = hitShaderIndex;
    const u32 white = textureSlotForMaterialParameter(
        resources, uploadView, material->get(), "baseColorTexture", 0u);
    record.baseColorTexture = white;
    record.metallicRoughnessTexture =
        textureSlotForMaterialParameter(resources, uploadView, material->get(),
                                        "metallicRoughnessTexture", white);
    record.normalTexture = textureSlotForMaterialParameter(
        resources, uploadView, material->get(), "normalTexture", white);
    record.occlusionTexture = textureSlotForMaterialParameter(
        resources, uploadView, material->get(), "occlusionTexture", white);
    record.emissiveTexture = textureSlotForMaterialParameter(
        resources, uploadView, material->get(), "emissiveTexture", white);

    if (const auto baseColor =
            material->get().getMaterialEnvelope(StringID("baseColor"));
        baseColor.has_value() && baseColor->get().rgbValue.has_value()) {
      const Vec3f rgb = *baseColor->get().rgbValue;
      record.baseColor = Vec4f{rgb.x, rgb.y, rgb.z, 1.0f};
    }
    if (const auto metallic =
            material->get().getMaterialEnvelope(StringID("metallic"));
        metallic.has_value() && metallic->get().floatValue.has_value()) {
      record.metallic = *metallic->get().floatValue;
    }
    if (const auto roughness =
            material->get().getMaterialEnvelope(StringID("roughness"));
        roughness.has_value() && roughness->get().floatValue.has_value()) {
      record.roughness = *roughness->get().floatValue;
    }
    if (const auto emissive =
            material->get().getMaterialEnvelope(StringID("emissive"));
        emissive.has_value() && emissive->get().rgbValue.has_value()) {
      const Vec3f rgb = *emissive->get().rgbValue;
      record.emissive = Vec4f{rgb.x, rgb.y, rgb.z, 0.0f};
    }
    records[materialIndex] = record;
  }
  return records;
}

void appendRayProgramResources(const RenderWorkBuildContext &context,
                               const RenderComputeInput &compute,
                               RenderInputDesc &desc) {
  if (!desc.rayProgramTable.has_value() || !context.hasScene()) {
    return;
  }
  const SceneResourceTable &resources = context.resourceTable();
  const SceneResourceTableUploadView uploadView = resources.buildUploadView();
  if (shaderConsumesBinding(desc, StringID("RayPrimitiveHitShaders")) &&
      !descAlreadyHasDescriptor(desc, StringID("RayPrimitiveHitShaders"))) {
    const auto records = buildRayPrimitiveHitShaderRecords(
        uploadView, compute, *desc.rayProgramTable);
    appendStorageIfConsumed(
        desc, resources, StringID("RayPrimitiveHitShaders"),
        copyBytes(std::span<const RayPrimitiveHitShaderRecord>(
            records.data(), records.size())));
  }
  if (shaderConsumesBinding(desc, StringID("RayMaterialRecords")) &&
      !descAlreadyHasDescriptor(desc, StringID("RayMaterialRecords"))) {
    const auto records = buildRayMaterialRecords(resources, uploadView, compute,
                                                 *desc.rayProgramTable);
    appendStorageIfConsumed(desc, resources, StringID("RayMaterialRecords"),
                            copyBytes(std::span<const RayMaterialRecord>(
                                records.data(), records.size())));
  }
}

void validateBakeTextureDimension(const std::string &sourceName,
                                  const DescriptorResourceRef &descriptor,
                                  const ShaderResourceBinding &binding,
                                  RenderInputDesc &desc) {
  if (!descriptor.isResource() || !descriptor.resource().isValid()) {
    return;
  }
  const auto *sampler = dynamic_cast<const CombinedTextureSampler *>(
      &descriptor.resource().get());
  if (sampler == nullptr || !sampler->texture()) {
    return;
  }

  const TextureDimension dimension = sampler->texture()->desc().dimension;
  if (binding.type == ShaderPropertyType::TextureCube &&
      dimension != TextureDimension::TextureCube) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "bake source '" + sourceName +
               "' requires TextureCube payload for binding '" + binding.name +
               "'");
  }
  if (binding.type == ShaderPropertyType::Texture2D &&
      dimension != TextureDimension::Texture2D) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "bake source '" + sourceName +
               "' requires Texture2D payload for binding '" + binding.name +
               "'");
  }
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
      continue;
    }
    const ShaderResourceBinding *binding =
        findBindingForName(desc.pipelineBuildDesc.bindings, read.bindingName);
    if (binding != nullptr) {
      validateBakeTextureDimension(sourceName, *descriptor, *binding, desc);
    }
  }
}

void resolveReadbackContracts(const FramePass &pass,
                              const RenderWorkBuildContext &context,
                              RenderInputDesc &desc) {
  for (const RenderPathReadbackContract &contract : pass.readbacks) {
    if (contract.name.empty()) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "readback contract requires a name");
      continue;
    }
    if (contract.target.empty()) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "readback '" + contract.name + "' requires a target");
      continue;
    }
    if (contract.extentFrom.empty()) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "readback '" + contract.name + "' requires an extent source");
      continue;
    }
    const StringID bindingName =
        contract.binding.empty() ? StringID{} : StringID(contract.binding);
    const DescriptorResourceRef *descriptor = nullptr;
    if (bindingName.id != 0) {
      descriptor =
          findDescriptorForBinding(desc.bindingPlan.descriptors, bindingName);
      if (descriptor == nullptr || !descriptor->isResource() ||
          !descriptor->resource().isValid() ||
          isFrameGraphPlaceholderResource(*descriptor)) {
        reject(desc, RenderInputDiagnosticCode::MissingResource,
               "readback '" + contract.name +
                   "' requires a live descriptor resource for binding '" +
                   contract.binding + "'");
        continue;
      }
    }

    const std::optional<Vec3u> extent =
        context.findRuntimeExtent(StringID(contract.extentFrom));
    if (!extent.has_value()) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "readback '" + contract.name + "' requires runtime extent '" +
                 contract.extentFrom + "'");
      continue;
    }

    desc.readbacks.push_back(RenderInputDesc::Readback{
        .name = contract.name,
        .target = StringID(contract.target),
        .extentKey = StringID(contract.extentFrom),
        .binding = bindingName,
        .format = contract.format,
        .kind = contract.kind,
        .mediaType = contract.mediaType,
        .extent = *extent,
        .resource =
            descriptor != nullptr ? descriptor->resource() : GpuResourceRef{},
    });
  }
}

void validateBakeOutputPayloads(const FramePass &pass, RenderInputDesc &desc) {
  if (pass.readbacks.empty()) {
    return;
  }
  if (pass.input.kind != RenderPassInputKind::ComputeDispatch) {
    return;
  }
  if (desc.readbacks.empty()) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "bake compute payload requires a typed readback resource");
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
  const ShaderResourceBinding *skyboxMap =
      findReflectedBinding(desc.pipelineBuildDesc.bindings, "SkyboxMap");
  const ShaderResourceBinding *ubo = findReflectedBinding(
      desc.pipelineBuildDesc.bindings, "EnvironmentLightingUBO");
  if (skyboxMap == nullptr && ubo == nullptr) {
    return;
  }
  if (!context.hasScene()) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.environmentLighting requires a realtime scene resource "
           "table");
    return;
  }

  const SceneResourceTable &resources = context.scene().resources();
  const auto resolvedFeature =
      resolvePassFeature(pass, resources, "environmentLighting");
  if (!resolvedFeature.has_value()) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.environmentLighting RenderFeature payload is unresolved");
    return;
  }
  const RenderFeature &feature = resolvedFeature->feature.get();
  if (feature.feature != "environmentLighting") {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.environmentLighting RenderFeature payload is not "
           "environmentLighting");
    return;
  }

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

void validateSkyboxFeatureBindings(const FramePass &pass,
                                   const RenderWorkBuildContext &context,
                                   RenderInputDesc &desc) {
  if (!passUsesFeature(pass, "feature.skybox")) {
    return;
  }
  const ShaderResourceBinding *skyboxMap =
      findReflectedBinding(desc.pipelineBuildDesc.bindings, "SkyboxMap");
  const ShaderResourceBinding *ubo =
      findReflectedBinding(desc.pipelineBuildDesc.bindings, "SkyboxUBO");
  if (skyboxMap == nullptr && ubo == nullptr) {
    return;
  }
  if (!context.hasScene()) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.skybox requires a scene resource table");
    return;
  }

  const SceneResourceTable &resources = context.scene().resources();
  const auto skyboxState = resources.skyboxRuntimeState();
  if (!skyboxState.has_value() || !skyboxState->nodePresent) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.skybox requires an infinite scene skybox node");
    return;
  }

  const auto resolvedFeature = resources.resolve(skyboxState->feature);
  if (!resolvedFeature.has_value()) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.skybox RenderFeature payload is unresolved");
    return;
  }
  const RenderFeature &feature = resolvedFeature->get();
  if (feature.feature != "skybox") {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "scene skybox node RenderFeature payload is not skybox");
    return;
  }

  if (skyboxMap != nullptr) {
    const auto environmentMap = feature.parameters.find("environmentMap");
    if (environmentMap == feature.parameters.end() ||
        environmentMap->second.kind != "textureCube" ||
        environmentMap->second.binding != "SkyboxMap" ||
        environmentMap->second.uri.empty()) {
      reject(desc, RenderInputDiagnosticCode::MissingBinding,
             "feature.skybox parameter environmentMap does not satisfy "
             "reflected binding SkyboxMap");
    }
  }

  if (ubo == nullptr) {
    return;
  }
  for (const StructMemberInfo &member : ubo->members) {
    const RenderFeatureParameter *parameter =
        findParameterForBindingMember(feature, "SkyboxUBO", member.name);
    if (parameter == nullptr) {
      reject(desc, RenderInputDiagnosticCode::MissingBinding,
             "feature.skybox is missing SkyboxUBO." + member.name);
      continue;
    }
    if (!environmentParameterKindMatches(member.name, parameter->kind)) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "feature.skybox parameter for SkyboxUBO." + member.name +
                 " has incompatible kind " + parameter->kind);
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

[[nodiscard]] bool
volatileFeatureBoolValue(const RenderFeatureVolatileValue &value) {
  return value.value == "true" || value.value == "1";
}

[[nodiscard]] bool
surfaceLightingIblEnabled(const FramePass &pass,
                          const RenderWorkBuildContext &context) {
  const auto runtimeValue = context.findFeatureValue(
      StringID("feature.surfaceLighting.enableIblLighting"));
  if (runtimeValue.has_value()) {
    return volatileFeatureBoolValue(runtimeValue->get());
  }
  if (!context.hasScene()) {
    return false;
  }
  const SceneResourceTable &resources = context.scene().resources();
  const auto feature = resolvePassFeature(pass, resources, "surfaceLighting");
  if (!feature.has_value()) {
    return false;
  }
  const auto enableIblLighting =
      feature->feature.get().parameters.find("enableIblLighting");
  if (enableIblLighting == feature->feature.get().parameters.end()) {
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
  if (!surfaceLightingIblEnabled(pass, context)) {
    return;
  }
  if (!passUsesFeature(pass, "feature.environmentLighting")) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "feature.surfaceLighting enableIblLighting=true requires "
           "feature.environmentLighting source");
  }
}

[[nodiscard]] std::optional<std::reference_wrapper<const RenderFeature>>
findPassHitShaderTableFeature(const FramePass &pass,
                              const RenderWorkBuildContext &context) {
  if (!context.hasScene()) {
    return std::nullopt;
  }
  const SceneResourceTable &resources = context.resourceTable();
  for (const FrameGraphRead &read : pass.reads) {
    const auto featureName = featureNameFromGraphResource(read.resource);
    if (!featureName.has_value()) {
      continue;
    }
    const auto feature = resolvePassFeature(pass, resources, *featureName);
    if (feature.has_value() &&
        feature->feature.get().hitShaderTable.has_value()) {
      return std::cref(feature->feature.get());
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::reference_wrapper<const RayHitShaderProgram>>
findHitShaderProgram(const RayProgramTable &table, StringID materialType,
                     const ResourceUri &uri) {
  const std::string materialTypeText = stringIdText(materialType);
  const auto it = std::find_if(
      table.hitShaders.begin(), table.hitShaders.end(),
      [&](const RayHitShaderProgram &program) {
        const std::string hitShaderMaterialType =
            stringIdText(program.materialType);
        const bool materialTypeMatches =
            materialTypeText == hitShaderMaterialType ||
            materialTypeText.rfind(hitShaderMaterialType + "-", 0) == 0;
        return materialTypeMatches && program.uri == uri;
      });
  if (it == table.hitShaders.end()) {
    return std::nullopt;
  }
  return std::cref(*it);
}

void buildRayProgramTable(const FramePass &pass,
                          const RenderWorkBuildContext &context,
                          const RenderComputeInput &compute,
                          RenderInputDesc &desc) {
  if (compute.sceneParticipants.empty()) {
    return;
  }

  const auto feature = findPassHitShaderTableFeature(pass, context);
  if (!feature.has_value()) {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "scene-consuming ray compute pass requires a RenderFeature "
           "hitShaderTable");
    return;
  }
  const RenderFeatureHitShaderTable &featureTable =
      *feature->get().hitShaderTable;
  if (featureTable.payload != "radiance") {
    reject(desc, RenderInputDiagnosticCode::MissingResource,
           "only radiance hit shader payload is supported");
    return;
  }

  RayProgramTable table;
  table.payload = RayProgramPayload::Radiance;
  table.dispatchFunction = StringID(featureTable.dispatchFunction);
  table.hitShaders.reserve(featureTable.entries.size());
  for (const RenderFeatureHitShaderTableEntry &entry : featureTable.entries) {
    table.hitShaders.push_back(RayHitShaderProgram{
        .hitShaderIndex = entry.hitShaderIndex,
        .materialType = StringID(entry.materialType),
        .uri = entry.uri,
        .function = entry.function,
        .castsShadow = entry.castsShadow,
    });
  }

  const SceneResourceTable &resources = context.resourceTable();
  for (u32 i = 0; i < compute.sceneParticipants.size(); ++i) {
    const RenderSceneParticipant &participant = compute.sceneParticipants[i];
    if (!participant.material.isValid()) {
      reject(desc, RenderInputDiagnosticCode::MaterialRequired,
             "ray participant is missing a material handle");
      continue;
    }
    const auto material = resources.resolve(participant.material);
    if (!material.has_value()) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "ray participant material handle is unresolved");
      continue;
    }
    const auto hitUri = material->get().getRadianceHitShaderUri();
    if (!hitUri.has_value()) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "standard-pbr material is missing hit.radiance.uri");
      continue;
    }
    const auto program = findHitShaderProgram(
        table, participant.materialTypeSignature, hitUri->get());
    if (!program.has_value()) {
      reject(desc, RenderInputDiagnosticCode::MissingResource,
             "material hit.radiance.uri is not present in hitShaderTable");
      continue;
    }
    table.primitiveHitShaders.push_back(PrimitiveHitShader{
        .participantIndex = i,
        .primitiveIndex = participant.primitiveIndex == u32_max
                              ? 0u
                              : participant.primitiveIndex,
        .hitShaderIndex = program->get().hitShaderIndex,
        .castsShadow = program->get().castsShadow,
    });
  }

  if (desc.accepted()) {
    desc.rayProgramTable = std::move(table);
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

[[nodiscard]] bool
materialTypeAllowed(const RenderSceneParticipant &participant,
                    const FramePass &pass) {
  if (pass.input.material.types.empty()) {
    return true;
  }
  if (participant.materialTypeSignature.id == 0) {
    return false;
  }
  return std::any_of(
      pass.input.material.types.begin(), pass.input.material.types.end(),
      [&participant](const std::string &type) {
        return materialTypeMatches(participant.materialTypeSignature, type);
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

[[nodiscard]] bool objectClassAllowed(const RenderSceneParticipant &participant,
                                      const FramePass &pass) {
  const std::vector<std::string> &classes = pass.input.object.renderClasses;
  if (classes.empty()) {
    return true;
  }

  const std::string renderType = stringIdText(participant.objectRenderType);
  return std::any_of(
      classes.begin(), classes.end(),
      [&participant, &renderType](const std::string &renderClass) {
        if (isDebugObjectClass(renderClass)) {
          return participant.debugOnly || isDebugObjectClass(renderType);
        }
        if (!renderType.empty()) {
          return renderType == renderClass;
        }
        return false;
      });
}

[[nodiscard]] VisibilityLayerMask
resolveVisibleMask(const Scene &scene, const FramePass &pass,
                   const RenderWorkBuildContext::Options &options) {
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

[[nodiscard]] u32 sceneDrawIndexForObject(const Scene &scene,
                                          ObjectHandle object) {
  if (!object.isValid()) {
    return 0u;
  }
  const SceneResourceTableUploadView uploadView =
      scene.resources().buildUploadView();
  const auto objectIt =
      std::find_if(uploadView.objectIndexByHandle.begin(),
                   uploadView.objectIndexByHandle.end(),
                   [object](const SceneResourceObjectUploadIndex &entry) {
                     return entry.handle == object;
                   });
  if (objectIt == uploadView.objectIndexByHandle.end()) {
    return 0u;
  }
  const auto drawIt = std::find_if(
      uploadView.draws.begin(), uploadView.draws.end(),
      [objectIndex = objectIt->typedIndex](const SceneGpuDrawRecord &draw) {
        return draw.objectIndex == objectIndex;
      });
  if (drawIt == uploadView.draws.end()) {
    return 0u;
  }
  return static_cast<u32>(std::distance(uploadView.draws.begin(), drawIt));
}

void fillSceneDrawCommand(const Scene &scene,
                          const ValidatedRenderablePassData &validatedData,
                          RenderSceneParticipant &participant) {
  const u32 sceneDrawIndex =
      sceneDrawIndexForObject(scene, validatedData.objectHandle);
  if (participant.indexBuffer.isValid()) {
    const auto *indexBuffer =
        dynamic_cast<const IndexBuffer *>(&participant.indexBuffer.get());
    if (indexBuffer != nullptr && indexBuffer->indexCount() > 0) {
      participant.drawCommands.push_back(RenderDrawCommand{
          .indexCount = static_cast<u32>(indexBuffer->indexCount()),
          .instanceCount = 1,
          .firstInstance = sceneDrawIndex,
          .vertexBuffer = participant.vertexBuffer,
          .indexBuffer = participant.indexBuffer,
      });
      return;
    }
  }

  if (participant.mesh.isValid()) {
    const auto mesh = scene.resources().resolve(participant.mesh);
    if (mesh.has_value() && mesh->get().getIndexCount() > 0) {
      participant.drawCommands.push_back(RenderDrawCommand{
          .indexCount = mesh->get().getIndexCount(),
          .instanceCount = 1,
          .firstIndex = mesh->get().getIndexOffset(),
          .firstInstance = sceneDrawIndex,
          .vertexBuffer = participant.vertexBuffer,
          .indexBuffer = participant.indexBuffer,
      });
      return;
    }
  }
}

void fillRenderType(const IRenderable &renderable,
                    RenderSceneParticipant &participant) {
  participant.debugOnly = renderable.isDebugOnlyRenderable();
  if (const auto renderType = renderable.getRenderType()) {
    participant.objectRenderType = *renderType;
  } else if (participant.debugOnly) {
    participant.objectRenderType = StringID("debug.mesh");
  }
}

void fillRawRenderableResources(const IRenderable &renderable,
                                RenderSceneParticipant &participant) {
  participant.vertexBuffer = renderable.getVertexBuffer();
  participant.indexBuffer = renderable.getIndexBuffer();
  if (!participant.drawCommands.empty() || !participant.indexBuffer.isValid()) {
    return;
  }
  const auto *indexBuffer =
      dynamic_cast<const IndexBuffer *>(&participant.indexBuffer.get());
  if (indexBuffer != nullptr && indexBuffer->indexCount() > 0) {
    participant.drawCommands.push_back(RenderDrawCommand{
        .indexCount = static_cast<u32>(indexBuffer->indexCount()),
        .instanceCount = 1,
        .firstInstance = 0,
        .vertexBuffer = participant.vertexBuffer,
        .indexBuffer = participant.indexBuffer,
    });
  }
}

[[nodiscard]] StringID
materialTypeSignatureFor(const MaterialInstance &material,
                         const RenderState &renderState) {
  const std::string &bsdfType = material.getBsdfType();
  const std::string normalizedType =
      (bsdfType.empty() ? std::string("<unspecified>") : bsdfType) + "-" +
      (renderState.blendEnable ? "transparent" : "opaque");
  return StringID(normalizedType);
}

[[nodiscard]] std::vector<RenderSceneParticipant>
selectResourceTableParticipants(const FramePass &pass,
                                const RenderWorkBuildContext &context,
                                bool applyInputFilters,
                                VisibilityLayerMask visibleMask) {
  const SceneResourceTable &resources = context.resourceTable();
  const RenderSceneSnapshot snapshot = resources.buildSnapshot();
  std::vector<RenderSceneParticipant> participants;
  participants.reserve(snapshot.objectHandles.size());

  for (usize objectIndex = 0; objectIndex < snapshot.objectHandles.size();
       ++objectIndex) {
    const ObjectHandle objectHandle = snapshot.objectHandles[objectIndex];
    const auto object = resources.resolve(objectHandle);
    if (!object.has_value() || !object->get().visible ||
        (object->get().visibilityMask & visibleMask) == 0) {
      continue;
    }
    const auto mesh = resources.resolve(object->get().mesh);
    const auto material = resources.resolve(object->get().material);
    if (!mesh.has_value() || !material.has_value()) {
      continue;
    }

    GpuResourceRef vertexBuffer;
    GpuResourceRef indexBuffer;
    if (const GeometryStorageHandle storageHandle =
            mesh->get().getGeometryStorageHandle();
        storageHandle.isValid()) {
      const auto storage = resources.resolve(storageHandle);
      if (storage.has_value()) {
        vertexBuffer = GpuResourceRef{storage->get().getVertexBuffer()};
        indexBuffer = GpuResourceRef{storage->get().getIndexBuffer()};
      }
    } else {
      vertexBuffer = GpuResourceRef{mesh->get().getVertexBuffer()};
      indexBuffer = GpuResourceRef{mesh->get().getIndexBuffer()};
    }

    RenderSceneParticipant participant;
    participant.sourceRenderableIndex = static_cast<u32>(objectIndex);
    participant.debugId = object->get().debugId.id != 0
                              ? object->get().debugId
                              : StringID("offline.object");
    participant.object = objectHandle;
    participant.mesh = object->get().mesh;
    participant.material = object->get().material;
    participant.vertexBuffer = vertexBuffer;
    participant.indexBuffer = indexBuffer;
    participant.primitiveIndex = 0;
    participant.sortCenter = object->get().worldBounds.isValid()
                                 ? object->get().worldBounds.getCenter()
                                 : Vec3f{};
    participant.objectDataSignature = StringID("BindlessObjectData.v1");
    participant.objectRenderType = object->get().renderType;
    participant.materialTypeSignature =
        materialTypeSignatureFor(material->get(), pass.renderState);
    participant.debugOnly = object->get().debugOnly;
    if (mesh->get().getIndexCount() > 0) {
      participant.drawCommands.push_back(RenderDrawCommand{
          .indexCount = mesh->get().getIndexCount(),
          .instanceCount = 1,
          .firstIndex = mesh->get().getIndexOffset(),
          .firstInstance = static_cast<u32>(objectIndex),
          .vertexBuffer = vertexBuffer,
          .indexBuffer = indexBuffer,
      });
    }

    if (applyInputFilters && (!objectClassAllowed(participant, pass) ||
                              !materialTypeAllowed(participant, pass))) {
      continue;
    }
    participants.push_back(std::move(participant));
  }

  return participants;
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

[[nodiscard]] std::vector<RenderSceneParticipant>
selectSceneParticipants(const FramePass &pass,
                        const RenderWorkBuildContext &context,
                        bool applyInputFilters) {
  if (!context.hasScene()) {
    throw std::logic_error("RenderWorkCompiler scene input requires a scene");
  }

  const Scene &scene = context.scene();
  const VisibilityLayerMask visibleMask =
      resolveVisibleMask(scene, pass, context.options());

  std::vector<RenderSceneParticipant> participants;
  const auto &renderables = scene.getRenderables();
  if (renderables.empty() && context.domain() == RenderDomain::Offline) {
    return selectResourceTableParticipants(pass, context, applyInputFilters,
                                           visibleMask);
  }
  for (usize renderableIndex = 0; renderableIndex < renderables.size();
       ++renderableIndex) {
    const auto &renderable = renderables[renderableIndex];
    if (!renderable) {
      continue;
    }
    if ((renderable->getVisibilityLayerMask() & visibleMask) == 0) {
      continue;
    }

    RenderSceneParticipant participant;
    participant.sourceRenderableIndex = static_cast<u32>(renderableIndex);
    participant.debugId = renderable->getDebugId().id != 0
                              ? renderable->getDebugId()
                              : StringID(renderable->getNodeName());
    participant.objectDataSignature = StringID("BindlessObjectData.v1");
    fillRenderType(*renderable, participant);
    fillRawRenderableResources(*renderable, participant);

    const auto validated = renderable->getValidatedPassData(pass.name);
    if (validated.has_value()) {
      const ValidatedRenderablePassData &data = validated->get();
      participant.object = data.objectHandle;
      participant.mesh = resolveObjectMesh(scene, data.objectHandle);
      participant.material = data.materialHandle;
      participant.vertexBuffer = data.vertexBuffer;
      participant.indexBuffer = data.indexBuffer;
      participant.primitiveIndex = 0;
      participant.sortCenter = data.sortCenter;
      participant.materialTypeSignature = data.materialTypeSignature;
      participant.drawCommands.clear();
      fillSceneDrawCommand(scene, data, participant);
    }

    if (applyInputFilters && (!objectClassAllowed(participant, pass) ||
                              !materialTypeAllowed(participant, pass))) {
      continue;
    }
    participants.push_back(std::move(participant));
  }
  if (participants.empty() && context.domain() == RenderDomain::Offline) {
    return selectResourceTableParticipants(pass, context, applyInputFilters,
                                           visibleMask);
  }
  return participants;
}

void copyParticipantToDrawInput(const RenderSceneParticipant &participant,
                                RenderDrawInput &draw) {
  draw.sourceRenderableIndex = participant.sourceRenderableIndex;
  draw.debugId = participant.debugId;
  draw.object = participant.object;
  draw.mesh = participant.mesh;
  draw.material = participant.material;
  draw.vertexBuffer = participant.vertexBuffer;
  draw.indexBuffer = participant.indexBuffer;
  draw.primitiveIndex = participant.primitiveIndex;
  draw.sortCenter = participant.sortCenter;
  draw.objectDataSignature = participant.objectDataSignature;
  draw.objectRenderType = participant.objectRenderType;
  draw.materialTypeSignature = participant.materialTypeSignature;
  draw.debugOnly = participant.debugOnly;
  draw.drawCommands = participant.drawCommands;
}

void appendParticipantToDrawInput(const RenderSceneParticipant &participant,
                                  RenderDrawInput &draw) {
  if (draw.drawCommands.empty()) {
    copyParticipantToDrawInput(participant, draw);
    return;
  }
  draw.drawCommands.insert(draw.drawCommands.end(),
                           participant.drawCommands.begin(),
                           participant.drawCommands.end());
}

[[nodiscard]] bool
computeInputRequestsSceneParticipants(const RenderPassInputContract &input) {
  return !input.object.renderClasses.empty() || !input.material.types.empty() ||
         !input.material.required || input.geometry.has_value();
}

void buildSceneRenderableInputs(
    const FramePass &pass, const RenderWorkBuildContext &context,
    std::vector<std::unique_ptr<RenderInput>> &out) {
  const usize firstPassInput = out.size();
  std::vector<RenderSceneParticipant> participants =
      selectSceneParticipants(pass, context, true);

  if (pass.input.batching.mode == RenderPassBatchingMode::None) {
    for (const RenderSceneParticipant &participant : participants) {
      auto draw = std::make_unique<RenderDrawInput>();
      draw->source = RenderDrawInputSource::SceneRenderable;
      draw->pass = pass.name;
      copyParticipantToDrawInput(participant, *draw);
      out.push_back(std::move(draw));
    }
  } else if (pass.input.batching.mode == RenderPassBatchingMode::Material) {
    std::stable_sort(participants.begin(), participants.end(),
                     [](const RenderSceneParticipant &lhs,
                        const RenderSceneParticipant &rhs) {
                       return stringIdText(lhs.materialTypeSignature) <
                              stringIdText(rhs.materialTypeSignature);
                     });
    StringID currentMaterialType;
    RenderDrawInput *currentDraw = nullptr;
    for (const RenderSceneParticipant &participant : participants) {
      if (currentDraw == nullptr ||
          participant.materialTypeSignature != currentMaterialType) {
        auto draw = std::make_unique<RenderDrawInput>();
        draw->source = RenderDrawInputSource::SceneRenderable;
        draw->pass = pass.name;
        currentDraw = draw.get();
        currentMaterialType = participant.materialTypeSignature;
        out.push_back(std::move(draw));
      }
      appendParticipantToDrawInput(participant, *currentDraw);
    }
  } else {
    auto draw = std::make_unique<RenderDrawInput>();
    draw->source = RenderDrawInputSource::SceneRenderable;
    draw->pass = pass.name;
    for (const RenderSceneParticipant &participant : participants) {
      appendParticipantToDrawInput(participant, *draw);
    }
    if (!draw->drawCommands.empty()) {
      out.push_back(std::move(draw));
    }
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
  if (draw.mesh.isValid() && context.hasScene()) {
    const Scene &scene = context.scene();
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
    if (pass.compute.has_value()) {
      const auto extent =
          context.findRuntimeExtent(StringID(pass.compute->dispatchFrom));
      if (extent.has_value()) {
        compute->groupCountX = ceilDiv(extent->x, pass.compute->localSize.x);
        compute->groupCountY = ceilDiv(extent->y, pass.compute->localSize.y);
        compute->groupCountZ = ceilDiv(extent->z, pass.compute->localSize.z);
      }
    }
    if (computeInputRequestsSceneParticipants(pass.input)) {
      compute->sceneParticipants = selectSceneParticipants(pass, context, true);
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
        if (desc.accepted()) {
          appendDerivedFeatureResourcesForPass(pass, context, desc);
        }
        if (desc.accepted()) {
          appendOfflineComputeSceneResources(pass, context, desc);
        }
        if (desc.accepted()) {
          buildRayProgramTable(pass, context, *compute, desc);
        }
        if (desc.accepted()) {
          appendRayProgramResources(context, *compute, desc);
        }
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
      validateSkyboxFeatureBindings(pass, context, desc);
    }
    if (desc.accepted()) {
      validateSurfaceLightingFeatureRead(pass, desc);
    }
    if (desc.accepted()) {
      validateSurfaceLightingIblBakeSources(pass, context, desc);
    }
    if (desc.accepted()) {
      appendFrameGraphSampledResourcesForPass(pass, context, desc);
    }
    if (desc.accepted()) {
      validateBakeSourcePayloads(pass, desc);
    }
    if (desc.accepted()) {
      resolveReadbackContracts(pass, context, desc);
    }
    if (desc.accepted()) {
      validateBakeOutputPayloads(pass, desc);
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
