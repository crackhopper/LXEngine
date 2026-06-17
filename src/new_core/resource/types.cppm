module;
#include <cstddef>
#include <cstdint>

export module LX_New_Core.Resource:Types;

import LX_New_Common.Platform;

export namespace LX_New_Core {
using MemoryIndex = u32;
using MemoryGeneration = u16;
using MemoryTypeId = u8;

constexpr MemoryIndex kInvalidIndex = 0xFFFFFFFF;
constexpr u64 kInvalidHandle = 0;
constexpr MemoryIndex kNoneChunk = 0xFFFFFFFF;
}
