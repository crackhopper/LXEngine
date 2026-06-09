#pragma once

#include "core/asset/material_instance.hpp"
#include "core/scene/component.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <functional>
#include <string>
#include <unordered_map>

namespace LX_core {

class MaterialComponent final : public IComponent {
public:
  explicit MaterialComponent(MaterialInstanceSharedPtr material)
      : m_pendingMaterial(std::move(material)) {}
  MaterialComponent(std::string tag, MaterialInstanceSharedPtr material);
  ~MaterialComponent() override;

  ComponentTypeId getTypeId() const override {
    return componentTypeId<MaterialComponent>();
  }
  bool affectsRenderableStructure() const override { return true; }

  const MaterialInstanceSharedPtr &getPendingMaterialInstance() const {
    return m_pendingMaterial;
  }
  void setMaterialInstance(MaterialInstanceSharedPtr material);
  void setTaggedMaterial(std::string tag, MaterialInstanceSharedPtr material);
  void setTaggedMaterialHandle(std::string tag, MaterialHandle handle);
  void clearPendingMaterials();
  [[nodiscard]] bool setActiveMaterialTag(const std::string &tag);
  [[nodiscard]] const std::string &getActiveMaterialTag() const {
    return m_activeMaterialTag;
  }
  [[nodiscard]] bool hasMaterialTag(const std::string &tag) const;
  [[nodiscard]] MaterialHandle getMaterialHandle() const {
    return m_materialHandle;
  }
  void setMaterialHandle(MaterialHandle handle) { m_materialHandle = handle; }
  [[nodiscard]] MaterialHandle
  getMaterialHandleForTag(const std::string &tag) const;
  void forEachPendingMaterial(
      const std::function<void(const std::string &,
                               const MaterialInstanceSharedPtr &)> &callback)
      const;
  void forEachMaterialHandle(
      const std::function<void(const std::string &, MaterialHandle)> &callback)
      const;

protected:
  void onAttached() override;
  void onDetaching() override;

private:
  void registerPassStateListener();
  void unregisterPassStateListener();
  void revalidateOwnerForPassChange() const;

  MaterialInstanceSharedPtr m_pendingMaterial;
  std::unordered_map<std::string, MaterialInstanceSharedPtr>
      m_pendingMaterialsByTag;
  std::unordered_map<std::string, MaterialHandle> m_materialHandlesByTag;
  std::string m_activeMaterialTag;
  MaterialHandle m_materialHandle;
  u64 m_passListenerId = 0;
};

} // namespace LX_core
