#pragma once

#include "backend/vulkan/vulkan_renderer_types.hpp"

#include <string>
#include <string_view>

namespace LX_demo::lxe_editor {

[[nodiscard]] std::string debugColorTransferExportResultJson(
    const LX_core::backend::VulkanDebugColorTransferExportResult &result);

} // namespace LX_demo::lxe_editor
