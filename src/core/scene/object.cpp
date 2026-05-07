#include "object.hpp"
#include "scene.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
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

[[noreturn]] void fatalValidation(const SceneNode &node, StringID pass,
                                  const MaterialInstance &material,
                                  const ShaderProgramSet &programSet,
                                  const std::string &reason,
                                  std::optional<std::reference_wrapper<
                                      const VertexLayout>> layout =
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

bool requiresRenderableOwnedResource(const ShaderResourceBinding &binding) {
  if (isSystemOwnedBinding(binding.name)) {
    // Bones is system-owned (not material-owned) but still provided by the
    // renderable's skeleton, so it counts as renderable-owned.
    return binding.name == "Bones";
  }
  // Buffer descriptors are structural requirements. Sampled resources may be
  // intentionally left unset and gated by shader parameters.
  return binding.type == ShaderPropertyType::UniformBuffer ||
         binding.type == ShaderPropertyType::StorageBuffer;
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
    : m_nodeName("__scene_root__"), m_perDrawData(std::make_shared<PerDrawData>()),
      m_isPathRoot(true) {}

SceneNode::SceneNode(std::string nodeName)
    : m_nodeName(std::move(nodeName)),
      m_perDrawData(std::make_shared<PerDrawData>()) {
  syncPerDrawModelMatrix();
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
}

void SceneNode::setTranslation(const Vec3f &translation) {
  m_localTransform.translation = translation;
  markWorldTransformDirty();
}

void SceneNode::setRotation(const Quatf &rotation) {
  m_localTransform.rotation = rotation.normalized();
  markWorldTransformDirty();
}

void SceneNode::setScale(const Vec3f &scale) {
  m_localTransform.scale = scale;
  markWorldTransformDirty();
}

void SceneNode::setName(std::string name) {
  m_name = sanitizeName(std::move(name));
  warnIfSiblingNameIsDuplicated();
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

  const auto parentPath = parent->getPath();
  if (parentPath == "/") {
    return parentPath + pathSegment;
  }
  return parentPath + "/" + pathSegment;
}

const Mat4f &SceneNode::getWorldTransform() const {
  updateWorldTransformIfNeeded();
  return m_worldTransform;
}

BoundingBox SceneNode::getLocalBounds() const {
  const auto meshComponent = getMeshComponent(*this);
  if (!meshComponent || !meshComponent->get().getMesh()) {
    return {};
  }
  return meshComponent->get().getMesh()->bounds;
}

BoundingBox SceneNode::getWorldBounds() const {
  const BoundingBox localBounds = getLocalBounds();
  if (!localBounds.isValid()) {
    return localBounds;
  }
  return localBounds.transformed(getWorldTransform());
}

void SceneNode::setParent(const SharedPtr &parent) {
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
}

void SceneNode::clearParent() {
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

IGpuResourceSharedPtr SceneNode::getVertexBuffer() const {
  const auto meshComponent = getMeshComponent(*this);
  if (!meshComponent || !meshComponent->get().getMesh()) {
    return nullptr;
  }
  return std::static_pointer_cast<IGpuResource>(
      meshComponent->get().getMesh()->vertexBuffer);
}

IGpuResourceSharedPtr SceneNode::getIndexBuffer() const {
  const auto meshComponent = getMeshComponent(*this);
  if (!meshComponent || !meshComponent->get().getMesh()) {
    return nullptr;
  }
  return std::static_pointer_cast<IGpuResource>(
      meshComponent->get().getMesh()->indexBuffer);
}

std::vector<IGpuResourceSharedPtr>
SceneNode::getDescriptorResources(StringID pass) const {
  auto data = getValidatedPassData(pass);
  if (data)
    return data->get().descriptorResources;
  return {};
}

IShaderSharedPtr SceneNode::getShaderInfo() const {
  auto data = getValidatedPassData(Pass_Forward);
  if (data)
    return data->get().shaderInfo;
  const auto materialComponent = getMaterialComponent(*this);
  if (!materialComponent || !materialComponent->get().getMaterialInstance()) {
    return nullptr;
  }
  return materialComponent->get().getMaterialInstance()->getPassShader(
      Pass_Forward);
}

PerDrawDataSharedPtr SceneNode::getPerDrawData() const {
  updateWorldTransformIfNeeded();
  return m_perDrawData;
}

StringID SceneNode::getPipelineSignature(StringID pass) const {
  const auto meshComponent = getMeshComponent(*this);
  if (!meshComponent || !meshComponent->get().getMesh())
    return StringID{};
  StringID meshSig = meshComponent->get().getMesh()->getPipelineSignature(pass);
  StringID fields[] = {meshSig};
  return GlobalStringTable::get().compose(TypeTag::ObjectRender, fields);
}

bool SceneNode::supportsPass(StringID pass) const {
  const auto materialComponent = getMaterialComponent(*this);
  return materialComponent &&
         materialComponent->get().getMaterialInstance() &&
         materialComponent->get().getMaterialInstance()->isPassEnabled(pass) &&
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
  syncPerDrawModelMatrix();
  m_worldTransformDirty = false;
}

void SceneNode::syncPerDrawModelMatrix() const {
  if (m_perDrawData) {
    m_perDrawData->updateModelMatrix(m_worldTransform);
  }
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

  if (m_nodeName.empty()) {
    throw std::logic_error("SceneNodeValidation empty nodeName");
  }

  const auto meshComponent = getMeshComponent(*this);
  const auto materialComponent = getMaterialComponent(*this);
  if (!meshComponent || !materialComponent) {
    return;
  }

  const MeshSharedPtr mesh = meshComponent->get().getMesh();
  const MaterialInstanceSharedPtr material =
      materialComponent->get().getMaterialInstance();
  if (!mesh || !material || !material->getTemplate()) {
    throw std::logic_error("SceneNodeValidation node=" + m_nodeName +
                           " missing mesh/material template");
  }

  const auto skeletonComponent = getSkeletonComponent(*this);
  const SkeletonSharedPtr skeleton =
      skeletonComponent ? skeletonComponent->get().getSkeleton()
                        : SkeletonSharedPtr{};
  const auto &layout = mesh->getVertexLayout();
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
    const bool hasBonesBinding =
        shader->findBinding("Bones").has_value();

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

    auto descriptorResources = material->getDescriptorResources(pass);

    // Validate reserved-name type contract and renderable-owned resources.
    for (const auto &binding : shader->getReflectionBindings()) {
      // REQ-031 R3: reserved-name type misuse is a fatal authoring error.
      auto expectedType = getExpectedTypeForSystemBinding(binding.name);
      if (expectedType && binding.type != *expectedType) {
        fatalValidation(
            *this, pass, *material, entry.shaderProgram,
            "reserved binding '" + binding.name +
                "' has wrong descriptor type (shader authoring error)",
            std::cref(layout));
      }

      if (!requiresRenderableOwnedResource(binding))
        continue;

      if (binding.name == "Bones") {
        if (!skeleton) {
          fatalValidation(*this, pass, *material, entry.shaderProgram,
                          "missing Bones resource", std::cref(layout));
        }
        descriptorResources.push_back(std::static_pointer_cast<IGpuResource>(
            skeleton->getUBO()));
        continue;
      }

      // Non-system-owned binding: verify material provides a matching resource.
      const StringID bindingId(binding.name);
      bool found = false;
      for (const auto &res : descriptorResources) {
        if (res && res->getBindingName() == bindingId) {
          found = true;
          break;
        }
      }
      if (!found) {
        fatalValidation(*this, pass, *material, entry.shaderProgram,
                        "missing material-owned resource '" + binding.name +
                            "'",
                        std::cref(layout));
      }
    }

    ValidatedRenderablePassData data;
    data.pass = pass;
    data.material = material;
    data.shaderInfo = shader;
    data.drawData = m_perDrawData;
    data.vertexBuffer = getVertexBuffer();
    data.indexBuffer = getIndexBuffer();
    data.descriptorResources = std::move(descriptorResources);
    data.objectSignature = getPipelineSignature(pass);
    data.pipelineKey = PipelineKey::build(
        data.objectSignature, material->getPipelineSignature(pass));
    m_validatedPasses[pass] = std::move(data);
  }
}

void SceneNode::clearComponents() {
  for (auto &component : m_components) {
    component->detachFromOwner();
  }
  m_components.clear();
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
    const bool allowed = std::isalnum(uc) != 0 || c == '_' || c == '-';
    if (!allowed) {
      c = '_';
      mutated = true;
    }
  }
  assert(!mutated && "SceneNode::setName sanitized illegal characters");
  return name;
}

} // namespace LX_core
