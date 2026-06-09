#pragma once

#include "core/asset/skeleton.hpp"
#include "core/scene/component.hpp"
#include "core/scene/scene_resource_handles.hpp"

namespace LX_core {

class SkeletonComponent final : public IComponent {
public:
  explicit SkeletonComponent(SkeletonSharedPtr skeleton)
      : m_skeleton(std::move(skeleton)) {}

  ComponentTypeId getTypeId() const override {
    return componentTypeId<SkeletonComponent>();
  }
  bool affectsRenderableStructure() const override { return true; }

  const SkeletonSharedPtr &getPendingSkeleton() const { return m_skeleton; }
  void clearPendingSkeleton() { m_skeleton.reset(); }
  void setSkeleton(SkeletonSharedPtr skeleton);
  [[nodiscard]] SkeletonHandle getSkeletonHandle() const {
    return m_skeletonHandle;
  }
  void setSkeletonHandle(SkeletonHandle handle) { m_skeletonHandle = handle; }

private:
  SkeletonSharedPtr m_skeleton;
  SkeletonHandle m_skeletonHandle;
};

} // namespace LX_core
