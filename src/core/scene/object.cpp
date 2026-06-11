#include "object.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/scene_system_abi_validation.hpp"
#include "scene.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace LX_core {

namespace {

std::string vertexLayoutDebugString(const VertexLayout &layout) {
  std::ostringstream oss;
  oss << "stride=" << layout.getStride() << " [";
  bool first = true;
  for (const auto &item : layout.getItems()) {
    if (!first)
      oss << ", ";
    first = false;
    oss << "loc" << item.location << ":" << item.name << "/"
        << toString(item.type);
  }
  oss << "]";
  return oss.str();
}

std::string variantsDebugString(const ShaderProgramSet &programSet) {
  std::ostringstream oss;
  bool first = true;
  for (const auto &variant : programSet.variants) {
    if (!variant.enabled)
      continue;
    if (!first)
      oss << ",";
    first = false;
    oss << variant.macroName;
  }
  return first ? "(none)" : oss.str();
}

[[noreturn]] void fatalValidation(
    const SceneNode &node, StringID pass, const MaterialInstance &material,
    const ShaderProgramSet &programSet, const std::string &reason,
    std::optional<std::reference_wrapper<const VertexLayout>> layout =
        std::nullopt) {
  std::ostringstream oss;
  oss << "SceneNodeValidation node=" << node.getNodeName()
      << " pass=" << GlobalStringTable::get().toDebugString(pass)
      << " material="
      << (material.getTemplate() ? material.getTemplate()->getName()
                                 : std::string("<null>"))
      << " shader=" << programSet.shaderName
      << " variants=" << variantsDebugString(programSet)
      << " reason=" << reason;
  if (layout) {
    oss << " vertexLayout=" << vertexLayoutDebugString(layout->get());
  }
  throw std::logic_error(oss.str());
}

std::optional<std::reference_wrapper<const VertexLayoutItem>>
findLayoutItem(const VertexLayout &layout, u32 location) {
  for (const auto &item : layout.getItems()) {
    if (item.location == location)
      return std::cref(item);
  }
  return std::nullopt;
}

struct ResolvedMeshForNode final {
  std::reference_wrapper<const MeshBuffer> mesh;
  std::reference_wrapper<const GeometryStorage> storage;
};

[[nodiscard]] std::optional<ResolvedMeshForNode>
resolveMeshForNode(const SceneNode &node, const MeshComponent &component) {
  if (auto scene = node.getAttachedScene()) {
    const MeshHandle handle = component.getMeshHandle();
    if (const auto mesh = scene->resources().resolve(handle)) {
      const GeometryStorageHandle storageHandle =
          mesh->get().getGeometryStorageHandle();
      if (const auto storage = scene->resources().resolve(storageHandle)) {
        return ResolvedMeshForNode{std::cref(mesh->get()),
                                   std::cref(storage->get())};
      }
    }
  }
  const auto &pendingMesh = component.getPendingMesh();
  if (!pendingMesh) {
    return std::nullopt;
  }
  const auto &pendingStorage = pendingMesh->getGeometryStorage();
  if (!pendingStorage) {
    return std::nullopt;
  }
  return ResolvedMeshForNode{std::cref(*pendingMesh),
                             std::cref(*pendingStorage)};
}

[[nodiscard]] const MaterialInstance *
resolveMaterialForNode(const SceneNode &node,
                       const MaterialComponent &component) {
  if (auto scene = node.getAttachedScene()) {
    const MaterialHandle handle = component.getMaterialHandle();
    if (const auto material = scene->resources().resolve(handle)) {
      return &material->get();
    }
  }
  return component.getPendingMaterialInstance().get();
}

[[nodiscard]] const Skeleton *
resolveSkeletonForNode(const SceneNode &node,
                       const SkeletonComponent &component) {
  if (auto scene = node.getAttachedScene()) {
    const SkeletonHandle handle = component.getSkeletonHandle();
    if (const auto skeleton = scene->resources().resolve(handle)) {
      return &skeleton->get();
    }
  }
  return component.getPendingSkeleton().get();
}

StringID makeObjectPipelineSignature(const IVertexBuffer &vertexBuffer,
                                     PrimitiveTopology topology) {
  StringID meshFields[] = {
      vertexBuffer.getPipelineSignature(),
      topologyPipelineSignature(topology),
  };
  const StringID meshSignature =
      GlobalStringTable::get().compose(TypeTag::MeshRender, meshFields);
  StringID objectFields[] = {meshSignature};
  return GlobalStringTable::get().compose(TypeTag::ObjectRender, objectFields);
}

std::vector<u32> copyIndexBufferData(const IndexBuffer &indexBuffer) {
  std::vector<u32> indices(indexBuffer.indexCount());
  if (!indices.empty()) {
    std::memcpy(indices.data(), indexBuffer.getRawData(),
                indices.size() * sizeof(u32));
  }
  return indices;
}

std::optional<std::reference_wrapper<const MeshComponent>>
getMeshComponent(const SceneNode &node) {
  return node.getComponent<MeshComponent>();
}

std::optional<std::reference_wrapper<const MaterialComponent>>
getMaterialComponent(const SceneNode &node) {
  return node.getComponent<MaterialComponent>();
}

std::optional<std::reference_wrapper<const SkeletonComponent>>
getSkeletonComponent(const SceneNode &node) {
  return node.getComponent<SkeletonComponent>();
}

} // namespace

