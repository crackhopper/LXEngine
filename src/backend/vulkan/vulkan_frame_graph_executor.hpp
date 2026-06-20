#pragma once

#include "core/frame_graph/frame_graph_executor.hpp"

#include <functional>

namespace LX_core::backend {

class VulkanCommandBuffer;
class VulkanCommandBufferManager;
class VulkanDevice;
class VulkanResourceManager;

enum class VulkanFrameGraphExecutionMode {
  RecordOnly,
  ImmediateSubmitReadback,
};

struct VulkanFrameGraphExecutionTarget final {
  VulkanFrameGraphExecutionMode mode = VulkanFrameGraphExecutionMode::RecordOnly;
  VulkanDevice *device = nullptr;
  VulkanCommandBufferManager *commandManager = nullptr;
  VulkanResourceManager *resourceManager = nullptr;
  VulkanCommandBuffer *commandBuffer = nullptr;

  [[nodiscard]] bool recordsCommands() const {
    return resourceManager != nullptr && commandBuffer != nullptr;
  }

  [[nodiscard]] bool isPartial() const {
    if (mode == VulkanFrameGraphExecutionMode::RecordOnly) {
      return (resourceManager == nullptr) != (commandBuffer == nullptr);
    }
    return device == nullptr || commandManager == nullptr ||
           resourceManager == nullptr;
  }
};

namespace detail {

struct VulkanPreparedFramePassRecordStats final {
  u64 pipelineLookupCount = 0;
  u64 boundInputCount = 0;
  u64 executedInputCount = 0;
};

struct VulkanPreparedFramePassRecordHooks final {
  std::function<void(const RenderInputDesc &)> observeDesc;
  std::function<void(const RenderInputDesc &)> observeRejectedDesc;
};

[[nodiscard]] VulkanPreparedFramePassRecordStats recordPreparedFramePassWork(
    const VulkanFrameGraphExecutionTarget &target,
    const CompiledFrameGraphPass &compiledPass,
    const PreparedFramePassWork &work,
    const VulkanPreparedFramePassRecordHooks &hooks = {});

} // namespace detail

class VulkanFrameGraphExecutor final : public FrameGraphExecutor {
public:
  explicit VulkanFrameGraphExecutor(
      VulkanFrameGraphExecutionTarget executionTarget = {})
      : m_executionTarget(executionTarget) {}

  FrameGraphExecutionResult
  execute(const FrameGraphExecutionRequest &request) override;

private:
  VulkanFrameGraphExecutionTarget m_executionTarget;
};

} // namespace LX_core::backend
