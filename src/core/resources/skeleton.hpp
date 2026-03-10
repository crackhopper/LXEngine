#pragma once
#include <string>
#include <vector>
#include "../math/vec.hpp"
#include "../math/quat.hpp"
namespace LX_core {

const u32 MAX_BONE_COUNT = 128;

struct Bone {
  std::string name;
  int parentIndex;
  Vec3f position;
  Quatf rotation;
  Vec3f scale = Vec3f(1, 1, 1);
};

struct Skeleton {
  Skeleton() {
    bones.reserve(MAX_BONE_COUNT);
  }

  bool addBone(const Bone &bone) {
    if (bones.size() >= MAX_BONE_COUNT) {
      return false;
    }
    bones.push_back(bone);
    return true;
  }

  const std::vector<Bone> &getBones() const {
    return bones;
  }

private:
  std::vector<Bone> bones;
};
} // namespace LX_core
