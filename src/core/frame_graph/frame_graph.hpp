#pragma once

#include "core/frame_graph/render_target.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/utils/string_table.hpp"
#include <vector>

namespace LX_core {

class Scene; // forward decl

/// 一个渲染 pass 的完整描述：名字（StringID，匹配 REQ-007 Pass_* 常量）、
/// 输出目标、以及该 pass 的 RenderQueue。
struct FramePass {
  StringID name;
  RenderTarget target;
  RenderQueue queue;
};

/// FrameGraph 是加载期预构建的入口：从 Scene 扫描所有 renderable × pass，
/// 填充每个 FramePass 的 queue，再汇总所有 PipelineBuildDesc 交给 backend。
class FrameGraph {
public:
  void addPass(FramePass pass);

  /// 遍历 scene.getRenderables()，对每个 FramePass 填充 queue。
  /// 已有的 queue 会被清空重填。
  void buildFromScene(const Scene &scene);

  /// 汇总所有 pass 的 PipelineBuildDesc，跨 pass 按 PipelineKey 再次去重。
  std::vector<PipelineBuildDesc> collectAllPipelineBuildDescs() const;

  const std::vector<FramePass> &getPasses() const { return m_passes; }
  std::vector<FramePass> &getPasses() { return m_passes; }

private:
  std::vector<FramePass> m_passes;
};

} // namespace LX_core
