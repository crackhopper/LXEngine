#pragma once

namespace LX_core {

struct SceneRenderSettings final {
  bool shadows = false;
};

struct SceneRealtimeRenderSettings final {
  bool ibl = false;
  bool alphaTransparency = false;
};

} // namespace LX_core
