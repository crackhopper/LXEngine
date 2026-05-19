#include "mesh.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <vector>

namespace LX_core {

std::vector<u32>
makeUniqueTriangleEdgeLineIndices(const std::vector<u32> &triangleIndices) {
  if (triangleIndices.size() % 3 != 0) {
    throw std::logic_error(
        "makeUniqueTriangleEdgeLineIndices requires a multiple of 3 indices");
  }

  std::set<std::array<u32, 2>> seenEdges;
  std::vector<u32> lineIndices;
  lineIndices.reserve(triangleIndices.size() * 2);

  for (usize i = 0; i < triangleIndices.size(); i += 3) {
    const u32 a = triangleIndices[i];
    const u32 b = triangleIndices[i + 1];
    const u32 c = triangleIndices[i + 2];
    const std::array<std::array<u32, 2>, 3> triangleEdges{{
        {std::min(a, b), std::max(a, b)},
        {std::min(b, c), std::max(b, c)},
        {std::min(a, c), std::max(a, c)},
    }};

    for (const auto &edge : triangleEdges) {
      const auto [it, inserted] = seenEdges.insert(edge);
      (void)it;
      if (inserted) {
        lineIndices.push_back(edge[0]);
        lineIndices.push_back(edge[1]);
      }
    }
  }

  return lineIndices;
}

} // namespace LX_core
