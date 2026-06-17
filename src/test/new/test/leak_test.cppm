module;
#include <cassert>
#include <iostream>
#include <vector>
#include <array>

export module LX_New_Test.Test.LeakTest;

import LX_New_Common.Memory;

export namespace LX_New_Test {

inline bool run_leak_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    // TL1: create + destroy scene graph
    {
        GameObjectManager gom;
        u32 scene = gom.allocMeta(), node = gom.allocMeta(), ro = gom.allocMeta();
        gom.setRefs(scene, std::array<u32,1>{node});
        gom.setRefs(node, std::array<u32,1>{ro});
        gom.addToRoot(scene);
        gom.tick();
        gom.removeFromRoot(scene);
        gom.tick();
        check(!gom.isAlive(scene), "scene collected");
    }

    // TL2: alloc resources, add root, remove, tick
    {
        GameObjectManager gom;
        u32 obj = gom.allocMeta();
        u64 fakeHandle = 0x12345678ull;
        fakeHandle |= (u64)1 << 16;
        gom.setHandles(obj, std::array<u64,1>{fakeHandle});
        gom.addToRoot(obj);
        gom.tick();
        gom.removeFromRoot(obj);
        gom.tick();
        check(!gom.isAlive(obj), "obj collected");
    }

    // TL3: spill pool alloc + free
    {
        SpillPool<u32, 4> pool;
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 20; ++i) head = pool.append(head, i);
        pool.freeChain(head);
        check(pool.freeListSize() > 0, "spill blocks freed");
    }

    // TL4: raw buffer alloc + free — verifies no leak
    {
        RawBuffer buf;
        std::vector<u32> offsets;
        for (int i = 0; i < 100; ++i) offsets.push_back(buf.allocate(64, 16));
        u32 capAfterAlloc = buf.capacity();
        for (auto o : offsets) buf.free(o, 64);
        // After freeing all, allocate again — should reuse freed space, not expand
        u32 reuse = buf.allocate(64, 16);
        check(reuse == 0, "reuses first freed block");
        check(buf.capacity() == capAfterAlloc, "capacity unchanged (no expansion)");
    }

    // TL5: cyclic refs unrooted → GC collects
    {
        GameObjectManager gom;
        u32 a = gom.allocMeta(), b = gom.allocMeta();
        gom.setRefs(a, std::array<u32,1>{b});
        gom.setRefs(b, std::array<u32,1>{a});
        gom.addToRoot(a);
        gom.tick();
        gom.removeFromRoot(a);
        gom.tick();
        check(!gom.isAlive(a) && !gom.isAlive(b), "cycle collected");
    }

    return pass;
}

} // namespace LX_New_Test
