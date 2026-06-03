#pragma once

#include "core/asset/mesh.hpp"

#include <string_view>

namespace LX_core {

[[nodiscard]] MeshSharedPtr buildBuiltinPrimitiveMesh(std::string_view meshUri);
[[nodiscard]] bool isBuiltinPrimitiveMeshUri(std::string_view meshUri);

} // namespace LX_core
