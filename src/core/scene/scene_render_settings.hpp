#pragma once

namespace LX_core {

struct SceneRenderSettings final {
  bool shadows = false;
};

enum class SceneRealtimeRenderMode {
  Forward,
  Deferred,
};

struct SceneRealtimeRenderSettings final {
  SceneRealtimeRenderMode mode = SceneRealtimeRenderMode::Forward;
  bool ibl = false;
  bool alphaTransparency = false;
};

} // namespace LX_core
