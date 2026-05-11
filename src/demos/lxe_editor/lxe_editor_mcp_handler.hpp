#pragma once

#include "demos/lxe_editor/lxe_editor_api_service.hpp"

#include <string>
#include <string_view>

namespace LX_demo::lxe_editor {

struct LxeEditorMcpResponse final {
  bool hasBody = true;
  std::string body;
};

[[nodiscard]] LxeEditorMcpResponse
handleLxeEditorMcpHttpRequest(std::string_view payload,
                              LxeEditorApiService& service);

} // namespace LX_demo::lxe_editor
