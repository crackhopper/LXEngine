#include "core/frame_graph/scene_descriptor_resource_resolver.hpp"

#include "core/asset/material_instance.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace LX_core {

namespace {

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
      auto resource = material.getParameterResource(bindingId);
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