SceneNode::SceneNode(PathRootTag)
    : m_nodeName("scene_root"), m_isPathRoot(true) {}

SceneNode::SceneNode(std::string nodeName)
    : m_nodeName(std::move(nodeName)) {
  rebuildValidatedCache();
}

SceneNode::~SceneNode() {
  clearParent();
  clearComponents();
}

SceneNode::SharedPtr SceneNode::createPathRoot() {
  return SharedPtr(new SceneNode(PathRootTag{}));
}

void SceneNode::setLocalTransform(const Transform &transform) {
  m_localTransform = transform.normalized();
  markWorldTransformDirty();
  emitRuntimeNodeChanged(SceneNodeAspect::Transform);
}

void SceneNode::setTranslation(const Vec3f &translation) {
  m_localTransform.translation = translation;
  markWorldTransformDirty();
  emitRuntimeNodeChanged(SceneNodeAspect::Transform);
}

void SceneNode::setRotation(const Quatf &rotation) {
  m_localTransform.rotation = rotation.normalized();
  markWorldTransformDirty();
  emitRuntimeNodeChanged(SceneNodeAspect::Transform);
}

void SceneNode::setScale(const Vec3f &scale) {
  m_localTransform.scale = scale;
  markWorldTransformDirty();
  emitRuntimeNodeChanged(SceneNodeAspect::Transform);
}

void SceneNode::setName(std::string name) {
  m_name = sanitizeName(std::move(name));
  warnIfSiblingNameIsDuplicated();
  emitRuntimeNodeChanged(SceneNodeAspect::Identity);
}

std::string SceneNode::getPath() const {
  if (m_isPathRoot) {
    return "/";
  }

  const auto pathSegment = getPathSegment();
  const auto parent = m_parent.lock();
  if (!parent) {
    return "/" + pathSegment;
  }

  if (parent->m_isPathRoot) {
    return "/" + pathSegment;
  }

  const auto parentPath = parent->getPath();
  return parentPath + "/" + pathSegment;
}

const Mat4f &SceneNode::getWorldTransform() const {
  updateWorldTransformIfNeeded();
  return m_worldTransform;
}

BoundingBox SceneNode::getLocalBounds() const {
  const auto meshComponent = getMeshComponent(*this);
  if (!meshComponent) {
    return {};
  }
  const auto mesh = resolveMeshForNode(*this, meshComponent->get());
  return mesh ? mesh->mesh.get().getBounds() : BoundingBox{};
}

BoundingBox SceneNode::getWorldBounds() const {
  const BoundingBox localBounds = getLocalBounds();
  if (!localBounds.isValid()) {
    return localBounds;
  }
  return localBounds.transformed(getWorldTransform());
}

void SceneNode::setParent(const SharedPtr &parent) {
  setParentInternal(parent, true);
}

