#include "core/asset/mesh.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace LX_core;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void expectEdges(const std::vector<u32> &triangleIndices,
                 const std::vector<u32> &expected) {
  const std::vector<u32> actual =
      makeUniqueTriangleEdgeLineIndices(triangleIndices);

  EXPECT(actual == expected, "edge line indices must match expected order");
}

void testSingleTriangleProducesThreeLineEdges() {
  expectEdges({0, 1, 2}, {0, 1, 1, 2, 0, 2});
}

void testSharedTriangleEdgeIsDeduplicated() {
  expectEdges({0, 1, 2, 2, 1, 3}, {0, 1, 1, 2, 0, 2, 1, 3, 2, 3});
}

void testRejectsIncompleteTriangleIndexList() {
  bool threw = false;
  try {
    (void)makeUniqueTriangleEdgeLineIndices({0, 1});
  } catch (const std::logic_error &error) {
    threw = true;
    EXPECT(std::string(error.what()).find("multiple of 3") != std::string::npos,
           "logic_error message must mention multiple of 3");
  }

  EXPECT(threw, "non-triangle index count must throw logic_error");
}

} // namespace

int main() {
  testSingleTriangleProducesThreeLineEdges();
  testSharedTriangleEdgeIsDeduplicated();
  testRejectsIncompleteTriangleIndexList();

  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "test_mesh_debug_geometry passed\n";
  return 0;
}
