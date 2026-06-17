module;

export module LX_New_Core;

import LX_New_Common.Platform;
import LX_New_Common.Memory;
import LX_New_Core.Resource;
import LX_New_Core.GameObject;

export namespace LX_New_Core {

// 占位入口模块：仅用于打通 new_core -> new_common 的跨 target 模块依赖。
// 后续按子系统拆分：rhi / scene / frame_graph / worker / assets。
inline constexpr LX_New_Common::u32 kVersion = 1;

// 占位：返回一个 new_common 的 Clock，证明 new_core 能复用 new_common 的设施。
inline LX_New_Common::Clock MakeBootClock() { return LX_New_Common::Clock{}; }

} // namespace LX_New_Core