void SceneNode::setParentInternal(const SharedPtr &parent,
                                  const bool emitHierarchyEvent) {
  if (parent.get() == this) {
    throw std::logic_error("SceneNodeHierarchy node=" + m_nodeName +
                           " cannot parent itself");
  }

  for (auto current = parent; current; current = current->getParent()) {
    if (current.get() == this) {
      throw std::logic_error("SceneNodeHierarchy node=" + m_nodeName +
                             " would create a parent cycle");
    }
  }

  auto currentParent = m_parent.lock();
  if (currentParent == parent) {
    return;
  }

  removeFromParentChildrenList();
  m_parent.reset();

  if (parent) {
    parent->pruneExpiredChildren();
    parent->m_children.push_back(weak_from_this());
    m_parent = parent;
  }

  warnIfSiblingNameIsDuplicated();
  markWorldTransformDirty();
  if (emitHierarchyEvent) {
    emitRuntimeNodeChanged(SceneNodeAspect::Hierarchy);
  }
}

void SceneNode::clearParent() { clearParentInternal(true); }

void SceneNode::clearParentInternal(const bool emitHierarchyEvent) {
  if (m_parent.expired()) {
    m_parent.reset();
    if (m_worldTransformHasParent) {
      markWorldTransformDirty();
    }
    return;
  }

  removeFromParentChildrenList();
  m_parent.reset();
  markWorldTransformDirty();
  if (emitHierarchyEvent) {
    emitRuntimeNodeChanged(SceneNodeAspect::Hierarchy);
  }
}

std::vector<SceneNode::SharedPtr> SceneNode::getChildren() const {
  std::vector<SceneNode::SharedPtr> children;
  children.reserve(m_children.size());
  for (const auto &childWeak : m_children) {
    if (auto child = childWeak.lock()) {
      children.push_back(std::move(child));
    }
  }
  return children;
}

GpuResourceRef SceneNode::getVertexBuffer() const {
  const auto meshComponent = getMeshComponent(*this);
  if (!meshComponent) {
    return {};
  }
  const auto mesh = resolveMeshForNode(*this, meshComponent->get());
  if (!mesh) {
    return {};
  }
  return GpuResourceRef{mesh->storage.get().getVertexBuffer()};
}

GpuResourceRef SceneNode::getIndexBuffer() const {
  const auto meshComponent = getMeshComponent(*this);
  if (!meshComponent) {
    return {};
  }
  const auto mesh = resolveMeshForNode(*this, meshComponent->get());
  if (!mesh) {
    return {};
  }
  return GpuResourceRef{mesh->storage.get().getIndexBuffer()};
}

IShaderSharedPtr SceneNode::getShaderInfo() const {
  auto data = getValidatedPassData(Pass_Forward);
  if (data)
    return data->get().shaderInfo;
  const auto materialComponent = getMaterialComponent(*this);
  if (!materialComponent) {
    return nullptr;
  }
  const MaterialInstance *material =
      resolveMaterialForNode(*this, materialComponent->get());
  return material ? material->getPassShader(Pass_Forward) : nullptr;
}

StringID SceneNode::getPipelineSignature(StringID pass) const {
  const auto meshComponent = getMeshComponent(*this);
  if (!meshComponent)
    return StringID{};
  const auto mesh = resolveMeshForNode(*this, meshComponent->get());
  if (!mesh) {
    return StringID{};
  }
  const auto materialComponent = getMaterialComponent(*this);
  bool usesMeshOverlay = false;
  const MaterialInstance *material =
      materialComponent
          ? resolveMaterialForNode(*this, materialComponent->get())
          : nullptr;
  if (material && material->getTemplate()) {
    const auto entry = material->getTemplate()->getPassDefinition(pass);
    usesMeshOverlay = entry && entry->get().meshOverlay.enabled;
  }
  if (usesMeshOverlay) {
    return makeObjectPipelineSignature(mesh->storage.get().getVertexBuffer(),
                                       PrimitiveTopology::LineList);
  }

  StringID meshSig = mesh->storage.get().getPipelineSignature();
  StringID fields[] = {meshSig};
  return GlobalStringTable::get().compose(TypeTag::ObjectRender, fields);
}

bool SceneNode::supportsPass(StringID pass) const {
  const auto materialComponent = getMaterialComponent(*this);
  const MaterialInstance *material =
      materialComponent
          ? resolveMaterialForNode(*this, materialComponent->get())
          : nullptr;
  return material && material->isPassEnabled(pass) &&
         m_validatedPasses.find(pass) != m_validatedPasses.end();
}

