#include "skeleton.hpp"
#include <string>
#include <vector>
#include "../math/vec.hpp"
#include "../math/quat.hpp"
namespace LX_core {
struct Bone {
  std::string name;
  int parentIndex;
  Vec3f position;
  Quatf rotation;
  Vec3f scale = Vec3f(1, 1, 1);
};

struct Skeleton {
  std::vector<Bone> bones;
};
} // namespace LX_core
