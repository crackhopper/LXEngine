#pragma once

#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/render_input.hpp"
#include "core/frame_graph/render_work_build_context.hpp"

#include <memory>
#include <vector>

namespace LX_core {

class RenderWorkCompiler final {
public:
  void buildInputs(const FramePass &pass, const RenderWorkBuildContext &context,
                   std::vector<std::unique_ptr<RenderInput>> &outInputs) const;

  [[nodiscard]] std::vector<RenderInputDesc>
  prepare(const FramePass &pass, const RenderWorkBuildContext &context,
          const std::vector<std::unique_ptr<RenderInput>> &inputs) const;
};

} // namespace LX_core
