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
