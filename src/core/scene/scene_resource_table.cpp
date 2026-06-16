#include "core/scene/scene_resource_table.hpp"

#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/skeleton.hpp"
#include "core/asset/texture.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

namespace LX_core {
namespace {

[[nodiscard]] u32 nextGeneration(const u32 current) {
  const u32 next = current + 1;
  return next == 0 ? 1 : next;
}

[[nodiscard]] u64 nextGeneration(const u64 current) {
  const u64 next = current + 1;
  return next == 0 ? 1 : next;
}

[[nodiscard]] bool sameVec4(const Vec4f &lhs, const Vec4f &rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] bool
sameStorageFieldLayout(const MaterialContractStorageField &lhs,
                       const MaterialContractStorageField &rhs) {
  return lhs.name == rhs.name && lhs.type == rhs.type &&
         lhs.inputKind == rhs.inputKind &&
         lhs.parameterName == rhs.parameterName &&
         lhs.defaultTextureSemantic == rhs.defaultTextureSemantic &&
         sameVec4(lhs.defaultValue, rhs.defaultValue) &&
         lhs.defaultChannel == rhs.defaultChannel;
}

[[nodiscard]] bool
sameSourceStorageLayout(const MaterialContractReflection &lhs,
                        const MaterialContractReflection &rhs) {
  if (lhs.storageFields.size() != rhs.storageFields.size()) {
    return false;
  }
  for (usize i = 0; i < lhs.storageFields.size(); ++i) {
    if (!sameStorageFieldLayout(lhs.storageFields[i], rhs.storageFields[i])) {
      return false;
    }
  }
  return true;
}

struct CompactRecordIndex final {
  u32 generation = 0;
  u32 uploadIndex = u32_max;
};

[[nodiscard]] u32
findCompactRecordIndex(const std::vector<CompactRecordIndex> &indices,
                       const ResourceHandleBase &handle) {
  if (!handle.isValid() || handle.index >= indices.size()) {
    return u32_max;
  }
  const auto &entry = indices[handle.index];
  if (entry.generation != handle.generation) {
    return u32_max;
  }
  return entry.uploadIndex;
}

[[nodiscard]] std::string
missingRenderPathGraphDependencyMessage(const ResourceUri &graphUri,
                                        const char *dependencyType,
                                        const ResourceUri &dependencyUri) {
  return "RenderPathGraph '" + graphUri.string() + "' references missing " +
         dependencyType + " resource '" + dependencyUri.string() + "'";
}

[[nodiscard]] bool hasLiveShaderPayload(const ShaderResourceMetadata &shader) {
  if (!shader.payload) {
    return false;
  }
  if (shader.payload->getAllStages().empty()) {
    return false;
  }
  for (const ShaderStageCode &stage : shader.payload->getAllStages()) {
    if (stage.stage == ShaderStage::None || stage.bytecode.empty()) {
      return false;
    }
  }
  return !shader.payload->getReflectionBindings().empty() ||
         !shader.payload->getVertexInputs().empty();
}

[[nodiscard]] bool
hasResolvedMaterialSourceVariantPayload(const ShaderResourceMetadata &shader) {
  for (const auto &variant : shader.materialSourceVariants) {
    const auto shaderPayload = variant.shaderProgram.getShader();
    if (!shaderPayload) {
      continue;
    }
    if (shaderPayload->getAllStages().empty()) {
      continue;
    }
    if (!shaderPayload->getReflectionBindings().empty() ||
        !shaderPayload->getVertexInputs().empty()) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool
isShaderDependencyResolved(const ShaderResourceMetadata &shader) {
  if (!shader.sourceResolved || shader.sourceUris.empty()) {
    return false;
  }
  return hasLiveShaderPayload(shader) || shader.requiresMaterialSourceVariant;
}

[[nodiscard]] ResourceState
shaderMetadataState(const ShaderResourceMetadata &shader) {
  return isShaderDependencyResolved(shader) ? ResourceState::Ready
                                            : ResourceState::Unloaded;
}

[[nodiscard]] std::string
missingShaderPayloadMessage(const ResourceUri &graphUri,
                            const ResourceUri &shaderUri) {
  return "RenderPathGraph '" + graphUri.string() + "' references Shader '" +
         shaderUri.string() +
         "' with resolved source descriptors but no live compiled/reflected "
         "payload";
}

[[nodiscard]] const ResourceUri &defaultWhiteTextureUri() {
  static const ResourceUri uri("builtin://textures/default/white");
  return uri;
}

[[nodiscard]] const ResourceUri &defaultBlackTextureUri() {
  static const ResourceUri uri("builtin://textures/default/black");
  return uri;
}

[[nodiscard]] const ResourceUri &defaultFlatNormalTextureUri() {
  static const ResourceUri uri("builtin://textures/default/flat-normal");
  return uri;
}

[[nodiscard]] CombinedTextureSamplerUniquePtr
makeSolidDefaultTexture(u8 r, u8 g, u8 b, u8 a,
                        TextureContent content = TextureContent::Color) {
  auto texture = std::make_shared<Texture>(
      TextureDesc{1, 1, TextureFormat::RGBA8, content},
      std::vector<u8>{r, g, b, a});
  return std::make_unique<CombinedTextureSampler>(std::move(texture));
}

[[nodiscard]] const char *sceneResourceTypeName(SceneResourceType type) {
  switch (type) {
  case SceneResourceType::Mesh:
    return "Mesh";
  case SceneResourceType::Texture:
    return "Texture";
  case SceneResourceType::Material:
    return "Material";
  case SceneResourceType::MaterialHeader:
    return "MaterialHeader";
  case SceneResourceType::Spectrum:
    return "Spectrum";
  case SceneResourceType::BsdfTable:
    return "BsdfTable";
  case SceneResourceType::Camera:
    return "Camera";
  case SceneResourceType::Light:
    return "Light";
  case SceneResourceType::Renderer:
    return "Renderer";
  case SceneResourceType::RenderPathGraph:
    return "RenderPathGraph";
  case SceneResourceType::RenderFeature:
    return "RenderFeature";
  case SceneResourceType::Shader:
    return "Shader";
  case SceneResourceType::RenderEffect:
    return "RenderEffect";
  case SceneResourceType::Unknown:
    break;
  }
  return "Unknown";
}

[[nodiscard]] const VertexLayoutItem *
findVertexLayoutItem(const VertexLayout &layout, const char *name,
                     const u32 fallbackLocation) {
  for (const auto &item : layout.getItems()) {
    if (item.name == name) {
      return &item;
    }
  }
  for (const auto &item : layout.getItems()) {
    if (item.location == fallbackLocation) {
      return &item;
    }
  }
  return nullptr;
}

[[nodiscard]] CameraData::Param
makeCameraDataParam(const CameraResource &camera) {
  return CameraData::Param{
      .view = camera.view,
      .proj = camera.proj,
      .eyePos = camera.pose.eye,
      .pad = 0.0f,
  };
}

[[nodiscard]] Vec4f readVertexAttribute(const u8 *vertex,
                                        const VertexLayoutItem &item,
                                        Vec4f fallback) {
  const auto *attribute = vertex + item.offset;
  switch (item.type) {
  case DataType::Float1: {
    f32 x = 0.0f;
    std::memcpy(&x, attribute, sizeof(f32));
    fallback.x = x;
    return fallback;
  }
  case DataType::Float2: {
    f32 values[2]{};
    std::memcpy(values, attribute, sizeof(values));
    fallback.x = values[0];
    fallback.y = values[1];
    return fallback;
  }
  case DataType::Float3: {
    f32 values[3]{};
    std::memcpy(values, attribute, sizeof(values));
    fallback.x = values[0];
    fallback.y = values[1];
    fallback.z = values[2];
    return fallback;
  }
  case DataType::Float4: {
    f32 values[4]{};
    std::memcpy(values, attribute, sizeof(values));
    return {values[0], values[1], values[2], values[3]};
  }
  case DataType::Int4:
    return fallback;
  }
  return fallback;
}

void appendMeshGeometryRecords(
    const MeshBuffer &mesh, const GeometryStorage &storage,
    const u32 uploadVertexOffset, std::vector<Vec4f> &positions,
    std::vector<SceneGpuAttributeStreamRecord> &attributeStreams,
    std::vector<Vec4f> &attributeValues, std::vector<u32> &indices) {
  const auto &vertexBuffer = storage.getVertexBuffer();
  const auto &layout = vertexBuffer.getLayout();
  const auto stride = layout.getStride();
  const auto *rawVertices = static_cast<const u8 *>(vertexBuffer.getRawData());
  if (rawVertices != nullptr && stride != 0) {
    const u32 firstVertex = mesh.getVertexOffset();
    const u32 vertexCount = mesh.getVertexCount();
    positions.reserve(positions.size() + vertexCount);
    const auto *positionItem = findVertexLayoutItem(layout, "inPos", 0);
    for (u32 i = 0; i < vertexCount; ++i) {
      const auto *vertex = rawVertices + (firstVertex + i) * stride;
      Vec4f position{0.0f, 0.0f, 0.0f, 1.0f};
      if (positionItem != nullptr) {
        position = readVertexAttribute(vertex, *positionItem, position);
        position.w = 1.0f;
      }
      positions.push_back(position);
    }

    const auto appendAttributeStream = [&](const char *name,
                                           u32 fallbackLocation, u32 semantic,
                                           u32 components, Vec4f fallback) {
      const auto *item = findVertexLayoutItem(layout, name, fallbackLocation);
      if (item == nullptr) {
        return;
      }
      const u32 valueOffset = static_cast<u32>(attributeValues.size());
      attributeValues.reserve(attributeValues.size() + vertexCount);
      for (u32 i = 0; i < vertexCount; ++i) {
        const auto *vertex = rawVertices + (firstVertex + i) * stride;
        attributeValues.push_back(readVertexAttribute(vertex, *item, fallback));
      }
      attributeStreams.push_back(SceneGpuAttributeStreamRecord{
          .semantic = semantic,
          .valueOffset = valueOffset,
          .valueCount = vertexCount,
          .components = components,
      });
    };

    appendAttributeStream("inNormal", 1, kSceneGpuAttributeSemanticNormal0, 3,
                          Vec4f{0.0f, 0.0f, 1.0f, 0.0f});
    appendAttributeStream("inUV", 2, kSceneGpuAttributeSemanticUv0, 2,
                          Vec4f{0.0f, 0.0f, 0.0f, 0.0f});
    appendAttributeStream("inTangent", 3, kSceneGpuAttributeSemanticTangent0, 4,
                          Vec4f{1.0f, 0.0f, 0.0f, 1.0f});
  }

  const auto &indexBuffer = storage.getIndexBuffer();
  const auto *rawIndices = static_cast<const u32 *>(indexBuffer.getRawData());
  if (rawIndices != nullptr) {
    const u32 firstIndex = mesh.getIndexOffset();
    const u32 indexCount = mesh.getIndexCount();
    const u32 firstVertex = mesh.getVertexOffset();
    indices.reserve(indices.size() + indexCount);
    for (u32 i = 0; i < indexCount; ++i) {
      indices.push_back(uploadVertexOffset + rawIndices[firstIndex + i] -
                        firstVertex);
    }
  }
}

[[nodiscard]] bool isMeshSliceValid(const MeshBuffer &mesh,
                                    const GeometryStorage &storage) {
  const auto &vertexBuffer = storage.getVertexBuffer();
  const auto &indexBuffer = storage.getIndexBuffer();
  const u32 vertexOffset = mesh.getVertexOffset();
  const u32 vertexCount = mesh.getVertexCount();
  const u32 indexOffset = mesh.getIndexOffset();
  const u32 indexCount = mesh.getIndexCount();
  if (indexBuffer.getTopology() != PrimitiveTopology::TriangleList) {
    return false;
  }
  if (vertexBuffer.getRawData() == nullptr ||
      indexBuffer.getRawData() == nullptr ||
      vertexBuffer.getLayout().getStride() == 0 || vertexCount == 0 ||
      indexCount == 0 || indexCount % 3 != 0) {
    return false;
  }

  const auto totalVertexCount = vertexBuffer.getVertexCount();
  if (vertexOffset > totalVertexCount ||
      static_cast<usize>(vertexCount) > totalVertexCount - vertexOffset) {
    return false;
  }

  const auto totalIndexCount = indexBuffer.indexCount();
  if (indexOffset > totalIndexCount ||
      static_cast<usize>(indexCount) > totalIndexCount - indexOffset) {
    return false;
  }

  const auto *rawIndices = static_cast<const u32 *>(indexBuffer.getRawData());
  const u32 vertexEnd = vertexOffset + vertexCount;
  for (u32 i = 0; i < indexCount; ++i) {
    const u32 index = rawIndices[indexOffset + i];
    if (index < vertexOffset || index >= vertexEnd) {
      return false;
    }
  }
  return true;
}

} // namespace

SceneResourceTable::SceneResourceTable() {
  (void)registerTexture(defaultWhiteTextureUri(),
                        makeSolidDefaultTexture(255, 255, 255, 255));
  (void)registerTexture(defaultBlackTextureUri(),
                        makeSolidDefaultTexture(0, 0, 0, 255));
  (void)registerTexture(
      defaultFlatNormalTextureUri(),
      makeSolidDefaultTexture(128, 128, 255, 255, TextureContent::Normal));
}
SceneResourceTable::~SceneResourceTable() = default;
SceneResourceTable::SceneResourceTable(SceneResourceTable &&) noexcept =
    default;
SceneResourceTable &
SceneResourceTable::operator=(SceneResourceTable &&) noexcept = default;

template <typename Resource, typename Handle>
Handle SceneResourceTable::add(std::vector<Entry<Resource>> &entries,
                               std::unique_ptr<Resource> resource) {
  assert(resource && "SceneResourceTable cannot register null resource");
  for (u32 i = 0; i < entries.size(); ++i) {
    auto &entry = entries[i];
    if (entry.state == SceneResourceEntryState::Alive) {
      continue;
    }
    entry.resource = std::move(resource);
    entry.metadataHandle = {};
    entry.generation = nextGeneration(entry.generation);
    entry.state = SceneResourceEntryState::Alive;
    Handle handle;
    handle.index = i;
    handle.generation = entry.generation;
    return handle;
  }

  Entry<Resource> entry;
  entry.resource = std::move(resource);
  entry.generation = 1;
  entry.state = SceneResourceEntryState::Alive;
  entries.push_back(std::move(entry));

  Handle handle;
  handle.index = static_cast<u32>(entries.size() - 1);
  handle.generation = 1;
  return handle;
}

template <typename Resource, typename Handle>
void SceneResourceTable::release(std::vector<Entry<Resource>> &entries,
                                 Handle handle) {
  if (!isAlive(entries, handle)) {
    return;
  }
  auto &entry = entries[handle.index];
  entry.resource.reset();
  entry.metadataHandle = {};
  entry.generation = nextGeneration(entry.generation);
  entry.state = SceneResourceEntryState::PendingRelease;
}

template <typename Resource, typename Handle>
std::optional<std::reference_wrapper<Resource>>
SceneResourceTable::resolveMutable(std::vector<Entry<Resource>> &entries,
                                   Handle handle) {
  if (!isAlive(entries, handle)) {
    return std::nullopt;
  }
  return std::ref(*entries[handle.index].resource);
}

template <typename Resource, typename Handle>
std::optional<std::reference_wrapper<const Resource>>
SceneResourceTable::resolveConst(const std::vector<Entry<Resource>> &entries,
                                 Handle handle) const {
  if (!isAlive(entries, handle)) {
    return std::nullopt;
  }
  return std::cref(*entries[handle.index].resource);
}

template <typename Resource, typename Handle>
bool SceneResourceTable::isAlive(const std::vector<Entry<Resource>> &entries,
                                 Handle handle) const {
  if (!handle.isValid() || handle.index >= entries.size()) {
    return false;
  }
  const auto &entry = entries[handle.index];
  return entry.state == SceneResourceEntryState::Alive &&
         entry.generation == handle.generation && entry.resource;
}

template <typename Resource>
usize SceneResourceTable::aliveCount(
    const std::vector<Entry<Resource>> &entries) const {
  usize count = 0;
  for (const auto &entry : entries) {
    if (entry.state == SceneResourceEntryState::Alive && entry.resource) {
      ++count;
    }
  }
  return count;
}

u32 SceneResourceTable::registerUploadTexture(TextureHandle texture) const {
  if (!isAlive(texture)) {
    return u32_max;
  }
  const auto *textureResource = m_textures[texture.index].resource.get();
  for (u32 i = 0; i < m_gpuTextures.size(); ++i) {
    if (&m_gpuTextures[i].get() == textureResource) {
      return i;
    }
  }
  m_gpuTextures.push_back(std::cref(*textureResource));
  const u32 typedIndex = static_cast<u32>(m_gpuTextures.size() - 1u);
  m_gpuTextureIndexByHandle.push_back(SceneResourceTextureUploadIndex{
      .handle = texture,
      .typedIndex = typedIndex,
  });
  return typedIndex;
}

void SceneResourceTable::advanceUploadGeneration() {
  m_generation = nextGeneration(m_generation);
}

ResourceIdentityHandle
SceneResourceTable::metadataHandleFor(MeshHandle handle) const {
  if (!isAlive(handle)) {
    return {};
  }
  return m_meshes[handle.index].metadataHandle;
}

ResourceIdentityHandle
SceneResourceTable::metadataHandleFor(MaterialHandle handle) const {
  if (!isAlive(handle)) {
    return {};
  }
  return m_materials[handle.index].metadataHandle;
}

ResourceIdentityHandle
SceneResourceTable::metadataHandleFor(TextureHandle handle) const {
  if (!isAlive(handle)) {
    return {};
  }
  return m_textures[handle.index].metadataHandle;
}

ResourceIdentityHandle
SceneResourceTable::metadataHandleFor(RenderPathGraphHandle handle) const {
  if (!isAlive(handle)) {
    return {};
  }
  return m_renderPathGraphs[handle.index].metadataHandle;
}

ResourceIdentityHandle
SceneResourceTable::metadataHandleFor(RenderFeatureHandle handle) const {
  if (!isAlive(handle)) {
    return {};
  }
  return m_renderFeatures[handle.index].metadataHandle;
}

ResourceIdentityHandle
SceneResourceTable::metadataHandleFor(ShaderHandle handle) const {
  if (!isAlive(handle)) {
    return {};
  }
  return m_shaders[handle.index].metadataHandle;
}

ResourceMetadata &
SceneResourceTable::mutableMetadata(ResourceIdentityHandle handle) {
  if (!handle.isValid() || handle.index >= m_resourceMetadata.size() ||
      handle.index >= m_resourceMetadataGenerations.size() ||
      m_resourceMetadataGenerations[handle.index] != handle.generation) {
    throw std::out_of_range("invalid scene resource metadata handle");
  }
  return m_resourceMetadata[handle.index];
}

const ResourceMetadata &
SceneResourceTable::constMetadata(ResourceIdentityHandle handle) const {
  if (!handle.isValid() || handle.index >= m_resourceMetadata.size() ||
      handle.index >= m_resourceMetadataGenerations.size() ||
      m_resourceMetadataGenerations[handle.index] != handle.generation) {
    throw std::out_of_range("invalid scene resource metadata handle");
  }
  return m_resourceMetadata[handle.index];
}

bool SceneResourceTable::hasLiveTypedResourceMetadata(
    ResourceIdentityHandle handle) const {
  const ResourceMetadata *metadata = findResourceMetadata(handle);
  if (metadata == nullptr) {
    return false;
  }

  const auto hasLiveEntry = [handle](const auto &entries) {
    for (const auto &entry : entries) {
      if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
          entry.metadataHandle == handle) {
        return true;
      }
    }
    return false;
  };

  switch (metadata->type) {
  case SceneResourceType::Mesh:
    return hasLiveEntry(m_meshes);
  case SceneResourceType::Texture:
    return hasLiveEntry(m_textures);
  case SceneResourceType::Material:
    return hasLiveEntry(m_materials);
  case SceneResourceType::RenderPathGraph:
    return hasLiveEntry(m_renderPathGraphs);
  case SceneResourceType::RenderFeature:
    return hasLiveEntry(m_renderFeatures);
  case SceneResourceType::Shader:
    for (const auto &entry : m_shaders) {
      if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
          entry.metadataHandle == handle) {
        return isShaderDependencyResolved(*entry.resource);
      }
    }
    return false;
  case SceneResourceType::Unknown:
  case SceneResourceType::MaterialHeader:
  case SceneResourceType::Spectrum:
  case SceneResourceType::BsdfTable:
  case SceneResourceType::Camera:
  case SceneResourceType::Light:
  case SceneResourceType::Renderer:
  case SceneResourceType::RenderEffect:
    return true;
  }
  return true;
}

ResourceIdentityHandle
SceneResourceTable::internResourceMetadata(ResourceMetadata metadata) {
  for (u32 i = 0; i < m_resourceMetadata.size(); ++i) {
    auto &entry = m_resourceMetadata[i];
    if (entry.type == metadata.type && entry.uri == metadata.uri) {
      if (metadata.state == ResourceState::Failed) {
        entry.state = ResourceState::Failed;
        entry.diagnostics.insert(entry.diagnostics.end(),
                                 metadata.diagnostics.begin(),
                                 metadata.diagnostics.end());
        ++entry.version;
      }
      return ResourceIdentityHandle{i, m_resourceMetadataGenerations[i]};
    }
  }
  metadata.generation = 1;
  m_resourceMetadata.push_back(std::move(metadata));
  m_resourceMetadataGenerations.push_back(1);
  return ResourceIdentityHandle{
      static_cast<u32>(m_resourceMetadata.size() - 1u), 1};
}

const ResourceMetadata *
SceneResourceTable::findResourceMetadata(ResourceIdentityHandle handle) const {
  if (!handle.isValid() || handle.index >= m_resourceMetadata.size() ||
      handle.index >= m_resourceMetadataGenerations.size() ||
      m_resourceMetadataGenerations[handle.index] != handle.generation) {
    return nullptr;
  }
  return &m_resourceMetadata[handle.index];
}

ResourceUri SceneResourceTable::resolveUri(const ResourceUri &baseUri,
                                           const ResourceUri &uri) const {
  return ResourceUri::canonicalize(baseUri.string(), uri.string());
}

ResourceIdentityHandle
SceneResourceTable::loadOrGetResource(SceneResourceType type,
                                      const ResourceUri &canonicalUri) {
  ResourceMetadata metadata;
  metadata.type = type;
  metadata.uri = canonicalUri;
  metadata.state = ResourceState::Ready;
  return internResourceMetadata(std::move(metadata));
}

void SceneResourceTable::registerDependency(
    ResourceIdentityHandle ownerHandle,
    ResourceIdentityHandle dependencyHandle) {
  addDependency(ownerHandle, dependencyHandle);
}

void SceneResourceTable::addDependency(
    ResourceIdentityHandle ownerHandle,
    ResourceIdentityHandle dependencyHandle) {
  if (!ownerHandle.isValid() || !dependencyHandle.isValid()) {
    return;
  }
  if (ownerHandle.index >= m_resourceMetadata.size() ||
      ownerHandle.index >= m_resourceMetadataGenerations.size() ||
      dependencyHandle.index >= m_resourceMetadata.size() ||
      dependencyHandle.index >= m_resourceMetadataGenerations.size()) {
    return;
  }
  if (m_resourceMetadataGenerations[ownerHandle.index] !=
          ownerHandle.generation ||
      m_resourceMetadataGenerations[dependencyHandle.index] !=
          dependencyHandle.generation) {
    return;
  }

  auto &dependencies = m_resourceMetadata[ownerHandle.index].dependencies;
  const ResourceUri &dependencyUri =
      m_resourceMetadata[dependencyHandle.index].uri;
  if (std::find(dependencies.begin(), dependencies.end(), dependencyUri) ==
      dependencies.end()) {
    dependencies.push_back(dependencyUri);
  }

  auto &dependencyHandles =
      m_resourceMetadata[ownerHandle.index].dependencyHandles;
  if (std::find(dependencyHandles.begin(), dependencyHandles.end(),
                dependencyHandle) == dependencyHandles.end()) {
    dependencyHandles.push_back(dependencyHandle);
  }

  auto &dependents = m_resourceMetadata[dependencyHandle.index].dependents;
  if (std::find(dependents.begin(), dependents.end(), ownerHandle) ==
      dependents.end()) {
    dependents.push_back(ownerHandle);
  }
}

void SceneResourceTable::addDependency(ResourceIdentityHandle ownerHandle,
                                       RenderPathGraphHandle dependencyHandle) {
  addDependency(ownerHandle, metadataHandleFor(dependencyHandle));
}

void SceneResourceTable::addDependency(RenderPathGraphHandle ownerHandle,
                                       RenderFeatureHandle dependencyHandle) {
  addDependency(metadataHandleFor(ownerHandle),
                metadataHandleFor(dependencyHandle));
}

void SceneResourceTable::addDependency(RenderPathGraphHandle ownerHandle,
                                       ShaderHandle dependencyHandle) {
  addDependency(metadataHandleFor(ownerHandle),
                metadataHandleFor(dependencyHandle));
}

void SceneResourceTable::addDependency(MaterialHandle ownerHandle,
                                       TextureHandle dependencyHandle) {
  addDependency(metadataHandleFor(ownerHandle),
                metadataHandleFor(dependencyHandle));
}

void SceneResourceTable::markDirty(ResourceIdentityHandle handle,
                                   std::string reason) {
  std::vector<ResourceIdentityHandle> visited;
  const ResourceIdentityHandle dirtyRoot = handle;
  const auto appendDiagnostic = [this, &reason,
                                 dirtyRoot](ResourceMetadata &metadata,
                                            const char *prefix) {
    const ResourceUri resourceUri =
        dirtyRoot.isValid() ? constMetadata(dirtyRoot).uri : metadata.uri;
    metadata.diagnostics.push_back(ResourceDiagnostic{
        .ownerUri = metadata.uri,
        .resourceUri = resourceUri,
        .parserName = "SceneResourceTable",
        .message = std::string(prefix) + reason,
    });
  };

  const std::function<void(ResourceIdentityHandle, bool)> visit =
      [&](ResourceIdentityHandle current, bool root) {
        if (!current.isValid() || std::find(visited.begin(), visited.end(),
                                            current) != visited.end()) {
          return;
        }
        visited.push_back(current);

        ResourceMetadata &entry = mutableMetadata(current);
        entry.version = nextGeneration(entry.version);
        if (!root) {
          entry.state = ResourceState::Dirty;
        }
        appendDiagnostic(entry, root ? "" : "dependency dirty: ");

        const auto dependents = entry.dependents;
        for (const ResourceIdentityHandle dependent : dependents) {
          visit(dependent, false);
        }
      };

  visit(handle, true);
}

void SceneResourceTable::markDirty(TextureHandle handle, std::string reason) {
  markDirty(metadataHandleFor(handle), std::move(reason));
}

void SceneResourceTable::markDirty(RenderFeatureHandle handle,
                                   std::string reason) {
  markDirty(metadataHandleFor(handle), std::move(reason));
}

void SceneResourceTable::markDirty(ShaderHandle handle, std::string reason) {
  markDirty(metadataHandleFor(handle), std::move(reason));
}

const ResourceMetadata &
SceneResourceTable::metadata(ResourceIdentityHandle handle) const {
  return constMetadata(handle);
}

const ResourceMetadata &
SceneResourceTable::metadata(MaterialHandle handle) const {
  return constMetadata(metadataHandleFor(handle));
}

const ResourceMetadata &
SceneResourceTable::metadata(TextureHandle handle) const {
  return constMetadata(metadataHandleFor(handle));
}

const ResourceMetadata &
SceneResourceTable::metadata(RenderPathGraphHandle handle) const {
  return constMetadata(metadataHandleFor(handle));
}

const ResourceMetadata &
SceneResourceTable::metadata(RenderFeatureHandle handle) const {
  return constMetadata(metadataHandleFor(handle));
}

const ResourceMetadata &
SceneResourceTable::metadata(ShaderHandle handle) const {
  return constMetadata(metadataHandleFor(handle));
}

ResourceIdentityHandle
SceneResourceTable::metadataHandle(RenderPathGraphHandle handle) const {
  return metadataHandleFor(handle);
}

ResourceIdentityHandle
SceneResourceTable::metadataHandle(RenderFeatureHandle handle) const {
  return metadataHandleFor(handle);
}

ResourceIdentityHandle
SceneResourceTable::metadataHandle(ShaderHandle handle) const {
  return metadataHandleFor(handle);
}

ResourceIdentityHandle SceneResourceTable::internMaterialInstanceIdentity(
    const ResourceUri &sourceMaterialUri, std::string overrideHash) {
  ResourceMetadata metadata;
  metadata.type = SceneResourceType::Material;
  metadata.uri = ResourceUri(sourceMaterialUri.string() + "#" +
                             (overrideHash.empty() ? "base" : overrideHash));
  metadata.contentHash = std::move(overrideHash);
  return internResourceMetadata(std::move(metadata));
}

SceneResourceGraphExport SceneResourceTable::exportResourceGraph() const {
  SceneResourceGraphExport graph;
  graph.handles.reserve(m_resourceMetadata.size());
  graph.resources.reserve(m_resourceMetadata.size());
  for (u32 i = 0; i < m_resourceMetadata.size(); ++i) {
    if (i >= m_resourceMetadataGenerations.size() ||
        m_resourceMetadataGenerations[i] == 0) {
      continue;
    }
    graph.handles.push_back(
        ResourceIdentityHandle{i, m_resourceMetadataGenerations[i]});
    graph.resources.push_back(m_resourceMetadata[i]);
  }

  for (const auto &owner : graph.resources) {
    for (const ResourceIdentityHandle dependencyHandle :
         owner.dependencyHandles) {
      const ResourceMetadata *dependency =
          findResourceMetadata(dependencyHandle);
      if (dependency == nullptr) {
        throw std::logic_error("resource '" + owner.uri.string() +
                               "' references stale dependency handle");
      }
      if (dependency->state == ResourceState::Failed ||
          dependency->state == ResourceState::Unloaded) {
        throw std::logic_error(
            "resource '" + owner.uri.string() + "' references " +
            std::string(sceneResourceTypeName(dependency->type)) + " '" +
            dependency->uri.string() + "' in non-uploadable state");
      }
      if (owner.type == SceneResourceType::RenderPathGraph &&
          (dependency->type == SceneResourceType::RenderFeature ||
           dependency->type == SceneResourceType::Shader) &&
          !hasLiveTypedResourceMetadata(dependencyHandle)) {
        throw std::logic_error(
            "resource '" + owner.uri.string() + "' references released " +
            std::string(sceneResourceTypeName(dependency->type)) + " '" +
            dependency->uri.string() + "'");
      }
    }
  }
  return graph;
}

GeometryStorageHandle
SceneResourceTable::registerGeometryStorage(GeometryStorageUniquePtr storage) {
  auto handle = add<GeometryStorage, GeometryStorageHandle>(m_geometryStorage,
                                                            std::move(storage));
  advanceUploadGeneration();
  return handle;
}

MeshHandle SceneResourceTable::registerMesh(MeshBufferUniquePtr mesh) {
  if (!mesh) {
    return MeshHandle{};
  }
  if (!mesh->getGeometryStorageHandle().isValid()) {
    const auto &pendingStorage = mesh->getGeometryStorage();
    if (!pendingStorage) {
      return MeshHandle{};
    }
    const GeometryStorageHandle storageHandle =
        registerGeometryStorage(pendingStorage->cloneUnique());
    mesh = mesh->cloneUniqueWithGeometryHandle(storageHandle);
  }
  auto handle = add<MeshBuffer, MeshHandle>(m_meshes, std::move(mesh));
  advanceUploadGeneration();
  return handle;
}

MeshHandle SceneResourceTable::registerMesh(const ResourceUri &uri,
                                            MeshBufferUniquePtr mesh) {
  for (u32 i = 0; i < m_meshes.size(); ++i) {
    const auto &entry = m_meshes[i];
    const ResourceMetadata *metadata =
        findResourceMetadata(entry.metadataHandle);
    if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
        metadata != nullptr && metadata->type == SceneResourceType::Mesh &&
        metadata->uri == uri) {
      return MeshHandle{i, entry.generation};
    }
  }

