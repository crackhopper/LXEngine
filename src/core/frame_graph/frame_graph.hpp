#pragma once

#include "core/asset/material_instance.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/utils/string_table.hpp"
#include <string>
#include <vector>

namespace LX_core {

class Scene; // forward decl

struct FrameGraphResourceRef {
  StringID name;
  FrameGraphAttachmentKind kind = FrameGraphAttachmentKind::Color;

  static FrameGraphResourceRef colorAttachment(StringID name);
  static FrameGraphResourceRef depthAttachment(StringID name);
};

struct FrameGraphRead {
  StringID resource;
  StringID bindingName;

  static FrameGraphRead sampled(StringID resource,
                                StringID bindingName = StringID{});
};

struct FrameGraphWrite {
  FrameGraphResourceRef resource;
};

enum class FramePassKind {
  RenderQueue,
  FullscreenProcedural,
};

/*
@source_analysis.section FramePass：三元组 (name, target, queue)
`FramePass` 把三件本来分散的事打包成一个结构体：

- `name`：StringID，匹配 REQ-007 的 `Pass_*` 常量；它是这条 pass 在 scene-level
  资源筛选、material pass 选择、shader 变体合并里的统一身份
- `target`：这条 pass 的输出形状，使用 `RenderTargetDesc` 保留 offscreen /
  depth-only 等结构性描述；旧的 scene camera matching 边界再转回 `RenderTarget`
- `queue`：这条 pass 内部的 RenderingItem 收口（见 `render_queue.md`）

之所以打包而不是让 `FrameGraph` 持有三个并行 vector，是因为这三个字段在每条
pass 上是强绑定的：`name` 决定 queue 怎么过滤，`target` 决定 queue 怎么注入
scene-level 资源；分开存就要在 `FrameGraph` 里维护"i-th name 对应 i-th target"
的隐式索引，容易写出 off-by-one。

注意 FramePass 不持有任何 backend 资源（renderpass / framebuffer / pipeline
都在 backend 侧）— 它纯粹是 core 层的"这条 pass 怎么从 scene 里挑出 draw
列表"的描述符。
*/
struct FramePass {
  StringID name;
  RenderTargetDesc target;
  RenderQueue queue;
  std::vector<FrameGraphRead> reads;
  std::vector<FrameGraphWrite> writes;
  FramePassKind kind = FramePassKind::RenderQueue;
  MaterialInstanceSharedPtr fullscreenMaterial;
};

struct CompiledFrameGraphPass {
  StringID name;
  RenderTargetDesc target;
  std::vector<FrameGraphRead> reads;
  std::vector<FrameGraphWrite> writes;
  FramePassKind kind = FramePassKind::RenderQueue;
  MaterialInstanceSharedPtr fullscreenMaterial;
};

class FrameGraphSampledResource final : public IGpuResource {
public:
  FrameGraphSampledResource(StringID resourceName, StringID bindingName)
      : m_resourceName(resourceName), m_bindingName(bindingName) {}

  ResourceType getType() const override { return ResourceType::Special; }
  const void *getRawData() const override { return nullptr; }
  u32 getByteSize() const override { return 0; }
  StringID getBindingName() const override { return m_bindingName; }
  StringID getResourceName() const { return m_resourceName; }

private:
  StringID m_resourceName;
  StringID m_bindingName;
};

class CompiledFrameGraph {
public:
  [[nodiscard]] bool isValid() const;
  [[nodiscard]] const std::vector<std::string> &getErrors() const;
  [[nodiscard]] std::string errorText() const;
  [[nodiscard]] const std::vector<CompiledFrameGraphPass> &getPasses() const;

private:
  friend class FrameGraph;