std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
SceneNode::getValidatedPassData(StringID pass) const {
  updateWorldTransformIfNeeded();
  auto it = m_validatedPasses.find(pass);
  if (it == m_validatedPasses.end())
    return std::nullopt;
  return std::cref(it->second);
}

void SceneNode::markWorldTransformDirty() {
  m_worldTransformDirty = true;
  pruneExpiredChildren();
  for (auto &childWeak : m_children) {
    if (auto child = childWeak.lock()) {
      child->markWorldTransformDirty();
    }
  }
}

void SceneNode::updateWorldTransformIfNeeded() const {
  if (!m_worldTransformDirty &&
      !(m_worldTransformHasParent && m_parent.expired())) {
    return;
  }

  Mat4f world = m_localTransform.toMat4();
  const auto parent = m_parent.lock();
  if (parent) {
    world = parent->getWorldTransform() * world;
  }

  m_worldTransform = world;
  m_worldTransformHasParent = static_cast<bool>(parent);
  m_worldTransformDirty = false;
}

void SceneNode::removeFromParentChildrenList() {
  auto parent = m_parent.lock();
  if (!parent) {
    return;
  }

  auto &siblings = parent->m_children;
  siblings.erase(
      std::remove_if(siblings.begin(), siblings.end(),
                     [this](const std::weak_ptr<SceneNode> &candidate) {
                       auto child = candidate.lock();
                       return !child || child.get() == this;
                     }),
      siblings.end());
}

void SceneNode::pruneExpiredChildren() {
  m_children.erase(
      std::remove_if(m_children.begin(), m_children.end(),
                     [](const std::weak_ptr<SceneNode> &candidate) {
                       return candidate.expired();
                     }),
      m_children.end());
}

std::vector<std::reference_wrapper<IComponent>> SceneNode::listComponents() {
  std::vector<std::reference_wrapper<IComponent>> out;
  out.reserve(m_components.size());
  for (auto &component : m_components) {
    out.push_back(*component);
  }
  return out;
}

std::vector<std::reference_wrapper<const IComponent>>
SceneNode::listComponents() const {
  std::vector<std::reference_wrapper<const IComponent>> out;
  out.reserve(m_components.size());
  for (const auto &component : m_components) {
    out.push_back(*component);
  }
  return out;
}

