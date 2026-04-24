#include "object.hpp"
#include "scene.hpp"
#include "core/asset/shader_binding_ownership.hpp"

#include <algorithm>
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

} // namespace

SceneNode::SceneNode(std::string nodeName, MeshSharedPtr mesh,
                     MaterialInstanceSharedPtr material, SkeletonSharedPtr skeleton)
    : m_nodeName(std::move(nodeName)), m_mesh(std::move(mesh)),
      m_materialInstance(std::move(material)),
      m_perDrawData(std::make_shared<PerDrawData>()) {
  if (skeleton) {
    m_skeleton = std::move(skeleton);
  }
  registerMaterialPassListener();
  syncPerDrawModelMatrix();
  rebuildValidatedCache();
}

SceneNode::~SceneNode() {
  clearParent();
  unregisterMaterialPassListener();
}

void SceneNode::setMesh(MeshSharedPtr mesh) {
  m_mesh = std::move(mesh);
  rebuildValidatedCache();
}

void SceneNode::setMaterialInstance(MaterialInstanceSharedPtr material) {
  unregisterMaterialPassListener();
  m_materialInstance = std::move(material);
  registerMaterialPassListener();
  rebuildValidatedCache();
}

void SceneNode::setSkeleton(SkeletonSharedPtr skeleton) {
  if (skeleton) {
    m_skeleton = std::move(skeleton);
  } else {
    m_skeleton.reset();
  }
  rebuildValidatedCache();
}

void SceneNode::setLocalTransform(const Mat4f &transform) {
  m_localTransform = transform;
  markWorldTransformDirty();
}

const Mat4f &SceneNode::getWorldTransform() const {
  updateWorldTransformIfNeeded();
  return m_worldTransform;
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

IGpuResourceSharedPtr SceneNode::getVertexBuffer() const {
  return m_mesh ? std::static_pointer_cast<IGpuResource>(m_mesh->vertexBuffer)
                : nullptr;
}

IGpuResourceSharedPtr SceneNode::getIndexBuffer() const {
  return m_mesh ? std::static_pointer_cast<IGpuResource>(m_mesh->indexBuffer)
                : nullptr;
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
  return m_materialInstance ? m_materialInstance->getPassShader(Pass_Forward)
                            : nullptr;
}

PerDrawDataSharedPtr SceneNode::getPerDrawData() const {
  updateWorldTransformIfNeeded();
  return m_perDrawData;
}

StringID SceneNode::getPipelineSignature(StringID pass) const {
  if (!m_mesh)
    return StringID{};
  StringID meshSig = m_mesh->getPipelineSignature(pass);
  StringID fields[] = {meshSig};
  return GlobalStringTable::get().compose(TypeTag::ObjectRender, fields);
}

bool SceneNode::supportsPass(StringID pass) const {
  return m_materialInstance && m_materialInstance->isPassEnabled(pass) &&
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

  Mat4f world = m_localTransform;
  const auto parent = m_parent.lock();
  if (parent) {
    world = parent->getWorldTransform() * m_localTransform;
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

void SceneNode::rebuildValidatedCache() {
  m_validatedPasses.clear();

  if (m_nodeName.empty()) {
    throw std::logic_error("SceneNodeValidation empty nodeName");
  }
  if (!m_mesh || !m_materialInstance || !m_materialInstance->getTemplate()) {
    throw std::logic_error("SceneNodeValidation node=" + m_nodeName +
                           " missing mesh/material template");
  }

  const auto &layout = m_mesh->getVertexLayout();
  const auto enabledPasses = m_materialInstance->getEnabledPasses();

  for (const auto &pass : enabledPasses) {
    auto entryOpt = m_materialInstance->getTemplate()->getPassDefinition(pass);
    if (!entryOpt) {
      continue;
    }

    const auto &entry = entryOpt->get();
    auto shader = entry.shaderProgram.getShader();
    if (!shader) {
      shader = m_materialInstance->getPassShader(pass);
    }
    if (!shader) {
      fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                      "missing shader for enabled pass", std::cref(layout));
    }

    const bool usesSkinning =
        entry.shaderProgram.hasEnabledVariant("USE_SKINNING");
    const bool hasBonesBinding =
        shader->findBinding("Bones").has_value();

    if (usesSkinning != hasBonesBinding) {
      fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                      "shader variant / Bones binding mismatch",
                      std::cref(layout));
    }
    if (usesSkinning && (!m_skeleton.has_value() || !m_skeleton.value())) {
      fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                      "skinning pass requires skeleton", std::cref(layout));
    }

    for (const auto &input : shader->getVertexInputs()) {
      auto layoutItem = findLayoutItem(layout, input.location);
      if (!layoutItem) {
        fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                        "missing vertex input '" + input.name +
                            "' at location " + std::to_string(input.location),
                        std::cref(layout));
      }
      if (layoutItem->get().type != input.type) {
        fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                        "vertex input type mismatch for '" + input.name +
                            "' at location " + std::to_string(input.location),
                        std::cref(layout));
      }
    }

    auto descriptorResources = m_materialInstance->getDescriptorResources(pass);

    // Validate reserved-name type contract and renderable-owned resources.
    for (const auto &binding : shader->getReflectionBindings()) {
      // REQ-031 R3: reserved-name type misuse is a fatal authoring error.
      auto expectedType = getExpectedTypeForSystemBinding(binding.name);
      if (expectedType && binding.type != *expectedType) {
        fatalValidation(
            *this, pass, *m_materialInstance, entry.shaderProgram,
            "reserved binding '" + binding.name +
                "' has wrong descriptor type (shader authoring error)",
            std::cref(layout));
      }

      if (!requiresRenderableOwnedResource(binding))
        continue;

      if (binding.name == "Bones") {
        if (!m_skeleton.has_value() || !m_skeleton.value()) {
          fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                          "missing Bones resource", std::cref(layout));
        }
        descriptorResources.push_back(std::static_pointer_cast<IGpuResource>(
            m_skeleton.value()->getUBO()));
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
        fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                        "missing material-owned resource '" + binding.name +
                            "'",
                        std::cref(layout));
      }
    }

    ValidatedRenderablePassData data;
    data.pass = pass;
    data.material = m_materialInstance;
    data.shaderInfo = shader;
    data.drawData = m_perDrawData;
    data.vertexBuffer = getVertexBuffer();
    data.indexBuffer = getIndexBuffer();
    data.descriptorResources = std::move(descriptorResources);
    data.objectSignature = getPipelineSignature(pass);
    data.pipelineKey = PipelineKey::build(
        data.objectSignature, m_materialInstance->getPipelineSignature(pass));
    m_validatedPasses[pass] = std::move(data);
  }
}

void SceneNode::registerMaterialPassListener() {
  if (!m_materialInstance)
    return;
  m_materialPassListenerId = m_materialInstance->addPassStateListener([this]() {
    if (auto scene = m_scene.lock(); scene && m_materialInstance) {
      scene->revalidateNodesUsing(m_materialInstance);
      return;
    }
    rebuildValidatedCache();
  });
}

void SceneNode::unregisterMaterialPassListener() {
  if (m_materialInstance && m_materialPassListenerId != 0) {
    m_materialInstance->removePassStateListener(m_materialPassListenerId);
    m_materialPassListenerId = 0;
  }
}

} // namespace LX_core
