module;
#include <cstddef>
#include <cstdint>

export module LX_New_Core.Resource:Types;

import LX_New_Common.Platform;

export namespace LX_New_Core {
using namespace LX_New_Common;

using MemoryIndex = u32;
using MemoryGeneration = u16;
using MemoryTypeId = u8;

constexpr MemoryIndex kInvalidIndex = 0xFFFFFFFF;
constexpr u64 kInvalidHandle = 0;
constexpr MemoryIndex kNoneChunk = 0xFFFFFFFF;

// ResourceTypeTableBase — 类型擦除基类，供 ResourceManager 按 typeId 分发 release。
// 用 u64 rawHandle 避免与 ResourceHandle 的循环依赖。
class ResourceTypeTableBase {
public:
    virtual ~ResourceTypeTableBase() = default;
    virtual void release(u64 rawHandle) = 0;
};
}