  const MeshHandle handle = registerMesh(std::move(mesh));
  if (handle.isValid()) {
    m_meshes[handle.index].metadataHandle =
        loadOrGetResource(SceneResourceType::Mesh, uri);
  }
  return handle;
}

MaterialHandle
SceneResourceTable::registerMaterial(MaterialInstanceUniquePtr material) {
  if (!material) {
    return MaterialHandle{};
  }
  material->forEachPendingTextureBinding(
      [this, &material](StringID bindingName,
                        const CombinedTextureSamplerSharedPtr &texture) {
        if (!texture) {
          return;
        }
        auto tableTexture = texture->cloneUnique();
        tableTexture->setBindingName(bindingName);
        const TextureHandle textureHandle =
            registerTexture(std::move(tableTexture));
        material->setTextureHandle(bindingName, textureHandle);
      });
  auto handle =
      add<MaterialInstance, MaterialHandle>(m_materials, std::move(material));
  advanceUploadGeneration();
  return handle;
}

MaterialHandle SceneResourceTable::registerMaterialInstance(
    const ResourceUri &uri, MaterialInstanceUniquePtr material) {
  for (u32 i = 0; i < m_materials.size(); ++i) {
    const auto &entry = m_materials[i];
    const ResourceMetadata *metadata =
        findResourceMetadata(entry.metadataHandle);
    if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
        metadata != nullptr && metadata->type == SceneResourceType::Material &&
        metadata->uri == uri) {
      return MaterialHandle{i, entry.generation};
    }
  }

  const MaterialHandle handle = registerMaterial(std::move(material));
  if (handle.isValid()) {
    m_materials[handle.index].metadataHandle =
        loadOrGetResource(SceneResourceType::Material, uri);
  }
  return handle;
}

