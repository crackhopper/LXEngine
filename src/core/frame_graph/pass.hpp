#pragma once
#include "core/platform/types.hpp"
#include "core/utils/string_table.hpp"

namespace LX_core {

// Pass 常量：用于 RenderWorkCompiler / RenderPathGraph /
// Renderable/Material getPipelineSignature(pass) 的键。使用 inline const 而非
// constexpr，因为 StringID 的构造会把字符串 intern 到 GlobalStringTable，有副作用。
inline const StringID Pass_Forward = StringID("Forward");
inline const StringID Pass_ForwardTransparent = StringID("ForwardTransparent");
inline const StringID Pass_Deferred = StringID("Deferred");
inline const StringID Pass_DeferredLighting = StringID("DeferredLighting");
inline const StringID Pass_EnvironmentBox = StringID("EnvironmentBox");
inline const StringID Pass_SkyboxBackground = StringID("SkyboxBackground");
inline const StringID Pass_Shadow = StringID("Shadow");
inline const StringID Pass_BloomThreshold = StringID("BloomThreshold");
inline const StringID Pass_BloomBlurH = StringID("BloomBlurH");
inline const StringID Pass_BloomBlurV = StringID("BloomBlurV");
inline const StringID Pass_PostProcess = StringID("PostProcess");
inline const StringID Pass_DebugToneMapLinear =
    StringID("DebugToneMapLinear");
inline const StringID Pass_DebugSrgbAttachment =
    StringID("DebugSrgbAttachment");
inline const StringID Pass_DebugUnormManualSrgb =
    StringID("DebugUnormManualSrgb");
inline const StringID Pass_DebugRampSrgb = StringID("DebugRampSrgb");
inline const StringID Pass_DebugRampUnormManualSrgb =
    StringID("DebugRampUnormManualSrgb");
inline const StringID Pass_DebugOverlay = StringID("DebugOverlay");

} // namespace LX_core
