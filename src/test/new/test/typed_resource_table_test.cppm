module;
#include <cassert>
#include <iostream>

export module LX_New_Test.Test.TypedResourceTableTest;

import LX_New_Common.Platform;
import LX_New_Core.Resource;

export namespace LX_New_Test {

struct TestResource { LX_New_Common::f32 value[4]; };

inline bool run_typed_resource_table_tests() {
    using namespace LX_New_Core;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    TypedResourceTable<TestResource> table;
    table.setTypeId(1);

    // T1: first allocation gets index 0, generation 1
    {
        TestResource data = {{1.0f, 2.0f, 3.0f, 4.0f}};
        ResourceHandle h = table.allocate(data);
        check(h.index() == 0, "first alloc_index 0");
        check(h.generation() == 1, "first alloc generation 1");
        check(h.type_id() == 1, "type_id matches");
    }

    // T2: second allocation gets index 1
    {
        TestResource data = {{5.0f, 6.0f, 7.0f, 8.0f}};
        ResourceHandle h = table.allocate(data);
        check(h.index() == 1, "second alloc index 1");
    }

    // T3: get returns valid pointer to data
    {
        TestResource data = {{9.0f, 10.0f, 11.0f, 12.0f}};
        ResourceHandle h = table.allocate(data);
        TestResource* ptr = table.get(h);
        check(ptr != nullptr, "get returns non-null");
        check(ptr->value[0] == 9.0f, "get returns correct data");
    }

    // T4: release refCount decrements, not freed while > 0
    {
        TestResource data = {{13.0f, 14.0f, 15.0f, 16.0f}};
        ResourceHandle h = table.allocate(data);
        table.release(h); // refCount: 1 -> 0
        TestResource* ptr = table.get(h);
        check(ptr == nullptr, "get returns null after release");
    }

    // T5: reuse freed slot with generation + 1
    {
        TestResource data = {{17.0f, 18.0f, 19.0f, 20.0f}};
        ResourceHandle h1 = table.allocate(data);
        table.release(h1);
        ResourceHandle h2 = table.allocate(data);
        check(h2.index() == h1.index(), "reuses same index");
        check(h2.generation() == h1.generation() + 1, "generation incremented");
    }

    // T6: stale handle returns nullptr
    {
        TestResource data = {{21.0f, 22.0f, 23.0f, 24.0f}};
        ResourceHandle h1 = table.allocate(data);
        table.release(h1);
        table.allocate(data);
        ResourceHandle stale = h1;
        TestResource* ptr = table.get(stale);
        check(ptr == nullptr, "stale handle returns nullptr");
    }

    return pass;
}

} // namespace LX_New_Test