TextureHandle
SceneResourceTable::registerTexture(CombinedTextureSamplerUniquePtr texture) {
  if (!texture) {
    return TextureHandle{};
  }
  auto handle = add<CombinedTextureSampler, TextureHandle>(m_textures,
                                                           std::move(texture));
  advanceUploadGeneration();
  return handle;
}

TextureHandle
SceneResourceTable::registerTexture(const ResourceUri &uri,
                                    CombinedTextureSamplerUniquePtr texture) {
  for (u32 i = 0; i < m_textures.size(); ++i) {
    const auto &entry = m_textures[i];
    const ResourceMetadata *metadata =
        findResourceMetadata(entry.metadataHandle);
    if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
        metadata != nullptr && metadata->type == SceneResourceType::Texture &&
        metadata->uri == uri) {
      return TextureHandle{i, entry.generation};
    }
  }

  const TextureHandle handle = registerTexture(std::move(texture));
  if (handle.isValid()) {
    m_textures[handle.index].metadataHandle =
        loadOrGetResource(SceneResourceType::Texture, uri);
  }
  return handle;
}

std::optional<MeshHandle>
SceneResourceTable::findMesh(const ResourceUri &uri) const {
  for (u32 i = 0; i < m_meshes.size(); ++i) {
    const auto &entry = m_meshes[i];
    const ResourceMetadata *metadata =
        findResourceMetadata(entry.metadataHandle);
    if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
        metadata != nullptr && metadata->type == SceneResourceType::Mesh &&
        metadata->uri == uri) {
      return MeshHandle{i, entry.generation};
    }
  }
  return std::nullopt;
}

