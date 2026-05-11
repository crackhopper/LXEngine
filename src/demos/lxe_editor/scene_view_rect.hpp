#pragma once

#include "core/math/vec.hpp"

#include <algorithm>

namespace LX_demo::lxe_editor {

struct SceneViewRect final {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;

  [[nodiscard]] bool isValid() const { return width > 1.0f && height > 1.0f; }
  [[nodiscard]] bool contains(const LX_core::Vec2f& pixel) const {
    return isValid() && pixel.x >= x && pixel.y >= y && pixel.x < x + width &&
           pixel.y < y + height;
  }
  [[nodiscard]] LX_core::Vec2f localPixel(const LX_core::Vec2f& pixel) const {
    return {pixel.x - x, pixel.y - y};
  }
  [[nodiscard]] LX_core::Vec2f size() const { return {width, height}; }
};

[[nodiscard]] inline SceneViewRect makeSceneViewRect(
    float windowWidth, float windowHeight, float leftInset, float topInset,
    float rightInset, float bottomInset) {
  return SceneViewRect{
      .x = std::max(0.0f, leftInset),
      .y = std::max(0.0f, topInset),
      .width = std::max(0.0f, windowWidth - std::max(0.0f, leftInset) -
                                   std::max(0.0f, rightInset)),
      .height = std::max(0.0f, windowHeight - std::max(0.0f, topInset) -
                                    std::max(0.0f, bottomInset)),
  };
}

} // namespace LX_demo::lxe_editor