void SceneNode::rebuildValidatedCache() {
  m_validatedPasses.clear();
  m_overlayIndexBuffers.clear();

  if (m_nodeName.empty()) {
    throw std::logic_error("SceneNodeValidation empty nodeName");
  }

  const auto meshComponent = getMeshComponent(*this);
  const auto materialComponent = getMaterialComponent(*this);
  if (!meshComponent || !materialComponent) {
    return;
  }

  const auto mesh = resolveMeshForNode(*this, meshComponent->get());
  const MaterialInstance *material =
      resolveMaterialForNode(*this, materialComponent->get());
  if (!mesh || !material) {
    return;
  }
  if (!material->getTemplate()) {
    throw std::logic_error("SceneNodeValidation node=" + m_nodeName +
                           " missing mesh/material template");
  }

  const auto skeletonComponent = getSkeletonComponent(*this);
  const Skeleton *skeleton =
      skeletonComponent
          ? resolveSkeletonForNode(*this, skeletonComponent->get())
          : nullptr;
  const auto &meshSlice = mesh->mesh.get();
  const auto &geometryStorage = mesh->storage.get();
  const auto &layout = geometryStorage.getVertexLayout();
  const auto enabledPasses = material->getEnabledPasses();

  for (const auto &pass : enabledPasses) {
    auto entryOpt = material->getTemplate()->getPassDefinition(pass);
    if (!entryOpt) {
      continue;
    }

    const auto &entry = entryOpt->get();
    auto shader = entry.shaderProgram.getShader();
    if (!shader) {
      shader = material->getPassShader(pass);
    }
    if (!shader) {
      fatalValidation(*this, pass, *material, entry.shaderProgram,
                      "missing shader for enabled pass", std::cref(layout));
    }

    const bool usesSkinning =
        entry.shaderProgram.hasEnabledVariant("USE_SKINNING");
    const bool hasBonesBinding = shader->findBinding("Bones").has_value();

    if (usesSkinning != hasBonesBinding) {
      fatalValidation(*this, pass, *material, entry.shaderProgram,
                      "shader variant / Bones binding mismatch",
                      std::cref(layout));
    }
    if (usesSkinning && !skeleton) {
      fatalValidation(*this, pass, *material, entry.shaderProgram,
                      "skinning pass requires skeleton", std::cref(layout));
    }

    for (const auto &input : shader->getVertexInputs()) {
      auto layoutItem = findLayoutItem(layout, input.location);
      if (!layoutItem) {
        fatalValidation(*this, pass, *material, entry.shaderProgram,
                        "missing vertex input '" + input.name +
                            "' at location " + std::to_string(input.location),
                        std::cref(layout));
      }
      if (layoutItem->get().type != input.type) {
        fatalValidation(*this, pass, *material, entry.shaderProgram,
                        "vertex input type mismatch for '" + input.name +
                            "' at location " + std::to_string(input.location),
                        std::cref(layout));
      }
    }

    GpuResourceRef bonesResource;

    // Validate reserved system ABI contract and renderable-owned resources.
    for (const auto &binding : shader->getReflectionBindings()) {
      if (auto abiDiagnostic = validateSystemAbiBindingContract(binding)) {
        fatalValidation(*this, pass, *material, entry.shaderProgram,
                        *abiDiagnostic, std::cref(layout));
      }

      if (binding.name == "Bones") {
        if (!skeleton) {
          fatalValidation(*this, pass, *material, entry.shaderProgram,
                          "missing Bones resource", std::cref(layout));
        }
        bonesResource = GpuResourceRef{skeleton->getUBO()};
        continue;
      }

      if (!isMaterialOwnedBinding(binding.name)) {
        continue;
      }

      // Non-system-owned buffer bindings are structural material requirements.
      // Sampled resources may be intentionally left unset and gated by shader
      // parameters.
      if (binding.type != ShaderPropertyType::UniformBuffer &&
          binding.type != ShaderPropertyType::StorageBuffer) {
        continue;
      }

      const StringID bindingId(binding.name);
      const auto resource = material->getShaderBindingResource(bindingId);
      if (!resource.isValid()) {
        fatalValidation(*this, pass, *material, entry.shaderProgram,
                        "missing material-owned resource '" + binding.name +
                            "'",
                        std::cref(layout));
      }
    }

    ValidatedRenderablePassData data;
    data.pass = pass;
    data.materialHandle = materialComponent->get().getMaterialHandle();
    data.shaderInfo = shader;
    data.vertexBuffer = getVertexBuffer();
    data.indexBuffer = getIndexBuffer();
    data.bonesResource = std::move(bonesResource);
    data.renderState = material->getPassRenderState(pass);
    const BoundingBox worldBounds = getWorldBounds();
    data.sortCenter =
        worldBounds.isValid()
            ? worldBounds.getCenter()
            : Transform::fromMat4(getWorldTransform()).translation;
    data.objectSignature = getPipelineSignature(pass);
    data.materialSignature = material->getPipelineSignature(pass);

    if (entry.meshOverlay.enabled) {
      if (geometryStorage.getIndexBuffer().getTopology() !=
          PrimitiveTopology::TriangleList) {
        fatalValidation(*this, pass, *material, entry.shaderProgram,
                        "meshOverlay requires triangle-list source geometry",
                        std::cref(layout));
      }
      if (geometryStorage.getIndexBuffer().indexCount() % 3 != 0) {
        fatalValidation(*this, pass, *material, entry.shaderProgram,
                        "meshOverlay source index count is not triangular",
                        std::cref(layout));
      }

      auto overlayIndices = makeUniqueTriangleEdgeLineIndices(
          copyIndexBufferData(geometryStorage.getIndexBuffer()));
      auto overlayIndexBuffer = IndexBuffer::createUnique(
          std::move(overlayIndices), PrimitiveTopology::LineList);
      data.indexBuffer = GpuResourceRef{*overlayIndexBuffer};
      m_overlayIndexBuffers[pass] = std::move(overlayIndexBuffer);
    }

    m_validatedPasses[pass] = std::move(data);
  }
}

void SceneNode::clearComponents() {
  for (auto &component : m_components) {
    component->detachFromOwner();
  }
  m_components.clear();
}

