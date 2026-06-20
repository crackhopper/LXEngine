#pragma once

#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/render_input.hpp"
#include "core/platform/types.hpp"

#include <span>
#include <string>
#include <vector>

namespace LX_core {

struct FrameGraphExecutionRequest final {
  const FrameGraph *graph = nullptr;
  const CompiledFrameGraph *compiled = nullptr;
  std::span<const PreparedFramePassWork> preparedPasses;
};

struct FrameGraphExecutionPayload final {
  std::string name;
  std::string target;
  std::string format;
  RenderPathOutputKind kind = RenderPathOutputKind::Buffer;
  Vec3u extent{1u, 1u, 1u};
  std::string mediaType;
  std::vector<u8> bytes;
};

struct FrameGraphExecutionResult final {
  bool ok = false;
  std::vector<std::string> diagnostics;
  std::vector<FrameGraphExecutionPayload> outputs;
};

class FrameGraphExecutor {
public:
  virtual ~FrameGraphExecutor() = default;
  virtual FrameGraphExecutionResult
  execute(const FrameGraphExecutionRequest &request) = 0;
};

} // namespace LX_core
