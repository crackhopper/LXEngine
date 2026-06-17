#include "core/frame_graph/scene_descriptor_resource_resolver.hpp"

#include "core/asset/material_instance.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace LX_core {

namespace {

class SceneStorageBufferResource final : public IGpuResource {
public:
  SceneStorageBufferResource(StringID bindingName, std::vector<std::byte> bytes)
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

bool shaderConsumesIbl(const IShaderSharedPtr &shader) {
  if (!shader) {
    return false;
  }
  for (const auto &binding : shader->getReflectionBindings()) {
    const std::string_view name(binding.name);
    if (name == "SkyboxMap" || name == "IrradianceMap" ||
        name == "PrefilteredEnvMap" || name == "BrdfLut" ||
        name == "EnvironmentUBO") {
      return true;
    }
  }
  return false;
}

[[nodiscard]] u32 bindingSortKey(const ShaderResourceBinding &binding) {
  return (binding.set << 16u) | binding.binding;
}

void appendSortedSceneMaterialResources(DescriptorResourceList &out,
                                        const MaterialInstance &material,
                                        const IShaderSharedPtr &shader,
                                        const SceneResourceTable &resources) {
  if (!shader) {
    return;
  }

  std::vector<std::pair<u32, DescriptorResourceRef>> sorted;
  for (const auto &binding : shader->getReflectionBindings()) {
    if (!isMaterialOwnedBinding(binding.name)) {
      continue;
    }

    const StringID bindingId(binding.name);
    if (binding.type == ShaderPropertyType::UniformBuffer ||
        binding.type == ShaderPropertyType::StorageBuffer) {
      auto resource = material.getShaderBindingResource(bindingId);
      if (!resource.isValid()) {
        throw std::logic_error("SceneDescriptorResourceResolver missing "
                               "material buffer resource '" +
                               binding.name + "'");
      }
      sorted.emplace_back(bindingSortKey(binding),
                          DescriptorResourceRef{resource.get()});
      continue;
    }

    if (binding.type == ShaderPropertyType::Texture2D ||
        binding.type == ShaderPropertyType::TextureCube) {
      const TextureHandle handle = material.getTextureHandle(bindingId);
      if (!handle.isValid()) {
        continue;
      }
      auto resolved = resources.resolve(handle);
      if (!resolved) {
        throw std::logic_error("SceneDescriptorResourceResolver cannot resolve "
                               "material texture '" +
                               binding.name + "'");
      }
      const auto &tableTexture = resolved->get();
      sorted.emplace_back(bindingSortKey(binding),
                          DescriptorResourceRef{tableTexture});
    }
  }

  std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  out.reserve(out.size() + sorted.size());
  for (auto &[_, resource] : sorted) {
    out.push_back(std::move(resource));
  }
}

void appendRenderableSystemResources(
    DescriptorResourceList &out,
    const ValidatedRenderablePassData &renderable) {
  if (!renderable.shaderInfo || !renderable.bonesResource.isValid()) {
    return;
  }
  for (const auto &binding : renderable.shaderInfo->getReflectionBindings()) {
    if (binding.name == "Bones") {
      out.emplace_back(renderable.bonesResource.get());
      return;
    }
  }
}

[[nodiscard]] bool shaderConsumesBinding(const IShaderSharedPtr &shader,
                                         std::string_view bindingName) {
  if (!shader) {
    return false;
  }
  return std::any_of(shader->getReflectionBindings().begin(),
                     shader->getReflectionBindings().end(),
                     [&](const ShaderResourceBinding &binding) {
                       return binding.name == bindingName;
                     });
}

[[nodiscard]] std::optional<SceneGpuDrawRecord>
findDrawRecordForRenderable(const SceneResourceTableUploadView &uploadView,
                            ObjectHandle objectHandle) {
  if (!objectHandle.isValid()) {
    return std::nullopt;
  }

  const auto objectIt = std::find_if(
      uploadView.objectIndexByHandle.begin(), uploadView.objectIndexByHandle.end(),
      [objectHandle](const SceneResourceObjectUploadIndex &entry) {
        return entry.handle == objectHandle;
      });
  if (objectIt == uploadView.objectIndexByHandle.end()) {
    return std::nullopt;
  }

  const auto drawIt = std::find_if(
      uploadView.draws.begin(), uploadView.draws.end(),
      [objectIndex = objectIt->typedIndex](const SceneGpuDrawRecord &draw) {
        return draw.objectIndex == objectIndex;
      });
  if (drawIt == uploadView.draws.end()) {
    return std::nullopt;
  }
  return *drawIt;
}

GpuResourceRef addStorageResource(const SceneResourceTable &resources,
                                  StringID bindingName,
                                  std::vector<std::byte> bytes) {
  const GpuResourceRef resource = resources.addRenderGpuResource(
      std::make_unique<SceneStorageBufferResource>(bindingName,
                                                   std::move(bytes)));
  return resource;
}

template <typename T>
GpuResourceRef addSingleRecordStorageResource(const SceneResourceTable &resources,
                                              StringID bindingName,
                                              const T &record) {
  std::vector<std::byte> bytes(sizeof(T));
  std::memcpy(bytes.data(), &record, bytes.size());
  return addStorageResource(resources, bindingName, std::move(bytes));
}

void appendRenderableSourceMaterialResources(
    DescriptorResourceList &out, const SceneResourceTable &resources,
    const SceneResourceTableUploadView &uploadView,
    const SceneGpuDrawRecord &drawRecord,
    const ValidatedRenderablePassData &renderable) {
  if (drawRecord.materialRefIndex == u32_max ||
      drawRecord.materialRefIndex >= uploadView.materialRefs.size()) {
    return;
  }
  const bool needsMaterialRefs =
      shaderConsumesBinding(renderable.shaderInfo, "SceneMaterialRefs");
  const bool needsSourceRecords =
      shaderConsumesBinding(renderable.shaderInfo, "SceneSourceMaterialRecords");
  if (!needsMaterialRefs && !needsSourceRecords) {
    return;
  }

  const SceneGpuMaterialRefRecord &globalRef =
      uploadView.materialRefs[drawRecord.materialRefIndex];
  if (globalRef.sourceStorageIndex >= uploadView.sourceMaterialStorages.size()) {
    return;
  }
  const SceneSourceLocalMaterialStorageView &storage =
      uploadView.sourceMaterialStorages[globalRef.sourceStorageIndex];
  if (globalRef.sourceLocalMaterialIndex >= storage.recordCount) {
    return;
  }
  const u32 globalSourceRecordIndex =
      storage.recordOffset + globalRef.sourceLocalMaterialIndex;
  if (globalSourceRecordIndex >= uploadView.sourceMaterialRecords.size()) {
    return;
  }

  if (needsMaterialRefs) {
    const SceneGpuMaterialRefRecord localRef{
        .sourceStorageIndex = 0u,
        .sourceLocalMaterialIndex = 0u,
    };
    const GpuResourceRef resource = addSingleRecordStorageResource(
        resources, StringID("SceneMaterialRefs"), localRef);
    if (resource.isValid()) {
      out.emplace_back(resource.get());
    }
  }
  if (needsSourceRecords) {
    const SourceLocalMaterialRecord &sourceRecord =
        uploadView.sourceMaterialRecords[globalSourceRecordIndex];
    std::vector<std::byte> bytes(sourceRecord.bytes.size());
    if (!bytes.empty()) {
      std::memcpy(bytes.data(), sourceRecord.bytes.data(), bytes.size());
    }
    const GpuResourceRef resource = addStorageResource(
        resources, StringID("SceneSourceMaterialRecords"), std::move(bytes));
    if (resource.isValid()) {
      out.emplace_back(resource.get());
    }
  }
}

void appendRenderableSceneDrawResources(
    DescriptorResourceList &out, const SceneResourceTable &resources,
    const ValidatedRenderablePassData &renderable) {
  const bool needsSceneDraws =
      shaderConsumesBinding(renderable.shaderInfo, "SceneDraws");
  const bool needsSourceMaterial =
      shaderConsumesBinding(renderable.shaderInfo, "SceneMaterialRefs") ||
      shaderConsumesBinding(renderable.shaderInfo, "SceneSourceMaterialRecords");
  if (!needsSceneDraws && !needsSourceMaterial) {
    return;
  }

  const SceneResourceTableUploadView uploadView = resources.buildUploadView();
  std::optional<SceneGpuDrawRecord> drawRecord =
      findDrawRecordForRenderable(uploadView, renderable.objectHandle);
  if (!drawRecord.has_value()) {
    return;
  }

  appendRenderableSourceMaterialResources(out, resources, uploadView,
                                          *drawRecord, renderable);
  if (drawRecord->materialRefIndex != u32_max && needsSourceMaterial) {
    drawRecord->materialRefIndex = 0u;
  }
  if (!needsSceneDraws) {
    return;
  }
  const GpuResourceRef resource = addSingleRecordStorageResource(
      resources, StringID("SceneDraws"), *drawRecord);
  if (resource.isValid()) {
    out.emplace_back(resource.get());
  }
}

} // namespace

DescriptorResourceList
buildSceneMaterialDescriptorResources(const SceneResourceTable &resources,
                                      MaterialHandle material,
                                      const IShaderSharedPtr &shader) {
  DescriptorResourceList out;
  if (const auto resolved = resources.resolve(material)) {
    appendSortedSceneMaterialResources(out, resolved->get(), shader, resources);
  }
  return out;
}

DescriptorResourceList
buildSceneDescriptorResources(const SceneDescriptorResourceContext &context) {
  DescriptorResourceList out;
  if (const auto material = context.scene.resources().resolve(
          context.renderable.materialHandle)) {
    appendSortedSceneMaterialResources(out, material->get(),
                                       context.renderable.shaderInfo,
                                       context.scene.resources());
  }
  appendRenderableSystemResources(out, context.renderable);
  appendRenderableSceneDrawResources(out, context.scene.resources(),
                                     context.renderable);

  out.insert(out.end(), context.sceneResources.begin(),
             context.sceneResources.end());
  if (shaderConsumesIbl(context.renderable.shaderInfo)) {
    auto iblResources = context.scene.resources().getIblEnvironmentResources();
    out.reserve(out.size() + iblResources.size());
    for (auto &resource : iblResources) {
      if (resource.isValid()) {
        out.emplace_back(resource.get());
      }
    }
  }
  return out;
}

} // namespace LX_core
