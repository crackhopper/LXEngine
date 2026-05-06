#pragma once

#include "core/asset/skeleton.hpp"
#include "core/scene/component.hpp"

namespace LX_core {

class SkeletonComponent final : public IComponent {
public:
  explicit SkeletonComponent(SkeletonSharedPtr skeleton)
      : m_skeleton(std::move(skeleton)) {}

  ComponentTypeId getTypeId() const override {
    return componentTypeId<SkeletonComponent>();
  }
  bool affectsRenderableStructure() const override { return true; }

  const SkeletonSharedPtr &getSkeleton() const { return m_skeleton; }
  void setSkeleton(SkeletonSharedPtr skeleton);

private:
  SkeletonSharedPtr m_skeleton;
};

} // namespace LX_core
