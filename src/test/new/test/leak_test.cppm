module;
#include <cassert>
#include <iostream>
#include <vector>

export module LX_New_Test.Test.LeakTest;

import LX_New_Common.Platform;
import LX_New_Common.Memory;

export namespace LX_New_Test {

inline bool run_leak_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    // TL1: spill pool alloc + free — verifies no leak
    {
        SpillPool<u32, 4> pool;
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 20; ++i) head = pool.append(head, i);
        pool.freeChain(head);
        check(pool.freeListSize() > 0, "spill blocks freed");
    }

    // TL2: raw buffer alloc + free — verifies no leak, reuse without growth
    {
        RawBuffer buf;
        std::vector<u32> offsets;
        for (int i = 0; i < 100; ++i) offsets.push_back(buf.allocate(64, 16));
        u32 capAfterAlloc = buf.capacity();
        for (auto o : offsets) buf.free(o, 64);
        u32 reuse = buf.allocate(64, 16);
        check(reuse == 0, "reuses first freed block");
        check(buf.capacity() == capAfterAlloc, "capacity unchanged (no expansion)");
    }

    // TL3: raw buffer move-then-destroy — no double free (ASAN verifies)
    {
        RawBuffer buf;
        buf.allocate(256, 16);
        {
            RawBuffer moved(std::move(buf));
            moved.allocate(128, 16);
        }
        check(buf.getBase() == nullptr, "source null after move+destroy");
    }

    // TL4: many alloc/free/destroy cycles — ASAN verifies no leak
    {
        for (int round = 0; round < 20; ++round) {
            RawBuffer buf;
            std::vector<u32> offs;
            for (int i = 0; i < 50; ++i) offs.push_back(buf.allocate(128, 32));
            for (auto o : offs) buf.free(o, 128);
        }
        check(true, "20 alloc/free/destroy cycles completed");
    }

    return pass;
}

} // namespace LX_New_Test
