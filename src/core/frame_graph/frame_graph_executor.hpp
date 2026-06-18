#pragma once

#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/render_input.hpp"

#include <span>
#include <string>
#include <vector>

namespace LX_core {

struct FrameGraphExecutionRequest final {
  const FrameGraph *graph = nullptr;
  const CompiledFrameGraph *compiled = nullptr;
  std::span<const PreparedFramePassWork> preparedPasses;
};

struct FrameGraphExecutionResult final {
  bool ok = false;
  std::vector<std::string> diagnostics;
};

class FrameGraphExecutor {
public:
  virtual ~FrameGraphExecutor() = default;
  virtual FrameGraphExecutionResult
  execute(const FrameGraphExecutionRequest &request) = 0;
};

} // namespace LX_core
