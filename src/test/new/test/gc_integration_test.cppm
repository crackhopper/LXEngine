module;
#include <cassert>
#include <iostream>
#include <array>

export module LX_New_Test.Test.GcIntegrationTest;

import LX_New_Common.Platform;
import LX_New_Core.Resource;
import LX_New_Core.GameObject;

export namespace LX_New_Test {

inline bool run_gc_integration_tests() {
    using namespace LX_New_Core;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    // T1: scene graph, addToRoot, tick → nothing collected
    {
        GameObjectManager gom;
        u32 scene = gom.allocMeta(), node = gom.allocMeta(), ro = gom.allocMeta();
        gom.setRefs(scene, std::array<u32,1>{node});
        gom.setRefs(node, std::array<u32,1>{ro});
        gom.addToRoot(scene);
        gom.tick();
        check(gom.isAlive(scene) && gom.isAlive(node) && gom.isAlive(ro), "graph survives tick");
        gom.removeFromRoot(scene);
    }

    // T2: removeFromRoot, tick → entire graph collected
    {
        GameObjectManager gom;
        u32 scene = gom.allocMeta(), node = gom.allocMeta();
        gom.setRefs(scene, std::array<u32,1>{node});
        gom.addToRoot(scene);
        gom.tick();
        gom.removeFromRoot(scene);
        gom.tick();
        check(!gom.isAlive(scene) && !gom.isAlive(node), "both collected");
    }

    // T3: partial removal — only unreachable subgraph collected
    {
        GameObjectManager gom;
        u32 s1 = gom.allocMeta(), s2 = gom.allocMeta();
        u32 n1 = gom.allocMeta(), n2 = gom.allocMeta();
        gom.setRefs(s1, std::array<u32,1>{n1});
        gom.setRefs(s2, std::array<u32,1>{n2});
        gom.addToRoot(s1); gom.addToRoot(s2);
        gom.tick();
        gom.removeFromRoot(s1);
        gom.tick();
        check(!gom.isAlive(s1) && !gom.isAlive(n1), "s1/n1 collected");
        check(gom.isAlive(s2) && gom.isAlive(n2), "s2/n2 survive");
        gom.removeFromRoot(s2);
    }

    // T4: shared resource — survives while either root alive
    {
        GameObjectManager gom;
        u32 s1 = gom.allocMeta(), s2 = gom.allocMeta(), shared = gom.allocMeta();
        gom.setRefs(s1, std::array<u32,1>{shared});
        gom.setRefs(s2, std::array<u32,1>{shared});
        gom.addToRoot(s1); gom.addToRoot(s2);
        gom.tick();
        gom.removeFromRoot(s1);
        gom.tick();
        check(gom.isAlive(shared), "shared survives (s2 still references)");
        gom.removeFromRoot(s2);
        gom.tick();
        check(!gom.isAlive(shared), "shared collected after both removed");
    }

    // T5: handle release cascade — GameObject death cascades into ResourceManager::release
    {
        ResourceManager rm;
        TypedResourceTable<f32> table;
        table.setTypeId(3);
        rm.registerTable(3, &table);
        GameObjectManager gom(&rm);

        f32 v1 = 1.0f, v2 = 2.0f;
        ResourceHandle h1 = table.allocate(v1);
        ResourceHandle h2 = table.allocate(v2);
        check(table.get(h1) != nullptr, "h1 valid before GC");
        check(table.get(h2) != nullptr, "h2 valid before GC");

        u32 obj = gom.allocMeta();
        gom.setHandles(obj, std::array<u64,2>{h1.raw, h2.raw});
        gom.addToRoot(obj);
        gom.tick();
        check(table.get(h1) != nullptr, "h1 valid while rooted");
        check(table.get(h2) != nullptr, "h2 valid while rooted");

        gom.removeFromRoot(obj);
        gom.tick();
        check(!gom.isAlive(obj), "obj collected");
        check(table.get(h1) == nullptr, "h1 released by cascade");
        check(table.get(h2) == nullptr, "h2 released by cascade");
    }

    return pass;
}

} // namespace LX_New_Test
