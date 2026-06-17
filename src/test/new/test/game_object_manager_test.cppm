module;
#include <cassert>
#include <iostream>
#include <array>

export module LX_New_Test.Test.GameObjectManagerTest;

import LX_New_Common.Platform;
import LX_New_Core.Resource;
import LX_New_Core.GameObject;

export namespace LX_New_Test {

inline bool run_game_object_manager_tests() {
    using namespace LX_New_Core;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    GameObjectManager gom;

    // ── Meta 管理 ──────────────────────────────────────────────

    // T1: allocMeta — first gets index 0, alive=true
    {
        u32 idx = gom.allocMeta();
        check(idx == 0, "first meta index 0");
        check(gom.isAlive(idx), "alive after alloc");
    }

    // T2: freed meta reused
    {
        u32 idx1 = gom.allocMeta();
        gom.freeMeta(idx1);
        u32 idx2 = gom.allocMeta();
        check(idx2 == idx1, "freed meta reused");
    }

    // ── setRefs ───────────────────────────────────────────────

    // T3: setRefs — inline refs (≤ 4)
    {
        u32 idx = gom.allocMeta();
        u32 refs[] = {10u, 20u, 30u};
        gom.setRefs(idx, refs);
        check(gom.getRefCount(idx) == 3, "ref count 3");
    }

    // T4: setRefs — spills when > 4
    {
        u32 idx = gom.allocMeta();
        u32 refs[] = {1u, 2u, 3u, 4u, 5u, 6u};
        gom.setRefs(idx, refs);
        check(gom.getRefCount(idx) == 4, "inline capped at 4");
        check(gom.getSpillRefCount(idx) == 2, "spill count 2");
    }

    // ── setHandles ───────────────────────────────────────────

    // T5: setHandles — inline (≤ 4)
    {
        u32 idx = gom.allocMeta();
        u64 h[] = {0x1111ull, 0x2222ull};
        gom.setHandles(idx, h);
        check(gom.getHandleCount(idx) == 2, "handle count 2");
    }

    // T6: setHandles — spills when > 4
    {
        u32 idx = gom.allocMeta();
        u64 h[] = {1ull, 2ull, 3ull, 4ull, 5ull};
        gom.setHandles(idx, h);
        check(gom.getHandleCount(idx) == 4, "inline capped at 4");
        check(gom.getSpillHandleCount(idx) == 1, "spill handle count 1");
    }

    // ── Root 管理 ──────────────────────────────────────────────

    // T7: addToRoot / removeFromRoot
    {
        u32 idx = gom.allocMeta();
        gom.addToRoot(idx);
        check(gom.getRootCount() == 1, "root count 1");
        gom.removeFromRoot(idx);
        check(gom.getRootCount() == 0, "root count 0");
    }

    // ── Mark ──────────────────────────────────────────────────

    // T8: mark — single root marks itself
    {
        u32 idx = gom.allocMeta();
        gom.addToRoot(idx);
        gom.mark();
        check(gom.isMarked(idx), "root marked");
        gom.clearMarks();
        gom.removeFromRoot(idx);
    }

    // T9: mark — transitive refs (A→B→C)
    {
        u32 a = gom.allocMeta(), b = gom.allocMeta(), c = gom.allocMeta();
        gom.setRefs(a, std::array<u32,1>{b});
        gom.setRefs(b, std::array<u32,1>{c});
        gom.addToRoot(a);
        gom.mark();
        check(gom.isMarked(a) && gom.isMarked(b) && gom.isMarked(c), "transitive marked");
        gom.clearMarks();
        gom.removeFromRoot(a);
    }

    // T10: mark — dead meta not traversed
    {
        u32 a = gom.allocMeta(), b = gom.allocMeta();
        gom.setRefs(a, std::array<u32,1>{b});
        gom.freeMeta(b);
        gom.addToRoot(a);
        gom.mark();
        check(gom.isMarked(a), "a marked, b skipped (dead)");
        check(!gom.isMarked(b), "dead b not marked");
        gom.clearMarks();
        gom.removeFromRoot(a);
    }

    // T11: mark — cycle doesn't infinite loop (A→B→A)
    {
        u32 a = gom.allocMeta(), b = gom.allocMeta();
        gom.setRefs(a, std::array<u32,1>{b});
        gom.setRefs(b, std::array<u32,1>{a});
        gom.addToRoot(a);
        gom.mark();
        check(gom.isMarked(a) && gom.isMarked(b), "cycle both marked");
        gom.clearMarks();
        gom.removeFromRoot(a);
    }

    // ── Sweep ─────────────────────────────────────────────────

    // T12: sweep — unmarked meta released, handles released to ResourceManager
    {
        ResourceManager rm;
        TypedResourceTable<f32> table;
        table.setTypeId(7);
        rm.registerTable(7, &table);
        GameObjectManager gom2(&rm);

        f32 data = 42.0f;
        ResourceHandle h = table.allocate(data);
        check(h.isValid(), "resource allocated");

        u32 idx = gom2.allocMeta();
        gom2.setHandles(idx, std::array<u64,1>{h.raw});
        // No root → unmarked → swept
        gom2.tick();
        check(!gom2.isAlive(idx), "unrooted meta collected");
        check(table.get(h) == nullptr, "handle released to ResourceManager");
    }

    // T13: sweep — spill handles released and spill chain freed
    {
        ResourceManager rm;
        TypedResourceTable<f32> table;
        table.setTypeId(8);
        rm.registerTable(8, &table);
        GameObjectManager gom2(&rm);

        // Allocate 6 handles (> 4, forces spill)
        ResourceHandle handles[6];
        for (int i = 0; i < 6; ++i)
            handles[i] = table.allocate((f32)i);

        u32 idx = gom2.allocMeta();
        u64 raws[] = {handles[0].raw, handles[1].raw, handles[2].raw,
                      handles[3].raw, handles[4].raw, handles[5].raw};
        gom2.setHandles(idx, raws);
        gom2.tick();
        check(!gom2.isAlive(idx), "meta with spill handles collected");
        // All handles should be released
        bool allReleased = true;
        for (int i = 0; i < 6; ++i)
            if (table.get(handles[i]) != nullptr) allReleased = false;
        check(allReleased, "all spill handles released");
    }

    // T14: sweep — marked meta survives
    {
        GameObjectManager gom2;
        u32 idx = gom2.allocMeta();
        gom2.addToRoot(idx);
        gom2.tick();
        check(gom2.isAlive(idx), "rooted meta survives tick");
        gom2.removeFromRoot(idx);
    }

    // T15: tick full cycle — mark + sweep, unrooted collected
    {
        GameObjectManager gom2;
        u32 a = gom2.allocMeta();
        u32 b = gom2.allocMeta();
        u32 c = gom2.allocMeta();
        gom2.setRefs(a, std::array<u32,1>{b});
        gom2.setRefs(b, std::array<u32,1>{c});
        gom2.addToRoot(a);
        gom2.tick();
        check(gom2.isAlive(a) && gom2.isAlive(b) && gom2.isAlive(c), "rooted chain survives");
        gom2.removeFromRoot(a);
        gom2.tick();
        check(!gom2.isAlive(a) && !gom2.isAlive(b) && !gom2.isAlive(c), "unrooted chain collected");
    }

    // T16: cycle — A→B→A, both unrooted, both collected
    {
        GameObjectManager gom2;
        u32 a = gom2.allocMeta(), b = gom2.allocMeta();
        gom2.setRefs(a, std::array<u32,1>{b});
        gom2.setRefs(b, std::array<u32,1>{a});
        gom2.addToRoot(a);
        gom2.tick();
        check(gom2.isAlive(a) && gom2.isAlive(b), "cycle survives while rooted");
        gom2.removeFromRoot(a);
        gom2.tick();
        check(!gom2.isAlive(a) && !gom2.isAlive(b), "cycle collected when unrooted");
    }

    return pass;
}

} // namespace LX_New_Test
