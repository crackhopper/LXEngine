#pragma once

#include "core/asset/material_instance.hpp"
#include "core/scene/component.hpp"
#include "core/scene/scene_resource_table.hpp"

namespace LX_core {

class MaterialComponent final : public IComponent {
public:
  explicit MaterialComponent(MaterialInstanceSharedPtr material)
      : m_material(std::move(material)) {}
  ~MaterialComponent() override;

  ComponentTypeId getTypeId() const override {
    return componentTypeId<MaterialComponent>();
  }
  bool affectsRenderableStructure() const override { return true; }

  const MaterialInstanceSharedPtr &getMaterialInstance() const { return m_material; }
  void setMaterialInstance(MaterialInstanceSharedPtr material);
  [[nodiscard]] MaterialHandle getMaterialHandle() const {
    return m_materialHandle;
  }
  void setMaterialHandle(MaterialHandle handle) { m_materialHandle = handle; }

protected:
  void onAttached() override;
  void onDetaching() override;

private:
  void registerPassStateListener();
  void unregisterPassStateListener();
  void revalidateOwnerForPassChange() const;

  MaterialInstanceSharedPtr m_material;
  MaterialHandle m_materialHandle;
  u64 m_passListenerId = 0;
};

} // namespace LX_core