std::optional<TextureHandle>
SceneResourceTable::findTexture(const ResourceUri &uri) const {
  for (u32 i = 0; i < m_textures.size(); ++i) {
    const auto &entry = m_textures[i];
    const ResourceMetadata *metadata =
        findResourceMetadata(entry.metadataHandle);
    if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
        metadata != nullptr && metadata->type == SceneResourceType::Texture &&
        metadata->uri == uri) {
      return TextureHandle{i, entry.generation};
    }
  }
  return std::nullopt;
}

LightHandle SceneResourceTable::registerLight(LightBaseUniquePtr light) {
  auto handle = add<LightBase, LightHandle>(m_lights, std::move(light));
  advanceUploadGeneration();
  return handle;
}

SkeletonHandle
SceneResourceTable::registerSkeleton(std::unique_ptr<Skeleton> skeleton) {
  auto handle = add<Skeleton, SkeletonHandle>(m_skeletons, std::move(skeleton));
  advanceUploadGeneration();
  return handle;
}

ObjectHandle SceneResourceTable::registerObject(ObjectResource object) {
  auto handle = add<ObjectResource, ObjectHandle>(
      m_objects, std::make_unique<ObjectResource>(std::move(object)));
  advanceUploadGeneration();
  return handle;
}

CameraHandle SceneResourceTable::registerCamera(CameraResource camera) {
  const CameraData::Param cameraParam = makeCameraDataParam(camera);
  auto handle = add<CameraResource, CameraHandle>(
      m_cameras, std::make_unique<CameraResource>(std::move(camera)));
  if (handle.index >= m_cameraUbos.size()) {
    m_cameraUbos.resize(static_cast<usize>(handle.index) + 1u);
  }
  auto &ubo = m_cameraUbos[handle.index];
  if (!ubo) {
    ubo = std::make_unique<CameraData>();
  }
  ubo->param = cameraParam;
  ubo->setDirty();
  advanceUploadGeneration();
  return handle;
}

RenderFeatureHandle
SceneResourceTable::registerRenderFeature(const ResourceUri &uri,
                                          RenderFeature feature) {
  for (u32 i = 0; i < m_renderFeatures.size(); ++i) {
    const auto &entry = m_renderFeatures[i];
    const ResourceMetadata *metadata =
        findResourceMetadata(entry.metadataHandle);
    if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
        metadata != nullptr &&
        metadata->type == SceneResourceType::RenderFeature &&
        metadata->uri == uri) {
      return RenderFeatureHandle{i, entry.generation};
    }
  }

  auto handle = add<RenderFeature, RenderFeatureHandle>(
      m_renderFeatures, std::make_unique<RenderFeature>(std::move(feature)));
  if (handle.isValid()) {
    m_renderFeatures[handle.index].metadataHandle =
        loadOrGetResource(SceneResourceType::RenderFeature, uri);
  }
  advanceUploadGeneration();
  return handle;
}

ShaderHandle SceneResourceTable::registerShaderResource(
    const ResourceUri &uri, std::vector<ResourceUri> sourceUris,
    IShaderSharedPtr payload, bool requiresMaterialSourceVariant) {
  if (sourceUris.empty()) {
    return {};
  }

  for (u32 i = 0; i < m_shaders.size(); ++i) {
    auto &entry = m_shaders[i];
    const ResourceMetadata *metadata =
        findResourceMetadata(entry.metadataHandle);
    if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
        metadata != nullptr && metadata->type == SceneResourceType::Shader &&
        metadata->uri == uri && entry.resource->sourceResolved &&
        !entry.resource->sourceUris.empty()) {
      if ((!hasLiveShaderPayload(*entry.resource) && payload) ||
          (requiresMaterialSourceVariant &&
           !entry.resource->requiresMaterialSourceVariant)) {
        entry.resource->sourceUris = std::move(sourceUris);
        entry.resource->payload = std::move(payload);
        entry.resource->requiresMaterialSourceVariant =
            requiresMaterialSourceVariant;
        ResourceMetadata &stored = mutableMetadata(entry.metadataHandle);
        stored.state = shaderMetadataState(*entry.resource);
        stored.dependencies = entry.resource->sourceUris;
        stored.diagnostics.clear();
        advanceUploadGeneration();
      }
      return ShaderHandle{i, entry.generation};
    }
  }

  auto shader = std::make_unique<ShaderResourceMetadata>();
  shader->uri = uri;
  shader->canonicalUri = uri;
  shader->sourceUris = std::move(sourceUris);
  shader->payload = std::move(payload);
  shader->sourceResolved = true;
  shader->requiresMaterialSourceVariant = requiresMaterialSourceVariant;
  auto handle =
      add<ShaderResourceMetadata, ShaderHandle>(m_shaders, std::move(shader));
  if (handle.isValid()) {
    ResourceMetadata metadata;
    metadata.type = SceneResourceType::Shader;
    metadata.uri = uri;
    metadata.state = shaderMetadataState(*m_shaders[handle.index].resource);
    for (const ResourceUri &sourceUri :
         m_shaders[handle.index].resource->sourceUris) {
      metadata.dependencies.push_back(sourceUri);
    }
    m_shaders[handle.index].metadataHandle =
        internResourceMetadata(std::move(metadata));
    ResourceMetadata &stored =
        mutableMetadata(m_shaders[handle.index].metadataHandle);
    stored.state = shaderMetadataState(*m_shaders[handle.index].resource);
    stored.dependencies = m_shaders[handle.index].resource->sourceUris;
    stored.diagnostics.clear();
  }
  advanceUploadGeneration();
  return handle;
}

void SceneResourceTable::registerMaterialSourceShaderVariant(
    const ResourceUri &shaderUri, StringID materialTypeVariant,
    StringID renderPathNodeSignature, ShaderProgramSet shaderProgram) {
  if (!shaderProgram.getShader()) {
    throw std::invalid_argument("material source shader variant for Shader '" +
                                shaderUri.string() +
                                "' has no compiled/reflected payload");
  }

  for (auto &entry : m_shaders) {
    const ResourceMetadata *metadata =
        findResourceMetadata(entry.metadataHandle);
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource ||
        metadata == nullptr || metadata->type != SceneResourceType::Shader ||
        metadata->uri != shaderUri) {
      continue;
    }

    auto &variants = entry.resource->materialSourceVariants;
    auto existing = std::find_if(
        variants.begin(), variants.end(), [&](const auto &candidate) {
          return candidate.materialTypeVariant == materialTypeVariant &&
                 candidate.renderPathNodeSignature == renderPathNodeSignature;
        });
    if (existing != variants.end()) {
      existing->shaderProgram = std::move(shaderProgram);
    } else {
      variants.push_back(ShaderResourceMetadata::MaterialSourceVariant{
          .materialTypeVariant = materialTypeVariant,
          .renderPathNodeSignature = renderPathNodeSignature,
          .shaderProgram = std::move(shaderProgram),
      });
    }

    ResourceMetadata &stored = mutableMetadata(entry.metadataHandle);
    stored.state = shaderMetadataState(*entry.resource);
    advanceUploadGeneration();
    return;
  }

  throw std::invalid_argument("missing Shader resource '" + shaderUri.string() +
                              "' for material source variant registration");
}

void SceneResourceTable::forEachMaterialInstance(
    const std::function<void(MaterialHandle, const MaterialInstance &,
                             const ResourceUri &)> &callback) const {
  for (u32 i = 0; i < m_materials.size(); ++i) {
    const auto &entry = m_materials[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    const ResourceMetadata *metadata =
        findResourceMetadata(entry.metadataHandle);
    const ResourceUri uri = metadata != nullptr ? metadata->uri : ResourceUri{};
    callback(MaterialHandle{i, entry.generation}, *entry.resource, uri);
  }
}

void SceneResourceTable::forEachMaterialInstanceMutable(
    const std::function<void(MaterialHandle, MaterialInstance &,
                             const ResourceUri &)> &callback) {
  for (u32 i = 0; i < m_materials.size(); ++i) {
    auto &entry = m_materials[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    const ResourceMetadata *metadata =
        findResourceMetadata(entry.metadataHandle);
    const ResourceUri uri = metadata != nullptr ? metadata->uri : ResourceUri{};
    callback(MaterialHandle{i, entry.generation}, *entry.resource, uri);
  }
}

RenderPathGraphHandle
SceneResourceTable::registerRenderPathGraph(const ResourceUri &uri,
                                            RenderPathGraph graph) {
  for (u32 i = 0; i < m_renderPathGraphs.size(); ++i) {
    const auto &entry = m_renderPathGraphs[i];
    const ResourceMetadata *metadata =
        findResourceMetadata(entry.metadataHandle);
    if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
        metadata != nullptr &&
        metadata->type == SceneResourceType::RenderPathGraph &&
        metadata->uri == uri) {
      return RenderPathGraphHandle{i, entry.generation};
    }
  }

  const auto findFeatureHandleByUri =
      [this](const ResourceUri &dependencyUri) -> RenderFeatureHandle {
    for (u32 i = 0; i < m_renderFeatures.size(); ++i) {
      const auto &entry = m_renderFeatures[i];
      const ResourceMetadata *metadata =
          findResourceMetadata(entry.metadataHandle);
      if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
          metadata != nullptr &&
          metadata->type == SceneResourceType::RenderFeature &&
          (metadata->state == ResourceState::Ready ||
           metadata->state == ResourceState::Dirty) &&
          metadata->uri == dependencyUri) {
        return RenderFeatureHandle{i, entry.generation};
      }
    }
    return {};
  };
  const auto findShaderHandleByUri =
      [this](const ResourceUri &dependencyUri) -> ShaderHandle {
    for (u32 i = 0; i < m_shaders.size(); ++i) {
      const auto &entry = m_shaders[i];
      const ResourceMetadata *metadata =
          findResourceMetadata(entry.metadataHandle);
      if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
          metadata != nullptr && metadata->type == SceneResourceType::Shader &&
          (metadata->state == ResourceState::Ready ||
           metadata->state == ResourceState::Dirty) &&
          isShaderDependencyResolved(*entry.resource) &&
          metadata->uri == dependencyUri) {
        return ShaderHandle{i, entry.generation};
      }
    }
    return {};
  };
  const auto hasShaderEntryByUri =
      [this](const ResourceUri &dependencyUri) -> bool {
    for (const auto &entry : m_shaders) {
      const ResourceMetadata *metadata =
          findResourceMetadata(entry.metadataHandle);
      if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
          metadata != nullptr && metadata->type == SceneResourceType::Shader &&
          metadata->uri == dependencyUri) {
        return true;
      }
    }
    return false;
  };

  std::vector<RenderFeatureHandle> featureHandles;
  featureHandles.reserve(graph.features.size());
  for (const auto &featureDependency : graph.features) {
    const RenderFeatureHandle featureHandle =
        findFeatureHandleByUri(featureDependency.uri);
    if (!featureHandle.isValid()) {
      throw std::invalid_argument(missingRenderPathGraphDependencyMessage(
          uri, "RenderFeature", featureDependency.uri));
    }
    featureHandles.push_back(featureHandle);
  }

  std::vector<ShaderHandle> shaderHandles;
  shaderHandles.reserve(graph.passes.size());
  for (const auto &pass : graph.passes) {
    if (const auto inputError = validateRenderPassInputContract(pass)) {
      const std::string passName = pass.id.empty() ? "<unnamed>" : pass.id;
      throw std::invalid_argument("RenderPathGraph '" + uri.string() +
                                  "' pass '" + passName + "' " + *inputError);
    }
    if (pass.shaderUri.string().empty()) {
      continue;
    }
    const ShaderHandle shaderHandle = findShaderHandleByUri(pass.shaderUri);
    if (!shaderHandle.isValid()) {
      if (hasShaderEntryByUri(pass.shaderUri)) {
        throw std::invalid_argument(
            missingShaderPayloadMessage(uri, pass.shaderUri));
      }
      throw std::invalid_argument(missingRenderPathGraphDependencyMessage(
          uri, "Shader", pass.shaderUri));
    }
    shaderHandles.push_back(shaderHandle);
  }

  auto handle = add<RenderPathGraph, RenderPathGraphHandle>(
      m_renderPathGraphs, std::make_unique<RenderPathGraph>(std::move(graph)));
  if (!handle.isValid()) {
    return {};
  }
  m_renderPathGraphs[handle.index].metadataHandle =
      loadOrGetResource(SceneResourceType::RenderPathGraph, uri);

  for (const RenderFeatureHandle featureHandle : featureHandles) {
    addDependency(handle, featureHandle);
  }
  for (const ShaderHandle shaderHandle : shaderHandles) {
    addDependency(handle, shaderHandle);
  }
  advanceUploadGeneration();
  return handle;
}

