#pragma once

#include <functional>
#include <variant>

namespace LX_core::backend {

class VulkanComputePipeline;
class VulkanGraphicsPipeline;

using VulkanPipelineRef =
    std::variant<std::reference_wrapper<VulkanGraphicsPipeline>,
                 std::reference_wrapper<VulkanComputePipeline>>;

} // namespace LX_core::backend
