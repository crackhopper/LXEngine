module;
#include <cassert>
#include <iostream>
#include <cstring>

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

    // T5: reuse freed block
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

    return pass;
}

} // namespace LX_New_Test
