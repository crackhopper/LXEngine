#pragma once

#include "core/math/vec.hpp"
#include "core/platform/types.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "core/utils/string_table.hpp"

#include <optional>
#include <string>
#include <vector>

namespace LX_core::gpu {

struct LiveRenderRuntimeExtent final {
  StringID key;
  Vec3u extent{1u, 1u, 1u};

  [[nodiscard]] bool operator==(const LiveRenderRuntimeExtent &) const =
      default;
};

struct LiveRenderView final {
  std::string cameraPath;
  CameraResource cameraResource;
  VisibilityLayerMask visibleMask = Layer_All & ~Layer_EditorOverlay;
  Vec2f viewportExtent{0.0f, 0.0f};
  std::string realtimeRenderPathGraph;
  std::vector<LiveRenderRuntimeExtent> runtimeExtents;
  bool previewEnabled = false;
  bool editorOverlayVisible = true;
};

struct LiveRenderSubmissionStats final {
  usize compilerInputCount = 0;
  usize acceptedInputCount = 0;
  usize rejectedInputCount = 0;
  usize submittedDrawCount = 0;
  usize submittedDispatchCount = 0;
  usize fallbackObservedCount = 0;
  usize descPipelineLookupCount = 0;
  usize descBoundInputCount = 0;
  usize descExecutedInputCount = 0;
  usize bindlessSceneDescriptorCount = 0;
  bool usedExplicitCamera = false;
  bool usedBindlessSceneDescriptors = false;
};

} // namespace LX_core::gpu
