#include "object.hpp"
#include "scene.hpp"
#include "core/asset/shader_binding_ownership.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <utility>

namespace LX_core {

namespace {

uint64_t nextLegacyRenderableId() {
  static uint64_t counter = 0;
  return ++counter;
}

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
                                  const VertexLayout *layout = nullptr) {
  std::cerr << "FATAL [SceneNodeValidation] node=" << node.getNodeName()
            << " pass=" << GlobalStringTable::get().toDebugString(pass)
            << " material="
            << (material.getTemplate() ? material.getTemplate()->getName()
                                       : std::string("<null>"))
            << " shader=" << programSet.shaderName
            << " variants=" << variantsDebugString(programSet)
            << " reason=" << reason;
  if (layout) {
    std::cerr << " vertexLayout=" << vertexLayoutDebugString(*layout);
  }
  std::cerr << std::endl;
  std::terminate();
}

const VertexLayoutItem *findLayoutItem(const VertexLayout &layout,
                                       uint32_t location) {
  for (const auto &item : layout.getItems()) {
    if (item.location == location)
      return &item;
  }
  return nullptr;
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

ValidatedRenderablePassData
buildLegacyValidatedData(const RenderableSubMesh &sub, StringID pass) {
  ValidatedRenderablePassData data;
  data.pass = pass;
  data.material = sub.material;
  data.shaderInfo = sub.material ? sub.material->getPassShader(pass) : nullptr;
  data.drawData = sub.perDrawData;
  data.vertexBuffer = sub.getVertexBuffer();
  data.indexBuffer = sub.getIndexBuffer();
  data.descriptorResources = sub.getDescriptorResources(pass);
  data.objectSignature = sub.getRenderSignature(pass);
  if (sub.material) {
    data.pipelineKey = PipelineKey::build(
        data.objectSignature, sub.material->getMaterialSignature(pass));
  }
  return data;
}

} // namespace

SceneNode::SceneNode(std::string nodeName, MeshPtr mesh,
                     MaterialInstancePtr material, SkeletonPtr skeleton)
    : m_nodeName(std::move(nodeName)), m_mesh(std::move(mesh)),
      m_materialInstance(std::move(material)),
      m_perDrawData(std::make_shared<PerDrawData>()) {
  if (skeleton) {
    m_skeleton = std::move(skeleton);
  }
  registerMaterialPassListener();
  rebuildValidatedCache();
}

SceneNode::~SceneNode() { unregisterMaterialPassListener(); }

void SceneNode::setMesh(MeshPtr mesh) {
  m_mesh = std::move(mesh);
  rebuildValidatedCache();
}

void SceneNode::setMaterialInstance(MaterialInstancePtr material) {
  unregisterMaterialPassListener();
  m_materialInstance = std::move(material);
  registerMaterialPassListener();
  rebuildValidatedCache();
}

void SceneNode::setSkeleton(SkeletonPtr skeleton) {
  if (skeleton) {
    m_skeleton = std::move(skeleton);
  } else {
    m_skeleton.reset();
  }
  rebuildValidatedCache();
}

IGpuResourcePtr SceneNode::getVertexBuffer() const {
  return m_mesh ? std::static_pointer_cast<IGpuResource>(m_mesh->vertexBuffer)
                : nullptr;
}

IGpuResourcePtr SceneNode::getIndexBuffer() const {
  return m_mesh ? std::static_pointer_cast<IGpuResource>(m_mesh->indexBuffer)
                : nullptr;
}

std::vector<IGpuResourcePtr>
SceneNode::getDescriptorResources(StringID pass) const {
  auto data = getValidatedPassData(pass);
  if (data)
    return data->get().descriptorResources;
  return {};
}

IShaderPtr SceneNode::getShaderInfo() const {
  auto data = getValidatedPassData(Pass_Forward);
  if (data)
    return data->get().shaderInfo;
  return m_materialInstance ? m_materialInstance->getPassShader(Pass_Forward)
                            : nullptr;
}

StringID SceneNode::getRenderSignature(StringID pass) const {
  if (!m_mesh)
    return StringID{};
  StringID meshSig = m_mesh->getRenderSignature(pass);
  StringID fields[] = {meshSig};
  return GlobalStringTable::get().compose(TypeTag::ObjectRender, fields);
}

bool SceneNode::supportsPass(StringID pass) const {
  return m_materialInstance && m_materialInstance->isPassEnabled(pass) &&
         m_validatedPasses.find(pass) != m_validatedPasses.end();
}

std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
SceneNode::getValidatedPassData(StringID pass) const {
  auto it = m_validatedPasses.find(pass);
  if (it == m_validatedPasses.end())
    return std::nullopt;
  return std::cref(it->second);
}

void SceneNode::rebuildValidatedCache() {
  m_validatedPasses.clear();

  if (m_nodeName.empty()) {
    std::cerr << "FATAL [SceneNodeValidation] empty nodeName" << std::endl;
    std::terminate();
  }
  if (!m_mesh || !m_materialInstance || !m_materialInstance->getTemplate()) {
    std::cerr << "FATAL [SceneNodeValidation] node=" << m_nodeName
              << " missing mesh/material template" << std::endl;
    std::terminate();
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
                      "missing shader for enabled pass", &layout);
    }

