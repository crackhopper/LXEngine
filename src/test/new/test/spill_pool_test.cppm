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

    // ── 基础功能 ──────────────────────────────────────────────

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
        u32 head = kNoneChunk;
        head = pool.append(head, 10);
        head = pool.append(head, 20);
        check(pool.itemCount(head) == 2, "two items appended");
    }

    // T4: spill — spills to second block
    {
        SpillPool<u32, 2> pool;
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 5; ++i) head = pool.append(head, i * 100);
        check(pool.itemCount(head) == 5, "5 items across spill blocks");
    }

    // T5: iterator — iterates all items
    {
        SpillPool<u32, 2> pool;
        u32 head = kNoneChunk;
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
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 6; ++i) head = pool.append(head, i);
        pool.freeChain(head);
        check(pool.freeListSize() == 3, "all 3 blocks freed");
    }

    // ── 空状态与边界 ──────────────────────────────────────────

    // T7: empty pool state
    {
        SpillPool<u32, 4> pool;
        check(pool.freeListSize() == 0, "empty pool: freeList empty");
        check(pool.itemCount(kNoneChunk) == 0, "empty chain: itemCount 0");
    }

    // T8: iterator on empty chain — no items yielded
    {
        SpillPool<u32, 4> pool;
        u32 count = 0;
        for (auto it = pool.begin(kNoneChunk); it != pool.end(); ++it) { count++; }
        check(count == 0, "empty chain: no items iterated");
    }

    // T9: exact fill — N items in N-capacity block, no spill yet
    {
        SpillPool<u32, 4> pool;
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 4; ++i) head = pool.append(head, i);
        check(pool.itemCount(head) == 4, "exactly 4 items, no spill");
        check(pool.freeListSize() == 0, "no freed blocks during exact fill");
    }

    // ── 块回收 ────────────────────────────────────────────────

    // T10: block recycling after freeChain — allocBlock reuses from freeList
    {
        SpillPool<u32, 4> pool;
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 10; ++i) head = pool.append(head, i);  // 3 blocks (4+4+2)
        check(pool.freeListSize() == 0, "no free blocks while chain active");
        pool.freeChain(head);
        check(pool.freeListSize() == 3, "3 blocks returned to freeList");

        u32 b1 = pool.allocBlock();
        u32 b2 = pool.allocBlock();
        u32 b3 = pool.allocBlock();
        check(pool.freeListSize() == 0, "freeList empty after reusing 3 blocks");
    }

    // T11: reused block is properly reset
    {
        SpillPool<u32, 4> pool;
        u32 head = kNoneChunk;
        head = pool.append(head, 999);
        head = pool.append(head, 888);
        pool.freeChain(head);

        // Start fresh chain using recycled blocks
        head = kNoneChunk;
        head = pool.append(head, 1);
        check(pool.itemCount(head) == 1, "recycled block: only 1 item");
        for (auto it = pool.begin(head); it != pool.end(); ++it)
            check(*it == 1, "recycled block: correct value");
    }

    // ── 多独立链 ──────────────────────────────────────────────

    // T12: two independent chains in same pool
    {
        SpillPool<u32, 4> pool;
        u32 chainA = kNoneChunk;
        u32 chainB = kNoneChunk;
        chainA = pool.append(chainA, 100);
        chainB = pool.append(chainB, 200);
        chainA = pool.append(chainA, 101);
        chainB = pool.append(chainB, 201);
        check(pool.itemCount(chainA) == 2, "chainA has 2 items");
        check(pool.itemCount(chainB) == 2, "chainB has 2 items");

        // Free only chainA — chainB must remain intact
        pool.freeChain(chainA);
        check(pool.itemCount(chainB) == 2, "chainB intact after freeing chainA");
        u32 sum = 0;
        for (auto it = pool.begin(chainB); it != pool.end(); ++it) sum += *it;
        check(sum == 401, "chainB values correct (200+201)");
    }

    // ── 数据完整性 ────────────────────────────────────────────

    // T13: data integrity across spill boundary
    {
        SpillPool<u32, 2> pool;
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 5; ++i) head = pool.append(head, (i + 1) * 10);
        // Block 0: {10, 20}, Block 1: {30, 40}, Block 2: {50}
        u32 vals[5];
        u32 idx = 0;
        for (auto it = pool.begin(head); it != pool.end(); ++it)
            vals[idx++] = *it;
        check(idx == 5, "spill boundary: iterator yielded 5 items");
        check(vals[0] == 10 && vals[1] == 20 && vals[2] == 30 &&
              vals[3] == 40 && vals[4] == 50, "spill boundary: values correct in order");
    }

    // T14: append returns valid head from kNoneChunk
    {
        SpillPool<u32, 4> pool;
        u32 head = pool.append(kNoneChunk, 42);
        check(head != kNoneChunk, "first append returns valid head");
        check(head == 0, "first append returns block 0");
        u32 same = pool.append(head, 43);
        check(same == head, "subsequent append returns same head");
    }

    // ── 压力测试 ──────────────────────────────────────────────

    // T15: 200 items — verify count, order, and freeList after cleanup
    {
        SpillPool<u32, 8> pool;
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 200; ++i) head = pool.append(head, i);
        check(pool.itemCount(head) == 200, "stress: 200 items across blocks");

        u32 idx = 0;
        bool ok = true;
        for (auto it = pool.begin(head); it != pool.end(); ++it) {
            if (*it != idx++) { ok = false; break; }
        }
        check(ok, "stress: all 200 values correct in order");
        check(idx == 200, "stress: iterator visited all 200 items");

        pool.freeChain(head);
        check(pool.freeListSize() == 25, "stress: 200/8 = 25 blocks freed");
    }

    // T16: multiple alloc/free/alloc cycles — no corruption
    {
        SpillPool<u32, 4> pool;
        for (int round = 0; round < 50; ++round) {
            u32 head = kNoneChunk;
            for (u32 i = 0; i < 20; ++i) head = pool.append(head, round * 100 + i);
            pool.freeChain(head);
        }
        check(pool.freeListSize() > 0, "stress cycles: blocks accumulated in freeList");

        // Verify recycled blocks work correctly
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 4; ++i) head = pool.append(head, i);
        check(pool.itemCount(head) == 4, "stress cycles: recycled blocks functional");
        pool.freeChain(head);
    }

    return pass;
}

} // namespace LX_New_Test
