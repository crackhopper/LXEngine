#pragma once

#include "core/platform/types.hpp"

namespace LX_core {

using VisibilityLayerMask = u32;

inline constexpr VisibilityLayerMask Layer_All = 0xffffffffu;
inline constexpr VisibilityLayerMask Layer_Default = 1u << 0;
inline constexpr VisibilityLayerMask Layer_EditorOverlay = 1u << 31;

inline constexpr VisibilityLayerMask VisibilityMask_All = Layer_All;
inline constexpr VisibilityLayerMask VisibilityMask_Default = Layer_Default;

} // namespace LX_core
