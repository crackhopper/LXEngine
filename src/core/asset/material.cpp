#include "material.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace LX_core {

namespace {

const RenderPassEntry *firstEntry(const MaterialTemplate &tmpl) {
  // Transitional: old accessors still assume a single Forward-only pass.
  // REQ-007 will route these through the render-signature pipeline later.
  auto opt = tmpl.getEntry(StringID("Forward"));
  if (opt)
    return &opt->get();
  return nullptr;
}

} // namespace

/*****************************************************************
 * MaterialInstance
 *****************************************************************/

MaterialInstance::MaterialInstance(Token, MaterialTemplate::Ptr tmpl,
                                   ResourcePassFlag passFlag)
    : m_template(std::move(tmpl)), m_passFlag(passFlag) {
  if (!m_template || !m_template->getShader()) {
    return;
  }
  // Convention: the per-material UBO block is named `MaterialUBO` in GLSL.
  // Scene-level UBOs (LightUBO, CameraUBO, Bones) also show up in
  // reflection but belong to other owners. If a future shader uses a
  // different name, promote this to a configurable lookup key.
  // TODO(multi-ubo): promote to a vector when a real shader needs 2+ UBOs.
  const auto &bindings = m_template->getShader()->getReflectionBindings();
  for (const auto &b : bindings) {
    if (b.type != ShaderPropertyType::UniformBuffer)
      continue;
    if (b.name != "MaterialUBO")
      continue;
    m_uboBinding = &b;
    m_uboBuffer.assign(b.size, uint8_t{0});
    m_uboResource = std::make_shared<UboByteBufferResource>(m_uboBuffer, b.size,
                                                            m_passFlag);
    break;
  }
}

void MaterialInstance::writeUboMember(StringID id, const void *src,
                                      size_t nbytes,
                                      ShaderPropertyType expected) {
  if (!m_uboBinding) {
    assert(false && "MaterialInstance has no UBO binding");
    return;
  }
  for (const auto &m : m_uboBinding->members) {
    if (StringID(m.name) != id)
      continue;
    assert(m.type == expected &&
           "MaterialInstance setter type does not match reflected member type");
    assert(static_cast<size_t>(m.offset) + nbytes <= m_uboBuffer.size() &&
           "UBO write would overflow the reflected buffer");
    std::memcpy(m_uboBuffer.data() + m.offset, src, nbytes);
    m_uboDirty = true;
    return;
  }
  assert(false && "MaterialInstance setter: member not found in UBO");
}

void MaterialInstance::setVec4(StringID id, const Vec4f &value) {
  writeUboMember(id, &value, sizeof(Vec4f), ShaderPropertyType::Vec4);
}

void MaterialInstance::setVec3(StringID id, const Vec3f &value) {
  // std140: vec3 occupies 12 bytes; if the next member packs into the
  // trailing 4 bytes of the 16-byte bucket, writing 16 would clobber it.
  writeUboMember(id, &value, sizeof(float) * 3, ShaderPropertyType::Vec3);
}

void MaterialInstance::setFloat(StringID id, float value) {
  writeUboMember(id, &value, sizeof(float), ShaderPropertyType::Float);
}

void MaterialInstance::setInt(StringID id, int32_t value) {
  writeUboMember(id, &value, sizeof(int32_t), ShaderPropertyType::Int);
}

void MaterialInstance::setTexture(StringID id, CombinedTextureSamplerPtr tex) {
  auto bindingOpt = m_template->findBinding(id);
  assert(bindingOpt && "texture binding not found in reflection");
  const auto type = bindingOpt->get().type;
  assert((type == ShaderPropertyType::Texture2D ||
          type == ShaderPropertyType::TextureCube) &&
         "setTexture target is not a sampled image binding");
  (void)type;
  m_textures[id] = std::move(tex);
}

void MaterialInstance::updateUBO() {
  if (!m_uboDirty || !m_uboResource)
    return;
  m_uboResource->setDirty();
  m_uboDirty = false;
}

std::vector<IRenderResourcePtr>
MaterialInstance::getDescriptorResources() const {
  std::vector<IRenderResourcePtr> out;

  if (m_uboResource && !m_uboBuffer.empty()) {
    out.push_back(m_uboResource);
  }

  // Collect textures sorted by (set << 16 | binding) so the descriptor
  // list appears in a deterministic order matching the shader layout.
  // `CombinedTextureSampler` inherits from `IRenderResource`, so the
  // conversion is a plain base-class upcast — no dynamic_cast needed.
  // Also stamps the reflected binding name onto each texture so backend
  // descriptor routing can find it by name.
  std::vector<std::pair<uint32_t, IRenderResourcePtr>> sorted;
  sorted.reserve(m_textures.size());
  for (const auto &[id, tex] : m_textures) {
    if (!tex)
      continue;
    auto b = m_template->findBinding(id);
    if (!b)
      continue;
    tex->setBindingName(id);
    const uint32_t key = (b->get().set << 16) | b->get().binding;
    sorted.emplace_back(key, std::static_pointer_cast<IRenderResource>(tex));
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  for (auto &[_, r] : sorted) {
    out.push_back(std::move(r));
  }

  return out;
}

IShaderPtr MaterialInstance::getShaderInfo() const {
  return m_template ? m_template->getShader() : nullptr;
}

RenderState MaterialInstance::getRenderState() const {
  const auto *entry = m_template ? firstEntry(*m_template) : nullptr;
  return entry ? entry->renderState : RenderState{};
}

StringID MaterialInstance::getRenderSignature(StringID pass) const {
  if (!m_template)
    return StringID{};
  StringID passSig = m_template->getRenderPassSignature(pass);
  StringID fields[] = {passSig};
  return GlobalStringTable::get().compose(TypeTag::MaterialRender, fields);
}

} // namespace LX_core
