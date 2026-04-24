#pragma once

#include "core/asset/material_pass_definition.hpp"
#include "core/asset/shader_binding_ownership.hpp"

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace LX_core {

/*
@source_analysis.section MaterialTemplate：多 pass 材质蓝图，而不是运行时数据容器
`MaterialTemplate` 描述的是“一个材质在结构上能做什么”：

- 支持哪些 pass
- 每个 pass 使用哪组 shader / variant / render state
- 每个 pass 暴露了哪些 material-owned binding

它故意不保存参数字节、纹理实例或脏标记；这些运行时状态属于
`MaterialInstance`。这条边界让 template 可以被多个 instance 共享，
并且把 pipeline 身份、反射结构和实例数据拆成三个稳定层次。
*/
class MaterialTemplate : public std::enable_shared_from_this<MaterialTemplate> {
  struct Token {};

public:
  using SharedPtr = std::shared_ptr<MaterialTemplate>;

  MaterialTemplate(Token, std::string name)
      : m_name(std::move(name)) {}

  static SharedPtr create(std::string name) {
    return std::make_shared<MaterialTemplate>(Token{}, std::move(name));
  }

  const std::string &getName() const { return m_name; }

  void setPassDefinition(StringID pass, MaterialPassDefinition definition) {
    m_passDefinitions[pass] = std::move(definition);
  }

  std::optional<std::reference_wrapper<const MaterialPassDefinition>>
  getPassDefinition(StringID pass) const {
    auto it = m_passDefinitions.find(pass);
    if (it != m_passDefinitions.end())
      return it->second;
    return std::nullopt;
  }

  StringID getPassDefinitionSignature(StringID pass) const {
    auto passDefinition = getPassDefinition(pass);
    if (!passDefinition)
      return StringID{};
    return passDefinition->get().getRenderSignature();
  }

/*
@source_analysis.section rebuildMaterialInterface：先 canonical 化材质接口，再派生 pass 视图
这个步骤不再把每个 pass 直接存成 `vector<ShaderResourceBinding>` 副本，
而是先建立 template 级的 canonical material binding 表：

- key 是 `StringID(binding.name)`
- value 是该名字对应的唯一 `ShaderResourceBinding` 结构事实

然后每个 pass 只保存“本 pass 用到哪些 canonical binding”的 `StringID` 列表。
这样 template 才真正成为材质结构真值来源：同名 binding 的跨 pass 一致性、
descriptor 类型支持范围、以及运行时实例需要面对的 binding 集合，都在这里一次收束。
*/
  void rebuildMaterialInterface() {
    m_canonicalMaterialBindings.clear();
    m_materialBindingIdsByPass.clear();

    for (const auto &[pass, definition] : m_passDefinitions) {
      auto shader = definition.shaderProgram.getShader();
      if (!shader)
        continue;
      auto &bindingIds = m_materialBindingIdsByPass[pass];
      for (const auto &binding : shader->getReflectionBindings()) {
        if (isSystemOwnedBinding(binding.name))
          continue;

        validateSupportedMaterialBindingType(binding, pass);

        const StringID bindingId(binding.name);
        auto it = m_canonicalMaterialBindings.find(bindingId);
        if (it == m_canonicalMaterialBindings.end()) {
          m_canonicalMaterialBindings.emplace(bindingId, binding);
        } else {
          validateCanonicalBindingCompatibility(it->second, binding, pass);
        }

        bindingIds.push_back(bindingId);
      }
    }
  }

  const std::vector<StringID> &getPassMaterialBindingIds(StringID pass) const {
    static const std::vector<StringID> kEmpty;
    auto it = m_materialBindingIdsByPass.find(pass);
    return it != m_materialBindingIdsByPass.end() ? it->second : kEmpty;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findCanonicalMaterialBinding(StringID id) const {
    auto it = m_canonicalMaterialBindings.find(id);
    if (it != m_canonicalMaterialBindings.end())
      return it->second;
    return std::nullopt;
  }

  const ShaderResourceBinding *getCanonicalMaterialBinding(StringID id) const {
    auto it = m_canonicalMaterialBindings.find(id);
    return it != m_canonicalMaterialBindings.end() ? &it->second : nullptr;
  }

  const std::unordered_map<StringID, ShaderResourceBinding, StringID::Hash> &
  getCanonicalMaterialBindings() const {
    return m_canonicalMaterialBindings;
  }

  const std::unordered_map<StringID, MaterialPassDefinition, StringID::Hash> &
  getAllPassDefinitions() const {
    return m_passDefinitions;
  }

private:
  [[noreturn]] void fatalBindingConflict(std::string_view bindingName,
                                         StringID pass,
                                         std::string_view reason) const {
    std::cerr << "FATAL [MaterialTemplate] binding='"
              << bindingName << "' pass="
              << GlobalStringTable::get().toDebugString(pass)
              << " reason=" << reason << std::endl;
    std::terminate();
  }

/*
@source_analysis.section Cross-pass consistency：同名 binding 必须收敛成同一个 canonical 契约
一旦 `MaterialTemplate` 选择“全局 canonical binding 表 + pass 内 StringID 索引”这条路，
它就隐含要求：同名 material-owned binding 在所有 pass 中都必须指向同一份结构契约。

否则每个 pass 只保存 `StringID` 就不够了，因为 runtime 将无法知道：

- 这个名字到底对应哪种 descriptor 类型
- 它的 set/binding 槽位是否稳定
- buffer 的大小和成员布局是否一致

所以现在的校验点不再是“给 instance 的补救检查”，而是 template 在构造自身结构时
就直接 fail-fast。只要 canonical 表建立成功，`MaterialInstance` 就可以放心地
按 bindingName 存 runtime 数据，而不必再重复做结构归并。
*/
  void validateSupportedMaterialBindingType(const ShaderResourceBinding &binding,
                                           StringID pass) const {
    switch (binding.type) {
    case ShaderPropertyType::UniformBuffer:
    case ShaderPropertyType::StorageBuffer:
    case ShaderPropertyType::Texture2D:
    case ShaderPropertyType::TextureCube:
      return;
    default:
      fatalBindingConflict(binding.name, pass,
                           "unsupported material-owned descriptor type");
    }
  }

  void validateCanonicalBindingCompatibility(
      const ShaderResourceBinding &canonical,
      const ShaderResourceBinding &candidate,
      StringID candidatePass) const {
    if (canonical.type != candidate.type) {
      fatalBindingConflict(candidate.name, candidatePass, "type mismatch");
    } else if (canonical.descriptorCount != candidate.descriptorCount) {
      fatalBindingConflict(candidate.name, candidatePass,
                           "descriptorCount mismatch");
    } else if (canonical.set != candidate.set) {
      fatalBindingConflict(candidate.name, candidatePass, "set mismatch");
    } else if (canonical.binding != candidate.binding) {
      fatalBindingConflict(candidate.name, candidatePass, "binding mismatch");
    } else if (canonical.size != candidate.size) {
      fatalBindingConflict(candidate.name, candidatePass, "size mismatch");
    } else if (canonical.members != candidate.members) {
      fatalBindingConflict(candidate.name, candidatePass,
                           "member layout mismatch");
    }
  }

  std::string m_name;
  std::unordered_map<StringID, MaterialPassDefinition, StringID::Hash>
      m_passDefinitions;
  std::unordered_map<StringID, ShaderResourceBinding, StringID::Hash>
      m_canonicalMaterialBindings;
  std::unordered_map<StringID, std::vector<StringID>, StringID::Hash>
      m_materialBindingIdsByPass;
};

using MaterialTemplateSharedPtr = MaterialTemplate::SharedPtr;

} // namespace LX_core
