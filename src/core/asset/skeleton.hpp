#pragma once
#include "core/rhi/gpu_resource.hpp"
#include "core/math/mat.hpp"
#include "core/math/quat.hpp"
#include "core/math/vec.hpp"
#include "core/utils/string_table.hpp"
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

namespace LX_core {

constexpr BoneCount MAX_BONE_COUNT = 128;

struct Bone {
  std::string name;
  int parentIndex;
  Vec3f position;
  Quatf rotation;
  Vec3f scale = Vec3f(1, 1, 1);
};

// SkeletonData is the GPU-facing bone palette buffer. Skeleton remains the
// CPU-side asset/container and updates this UBO-shaped view when animation changes.
struct alignas(16) SkeletonData : public IGpuResource {
  explicit SkeletonData(const std::vector<Bone> &bones) {}

  void updateBy(const std::vector<Bone> &bones) {
    assert(bones.size() <= MAX_BONE_COUNT);
    if (bones.empty()) {
      return;
    }
    for (size_t i = 0; i < bones.size(); i++) {
      writeToUbo(bones[i], m_bones[i]);
    }
    setDirty();
  }

  bool setBone(int index, const Bone &bone) {
    if (index < 0 || index >= MAX_BONE_COUNT) {
      return false;
    }
    auto succ = writeToUbo(bone, m_bones[index]);
    if (succ) {
      setDirty();
      return true;
    } else {
      return false;
    }
  }

  bool writeToUbo(const Bone &bone, Mat4f &out) {
    auto r = bone.rotation.toMat4();
    auto p2 = Mat4f::translate(bone.position);
    auto p1 = Mat4f::translate(-bone.position);
    auto local = p2 * r * p1;
    if (bone.parentIndex == -1)
      out = local;
    else {
      if (bone.parentIndex < 0 || bone.parentIndex >= MAX_BONE_COUNT) {
        return false;
      } else {
        out = m_bones[bone.parentIndex] * local;
      }
    }
    return true;
  }

  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  static constexpr ResourceByteSize32 ResourceSize =
      static_cast<ResourceByteSize32>(MAX_BONE_COUNT * sizeof(Mat4f));
  const void *getRawData() const override { return m_bones; }
  ResourceByteSize32 getByteSize() const override { return ResourceSize; }
  StringID getBindingName() const override {
    static const StringID kName("Bones");
    return kName;
  }

private:
  Mat4f m_bones[MAX_BONE_COUNT];
};

using SkeletonDataSharedPtr = std::shared_ptr<SkeletonData>;

class Skeleton;
using SkeletonSharedPtr = std::shared_ptr<Skeleton>;

class Skeleton {
  class Token {};

public:
  Skeleton(Token token, const std::vector<Bone> &bones)
      : bones(bones), ubo(std::make_shared<SkeletonData>(bones)) {}

  static SkeletonSharedPtr create(const std::vector<Bone> &bones) {
    Token token;
    return std::make_shared<Skeleton>(token, bones);
  }

  bool addBone(const Bone &bone) {
    if (bones.size() >= MAX_BONE_COUNT) {
      return false;
    }
    bones.push_back(bone);
    ubo->setBone(bones.size() - 1, bone);
    return true;
  }

  void syncGpuData() { ubo->updateBy(bones); }

  SkeletonDataSharedPtr getUBO() const { return ubo; }
private:
  std::vector<Bone> bones;
  SkeletonDataSharedPtr ubo;
};

} // namespace LX_core
