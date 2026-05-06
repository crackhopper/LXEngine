#pragma once

#include "core/asset/material_instance.hpp"
#include "core/scene/component.hpp"

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

protected:
  void onAttached() override;
  void onDetaching() override;

private:
  void registerPassStateListener();
  void unregisterPassStateListener();
  void revalidateOwnerForPassChange() const;

  MaterialInstanceSharedPtr m_material;
  u64 m_passListenerId = 0;
};

} // namespace LX_core