void SceneResourceTable::updateObject(ObjectHandle handle,
                                      ObjectResource object) {
  auto resolved = resolve(handle);
  if (!resolved.has_value()) {
    return;
  }
  resolved->get() = std::move(object);
  advanceUploadGeneration();
}

void SceneResourceTable::updateCamera(CameraHandle handle,
                                      CameraResource camera) {
  auto resolved = resolve(handle);
  if (!resolved.has_value()) {
    return;
  }
  const CameraData::Param cameraParam = makeCameraDataParam(camera);
  resolved->get() = std::move(camera);
  if (handle.index < m_cameraUbos.size() && m_cameraUbos[handle.index]) {
    m_cameraUbos[handle.index]->param = cameraParam;
    m_cameraUbos[handle.index]->setDirty();
  }
  advanceUploadGeneration();
}

void SceneResourceTable::release(GeometryStorageHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<GeometryStorage, GeometryStorageHandle>(m_geometryStorage, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(MeshHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<MeshBuffer, MeshHandle>(m_meshes, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(MaterialHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<MaterialInstance, MaterialHandle>(m_materials, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(TextureHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<CombinedTextureSampler, TextureHandle>(m_textures, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(LightHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<LightBase, LightHandle>(m_lights, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(SkeletonHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<Skeleton, SkeletonHandle>(m_skeletons, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(ObjectHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<ObjectResource, ObjectHandle>(m_objects, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(CameraHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<CameraResource, CameraHandle>(m_cameras, handle);
  if (handle.index < m_cameraUbos.size()) {
    m_cameraUbos[handle.index].reset();
  }
  advanceUploadGeneration();
}

void SceneResourceTable::release(RenderPathGraphHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<RenderPathGraph, RenderPathGraphHandle>(m_renderPathGraphs, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(RenderFeatureHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<RenderFeature, RenderFeatureHandle>(m_renderFeatures, handle);
  advanceUploadGeneration();
}

void SceneResourceTable::release(ShaderHandle handle) {
  if (!isAlive(handle)) {
    return;
  }
  release<ShaderResourceMetadata, ShaderHandle>(m_shaders, handle);
  advanceUploadGeneration();
}

std::optional<std::reference_wrapper<GeometryStorage>>
SceneResourceTable::resolve(GeometryStorageHandle handle) {
  return resolveMutable<GeometryStorage, GeometryStorageHandle>(
      m_geometryStorage, handle);
}

std::optional<std::reference_wrapper<const GeometryStorage>>
SceneResourceTable::resolve(GeometryStorageHandle handle) const {
  return resolveConst<GeometryStorage, GeometryStorageHandle>(m_geometryStorage,
                                                              handle);
}

std::optional<std::reference_wrapper<MeshBuffer>>
SceneResourceTable::resolve(MeshHandle handle) {
  return resolveMutable<MeshBuffer, MeshHandle>(m_meshes, handle);
}

std::optional<std::reference_wrapper<const MeshBuffer>>
SceneResourceTable::resolve(MeshHandle handle) const {
  return resolveConst<MeshBuffer, MeshHandle>(m_meshes, handle);
}

std::optional<std::reference_wrapper<MaterialInstance>>
SceneResourceTable::resolve(MaterialHandle handle) {
  return resolveMutable<MaterialInstance, MaterialHandle>(m_materials, handle);
}

std::optional<std::reference_wrapper<const MaterialInstance>>
SceneResourceTable::resolve(MaterialHandle handle) const {
  return resolveConst<MaterialInstance, MaterialHandle>(m_materials, handle);
}

std::optional<std::reference_wrapper<CombinedTextureSampler>>
SceneResourceTable::resolve(TextureHandle handle) {
  return resolveMutable<CombinedTextureSampler, TextureHandle>(m_textures,
                                                               handle);
}

std::optional<std::reference_wrapper<const CombinedTextureSampler>>
SceneResourceTable::resolve(TextureHandle handle) const {
  return resolveConst<CombinedTextureSampler, TextureHandle>(m_textures,
                                                             handle);
}

std::optional<std::reference_wrapper<LightBase>>
SceneResourceTable::resolve(LightHandle handle) {
  return resolveMutable<LightBase, LightHandle>(m_lights, handle);
}

std::optional<std::reference_wrapper<const LightBase>>
SceneResourceTable::resolve(LightHandle handle) const {
  return resolveConst<LightBase, LightHandle>(m_lights, handle);
}

std::optional<std::reference_wrapper<Skeleton>>
SceneResourceTable::resolve(SkeletonHandle handle) {
  return resolveMutable<Skeleton, SkeletonHandle>(m_skeletons, handle);
}

std::optional<std::reference_wrapper<const Skeleton>>
SceneResourceTable::resolve(SkeletonHandle handle) const {
  return resolveConst<Skeleton, SkeletonHandle>(m_skeletons, handle);
}

std::optional<std::reference_wrapper<ObjectResource>>
SceneResourceTable::resolve(ObjectHandle handle) {
  return resolveMutable<ObjectResource, ObjectHandle>(m_objects, handle);
}

std::optional<std::reference_wrapper<const ObjectResource>>
SceneResourceTable::resolve(ObjectHandle handle) const {
  return resolveConst<ObjectResource, ObjectHandle>(m_objects, handle);
}

std::optional<std::reference_wrapper<CameraResource>>
SceneResourceTable::resolve(CameraHandle handle) {
  return resolveMutable<CameraResource, CameraHandle>(m_cameras, handle);
}

std::optional<std::reference_wrapper<const CameraResource>>
SceneResourceTable::resolve(CameraHandle handle) const {
  return resolveConst<CameraResource, CameraHandle>(m_cameras, handle);
}

std::optional<std::reference_wrapper<RenderPathGraph>>
SceneResourceTable::resolve(RenderPathGraphHandle handle) {
  return resolveMutable<RenderPathGraph, RenderPathGraphHandle>(
      m_renderPathGraphs, handle);
}

std::optional<std::reference_wrapper<const RenderPathGraph>>
SceneResourceTable::resolve(RenderPathGraphHandle handle) const {
  return resolveConst<RenderPathGraph, RenderPathGraphHandle>(
      m_renderPathGraphs, handle);
}

std::optional<std::reference_wrapper<RenderFeature>>
SceneResourceTable::resolve(RenderFeatureHandle handle) {
  return resolveMutable<RenderFeature, RenderFeatureHandle>(m_renderFeatures,
                                                            handle);
}

std::optional<std::reference_wrapper<const RenderFeature>>
SceneResourceTable::resolve(RenderFeatureHandle handle) const {
  return resolveConst<RenderFeature, RenderFeatureHandle>(m_renderFeatures,
                                                          handle);
}

std::optional<std::reference_wrapper<ShaderResourceMetadata>>
SceneResourceTable::resolve(ShaderHandle handle) {
  return resolveMutable<ShaderResourceMetadata, ShaderHandle>(m_shaders,
                                                              handle);
}

std::optional<std::reference_wrapper<const ShaderResourceMetadata>>
SceneResourceTable::resolve(ShaderHandle handle) const {
  return resolveConst<ShaderResourceMetadata, ShaderHandle>(m_shaders, handle);
}

const MeshBuffer &SceneResourceTable::mesh(MeshHandle handle) const {
  auto resolved = resolve(handle);
  if (!resolved.has_value()) {
    throw std::out_of_range("invalid scene mesh handle");
  }
  return resolved->get();
}

const MaterialInstance &
SceneResourceTable::materialInstance(MaterialHandle handle) const {
  auto resolved = resolve(handle);
  if (!resolved.has_value()) {
    throw std::out_of_range("invalid scene material handle");
  }
  return resolved->get();
}

const CombinedTextureSampler &
SceneResourceTable::texture(TextureHandle handle) const {
  auto resolved = resolve(handle);
  if (!resolved.has_value()) {
    throw std::out_of_range("invalid scene texture handle");
  }
  return resolved->get();
}

bool SceneResourceTable::hasMesh(MeshHandle handle) const {
  return isAlive(handle);
}

bool SceneResourceTable::hasTexture(TextureHandle handle) const {
  return isAlive(handle);
}

GpuResourceRef
SceneResourceTable::getCameraUboResource(CameraHandle handle) const {
  if (!isAlive(handle) || handle.index >= m_cameraUbos.size()) {
    return {};
  }
  const auto &ubo = m_cameraUbos[handle.index];
  return ubo ? GpuResourceRef{*ubo} : GpuResourceRef{};
}

GpuResourceRef SceneResourceTable::buildRenderCameraUboResource(
    const CameraResource &camera) const {
  auto ubo = std::make_unique<CameraData>();
  ubo->param = makeCameraDataParam(camera);
  ubo->setDirty();
  return addRenderGpuResource(std::move(ubo));
}

GpuResourceRef SceneResourceTable::buildSceneLightsUboResource(
    const std::vector<LightHandle> &lightHandles, StringID pass) const {
  bool hasSceneLights = false;
  u32 directionalCount = 0;
  u32 pointCount = 0;
  u32 spotCount = 0;
  m_sceneLightsUbo->param = {};

  for (const LightHandle lightHandle : lightHandles) {
    const auto resolvedLight = resolve(lightHandle);
    if (!resolvedLight.has_value()) {
      continue;
    }
    const LightBase &light = resolvedLight->get();
    if (!light.getSceneNode()) {
      continue;
    }
    if (!light.supportsPass(pass)) {
      continue;
    }

    hasSceneLights = true;
    if (const auto *directionalLight =
            dynamic_cast<const DirectionalLight *>(&light)) {
      if (directionalCount >= MaxDirectionalLights) {
        std::cerr << "[SceneLightsUBO] directional light limit exceeded: max "
                  << MaxDirectionalLights << "\n";
        continue;
      }
      auto &entry = m_sceneLightsUbo->param.directional[directionalCount++];
      const Vec3f direction = directionalLight->getDirection();
      const Vec3f color = directionalLight->getColor();
      entry.direction = Vec4f{direction.x, direction.y, direction.z, 0.0f};
      entry.colorIntensity =
          Vec4f{color.x, color.y, color.z, directionalLight->getIntensity()};
      continue;
    }

    if (const auto *pointLight = dynamic_cast<const PointLight *>(&light)) {
      if (pointCount >= MaxPointLights) {
        std::cerr << "[SceneLightsUBO] point light limit exceeded: max "
                  << MaxPointLights << "\n";
        continue;
      }
      const auto node = pointLight->getSceneNode();
      const Vec3f position =
          node ? Transform::fromMat4(node->getWorldTransform()).translation
               : Vec3f{};
      const Vec3f color = pointLight->getColor();
      auto &entry = m_sceneLightsUbo->param.point[pointCount++];
      entry.positionRange =
          Vec4f{position.x, position.y, position.z, pointLight->getRange()};
      entry.colorIntensity =
          Vec4f{color.x, color.y, color.z, pointLight->getIntensity()};
      continue;
    }

    if (const auto *spotLight = dynamic_cast<const SpotLight *>(&light)) {
      if (spotCount >= MaxSpotLights) {
        std::cerr << "[SceneLightsUBO] spot light limit exceeded: max "
                  << MaxSpotLights << "\n";
        continue;
      }
      const auto node = spotLight->getSceneNode();
      const Vec3f position =
          node ? Transform::fromMat4(node->getWorldTransform()).translation
               : Vec3f{};
      const Vec3f direction = spotLight->getDirection();
      const Vec3f color = spotLight->getColor();
      auto &entry = m_sceneLightsUbo->param.spot[spotCount++];
      entry.positionRange =
          Vec4f{position.x, position.y, position.z, spotLight->getRange()};
      entry.directionCone = Vec4f{direction.x, direction.y, direction.z,
                                  spotLight->getOuterConeDegrees()};
      entry.colorIntensity =
          Vec4f{color.x, color.y, color.z, spotLight->getIntensity()};
    }
  }

  if (!hasSceneLights) {
    return {};
  }

  m_sceneLightsUbo->param.counts =
      Vec4i{static_cast<i32>(directionalCount), static_cast<i32>(pointCount),
            static_cast<i32>(spotCount), 0};
  m_sceneLightsUbo->setDirty();
  return GpuResourceRef{*m_sceneLightsUbo};
}

void SceneResourceTable::setIblEnvironmentResources(
    IblEnvironmentResources resources) {
  m_iblEnvironmentResources =
      completeIblEnvironmentResources(std::move(resources));
}

const IblEnvironmentResources *
SceneResourceTable::getIblEnvironmentResourceSet() const {
  if (!m_iblEnvironmentResources.has_value()) {
    return nullptr;
  }
  return &*m_iblEnvironmentResources;
}

IblEnvironmentResources *
SceneResourceTable::getMutableIblEnvironmentResources() {
  if (!m_iblEnvironmentResources.has_value()) {
    return nullptr;
  }
  return &*m_iblEnvironmentResources;
}

std::vector<GpuResourceRef>
SceneResourceTable::getIblEnvironmentResources() const {
  std::vector<GpuResourceRef> out;
  const auto *resources = getIblEnvironmentResourceSet();
  if (resources == nullptr) {
    return out;
  }
  if (resources->bakedSkyboxCubemap) {
    out.emplace_back(*resources->bakedSkyboxCubemap);
  } else if (resources->skyboxCubemap) {
    out.emplace_back(*resources->skyboxCubemap);
  }
  if (resources->bakedIrradianceCubemap) {
    out.emplace_back(*resources->bakedIrradianceCubemap);
  } else if (resources->irradianceCubemap) {
    out.emplace_back(*resources->irradianceCubemap);
  }
  if (resources->bakedPrefilteredRadianceCubemap) {
    out.emplace_back(*resources->bakedPrefilteredRadianceCubemap);
  } else if (resources->prefilteredRadianceCubemap) {
    out.emplace_back(*resources->prefilteredRadianceCubemap);
  }
  if (resources->bakedBrdfLut) {
    out.emplace_back(*resources->bakedBrdfLut);
  } else if (resources->brdfLut) {
    out.emplace_back(*resources->brdfLut);
  }
  if (resources->environmentUbo) {
    out.emplace_back(*resources->environmentUbo);
  }
  return out;
}

void SceneResourceTable::beginRenderResourceScope() {
  for (const MaterialHandle handle : m_renderMaterialHandles) {
    release(handle);
  }
  m_renderMaterialHandles.clear();
  m_renderGpuResources.clear();
  m_renderTextureSamplers.clear();
}

MaterialHandle
SceneResourceTable::addRenderMaterial(MaterialInstanceUniquePtr material) {
  if (!material) {
    return {};
  }

  bool hasPendingTextures = false;
  material->forEachPendingTextureBinding(
      [&hasPendingTextures](StringID,
                            const CombinedTextureSamplerSharedPtr &texture) {
        hasPendingTextures = hasPendingTextures || static_cast<bool>(texture);
      });
  if (hasPendingTextures) {
    throw std::logic_error("render-scope materials must bind textures through "
                           "SceneResourceTable texture handles");
  }

  const MaterialHandle handle = registerMaterial(std::move(material));
  if (handle.isValid()) {
    m_renderMaterialHandles.push_back(handle);
  }
  return handle;
}

GpuResourceRef SceneResourceTable::addRenderGpuResource(
    std::unique_ptr<IGpuResource> resource) const {
  if (!resource) {
    return {};
  }
  m_renderGpuResources.push_back(std::move(resource));
  return GpuResourceRef{*m_renderGpuResources.back()};
}

TextureSamplerRef SceneResourceTable::addRenderTextureSampler(
    CombinedTextureSamplerUniquePtr sampler) const {
  if (!sampler) {
    return {};
  }
  m_renderTextureSamplers.push_back(std::move(sampler));
  return TextureSamplerRef{*m_renderTextureSamplers.back()};
}

bool SceneResourceTable::isAlive(GeometryStorageHandle handle) const {
  return isAlive<GeometryStorage, GeometryStorageHandle>(m_geometryStorage,
                                                         handle);
}

bool SceneResourceTable::isAlive(MeshHandle handle) const {
  return isAlive<MeshBuffer, MeshHandle>(m_meshes, handle);
}

bool SceneResourceTable::isAlive(MaterialHandle handle) const {
  return isAlive<MaterialInstance, MaterialHandle>(m_materials, handle);
}

bool SceneResourceTable::isAlive(TextureHandle handle) const {
  return isAlive<CombinedTextureSampler, TextureHandle>(m_textures, handle);
}

bool SceneResourceTable::isAlive(LightHandle handle) const {
  return isAlive<LightBase, LightHandle>(m_lights, handle);
}

bool SceneResourceTable::isAlive(SkeletonHandle handle) const {
  return isAlive<Skeleton, SkeletonHandle>(m_skeletons, handle);
}

bool SceneResourceTable::isAlive(ObjectHandle handle) const {
  return isAlive<ObjectResource, ObjectHandle>(m_objects, handle);
}

bool SceneResourceTable::isAlive(CameraHandle handle) const {
  return isAlive<CameraResource, CameraHandle>(m_cameras, handle);
}

bool SceneResourceTable::isAlive(RenderPathGraphHandle handle) const {
  return isAlive<RenderPathGraph, RenderPathGraphHandle>(m_renderPathGraphs,
                                                         handle);
}

bool SceneResourceTable::isAlive(RenderFeatureHandle handle) const {
  return isAlive<RenderFeature, RenderFeatureHandle>(m_renderFeatures, handle);
}

bool SceneResourceTable::isAlive(ShaderHandle handle) const {
  return isAlive<ShaderResourceMetadata, ShaderHandle>(m_shaders, handle);
}

usize SceneResourceTable::geometryStorageCount() const {
  return aliveCount(m_geometryStorage);
}

usize SceneResourceTable::meshCount() const { return aliveCount(m_meshes); }

usize SceneResourceTable::materialCount() const {
  return aliveCount(m_materials);
}

usize SceneResourceTable::textureCount() const {
  return aliveCount(m_textures);
}

usize SceneResourceTable::lightCount() const { return aliveCount(m_lights); }

usize SceneResourceTable::skeletonCount() const {
  return aliveCount(m_skeletons);
}

usize SceneResourceTable::objectCount() const { return aliveCount(m_objects); }

usize SceneResourceTable::cameraCount() const { return aliveCount(m_cameras); }

usize SceneResourceTable::renderPathGraphCount() const {
  return aliveCount(m_renderPathGraphs);
}

usize SceneResourceTable::renderFeatureCount() const {
  return aliveCount(m_renderFeatures);
}

usize SceneResourceTable::shaderCount() const { return aliveCount(m_shaders); }

RenderSceneSnapshot SceneResourceTable::buildSnapshot() const {
  RenderSceneSnapshot snapshot;

  for (u32 i = 0; i < m_geometryStorage.size(); ++i) {
    const auto &entry = m_geometryStorage[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    GeometryStorageHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.geometryStorageHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_meshes.size(); ++i) {
    const auto &entry = m_meshes[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    MeshHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.meshHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_materials.size(); ++i) {
    const auto &entry = m_materials[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    MaterialHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.materialHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_textures.size(); ++i) {
    const auto &entry = m_textures[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    TextureHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.textureHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_lights.size(); ++i) {
    const auto &entry = m_lights[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    LightHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.lightHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_skeletons.size(); ++i) {
    const auto &entry = m_skeletons[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    SkeletonHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.skeletonHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_cameras.size(); ++i) {
    const auto &entry = m_cameras[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    CameraHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.cameraHandles.push_back(handle);
  }

  for (u32 i = 0; i < m_objects.size(); ++i) {
    const auto &entry = m_objects[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    ObjectHandle handle;
    handle.index = i;
    handle.generation = entry.generation;
    snapshot.objectHandles.push_back(handle);

    const auto &object = *entry.resource;
    snapshot.objects.push_back(ObjectInstanceView{
        .meshIndex = object.mesh.index,
        .materialIndex = object.material.index,
        .objectToWorld = object.objectToWorld,
        .worldToObject = object.worldToObject,
        .worldBounds = object.worldBounds,
        .visibilityMask = object.visibilityMask,
        .visible = object.visible,
    });
  }

  return snapshot;
}

SceneResourceTableUploadView SceneResourceTable::buildUploadView() const {
  const auto makeView = [this]() {
    return SceneResourceTableUploadView{
        .tableGeneration = m_generation,
        .positions = m_gpuPositions,
        .attributeStreams = m_gpuAttributeStreams,
        .attributeValues = m_gpuAttributeValues,
        .indices = m_gpuIndices,
        .meshes = m_gpuMeshes,
        .primitives = m_gpuPrimitives,
        .draws = m_gpuDraws,
        .objects = m_gpuObjects,
        .materials = m_gpuMaterials,
        .materialRefs = m_gpuMaterialRefs,
        .sourceMaterialRecords = m_gpuSourceMaterialRecords,
        .sourceMaterialStorages = m_gpuSourceMaterialStorages,
        .textures = m_gpuTextures,
        .cameras = m_gpuCameras,
        .lights = m_gpuLights,
        .renderPathGraphResources = m_gpuRenderPathGraphResources,
        .renderFeatureResources = m_gpuRenderFeatureResources,
        .shaderResources = m_gpuShaderResources,
        .renderPathGraphs = m_gpuRenderPathGraphs,
        .renderPathGraphPasses = m_gpuRenderPathGraphPasses,
        .renderPathGraphFeatures = m_gpuRenderPathGraphFeatures,
        .renderPathGraphShaders = m_gpuRenderPathGraphShaders,
        .meshIndexByHandle = m_gpuMeshIndexByHandle,
        .materialIndexByHandle = m_gpuMaterialIndexByHandle,
        .materialRefIndexByHandle = m_gpuMaterialRefIndexByHandle,
        .textureIndexByHandle = m_gpuTextureIndexByHandle,
        .objectIndexByHandle = m_gpuObjectIndexByHandle,
        .cameraIndexByHandle = m_gpuCameraIndexByHandle,
        .lightIndexByHandle = m_gpuLightIndexByHandle,
        .renderPathGraphIndexByHandle = m_gpuRenderPathGraphIndexByHandle,
        .renderFeatureIndexByHandle = m_gpuRenderFeatureIndexByHandle,
        .shaderIndexByHandle = m_gpuShaderIndexByHandle,
    };
  };

  m_gpuPositions.clear();
  m_gpuAttributeStreams.clear();
  m_gpuAttributeValues.clear();
  m_gpuIndices.clear();
  m_gpuMeshes.clear();
  m_gpuPrimitives.clear();
  m_gpuDraws.clear();
  m_gpuObjects.clear();
  m_gpuMaterials.clear();
  m_gpuMaterialRefs.clear();
  m_gpuSourceMaterialRecords.clear();
  m_gpuSourceMaterialStorages.clear();
  m_gpuTextures.clear();
  m_gpuCameras.clear();
  m_gpuLights.clear();
  m_gpuRenderPathGraphResources.clear();
  m_gpuRenderFeatureResources.clear();
  m_gpuShaderResources.clear();
  m_gpuRenderPathGraphs.clear();
  m_gpuRenderPathGraphPasses.clear();
  m_gpuRenderPathGraphFeatures.clear();
  m_gpuRenderPathGraphShaders.clear();
  m_gpuMeshIndexByHandle.clear();
  m_gpuMaterialIndexByHandle.clear();
  m_gpuMaterialRefIndexByHandle.clear();
  m_gpuTextureIndexByHandle.clear();
  m_gpuObjectIndexByHandle.clear();
  m_gpuCameraIndexByHandle.clear();
  m_gpuLightIndexByHandle.clear();
  m_gpuRenderPathGraphIndexByHandle.clear();
  m_gpuRenderFeatureIndexByHandle.clear();
  m_gpuShaderIndexByHandle.clear();

  const auto registerBuiltinUploadTexture =
      [this](const ResourceUri &uri) -> u32 {
    const auto texture = findTexture(uri);
    if (!texture.has_value()) {
      throw std::logic_error("missing builtin default texture resource '" +
                             uri.string() + "'");
    }
    return registerUploadTexture(*texture);
  };
  const MaterialContractDefaultTextureSlots defaultTextureSlots{
      .white = registerBuiltinUploadTexture(defaultWhiteTextureUri()),
      .black = registerBuiltinUploadTexture(defaultBlackTextureUri()),
      .flatNormal = registerBuiltinUploadTexture(defaultFlatNormalTextureUri()),
  };

  m_gpuCameras.reserve(aliveCount(m_cameras));
  for (u32 i = 0; i < m_cameras.size(); ++i) {
    const auto &entry = m_cameras[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    const u32 typedIndex = static_cast<u32>(m_gpuCameras.size());
    m_gpuCameras.push_back(std::cref(*entry.resource));
    m_gpuCameraIndexByHandle.push_back(SceneResourceCameraUploadIndex{
        .handle = CameraHandle{i, entry.generation},
        .typedIndex = typedIndex,
    });
  }

  m_gpuLights.reserve(aliveCount(m_lights));
  for (u32 i = 0; i < m_lights.size(); ++i) {
    const auto &entry = m_lights[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    const u32 typedIndex = static_cast<u32>(m_gpuLights.size());
    m_gpuLights.push_back(std::cref(*entry.resource));
    m_gpuLightIndexByHandle.push_back(SceneResourceLightUploadIndex{
        .handle = LightHandle{i, entry.generation},
        .typedIndex = typedIndex,
    });
  }

  std::vector<CompactRecordIndex> featureIndexToGpuRecord(
      m_renderFeatures.size());
  m_gpuRenderFeatureResources.reserve(aliveCount(m_renderFeatures));
  for (u32 i = 0; i < m_renderFeatures.size(); ++i) {
    const auto &entry = m_renderFeatures[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    const u32 typedIndex = static_cast<u32>(m_gpuRenderFeatureResources.size());
    m_gpuRenderFeatureResources.push_back(std::cref(*entry.resource));
    featureIndexToGpuRecord[i] = CompactRecordIndex{
        .generation = entry.generation,
        .uploadIndex = typedIndex,
    };
    m_gpuRenderFeatureIndexByHandle.push_back(
        SceneResourceRenderFeatureUploadIndex{
            .handle = RenderFeatureHandle{i, entry.generation},
            .typedIndex = typedIndex,
        });
  }

  std::vector<CompactRecordIndex> shaderIndexToGpuRecord(m_shaders.size());
  m_gpuShaderResources.reserve(aliveCount(m_shaders));
  m_gpuRenderPathGraphShaders.reserve(aliveCount(m_shaders));
  for (u32 i = 0; i < m_shaders.size(); ++i) {
    const auto &entry = m_shaders[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }
    if (!entry.resource->sourceResolved || entry.resource->sourceUris.empty()) {
      const ResourceMetadata *metadata =
          findResourceMetadata(entry.metadataHandle);
      const ResourceUri shaderUri =
          metadata != nullptr ? metadata->uri : entry.resource->uri;
      throw std::logic_error("Shader resource '" + shaderUri.string() +
                             "' has no resolved source descriptors");
    }
    if (!hasLiveShaderPayload(*entry.resource) &&
        !entry.resource->requiresMaterialSourceVariant) {
      const ResourceMetadata *metadata =
          findResourceMetadata(entry.metadataHandle);
      const ResourceUri shaderUri =
          metadata != nullptr ? metadata->uri : entry.resource->uri;
      throw std::logic_error("Shader resource '" + shaderUri.string() +
                             "' has resolved source descriptors but no live "
                             "compiled/reflected payload");
    }
    if (entry.resource->requiresMaterialSourceVariant &&
        !hasResolvedMaterialSourceVariantPayload(*entry.resource)) {
      const ResourceMetadata *metadata =
          findResourceMetadata(entry.metadataHandle);
      const ResourceUri shaderUri =
          metadata != nullptr ? metadata->uri : entry.resource->uri;
      throw std::logic_error(
          "Shader resource '" + shaderUri.string() +
          "' requires material source variant resolution but has no final "
          "compiled/reflected material source variants");
    }
    const u32 typedIndex = static_cast<u32>(m_gpuShaderResources.size());
    m_gpuShaderResources.push_back(std::cref(*entry.resource));
    m_gpuRenderPathGraphShaders.push_back(entry.metadataHandle);
    shaderIndexToGpuRecord[i] = CompactRecordIndex{
        .generation = entry.generation,
        .uploadIndex = typedIndex,
    };
    m_gpuShaderIndexByHandle.push_back(SceneResourceShaderUploadIndex{
        .handle = ShaderHandle{i, entry.generation},
        .typedIndex = typedIndex,
    });
  }

  const auto findFeatureHandleByUri =
      [this](const ResourceUri &uri) -> RenderFeatureHandle {
    for (u32 i = 0; i < m_renderFeatures.size(); ++i) {
      const auto &entry = m_renderFeatures[i];
      const ResourceMetadata *metadata =
          findResourceMetadata(entry.metadataHandle);
      if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
          metadata != nullptr &&
          metadata->type == SceneResourceType::RenderFeature &&
          (metadata->state == ResourceState::Ready ||
           metadata->state == ResourceState::Dirty) &&
          metadata->uri == uri) {
        return RenderFeatureHandle{i, entry.generation};
      }
    }
    return {};
  };

  const auto findShaderHandleByUri = [this](const ResourceUri &uri) {
    for (u32 i = 0; i < m_shaders.size(); ++i) {
      const auto &entry = m_shaders[i];
      const ResourceMetadata *metadata =
          findResourceMetadata(entry.metadataHandle);
      if (entry.state == SceneResourceEntryState::Alive && entry.resource &&
          metadata != nullptr && metadata->type == SceneResourceType::Shader &&
          (metadata->state == ResourceState::Ready ||
           metadata->state == ResourceState::Dirty) &&
          isShaderDependencyResolved(*entry.resource) && metadata->uri == uri) {
        return ShaderHandle{i, entry.generation};
      }
    }
    return ShaderHandle{};
  };

  m_gpuRenderPathGraphResources.reserve(aliveCount(m_renderPathGraphs));
  m_gpuRenderPathGraphs.reserve(aliveCount(m_renderPathGraphs));
  for (u32 i = 0; i < m_renderPathGraphs.size(); ++i) {
    const auto &entry = m_renderPathGraphs[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }

    const auto &graph = *entry.resource;
    const u32 typedIndex =
        static_cast<u32>(m_gpuRenderPathGraphResources.size());
    m_gpuRenderPathGraphResources.push_back(std::cref(graph));

    SceneGpuRenderPathGraphRecord graphRecord;
    graphRecord.passOffset =
        static_cast<u32>(m_gpuRenderPathGraphPasses.size());
    for (const auto &pass : graph.passes) {
      const ShaderHandle shaderHandle = findShaderHandleByUri(pass.shaderUri);
      const u32 shaderIndex =
          findCompactRecordIndex(shaderIndexToGpuRecord, shaderHandle);
      if (shaderIndex == u32_max) {
        const ResourceMetadata *metadata =
            findResourceMetadata(entry.metadataHandle);
        const ResourceUri graphUri =
            metadata != nullptr ? metadata->uri : ResourceUri("<unknown>");
        throw std::logic_error(missingRenderPathGraphDependencyMessage(
            graphUri, "Shader", pass.shaderUri));
      }
      m_gpuRenderPathGraphPasses.push_back(
          SceneGpuRenderPathGraphPassRecord{.shaderIndex = shaderIndex});
    }
    graphRecord.passCount =
        static_cast<u32>(m_gpuRenderPathGraphPasses.size()) -
        graphRecord.passOffset;

    graphRecord.featureOffset =
        static_cast<u32>(m_gpuRenderPathGraphFeatures.size());
    for (const auto &featureDependency : graph.features) {
      const RenderFeatureHandle featureHandle =
          findFeatureHandleByUri(featureDependency.uri);
      const u32 featureIndex =
          findCompactRecordIndex(featureIndexToGpuRecord, featureHandle);
      if (featureIndex == u32_max) {
        const ResourceMetadata *metadata =
            findResourceMetadata(entry.metadataHandle);
        const ResourceUri graphUri =
            metadata != nullptr ? metadata->uri : ResourceUri("<unknown>");
        throw std::logic_error(missingRenderPathGraphDependencyMessage(
            graphUri, "RenderFeature", featureDependency.uri));
      }
      m_gpuRenderPathGraphFeatures.push_back(
          SceneGpuRenderPathGraphFeatureRecord{.featureIndex = featureIndex});
    }
    graphRecord.featureCount =
        static_cast<u32>(m_gpuRenderPathGraphFeatures.size()) -
        graphRecord.featureOffset;

    m_gpuRenderPathGraphIndexByHandle.push_back(
        SceneResourceRenderPathGraphUploadIndex{
            .handle = RenderPathGraphHandle{i, entry.generation},
            .typedIndex = typedIndex,
        });
    m_gpuRenderPathGraphs.push_back(graphRecord);
  }

  std::vector<CompactRecordIndex> meshIndexToGpuRecord(m_meshes.size());

  for (u32 i = 0; i < m_meshes.size(); ++i) {
    const auto &entry = m_meshes[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }

    const auto &mesh = *entry.resource;
    const GeometryStorageHandle geometryHandle =
        mesh.getGeometryStorageHandle();
    const auto geometry = resolve(geometryHandle);
    if (!geometry.has_value()) {
      continue;
    }
    if (!isMeshSliceValid(mesh, geometry->get())) {
      continue;
    }
    SceneGpuMeshRecord record{
        .vertexOffset = static_cast<u32>(m_gpuPositions.size()),
        .indexOffset = static_cast<u32>(m_gpuIndices.size()),
        .indexCount = mesh.getIndexCount(),
        .geometryIndex = geometryHandle.index,
        .attributeStreamOffset = static_cast<u32>(m_gpuAttributeStreams.size()),
    };
    appendMeshGeometryRecords(mesh, geometry->get(), record.vertexOffset,
                              m_gpuPositions, m_gpuAttributeStreams,
                              m_gpuAttributeValues, m_gpuIndices);
    record.attributeStreamCount =
        static_cast<u32>(m_gpuAttributeStreams.size()) -
        record.attributeStreamOffset;
    meshIndexToGpuRecord[i] = CompactRecordIndex{
        .generation = entry.generation,
        .uploadIndex = static_cast<u32>(m_gpuMeshes.size()),
    };
    m_gpuMeshIndexByHandle.push_back(SceneResourceMeshUploadIndex{
        .handle = MeshHandle{i, entry.generation},
        .typedIndex = static_cast<u32>(m_gpuMeshes.size()),
    });
    m_gpuMeshes.push_back(record);
  }

  struct MaterialUploadRecordIndex final {
    u32 generation = 0;
    u32 materialIndex = u32_max;
    u32 materialRefIndex = u32_max;
  };

  std::vector<MaterialUploadRecordIndex> materialIndexToGpuRecord(
      m_materials.size());
  std::vector<std::vector<SourceLocalMaterialRecord>>
      sourceMaterialRecordsByStorage;
  std::vector<const MaterialContractReflection *> sourceContractsByStorage;
  m_gpuMaterials.reserve(aliveCount(m_objects));
  m_gpuMaterialRefs.reserve(aliveCount(m_objects));
  m_gpuTextures.reserve(aliveCount(m_objects) * 5u);
  sourceMaterialRecordsByStorage.reserve(aliveCount(m_objects));
  sourceContractsByStorage.reserve(aliveCount(m_objects));

  const auto textureSlotForUri = [this](const ResourceUri &uri) -> u32 {
    const auto texture = findTexture(uri);
    if (!texture.has_value()) {
      return u32_max;
    }
    return registerUploadTexture(*texture);
  };

  const auto textureHandleForMaterialParameter =
      [this](const MaterialInstance &material,
             std::string_view parameterName) -> TextureHandle {
    const std::string parameterKey(parameterName);
    const StringID parameterId(parameterKey);
    const auto envelope = material.getMaterialEnvelope(parameterId);
    if (!envelope.has_value() ||
        envelope->get().kind != MaterialEnvelopeKind::Texture) {
      return {};
    }
    const TextureHandle directHandle = material.getTextureHandle(parameterId);
    if (directHandle.isValid()) {
      return directHandle;
    }
    for (const auto &dependency : material.getMaterialDependencies()) {
      if (dependency.kind != MaterialEnvelopeKind::Texture ||
          dependency.parameterName != parameterKey) {
        continue;
      }
      if (const auto texture = findTexture(dependency.uri)) {
        return *texture;
      }
    }
    return {};
  };

  const auto ensureSourceStorage =
      [this, &sourceMaterialRecordsByStorage, &sourceContractsByStorage](
          const MaterialContractReflection &contract) -> u32 {
    const StringID sourceSignature = contract.sourceSignature();
    for (u32 i = 0; i < m_gpuSourceMaterialStorages.size(); ++i) {
      if (m_gpuSourceMaterialStorages[i].sourceSignature == sourceSignature) {
        if (!sameSourceStorageLayout(*sourceContractsByStorage[i], contract)) {
          throw std::logic_error("material source signature conflict for '" +
                                 contract.sourceUri.string() +
                                 "': inconsistent source storage layout");
        }
        return i;
      }
    }

    m_gpuSourceMaterialStorages.push_back(SceneSourceLocalMaterialStorageView{
        .sourceSignature = sourceSignature,
        .sourceUri = contract.sourceUri,
        .reflectionHash = contract.reflectionHash,
        .storageAbiHash = contract.storageAbiHash,
    });
    sourceMaterialRecordsByStorage.emplace_back();
    sourceContractsByStorage.push_back(&contract);
    return static_cast<u32>(m_gpuSourceMaterialStorages.size() - 1u);
  };

  const auto ensureMaterialRecord =
      [this, &materialIndexToGpuRecord, &defaultTextureSlots,
       &textureSlotForUri, &textureHandleForMaterialParameter,
       &ensureSourceStorage, &sourceMaterialRecordsByStorage](
          MaterialHandle handle) -> MaterialUploadRecordIndex {
    if (!isAlive(handle)) {
      return {};
    }
    auto &compact = materialIndexToGpuRecord[handle.index];
    if (compact.generation == handle.generation) {
      return compact;
    }
    const auto &entry = m_materials[handle.index];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource ||
        entry.generation != handle.generation) {
      return {};
    }

    if (const auto contract = entry.resource->getMaterialContractReflection()) {
      const StringID expectedSourceSignature =
          contract->get().sourceSignature();
      const StringID materialSourceSignature =
          entry.resource->getMaterialSourceSignature();
      if (materialSourceSignature.id == 0) {
        throw std::logic_error("missing material source signature for '" +
                               contract->get().sourceUri.string() + "'");
      }
      if (materialSourceSignature != expectedSourceSignature) {
        throw std::logic_error("material source signature mismatch for '" +
                               contract->get().sourceUri.string() + "'");
      }

      const u32 sourceStorageIndex = ensureSourceStorage(contract->get());
      auto &records = sourceMaterialRecordsByStorage[sourceStorageIndex];
      const u32 sourceLocalMaterialIndex = static_cast<u32>(records.size());

      MaterialContractPackInput packInput;
      packInput.material = entry.resource.get();
      packInput.contract = contract->get();
      packInput.defaultTextureSlots = defaultTextureSlots;
      packInput.sourceLocalMaterialIndex = sourceLocalMaterialIndex;
      packInput.textureSlotForParameter =
          [this, &entry, &textureHandleForMaterialParameter](
              std::string_view parameterName) -> u32 {
        return registerUploadTexture(
            textureHandleForMaterialParameter(*entry.resource, parameterName));
      };
      packInput.textureSlotForUri = textureSlotForUri;
      MaterialContractPackResult packed = packMaterialContractRecord(packInput);
      if (!packed.diagnostics.empty()) {
        throw std::logic_error("failed to pack source material record for '" +
                               contract->get().sourceUri.string() +
                               "': " + packed.diagnostics.front());
      }

      records.push_back(std::move(packed.record));
      compact = MaterialUploadRecordIndex{
          .generation = entry.generation,
          .materialIndex = u32_max,
          .materialRefIndex = static_cast<u32>(m_gpuMaterialRefs.size()),
      };
      m_gpuMaterialRefIndexByHandle.push_back(
          SceneResourceMaterialRefUploadIndex{
              .handle = handle,
              .typedIndex = compact.materialRefIndex,
          });
      m_gpuMaterialRefs.push_back(SceneGpuMaterialRefRecord{
          .sourceStorageIndex = sourceStorageIndex,
          .sourceLocalMaterialIndex = sourceLocalMaterialIndex,
      });
      return compact;
    }

    auto record = toGpuMaterialRecord(*entry.resource);
    record.baseColorTexture = registerUploadTexture(
        textureHandleForMaterialParameter(*entry.resource, "Kd"));
    record.normalTexture = registerUploadTexture(
        textureHandleForMaterialParameter(*entry.resource, "normalmap"));
    compact = MaterialUploadRecordIndex{
        .generation = entry.generation,
        .materialIndex = static_cast<u32>(m_gpuMaterials.size()),
        .materialRefIndex = u32_max,
    };
    m_gpuMaterialIndexByHandle.push_back(SceneResourceMaterialUploadIndex{
        .handle = handle,
        .typedIndex = compact.materialIndex,
    });
    m_gpuMaterials.push_back(record);
    return compact;
  };

  m_gpuObjects.reserve(aliveCount(m_objects));
  m_gpuDraws.reserve(aliveCount(m_objects));
  for (u32 i = 0; i < m_objects.size(); ++i) {
    const auto &entry = m_objects[i];
    if (entry.state != SceneResourceEntryState::Alive || !entry.resource) {
      continue;
    }

    const auto &object = *entry.resource;
    const u32 meshRecordIndex =
        findCompactRecordIndex(meshIndexToGpuRecord, object.mesh);
    if (meshRecordIndex == u32_max) {
      continue;
    }
    const MaterialUploadRecordIndex materialRecord =
        ensureMaterialRecord(object.material);
    if (materialRecord.materialIndex == u32_max &&
        materialRecord.materialRefIndex == u32_max) {
      continue;
    }
    const u32 objectRecordIndex = static_cast<u32>(m_gpuObjects.size());

    SceneGpuObjectRecord objectRecord;
    objectRecord.objectToWorld = toGpuColumns(object.objectToWorld);
    objectRecord.worldToObject = toGpuColumns(object.worldToObject);
    objectRecord.boundsMin = toGpuBoundsMin(object.worldBounds);
    objectRecord.boundsMax = toGpuBoundsMax(object.worldBounds);
    objectRecord.visible = object.visible ? 1u : 0u;
    objectRecord.flags = object.debugOnly ? 1u : 0u;
    objectRecord.visibilityMask = object.visibilityMask;
    objectRecord.debugId = object.debugId.id;
    m_gpuObjects.push_back(objectRecord);
    m_gpuDraws.push_back(SceneGpuDrawRecord{
        .objectIndex = objectRecordIndex,
        .materialIndex = materialRecord.materialIndex,
        .meshIndex = meshRecordIndex,
        .materialRefIndex = materialRecord.materialRefIndex,
    });
    m_gpuObjectIndexByHandle.push_back(SceneResourceObjectUploadIndex{
        .handle = ObjectHandle{i, entry.generation},
        .typedIndex = objectRecordIndex,
    });

    const auto &meshRecord = m_gpuMeshes[meshRecordIndex];
    for (u32 triangleIndexOffset = 0;
         triangleIndexOffset + 2 < meshRecord.indexCount;
         triangleIndexOffset += 3) {
      SceneGpuPrimitiveRecord primitiveRecord;
      primitiveRecord.indexOffset =
          meshRecord.indexOffset + triangleIndexOffset;
      primitiveRecord.meshIndex = meshRecordIndex;
      primitiveRecord.materialIndex = materialRecord.materialIndex;
      primitiveRecord.objectIndex = objectRecordIndex;
      m_gpuPrimitives.push_back(primitiveRecord);
    }
  }

  for (u32 storageIndex = 0; storageIndex < m_gpuSourceMaterialStorages.size();
       ++storageIndex) {
    SceneSourceLocalMaterialStorageView &storage =
        m_gpuSourceMaterialStorages[storageIndex];
    const auto &records = sourceMaterialRecordsByStorage[storageIndex];
    storage.recordOffset = static_cast<u32>(m_gpuSourceMaterialRecords.size());
    storage.recordCount = static_cast<u32>(records.size());
    m_gpuSourceMaterialRecords.insert(m_gpuSourceMaterialRecords.end(),
                                      records.begin(), records.end());
  }

  return makeView();
}

} // namespace LX_core
