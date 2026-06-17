module;
#include <cassert>
#include <iostream>

export module LX_New_Test.Test.ResourceHandleTest;

import LX_New_Common.Memory;

export namespace LX_New_Test {

inline bool run_resource_handle_tests() {
    using namespace LX_New_Common;
    bool pass = true;

    auto check = [&](bool cond, const char* msg) {
        if (!cond) {
            std::cerr << "  FAIL: " << msg << "\n";
            pass = false;
        }
    };

    // T1: isValid — invalid handle returns false
    {
        ResourceHandle h;
        h.raw = 0;
        check(!h.isValid(), "invalid handle should return false");
    }

    // T2: isValid — valid handle with generation != 0 returns true
    {
        ResourceHandle h;
        h.raw = 0;
        h.generation = 1;
        h.type_id = 1;
        h.index = 0;
        check(h.isValid(), "valid handle should return true");
    }

    // T3: packing/unpacking round-trip
    {
        ResourceHandle h;
        h.raw = 0;
        h.type_id = 3;
        h.index = 1000;
        h.generation = 5;
        h._reserved = 0xABCD;

        check(h.type_id == 3, "type_id round-trip");
        check(h.index == 1000, "index round-trip");
        check(h.generation == 5, "generation round-trip");
        check(h._reserved == 0xABCD, "reserved round-trip");
    }

    // T4: invalid() returns zero handle
    {
        ResourceHandle h = ResourceHandle::invalid();
        check(h.raw == 0, "invalid() returns zero");
        check(!h.isValid(), "invalid() is not valid");
    }

    return pass;
}

} // namespace LX_New_Test