  std::vector<CompiledFrameGraphPass> m_passes;
  std::vector<std::string> m_errors;
};

/*
@source_analysis.section FrameGraph：加载期预构建的 per-pass 调度器
`FrameGraph` 是把 scene 翻译成"按 pass 组织的 RenderingItem 列表"并校验
pass 间资源声明的入口。它的核心职责包括：

- 持有 `vector<FramePass>`：通过 `addPass` 累加，顺序即提交顺序
- 在 `buildFromScene` 时按 pass 顺序逐个调用 `RenderQueue::buildFromScene`，
  把 `pass.target` 经 `RenderTarget` 兼容外壳透传下去（REQ-009 target 轴的入口）
- 在 `compile` 时按声明顺序校验 read/write 资源：read 只能引用此前 pass 写过
  的资源，write 不能重名，也不能写 unnamed resource

注意它仍然不做 pass reorder、不做 attachment 复用，也不持有 backend attachment
资源；这些都留给 backend 执行层。core 层这里只提供声明顺序的最小资源图和
per-pass queue 预构建。

跨 pass 唯一的协调动作是 `collectAllPipelineBuildDescs`：在所有 queue 输出
的 PipelineBuildDesc 上做一次全局 PipelineKey 去重，避免相同 pipeline 在
不同 pass 里被 backend 重复构建。这是分层去重的外层 — 内层（同 pass 内）
由 `RenderQueue::collectUniquePipelineBuildDescs` 完成。
*/
class FrameGraph {
public:
  void addPass(FramePass pass);

  void buildFromScene(const Scene &scene);

  [[nodiscard]] CompiledFrameGraph compile() const;

  std::vector<PipelineBuildDesc> collectAllPipelineBuildDescs() const;

  const std::vector<FramePass> &getPasses() const { return m_passes; }
  std::vector<FramePass> &getPasses() { return m_passes; }

private:
  std::vector<FramePass> m_passes;
};

/*
@source_analysis.section buildFromScene：把 pass × scene 二维问题摊成一维循环
`FrameGraph::buildFromScene` 的实现核心是一行循环：每条 pass 上调用
`pass.queue.buildFromScene(scene, pass.name, RenderTarget{pass.target})`，把
"哪条 pass、画到哪种 target"两个参数从 FramePass 解包后透传给 RenderQueue。

这种"FrameGraph 不做语义、只做调度"的写法把"pass × scene"二维问题摊成
一维循环。每一条 pass 的 RenderQueue 内部独立完成 REQ-009 两轴筛选，
FrameGraph 只负责保证 *每条 pass 都被处理一次* 这一条简单不变量。

调用语义上这是重建而非增量：每次 `buildFromScene` 都触发每个 queue 的
`clearItems()` + 重新填入，符合 RenderQueue 自身的"重建语义优先于增量
正确性"约定。
*/

/*
@source_analysis.section collectAllPipelineBuildDescs：跨 pass 全局 PipelineKey 去重
这是 FrameGraph 唯一面向 backend 的输出：把所有 pass 的预构建需求汇总成
一份去重后的 `PipelineBuildDesc` 列表。

去重粒度是 `PipelineKey`，与 RenderQueue 内层去重一致。两层去重的关系：

- RenderQueue 内层：去掉同一条 queue 内重复的 RenderingItem（同材质同几何的
  多个实例只产出一条 build desc）
- FrameGraph 外层：去掉跨 pass 出现的相同 PipelineKey（理论上罕见，但当材质
  支持多 pass 且某两 pass 在 PipelineKey 上恰好碰撞时会出现）

之所以分两层而不是一次性全局去重，是因为 RenderQueue 不知道其它 pass 存在 —
让它去做全局判定会破坏单 pass 收口的封装。两层各自只看自己的视角，
FrameGraph 这一层只看到"队列已去重的输出"再做最少整理。

REQ-042 R5 落地后，`PipelineKey` 变三级 compose（含 targetSig）；本函数代码
不需要变，但跨 pass 重复 PipelineKey 的概率会进一步降低 — 不同 target
的 pass 在 PipelineKey 上自动不重叠。
*/

} // namespace LX_core
