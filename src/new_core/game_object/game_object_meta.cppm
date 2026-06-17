module;
#include <cstdint>

export module LX_New_Core.GameObject:GameObjectMeta;

import LX_New_Common.Platform;
import LX_New_Common.Memory;

export namespace LX_New_Core {
using namespace LX_New_Common;

// GameObjectMeta — 每个 GameObject 的固定大小元数据（~56 bytes 对齐后）。
//
// 字段布局：
//   GC 状态:  marked (mark-sweep 标记), alive (是否有效)
//   Inline refs (≤4): refs[4] + refCount, 溢出走 refSpillHead 链表
//   Inline handles (≤4): handles[4] + handleCount, 溢出走 handleSpillHead 链表
//
// kNoneChunk = 0xFFFFFFFF 表示无溢出链。
struct GameObjectMeta {
  u8 marked : 1;
  u8 alive : 1;
  u8 _pad : 6;

  u8 refCount = 0;
  u32 refs[4] = {};
  u32 refSpillHead = kNoneChunk;

  u8 handleCount = 0;
  u64 handles[4] = {};
  u32 handleSpillHead = kNoneChunk;

  constexpr GameObjectMeta() noexcept = default;
};

} // namespace LX_New_Core
