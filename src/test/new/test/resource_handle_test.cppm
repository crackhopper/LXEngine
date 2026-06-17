module;
#include <cassert>
#include <iostream>

export module LX_New_Test.Test.ResourceHandleTest;

import LX_New_Core.Resource;

export namespace LX_New_Test {

inline bool run_resource_handle_tests() {
  using namespace LX_New_Core;
  bool pass = true;

  auto check = [&](bool cond, const char *msg) {
    if (!cond) {
      std::cerr << "  FAIL: " << msg << "\n";
      pass = false;
    }
  };

  // T1: default constructed handle is zero
  {
    ResourceHandle h;
    check(h.raw == 0, "default handle raw is zero");
    check(h.generation() == 0, "default handle generation is zero");
  }

  // T2: construct with values
  {
    ResourceHandle h;
    h.set_all(static_cast<MemoryTypeId>(1), static_cast<MemoryIndex>(0), static_cast<MemoryGeneration>(1));
    check(h.generation() != 0, "constructed handle generation nonzero");
    check(h.type_id() == 1, "type_id matches");
    check(h.index() == 0, "index matches");
  }

  // T3: round-trip
  {
    ResourceHandle h(static_cast<MemoryTypeId>(3), static_cast<MemoryIndex>(1000), static_cast<MemoryGeneration>(5));
    check(h.type_id() == 3, "type_id round-trip");
    check(h.index() == 1000, "index round-trip");
    check(h.generation() == 5, "generation round-trip");
  }

  // T4: comparison
  {
    ResourceHandle a(static_cast<MemoryTypeId>(1), static_cast<MemoryIndex>(42), static_cast<MemoryGeneration>(1));
    ResourceHandle b(static_cast<MemoryTypeId>(1), static_cast<MemoryIndex>(42), static_cast<MemoryGeneration>(1));
    ResourceHandle c(static_cast<MemoryTypeId>(2), static_cast<MemoryIndex>(42), static_cast<MemoryGeneration>(1));
    check(a == b, "equal handles compare equal");
    check(a != c, "different handles compare unequal");
  }

  return pass;
}

} // namespace LX_New_Test
