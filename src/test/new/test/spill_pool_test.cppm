module;
#include <cassert>
#include <iostream>

export module LX_New_Test.Test.SpillPoolTest;

import LX_New_Common.Platform;
import LX_New_Common.Memory;

export namespace LX_New_Test {

inline bool run_spill_pool_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    // T1: allocBlock — first block gets index 0
    {
        SpillPool<u32, 4> pool;
        u32 idx = pool.allocBlock();
        check(idx == 0, "first block index 0");
    }

    // T2: freeChain — single block returns to freeList
    {
        SpillPool<u32, 4> pool;
        u32 idx = pool.allocBlock();
        pool.freeChain(idx);
        u32 reused = pool.allocBlock();
        check(reused == idx, "freed block reused");
    }

    // T3: append — adds items
    {
        SpillPool<u32, 4> pool;
        u32 head = kSpillNone;
        head = pool.append(head, 10);
        head = pool.append(head, 20);
        check(pool.itemCount(head) == 2, "two items appended");
    }

    // T4: spill — spills to second block
    {
        SpillPool<u32, 2> pool;
        u32 head = kSpillNone;
        for (u32 i = 0; i < 5; ++i) head = pool.append(head, i * 100);
        check(pool.itemCount(head) == 5, "5 items across spill blocks");
    }

    // T5: iterator — iterates all items
    {
        SpillPool<u32, 2> pool;
        u32 head = kSpillNone;
        for (u32 i = 1; i <= 5; ++i) head = pool.append(head, i);
        u32 count = 0, sum = 0;
        for (auto it = pool.begin(head); it != pool.end(); ++it) {
            sum += *it; count++;
        }
        check(count == 5, "iterator yields 5 items");
        check(sum == 15, "sum correct (1+2+3+4+5)");
    }

    // T6: freeChain multi-block
    {
        SpillPool<u32, 2> pool;
        u32 head = kSpillNone;
        for (u32 i = 0; i < 6; ++i) head = pool.append(head, i);
        pool.freeChain(head);
        check(pool.freeListSize() == 3, "all 3 blocks freed");
    }

    return pass;
}

} // namespace LX_New_Test
