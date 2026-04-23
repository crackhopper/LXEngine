#pragma once

#include "core/asset/material_pass_definition.hpp"
#include "core/asset/shader_binding_ownership.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
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
  using Ptr = std::shared_ptr<MaterialTemplate>;

  MaterialTemplate(Token, std::string name)
      : m_name(std::move(name)) {}

  static Ptr create(std::string name) {
    return std::make_shared<MaterialTemplate>(Token{}, std::move(name));
  }

  const std::string &getName() const { return m_name; }

  void setPass(StringID pass, MaterialPassDefinition definition) {
    m_passes[pass] = std::move(definition);
  }

  std::optional<std::reference_wrapper<const MaterialPassDefinition>>
  getEntry(StringID pass) const {
    auto it = m_passes.find(pass);
    if (it != m_passes.end())
      return it->second;
    return std::nullopt;
  }

  StringID getRenderPassSignature(StringID pass) const {
    auto it = m_passes.find(pass);
    if (it == m_passes.end())
      return StringID{};
    return it->second.getRenderSignature();
  }

/*
@source_analysis.section buildBindingCache：把每个 pass 的反射结果筛成材质可写视图
这个步骤顺着 `m_passes` 遍历每个 `MaterialPassDefinition` 的 shader 反射结果，
但不会把所有 binding 都原样抄下来。它会先用 `isSystemOwnedBinding()` 过滤掉
`CameraUBO` / `LightUBO` / `Bones` 这类由引擎负责供给的 binding，
只把真正归材质实例写入的资源保留在 `m_passMaterialBindings`。

结果仍然按 pass 分组存储。这样 `MaterialInstance` 后续读取时拿到的是
“这个 pass 需要哪些材质侧资源”的局部视图，而不是一个已经抹平作用域的全局表。
*/
  void buildBindingCache() {
    m_passMaterialBindings.clear();

    for (const auto &[pass, definition] : m_passes) {
      auto shader = definition.shaderSet.getShader();
      if (!shader)
        continue;
      auto &matBindings = m_passMaterialBindings[pass];
      for (const auto &binding : shader->getReflectionBindings()) {
        if (isSystemOwnedBinding(binding.name))
          continue;
        matBindings.push_back(binding);
      }
    }

    checkCrossPassBindingConsistency();
  }

  const std::vector<ShaderResourceBinding> &
  getMaterialBindings(StringID pass) const {
    static const std::vector<ShaderResourceBinding> kEmpty;
    auto it = m_passMaterialBindings.find(pass);
    return it != m_passMaterialBindings.end() ? it->second : kEmpty;
  }

  /// Search all passes for a material-owned binding by name.
  /// Returns the first match. Asserts if the same name appears with
  /// conflicting types across passes.
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findMaterialBinding(StringID id) const {
    const ShaderResourceBinding *found = nullptr;
    for (const auto &[_, bindings] : m_passMaterialBindings) {
      for (const auto &binding : bindings) {
        if (StringID(binding.name) != id)
          continue;
        if (!found) {
          found = &binding;
        } else {
          assert(found->type == binding.type &&
                 "findMaterialBinding: same name with different types across "
                 "passes");
        }
        break;
      }
    }
    if (found)
      return *found;
    return std::nullopt;
  }

  const std::unordered_map<StringID, MaterialPassDefinition, StringID::Hash> &
  getPasses() const {
    return m_passes;
  }

private:
  [[noreturn]] void fatalBindingConflict(const std::string &bindingName,
                                         StringID firstPass,
                                         StringID secondPass,
                                         const std::string &reason) const {
    std::cerr << "FATAL [MaterialTemplate] binding='"
              << bindingName << "' passA="
              << GlobalStringTable::get().toDebugString(firstPass)
              << " passB=" << GlobalStringTable::get().toDebugString(secondPass)
              << " reason=" << reason << std::endl;
    std::terminate();
  }

/*
@source_analysis.section Cross-pass consistency：同名 material binding 必须表达同一份结构契约
template 允许不同 pass 使用不同 shader，但不允许“同名且归材质所有”的 binding
在不同 pass 里偷偷变成不同布局。否则 `MaterialInstance` 就无法继续坚持
“单一 canonical 参数槽位 / 纹理表，被多个 pass 只读复用”这条设计。

因此这里按 binding 名跨 pass 收集首个定义，并在后续出现同名项时做 fail-fast：

- descriptor 类型必须一致
- UBO 大小必须一致
- 反射出的成员布局必须一致

只要任一项冲突，就直接终止，而不是让更晚的参数写入或 descriptor 组装阶段
再出现难以定位的歧义。
*/
  void checkCrossPassBindingConsistency() const {
    // Collect first-seen binding per name across all passes.
    std::unordered_map<std::string, std::pair<StringID, const ShaderResourceBinding *>>
        seen; // name -> (first pass, first binding)
    for (const auto &[pass, bindings] : m_passMaterialBindings) {
      for (const auto &binding : bindings) {
        auto it = seen.find(binding.name);
        if (it == seen.end()) {
          seen[binding.name] = {pass, &binding};
          continue;
        }
        const auto *first = it->second.second;
        if (first->type != binding.type) {
          fatalBindingConflict(binding.name, it->second.first, pass,
                               "type mismatch");
        } else if (first->size != binding.size) {
          fatalBindingConflict(binding.name, it->second.first, pass,
                               "size mismatch");
        } else if (first->members != binding.members) {
          fatalBindingConflict(binding.name, it->second.first, pass,
                               "member layout mismatch");
        }
      }
    }
  }

  std::string m_name;
  std::unordered_map<StringID, MaterialPassDefinition, StringID::Hash> m_passes;
  std::unordered_map<StringID, std::vector<ShaderResourceBinding>, StringID::Hash>
      m_passMaterialBindings;
};

} // namespace LX_core
