#pragma once

#include <optional>
#include <string>

namespace LX_demo::lxe_editor {

struct LxeEditorMcpMessage final {
  std::optional<std::string> idJson;
  std::string method;
  std::string paramsJson;
};

} // namespace LX_demo::lxe_editor
