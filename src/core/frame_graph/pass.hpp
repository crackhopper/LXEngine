#pragma once
#include "core/platform/types.hpp"
#include "core/utils/string_table.hpp"

namespace LX_core {

// Pass 常量：用于 RenderWorkQueue::build(context, pass, target) /
// Renderable/Material getPipelineSignature(pass) 的键。使用 inline const 而非
// constexpr，因为 StringID 的构造会把字符串 intern 到 GlobalStringTable，有副作用。
inline const StringID Pass_Forward = StringID("Forward");
inline const StringID Pass_Deferred = StringID("Deferred");
inline const StringID Pass_Shadow = StringID("Shadow");
inline const StringID Pass_BloomThreshold = StringID("BloomThreshold");
inline const StringID Pass_BloomBlurH = StringID("BloomBlurH");
inline const StringID Pass_BloomBlurV = StringID("BloomBlurV");
inline const StringID Pass_PostProcess = StringID("PostProcess");
inline const StringID Pass_DebugOverlay = StringID("DebugOverlay");
inline const StringID Pass_OfflineRayTrace = StringID("OfflineRayTrace");

} // namespace LX_core
