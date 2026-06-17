module;
#include <cassert>
#include <iostream>
#include <cstring>
#include <cstdint>
#include <vector>

export module LX_New_Test.Test.RawBufferTest;

import LX_New_Common.Platform;
import LX_New_Common.Memory;

export namespace LX_New_Test {

inline bool run_raw_buffer_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    // ── 基础功能 ──────────────────────────────────────────────

    // T1: initial state
    {
        RawBuffer buf;
        check(buf.capacity() == 0, "initial capacity zero");
    }

    // T2: first allocation at offset 0
    {
        RawBuffer buf;
        u32 off = buf.allocate(64, 1);
        check(off == 0, "first alloc at offset 0");
        check(buf.capacity() >= 64, "capacity expanded");
    }

    // T3: second allocation contiguous
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(32, 1);
        u32 o2 = buf.allocate(64, 1);
        check(o2 == 32, "second alloc contiguous after first");
    }

    // T4: free
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(32, 1);
        u32 o2 = buf.allocate(64, 1);
        buf.free(o1, 32);
        check(buf.freeBlockCount() == 1, "one free block after free");
    }

    // T5: reuse freed block (first-fit)
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(32, 1);
        buf.allocate(64, 1);
        buf.free(o1, 32);
        u32 o3 = buf.allocate(32, 1);
        check(o3 == o1, "reuses freed block");
    }

    // T6: alignment
    {
        RawBuffer buf;
        buf.allocate(7, 1);
        u32 off = buf.allocate(4, 16);
        check(off % 16 == 0, "16-byte alignment respected");
    }

    // T7: merge adjacent blocks
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(32, 1);
        u32 o2 = buf.allocate(32, 1);
        buf.free(o1, 32);
        buf.free(o2, 32);
        check(buf.freeBlockCount() <= 1, "adjacent freed blocks merged");
    }

    // T8: fragmentation ratio
    {
        RawBuffer buf;
        check(buf.fragmentationRatio() == 0.0f, "initial fragmentation zero");
        u32 o1 = buf.allocate(32, 1);
        u32 o2 = buf.allocate(32, 1);
        buf.allocate(32, 1);
        buf.free(o1, 32);
        buf.free(o2, 32);
        float frag = buf.fragmentationRatio();
        check(frag > 0.0f && frag <= 1.0f, "fragmentation reported");
    }

    // ── 数据完整性 ────────────────────────────────────────────

    // T9: write pattern, read back
    {
        RawBuffer buf;
        u32 off = buf.allocate(256, 1);
        u8* base = buf.getBase();
        for (u32 i = 0; i < 256; ++i) base[off + i] = static_cast<u8>(i * 7 + 3);
        bool ok = true;
        for (u32 i = 0; i < 256; ++i)
            if (base[off + i] != static_cast<u8>(i * 7 + 3)) { ok = false; break; }
        check(ok, "data written and read back correctly");
    }

    // T10: getBase() is 256-byte aligned
    {
        RawBuffer buf;
        buf.allocate(64, 1);
        uintptr_t addr = reinterpret_cast<uintptr_t>(buf.getBase());
        check(addr % 256 == 0, "getBase() is 256-byte aligned");
    }

    // T11: multiple allocations do not overlap
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(64, 1);
        u32 o2 = buf.allocate(64, 1);
        u32 o3 = buf.allocate(64, 1);
        u8* base = buf.getBase();
        std::memset(base + o1, 0xAA, 64);
        std::memset(base + o2, 0xBB, 64);
        std::memset(base + o3, 0xCC, 64);
        bool ok = true;
        for (int i = 0; i < 64; ++i) {
            if (base[o1 + i] != 0xAA) ok = false;
            if (base[o2 + i] != 0xBB) ok = false;
            if (base[o3 + i] != 0xCC) ok = false;
        }
        check(ok, "allocations don't overlap");
    }

    // T12: free middle block, neighbors remain intact
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(32, 1);
        u32 o2 = buf.allocate(32, 1);
        u32 o3 = buf.allocate(32, 1);
        u8* base = buf.getBase();
        std::memset(base + o1, 0x11, 32);
        std::memset(base + o2, 0x22, 32);
        std::memset(base + o3, 0x33, 32);
        buf.free(o2, 32);
        bool ok = true;
        for (int i = 0; i < 32; ++i) {
            if (base[o1 + i] != 0x11) ok = false;
            if (base[o3 + i] != 0x33) ok = false;
        }
        check(ok, "neighbors intact after freeing middle block");
    }

    // ── 释放顺序与合并 ────────────────────────────────────────

    // T13: reverse-order free — all three merge into one block
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(32, 1);
        u32 o2 = buf.allocate(32, 1);
        u32 o3 = buf.allocate(32, 1);
        buf.free(o3, 32);
        buf.free(o2, 32);
        buf.free(o1, 32);
        check(buf.freeBlockCount() == 1, "reverse free: 3 blocks merged into 1");
    }

    // T14: interleaved free — gaps prevent merge
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(32, 1);
        u32 o2 = buf.allocate(32, 1);
        u32 o3 = buf.allocate(32, 1);
        buf.free(o1, 32);
        buf.free(o3, 32);
        check(buf.freeBlockCount() == 2, "interleaved free: 2 blocks (gap prevents merge)");
        // Free the middle block now — should merge all three
        buf.free(o2, 32);
        check(buf.freeBlockCount() == 1, "filling gap merges all into 1");
    }

    // T15: reuse freed block with matching alignment
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(64, 16);
        buf.free(o1, 64);
        u32 o2 = buf.allocate(32, 16);
        check(o2 == o1, "reuse freed block at same offset");
        check(o2 % 16 == 0, "reused offset still 16-aligned");
    }

    // ── 对齐 ──────────────────────────────────────────────────

    // T16: various alignment values
    {
        const u32 aligns[] = {1, 4, 16, 64, 128, 256};
        for (u32 a : aligns) {
            RawBuffer buf;
            u32 off = buf.allocate(8, a);
            check(off % a == 0, "alignment respected for each value");
        }
    }

    // T17: alignment padding consumed, next alloc after padding
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(7, 1);    // 7 bytes, aligned to 1
        u32 o2 = buf.allocate(4, 32);   // 4 bytes, aligned to 32
        check(o2 >= o1 + 7, "second alloc after alignment padding");
        check(o2 % 32 == 0, "32-byte aligned offset");
    }

    // ── 移动语义（ASAN 重点） ─────────────────────────────────

    // T18: move constructor — ownership transferred, source nulled
    {
        RawBuffer buf;
        u32 off = buf.allocate(128, 1);
        u8* origBase = buf.getBase();
        origBase[off] = 0xAB;

        RawBuffer moved(std::move(buf));
        u8* movedBase = moved.getBase();
        check(movedBase == origBase, "move ctor: base pointer preserved");
        check(movedBase[off] == 0xAB, "move ctor: data preserved");
        check(buf.getBase() == nullptr, "move ctor: source nulled");
    }

    // T19: move assignment — old data freed, new data transferred
    {
        RawBuffer buf;
        u32 off = buf.allocate(128, 1);
        u8* origBase = buf.getBase();

        RawBuffer target;
        target.allocate(64, 1);  // target already has its own buffer
        u8* targetOld = target.getBase();
        check(targetOld != nullptr, "target has buffer before move");

        target = std::move(buf);
        check(target.getBase() == origBase, "move assign: new base pointer");
        check(buf.getBase() == nullptr, "move assign: source nulled");
    }

    // T20: move-then-destroy — no double free (ASAN verifies)
    {
        RawBuffer buf;
        buf.allocate(256, 16);
        {
            RawBuffer moved(std::move(buf));
            moved.allocate(64, 16);  // use moved buffer
        }  // moved destroyed here — buf is already nulled
        check(buf.getBase() == nullptr, "move+destroy: source still null");
    }

    // ── 压力测试（ASAN 覆盖：leak / overflow / use-after-free） ─

    // T21: 1000 alloc/free cycles
    {
        RawBuffer buf;
        std::vector<u32> offsets;
        const int N = 1000;
        for (int i = 0; i < N; ++i)
            offsets.push_back(buf.allocate(64, 16));
        u32 usedBefore = buf.capacity();
        for (auto o : offsets) buf.free(o, 64);
        check(buf.freeBlockCount() == 1, "stress: 1000 blocks merged to 1");

        // Re-allocate — should fit entirely in freed space
        std::vector<u32> reuse;
        for (int i = 0; i < N; ++i)
            reuse.push_back(buf.allocate(64, 16));
        check(buf.capacity() == usedBefore, "stress: capacity unchanged (no growth after reuse)");
    }

    // T22: alternating alloc/free — stress first-fit
    {
        RawBuffer buf;
        for (int round = 0; round < 100; ++round) {
            std::vector<u32> offs;
            for (int i = 0; i < 20; ++i)
                offs.push_back(buf.allocate(32, 4));
            // Free every other one
            for (int i = 0; i < 20; i += 2)
                buf.free(offs[i], 32);
            // Allocate again — should reuse freed slots
            for (int i = 0; i < 10; ++i)
                buf.allocate(32, 4);
            // Free all remaining
            for (int i = 1; i < 20; i += 2)
                buf.free(offs[i], 32);
        }
        check(buf.freeBlockCount() >= 1, "stress alternating: completed without crash");
    }

    return pass;
}

} // namespace LX_New_Test
