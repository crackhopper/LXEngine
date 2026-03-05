#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

// 为了计算 glm 数据结构的hash，需要引入
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include "platform.hpp"
struct Rect2Di {
  i32 x, y;
  u32 width, height;
};