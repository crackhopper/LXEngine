module;
#include <cassert>
#include <iostream>
#include <array>

export module LX_New_Test.Test.GcIntegrationTest;

import LX_New_Common.Memory;

export namespace LX_New_Test {

inline bool run_gc_integration_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    // T1: scene graph, addToRoot, tick → nothing collected
    std::cerr << "  T1...\n";
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
    std::cerr << "  T2...\n";
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
    std::cerr << "  T3...\n";
    {
        GameObjectManager gom;
        u32 s1 = gom.allocMeta(), s2 = gom.allocMeta(), n1 = gom.allocMeta(), n2 = gom.allocMeta();
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
    std::cerr << "  T4...\n";
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

    // T5: cycle — A→B→A, both unrooted, both collected
    std::cerr << "  T5...\n";
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
