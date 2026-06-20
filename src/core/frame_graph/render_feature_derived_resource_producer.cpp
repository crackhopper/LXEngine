#include "core/frame_graph/render_feature_derived_resource_producer.hpp"

#include "core/raytracing/software_bvh.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace LX_core {
namespace {

class DerivedStorageBufferResource final : public IGpuResource {
public:
  DerivedStorageBufferResource(StringID bindingName,
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

template <typename T>
std::vector<std::byte> copyBytes(std::span<const T> values) {
  std::vector<std::byte> bytes(sizeof(T) * values.size());
  if (!bytes.empty()) {
    std::memcpy(bytes.data(), values.data(), bytes.size());
  }
  return bytes;
}

template <typename T>
std::vector<std::byte> copyBytes(const std::vector<T> &values) {
  return copyBytes(std::span<const T>{values.data(), values.size()});
}

[[nodiscard]] bool isSoftwareSceneAccelerationRequest(
    const RenderFeatureResourceRequirement &resource) {
  return resource.api == RenderFeatureResourceApi::SceneAcceleration &&
         resource.function == "buildSceneAcceleration" &&
         resource.implementation ==
             RenderFeatureResourceImplementation::SoftwareBvh;
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

[[nodiscard]] std::optional<RenderFeatureDerivedResourceResult>
buildSoftwareSceneAcceleration(
    const RenderFeatureDerivedResourceRequest &request,
    std::string &diagnostic) {
  const RenderFeatureResourceRequirement &resource = *request.resource;
  if (resource.output.binding.empty()) {
    diagnostic = "scene acceleration resource output binding is empty";
    return std::nullopt;
  }
  if (resource.output.binding != "SceneBvhNodes") {
    diagnostic =
        "software scene acceleration currently supports SceneBvhNodes output";
    return std::nullopt;
  }

  try {
    const SceneResourceTableUploadView uploadView =
        request.sceneResources->buildUploadView();
    const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(uploadView);
    const std::vector<SceneGpuPrimitiveRecord> shaderPrimitives =
        makeShaderPrimitives(uploadView, bvh);
    const GpuResourceRef bvhNodesResource =
        request.sceneResources->addRenderGpuResource(
            std::make_unique<DerivedStorageBufferResource>(
                StringID(resource.output.binding), copyBytes(bvh.nodes())));
    if (!bvhNodesResource.isValid()) {
      diagnostic =
          "software scene acceleration failed to register SceneBvhNodes";
      return std::nullopt;
    }
    const GpuResourceRef primitivesResource =
        request.sceneResources->addRenderGpuResource(
            std::make_unique<DerivedStorageBufferResource>(
                StringID("ScenePrimitives"), copyBytes(shaderPrimitives)));
    if (!primitivesResource.isValid()) {
      diagnostic =
          "software scene acceleration failed to register ScenePrimitives";
      return std::nullopt;
    }

    RenderFeatureDerivedResourceResult result;
    result.descriptorResources.emplace_back(bvhNodesResource.get());
    result.descriptorResources.emplace_back(primitivesResource.get());
    return result;
  } catch (const std::exception &error) {
    diagnostic =
        std::string("software scene acceleration build failed: ") +
        error.what();
    return std::nullopt;
  }
}

} // namespace

std::optional<RenderFeatureDerivedResourceResult>
RenderFeatureDerivedResourceProducerRegistry::build(
    const RenderFeatureDerivedResourceRequest &request,
    std::string &diagnostic) {
  diagnostic.clear();
  if (request.feature == nullptr) {
    diagnostic = "derived resource request has no RenderFeature";
    return std::nullopt;
  }
  if (request.resource == nullptr) {
    diagnostic = "derived resource request has no resource requirement";
    return std::nullopt;
  }
  if (request.sceneResources == nullptr) {
    diagnostic = "derived resource request has no SceneResourceTable";
    return std::nullopt;
  }

  if (isSoftwareSceneAccelerationRequest(*request.resource)) {
    return buildSoftwareSceneAcceleration(request, diagnostic);
  }

  diagnostic =
      "no derived resource producer registered for RenderFeature resource";
  return std::nullopt;
}

} // namespace LX_core
