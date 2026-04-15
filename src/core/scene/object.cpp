#include "object.hpp"
#include "scene.hpp"

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
  if (binding.name == "CameraUBO" || binding.name == "LightUBO")
    return false;
  if (binding.name == "MaterialUBO")
    return true;
  if (binding.name == "Bones")
    return true;
  return false;
}

ValidatedRenderablePassData
buildLegacyValidatedData(const RenderableSubMesh &sub, StringID pass) {
  ValidatedRenderablePassData data;
  data.pass = pass;
  data.material = sub.material;
  data.shaderInfo = sub.material ? sub.material->getShaderInfo(pass) : nullptr;
  data.objectInfo = sub.objectPC;
  data.vertexBuffer = sub.getVertexBuffer();
  data.indexBuffer = sub.getIndexBuffer();
  data.descriptorResources = sub.getDescriptorResources();
  data.passMask = sub.getPassMask();
  data.objectSignature = sub.getRenderSignature(pass);
  if (sub.material) {
    data.pipelineKey = PipelineKey::build(data.objectSignature,
                                          sub.material->getRenderSignature(pass));
  }
  return data;
}

} // namespace

SceneNode::SceneNode(std::string nodeName, MeshPtr mesh,
                     MaterialInstance::Ptr material, SkeletonPtr skeleton)
    : m_nodeName(std::move(nodeName)), m_mesh(std::move(mesh)),
      m_materialInstance(std::move(material)),
      m_objectPC(std::make_shared<ObjectPC>()) {
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

void SceneNode::setMaterialInstance(MaterialInstance::Ptr material) {
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

IRenderResourcePtr SceneNode::getVertexBuffer() const {
  return m_mesh ? std::static_pointer_cast<IRenderResource>(m_mesh->vertexBuffer)
                : nullptr;
}

IRenderResourcePtr SceneNode::getIndexBuffer() const {
  return m_mesh ? std::static_pointer_cast<IRenderResource>(m_mesh->indexBuffer)
                : nullptr;
}

std::vector<IRenderResourcePtr> SceneNode::getDescriptorResources() const {
  auto data = getValidatedPassData(Pass_Forward);
  if (data)
    return data->get().descriptorResources;
  return {};
}

ResourcePassFlag SceneNode::getPassMask() const {
  return m_materialInstance ? m_materialInstance->getPassFlag()
                            : ResourcePassFlag::Forward;
}

IShaderPtr SceneNode::getShaderInfo() const {
  auto data = getValidatedPassData(Pass_Forward);
  if (data)
    return data->get().shaderInfo;
  return m_materialInstance ? m_materialInstance->getShaderInfo() : nullptr;
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
    auto entryOpt = m_materialInstance->getTemplate()->getEntry(pass);
    if (!entryOpt) {
      continue;
    }

    const auto &entry = entryOpt->get();
    auto shader = entry.shaderSet.getShader();
    if (!shader) {
      shader = m_materialInstance->getShaderInfo(pass);
    }
    if (!shader) {
      fatalValidation(*this, pass, *m_materialInstance, entry.shaderSet,
                      "missing shader for enabled pass", &layout);
    }

    const bool usesSkinning =
        entry.shaderSet.hasEnabledVariant("USE_SKINNING");
    const bool hasBonesBinding =
        shader->findBinding("Bones").has_value();

    if (usesSkinning != hasBonesBinding) {
      fatalValidation(*this, pass, *m_materialInstance, entry.shaderSet,
                      "shader variant / Bones binding mismatch", &layout);
    }
    if (usesSkinning && (!m_skeleton.has_value() || !m_skeleton.value())) {
      fatalValidation(*this, pass, *m_materialInstance, entry.shaderSet,
                      "skinning pass requires skeleton", &layout);
    }

    for (const auto &input : shader->getVertexInputs()) {
      const auto *layoutItem = findLayoutItem(layout, input.location);
      if (!layoutItem) {
        fatalValidation(*this, pass, *m_materialInstance, entry.shaderSet,
                        "missing vertex input '" + input.name +
                            "' at location " + std::to_string(input.location),
                        &layout);
      }
      if (layoutItem->type != input.type) {
        fatalValidation(*this, pass, *m_materialInstance, entry.shaderSet,
                        "vertex input type mismatch for '" + input.name +
                            "' at location " + std::to_string(input.location),
                        &layout);
      }
    }

    auto descriptorResources = m_materialInstance->getDescriptorResources();
    bool hasMaterialUbo = false;
    for (const auto &res : descriptorResources) {
      if (res && res->getBindingName() == StringID("MaterialUBO")) {
        hasMaterialUbo = true;
        break;
      }
    }

    for (const auto &binding : shader->getReflectionBindings()) {
      if (!requiresRenderableOwnedResource(binding))
        continue;
      if (binding.name == "MaterialUBO" && !hasMaterialUbo) {
        fatalValidation(*this, pass, *m_materialInstance, entry.shaderSet,
                        "missing MaterialUBO resource", &layout);
      }
      if (binding.name == "Bones") {
        if (!m_skeleton.has_value() || !m_skeleton.value()) {
          fatalValidation(*this, pass, *m_materialInstance, entry.shaderSet,
                          "missing Bones resource", &layout);
        }
        descriptorResources.push_back(std::static_pointer_cast<IRenderResource>(
            m_skeleton.value()->getUBO()));
      }
    }

    ValidatedRenderablePassData data;
    data.pass = pass;
    data.material = m_materialInstance;
    data.shaderInfo = shader;
    data.objectInfo = m_objectPC;
    data.vertexBuffer = getVertexBuffer();
    data.indexBuffer = getIndexBuffer();
    data.descriptorResources = std::move(descriptorResources);
    data.passMask = m_materialInstance->getPassFlag();
    data.objectSignature = getRenderSignature(pass);
    data.pipelineKey = PipelineKey::build(
        data.objectSignature, m_materialInstance->getRenderSignature(pass));
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

RenderableSubMesh::RenderableSubMesh(MeshPtr mesh_, MaterialPtr material_,
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
  objectPC = std::make_shared<ObjectPC>(
      material ? material->getPassFlag() : ResourcePassFlag::Forward);
}

IRenderResourcePtr RenderableSubMesh::getVertexBuffer() const {
  return mesh ? std::static_pointer_cast<IRenderResource>(mesh->vertexBuffer)
              : nullptr;
}

IRenderResourcePtr RenderableSubMesh::getIndexBuffer() const {
  return mesh ? std::static_pointer_cast<IRenderResource>(mesh->indexBuffer)
              : nullptr;
}

std::vector<IRenderResourcePtr> RenderableSubMesh::getDescriptorResources() const {
  std::vector<IRenderResourcePtr> ret;
  if (!material)
    return ret;
  auto res = material->getDescriptorResources();
  ret.insert(ret.end(), res.begin(), res.end());
  if (skeleton.has_value()) {
    ret.push_back(
        std::static_pointer_cast<IRenderResource>(skeleton.value()->getUBO()));
  }
  return ret;
}

ResourcePassFlag RenderableSubMesh::getPassMask() const {
  return material ? material->getPassFlag() : ResourcePassFlag::Forward;
}

IShaderPtr RenderableSubMesh::getShaderInfo() const {
  return material ? material->getShaderInfo() : nullptr;
}

StringID RenderableSubMesh::getRenderSignature(StringID pass) const {
  if (!mesh)
    return StringID{};
  StringID meshSig = mesh->getRenderSignature(pass);
  StringID fields[] = {meshSig};
  return GlobalStringTable::get().compose(TypeTag::ObjectRender, fields);
}

bool RenderableSubMesh::supportsPass(StringID pass) const {
  const auto flag = passFlagFromStringID(pass);
  return (static_cast<std::uint32_t>(getPassMask()) &
          static_cast<std::uint32_t>(flag)) != 0;
}

std::optional<std::reference_wrapper<const ValidatedRenderablePassData>>
RenderableSubMesh::getValidatedPassData(StringID pass) const {
  if (!supportsPass(pass) || !mesh || !material)
    return std::nullopt;
  m_lastValidatedData = buildLegacyValidatedData(*this, pass);
  return std::cref(m_lastValidatedData.value());
}

} // namespace LX_core
