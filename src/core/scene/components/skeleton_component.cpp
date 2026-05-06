#include "skeleton_component.hpp"

namespace LX_core {

void SkeletonComponent::setSkeleton(SkeletonSharedPtr skeleton) {
  m_skeleton = std::move(skeleton);
  notifyOwnerStructuralChange();
}

} // namespace LX_core
