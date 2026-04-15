#pragma once
#include "core/platform/types.hpp"
#include "core/utils/string_table.hpp"

namespace LX_core {

// Pass 常量：用于 RenderQueue::buildFromScene(scene, pass) /
// Renderable/Material getRenderSignature(pass) 的键。使用 inline const 而非
// constexpr，因为 StringID 的构造会把字符串 intern 到 GlobalStringTable，有副作用。
inline const StringID Pass_Forward = StringID("Forward");
inline const StringID Pass_Deferred = StringID("Deferred");
inline const StringID Pass_Shadow = StringID("Shadow");

// Forward-declare ResourcePassFlag to avoid pulling in render_resource.hpp.
// Defined in core/rhi/render_resource.hpp.
enum class ResourcePassFlag : u32;

/// Translate a pass StringID (Pass_Forward / Pass_Deferred / Pass_Shadow) to
/// the corresponding ResourcePassFlag bit. Unknown StringIDs return
/// ResourcePassFlag{0} (no bits set).
///
/// This helper bridges scene-layer pass identity (StringID) with resource-layer
/// pass masks (ResourcePassFlag). RenderQueue::buildFromScene uses it via
/// IRenderable::supportsPass to filter which renderables participate in which
/// pass.
ResourcePassFlag passFlagFromStringID(StringID pass);

} // namespace LX_core