void SceneNode::attachToScene(const std::weak_ptr<Scene> &scene) {
  m_scene = scene;
}

void SceneNode::detachFromScene() { m_scene.reset(); }

void SceneNode::setVisibilityLayerMask(const VisibilityLayerMask mask) {
  m_visibilityLayerMask = mask;
  emitRuntimeNodeChanged(SceneNodeAspect::Visibility);
}

void SceneNode::setDebugOnlyRenderable(const bool value) {
  if (m_debugOnlyRenderable == value) {
    return;
  }
  m_debugOnlyRenderable = value;
  emitRuntimeNodeChanged(SceneNodeAspect::RenderableStructure);
}

void SceneNode::warnIfSiblingNameIsDuplicated() const {
  if (m_isPathRoot || m_name.empty()) {
    return;
  }

  auto parent = m_parent.lock();
  if (parent) {
    for (const auto &siblingWeak : parent->m_children) {
      const auto sibling = siblingWeak.lock();
      if (!sibling || sibling.get() == this) {
        continue;
      }
      if (sibling->getName() != m_name) {
        continue;
      }
      std::cerr << "[WARN] Scene duplicate sibling name at parent "
                << parent->getPath() << ": '" << m_name
                << "' resolves to first inserted child\n";
      return;
    }
    return;
  }

  const auto scene = m_scene.lock();
  if (!scene) {
    return;
  }

  for (const auto &renderable : scene->getRenderables()) {
    const auto sibling = std::dynamic_pointer_cast<SceneNode>(renderable);
    if (!sibling || sibling.get() == this) {
      continue;
    }
    if (sibling->getParent()) {
      continue;
    }
    if (sibling->getName() != m_name) {
      continue;
    }
    std::cerr << "[WARN] Scene duplicate root node name in scene '"
              << scene->getSceneName() << "': '" << m_name
              << "' resolves to first inserted child\n";
    return;
  }
}

std::string SceneNode::getPathSegment() const {
  if (!m_name.empty()) {
    return m_name;
  }

  std::ostringstream oss;
  oss << "<unnamed-node-0x" << std::hex
      << reinterpret_cast<std::uintptr_t>(this) << ">";
  return oss.str();
}

std::string SceneNode::sanitizeName(std::string name) {
  bool mutated = false;
  for (char &c : name) {
    const unsigned char uc = static_cast<unsigned char>(c);
    const bool allowed =
        std::isalnum(uc) != 0 || c == '_' || c == '-' || c == '.';
    if (!allowed) {
      c = '_';
      mutated = true;
    }
  }
  assert(!mutated && "SceneNode::setName sanitized illegal characters");
  return name;
}

void SceneNode::emitRuntimeNodeChanged(const SceneNodeAspect aspect) const {
  const auto scene = m_scene.lock();
  if (!scene) {
    if (aspect == SceneNodeAspect::RenderableStructure) {
      const_cast<SceneNode &>(*this).rebuildValidatedCache();
    }
    return;
  }

  if (aspect == SceneNodeAspect::RenderableStructure) {
    scene->syncNodeResourceState(const_cast<SceneNode &>(*this));
    const_cast<SceneNode &>(*this).rebuildValidatedCache();
  } else if (aspect == SceneNodeAspect::Transform ||
             aspect == SceneNodeAspect::Visibility ||
             aspect == SceneNodeAspect::CameraProperties) {
    scene->syncNodeResourceState(const_cast<SceneNode &>(*this));
  }

  scene->events().emit(SceneEvent{
      .domain = SceneEventDomain::Runtime,
      .type = SceneEventType::SceneNodeChanged,
      .path = getPath(),
      .stableNodeName = getNodeName(),
      .aspects = {aspect},
  });
}

void SceneNode::emitRuntimeNodeLifecycle(const SceneEventType type) const {
  emitRuntimeNodeLifecycle(type, getPath());
}

void SceneNode::emitRuntimeNodeLifecycle(const SceneEventType type,
                                         const std::string &path) const {
  const auto scene = m_scene.lock();
  if (!scene) {
    return;
  }

  scene->events().emit(SceneEvent{
      .domain = SceneEventDomain::Runtime,
      .type = type,
      .path = path,
      .stableNodeName = getNodeName(),
  });
}

} // namespace LX_core
