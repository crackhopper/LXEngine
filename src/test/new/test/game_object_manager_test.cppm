module;
#include <cassert>
#include <iostream>
#include <array>

export module LX_New_Test.Test.GameObjectManagerTest;

import LX_New_Common.Memory;

export namespace LX_New_Test {

inline bool run_game_object_manager_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    GameObjectManager gom;

    // T1: allocMeta - first gets index 0, alive=true
    { u32 idx = gom.allocMeta(); check(idx == 0, "first meta index 0"); check(gom.isAlive(idx), "alive after alloc"); }

    // T2: freed meta reused
    { u32 idx1 = gom.allocMeta(); gom.freeMeta(idx1); u32 idx2 = gom.allocMeta(); check(idx2 == idx1, "freed meta reused"); }

    // T3: setRefs - inline refs stored
    { u32 idx = gom.allocMeta(); u32 refs[] = {10u, 20u, 30u}; gom.setRefs(idx, refs); check(gom.getRefCount(idx) == 3, "ref count 3"); }

    // T4: setRefs - spills when > 4
    { u32 idx = gom.allocMeta(); u32 refs[] = {1u,2u,3u,4u,5u,6u}; gom.setRefs(idx, refs); check(gom.getRefCount(idx) == 4, "inline capped at 4"); check(gom.getSpillRefCount(idx) == 2, "spill count 2"); }

    // T5: setHandles - inline stored
    { u32 idx = gom.allocMeta(); u64 h[] = {0x1111ull, 0x2222ull}; gom.setHandles(idx, h); check(gom.getHandleCount(idx) == 2, "handle count 2"); }

    // T6: setHandles - spills when > 4
    { u32 idx = gom.allocMeta(); u64 h[] = {1ull,2ull,3ull,4ull,5ull}; gom.setHandles(idx, h); check(gom.getHandleCount(idx) == 4, "inline capped at 4"); check(gom.getSpillHandleCount(idx) == 1, "spill handle count 1"); }

    // T7: addToRoot / removeFromRoot
    { u32 idx = gom.allocMeta(); gom.addToRoot(idx); check(gom.getRootCount() == 1, "root count 1"); gom.removeFromRoot(idx); check(gom.getRootCount() == 0, "root count 0"); }

    // T8: mark - single root marks itself
    { u32 idx = gom.allocMeta(); gom.addToRoot(idx); gom.mark(); check(gom.isMarked(idx), "root marked"); gom.clearMarks(); gom.removeFromRoot(idx); }

    // T9: mark - transitive refs (A->B->C)
    { u32 a = gom.allocMeta(), b = gom.allocMeta(), c = gom.allocMeta();
      gom.setRefs(a, std::array<u32,1>{b}); gom.setRefs(b, std::array<u32,1>{c});
      gom.addToRoot(a); gom.mark();
      check(gom.isMarked(a) && gom.isMarked(b) && gom.isMarked(c), "transitive marked");
      gom.clearMarks(); gom.removeFromRoot(a); }

    // T10: mark - dead meta not traversed
    { u32 a = gom.allocMeta(), b = gom.allocMeta();
      gom.setRefs(a, std::array<u32,1>{b}); gom.freeMeta(b);
      gom.addToRoot(a); gom.mark(); check(gom.isMarked(a), "a marked, b skipped");
      gom.clearMarks(); gom.removeFromRoot(a); }

    // T11: mark - cycle doesn't infinite loop (A->B->A)
    { u32 a = gom.allocMeta(), b = gom.allocMeta();
      gom.setRefs(a, std::array<u32,1>{b}); gom.setRefs(b, std::array<u32,1>{a});
      gom.addToRoot(a); gom.mark();
      check(gom.isMarked(a) && gom.isMarked(b), "cycle both marked");
      gom.clearMarks(); gom.removeFromRoot(a); }

    return pass;
}

} // namespace LX_New_Test
