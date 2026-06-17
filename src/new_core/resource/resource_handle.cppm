module;
#include <cstdint>
#include <compare>

export module LX_New_Core.Resource:ResourceHandle;

import LX_New_Common.Platform;
import :Types;

export namespace LX_New_Core {

struct ResourceHandle {
    u64 raw = 0;

    static constexpr u64 TYPE_BITS = 8;
    static constexpr u64 INDEX_BITS = 24;
    static constexpr u64 GEN_BITS = 16;

    static constexpr u64 TYPE_MASK = (1ULL << TYPE_BITS) - 1;
    static constexpr u64 INDEX_MASK = (1ULL << INDEX_BITS) - 1;
    static constexpr u64 GEN_MASK = (1ULL << GEN_BITS) - 1;

    static constexpr u64 TYPE_SHIFT = 0;
    static constexpr u64 INDEX_SHIFT = TYPE_BITS;
    static constexpr u64 GEN_SHIFT = TYPE_BITS + INDEX_BITS;

    constexpr ResourceHandle() noexcept = default;
    constexpr explicit ResourceHandle(u64 raw_val) noexcept : raw(raw_val) {}

    constexpr void set_all(MemoryTypeId t, MemoryIndex i, MemoryGeneration g) noexcept {
        raw = ((static_cast<u64>(t) & TYPE_MASK) << TYPE_SHIFT) |
              ((static_cast<u64>(i) & INDEX_MASK) << INDEX_SHIFT) |
              ((static_cast<u64>(g) & GEN_MASK) << GEN_SHIFT);
    }
    constexpr ResourceHandle(MemoryTypeId t, MemoryIndex i, MemoryGeneration g) noexcept {
        set_all(t, i, g);
    }

    [[nodiscard]] constexpr MemoryTypeId type_id() const noexcept {
        return static_cast<MemoryTypeId>((raw >> TYPE_SHIFT) & TYPE_MASK);
    }
    [[nodiscard]] constexpr MemoryIndex index() const noexcept {
        return static_cast<MemoryIndex>((raw >> INDEX_SHIFT) & INDEX_MASK);
    }
    [[nodiscard]] constexpr MemoryGeneration generation() const noexcept {
        return static_cast<MemoryGeneration>((raw >> GEN_SHIFT) & GEN_MASK);
    }

    constexpr void set_type_id(MemoryTypeId t) noexcept {
        u64 val = static_cast<u64>(t) & TYPE_MASK;
        raw = (raw & ~(TYPE_MASK << TYPE_SHIFT)) | (val << TYPE_SHIFT);
    }
    constexpr void set_index(MemoryIndex i) noexcept {
        u64 val = static_cast<u64>(i) & INDEX_MASK;
        raw = (raw & ~(INDEX_MASK << INDEX_SHIFT)) | (val << INDEX_SHIFT);
    }
    constexpr void set_generation(MemoryGeneration g) noexcept {
        u64 val = static_cast<u64>(g) & GEN_MASK;
        raw = (raw & ~(GEN_MASK << GEN_SHIFT)) | (val << GEN_SHIFT);
    }

    [[nodiscard]] auto operator<=>(const ResourceHandle &) const = default;
};

} // namespace LX_New_Core