    const bool usesSkinning =
        entry.shaderProgram.hasEnabledVariant("USE_SKINNING");
    const bool hasBonesBinding =
        shader->findBinding("Bones").has_value();

    if (usesSkinning != hasBonesBinding) {
      fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                      "shader variant / Bones binding mismatch", &layout);
    }
    if (usesSkinning && (!m_skeleton.has_value() || !m_skeleton.value())) {
      fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                      "skinning pass requires skeleton", &layout);
    }

    for (const auto &input : shader->getVertexInputs()) {
      const auto *layoutItem = findLayoutItem(layout, input.location);
      if (!layoutItem) {
        fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                        "missing vertex input '" + input.name +
                            "' at location " + std::to_string(input.location),
                        &layout);
      }
      if (layoutItem->type != input.type) {
        fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                        "vertex input type mismatch for '" + input.name +
                            "' at location " + std::to_string(input.location),
                        &layout);
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
            &layout);
      }

      if (!requiresRenderableOwnedResource(binding))
        continue;

      if (binding.name == "Bones") {
        if (!m_skeleton.has_value() || !m_skeleton.value()) {
          fatalValidation(*this, pass, *m_materialInstance, entry.shaderProgram,
                          "missing Bones resource", &layout);
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
                        &layout);
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
    data.objectSignature = getRenderSignature(pass);
    data.pipelineKey = PipelineKey::build(
        data.objectSignature, m_materialInstance->getMaterialSignature(pass));
    m_validatedPasses[pass] = std::move(data);
  }
}

void SceneNode::registerMaterialPassListener() {
  if (!m_materialInstance)
    return;
  m_materialPassListenerId = m_materialInstance->addPassStateListener([this]() {
    if (m_scene && m_materialInstance) {
      m_scene->revalidateNodesUsing(m_materialInstance);
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

RenderableSubMesh::RenderableSubMesh(MeshPtr mesh_,
                                     MaterialInstancePtr material_,
                                     SkeletonPtr skeleton_,
                                     std::string nodeName_)
    : mesh(std::move(mesh_)), material(std::move(material_)),
      nodeName(std::move(nodeName_)) {
  if (nodeName == "RenderableSubMesh") {
    nodeName += "_" + std::to_string(nextLegacyRenderableId());
  }
  if (skeleton_) {
    skeleton = skeleton_;
  }
  perDrawData = std::make_shared<PerDrawData>();
}

IGpuResourcePtr RenderableSubMesh::getVertexBuffer() const {
  return mesh ? std::static_pointer_cast<IGpuResource>(mesh->vertexBuffer)
              : nullptr;
}

IGpuResourcePtr RenderableSubMesh::getIndexBuffer() const {
  return mesh ? std::static_pointer_cast<IGpuResource>(mesh->indexBuffer)
              : nullptr;
}

std::vector<IGpuResourcePtr>
RenderableSubMesh::getDescriptorResources(StringID pass) const {
  std::vector<IGpuResourcePtr> ret;
  if (!material)
    return ret;
  auto res = material->getDescriptorResources(pass);
  ret.insert(ret.end(), res.begin(), res.end());
  if (skeleton.has_value()) {
    ret.push_back(
        std::static_pointer_cast<IGpuResource>(skeleton.value()->getUBO()));
  }
  return ret;
}

IShaderPtr RenderableSubMesh::getShaderInfo() const {
  return material ? material->getPassShader(Pass_Forward) : nullptr;
}

StringID RenderableSubMesh::getRenderSignature(StringID pass) const {
  if (!mesh)
    return StringID{};
  StringID meshSig = mesh->getRenderSignature(pass);
  StringID fields[] = {meshSig};
  return GlobalStringTable::get().compose(TypeTag::ObjectRender, fields);
}

bool RenderableSubMesh::supportsPass(StringID pass) const {
  return material && material->isPassEnabled(pass) &&
         material->getTemplate() &&
         material->getTemplate()->getPassDefinition(pass).has_value();
}

std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
RenderableSubMesh::getValidatedPassData(StringID pass) const {
  if (!supportsPass(pass) || !mesh || !material)
    return std::nullopt;
  m_lastValidatedData = buildLegacyValidatedData(*this, pass);
  return std::cref(m_lastValidatedData.value());
}

} // namespace LX_core
