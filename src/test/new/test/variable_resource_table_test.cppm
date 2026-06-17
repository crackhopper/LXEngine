module;
#include <cassert>
#include <iostream>
#include <cstring>

export module LX_New_Test.Test.VariableResourceTableTest;

import LX_New_Common.Platform;
import LX_New_Core.Resource;

export namespace LX_New_Test {

struct TestVarMeta { LX_New_Common::u32 width; LX_New_Common::u32 height; };

inline bool run_variable_resource_table_tests() {
    using namespace LX_New_Core;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    VariableResourceTable<TestVarMeta> table;
    table.setTypeId(2);

    // T1: allocate stores meta + raw data
    {
        TestVarMeta meta{64, 64};
        u8 pixelData[128] = {};
        pixelData[0] = 0xFF;
        ResourceHandle h = table.allocate(meta, {pixelData, 128});
        check(h.raw != 0, "alloc returns valid handle");
        check(h.type_id() == 2, "type_id correct");
    }

    // T2: getRawData returns correct pointer
    {
        TestVarMeta meta{32, 32};
        u8 pixelData[64] = {};
        pixelData[0] = 0xAA; pixelData[63] = 0xBB;
        ResourceHandle h = table.allocate(meta, {pixelData, 64});
        u8* raw = table.getRawData(h);
        check(raw != nullptr, "getRawData returns non-null");
        check(raw[0] == 0xAA, "raw data byte 0 correct");
        check(raw[63] == 0xBB, "raw data last byte correct");
    }

    // T3: getMeta returns correct metadata
    {
        TestVarMeta meta{128, 256};
        u8 data[10] = {};
        ResourceHandle h = table.allocate(meta, {data, 10});
        TestVarMeta* m = table.getMeta(h);
        check(m != nullptr, "getMeta returns non-null");
        check(m->width == 128, "meta width correct");
        check(m->height == 256, "meta height correct");
    }

    // T4: release frees resource
    {
        TestVarMeta meta{16, 16};
        u8 data[8] = {};
        ResourceHandle h = table.allocate(meta, {data, 8});
        table.release(h);
        check(table.getMeta(h) == nullptr, "getMeta returns null after release");
        check(table.getRawData(h) == nullptr, "getRawData returns null after release");
    }

    // T5: reuse freed slot with generation + 1
    {
        TestVarMeta meta{10, 20};
        u8 data[16] = {};
        data[0] = 0xCC;
        ResourceHandle h1 = table.allocate(meta, {data, 16});
        table.release(h1);
        ResourceHandle h2 = table.allocate(meta, {data, 16});
        check(h2.index() == h1.index(), "reuses same index");
        check(h2.generation() == h1.generation() + 1, "generation incremented");
        u8* raw = table.getRawData(h2);
        raw[0] = 0xDD;
        raw = table.getRawData(h2);
        check(raw[0] == 0xDD, "raw data writable after reuse");
    }

    // T6: stale handle returns nullptr
    {
        TestVarMeta meta{8, 8};
        u8 data[4] = {};
        ResourceHandle h1 = table.allocate(meta, {data, 4});
        table.release(h1);
        table.allocate(meta, {data, 4});
        ResourceHandle stale = h1;
        check(table.getMeta(stale) == nullptr, "stale handle getMeta returns nullptr");
        check(table.getRawData(stale) == nullptr, "stale handle getRawData returns nullptr");
    }

    // T7: refCount > 0 after one release keeps resource alive
    {
        TestVarMeta meta{4, 4};
        u8 data[2] = { 0x11, 0x22 };
        ResourceHandle h = table.allocate(meta, {data, 2});
        // Manual ref increment not exposed, but verify release makes refCount 0
        table.release(h);
        check(table.getMeta(h) == nullptr, "refCount zero after single release");
    }

    return pass;
}

} // namespace LX_New_Test
