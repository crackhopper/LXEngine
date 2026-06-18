#pragma once

#include "core/asset/render_effect.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/utils/string_table.hpp"
#include <optional>
#include <string>
#include <vector>

namespace LX_core {

class GraphResourceRegistry;

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
  std::optional<std::string> writeMode;
};

enum class FrameGraphPhase { PreEffect, Material, PostEffect, Debug };

/*
@source_analysis.section FramePass：pass 身份、target、input 与资源流
`FramePass` 把一条渲染 pass 的 core 层声明打包成一个结构体：

- `name`：StringID，来自 RenderPathGraph 的 pass 身份；它是这条 pass
  在后续 work 编译、material pass 选择、shader 变体合并里的统一身份
- `target`：这条 pass 的输出形状，使用 `RenderTargetDesc` 保留 offscreen /
  depth-only 等结构性描述；旧的 scene camera matching 边界再转回 `RenderTarget`
- `input`：RenderPathGraph 传入的 work 输入合同，只描述这条 pass 从哪里取
  输入，不在 FrameGraph 阶段生成 draw / dispatch payload
- `reads` / `writes`：有序 FrameGraph 的资源流声明，例如 Forward 写
  `scene.hdrColor`，PostProcess 再以 `SceneColor` binding 采样它

之所以打包而不是让 `FrameGraph` 持有多个并行 vector，是因为这些字段在每条
pass 上是强绑定的：`name` 决定后续编译诊断身份，`target` 决定 attachment
合同，`input` 决定后续编译输入来源，`reads` / `writes` 决定与前后 pass 的
attachment 依赖；分开
存就要在 `FrameGraph` 里维护"i-th name 对应 i-th target / resource flow"
的隐式索引，容易写出 off-by-one。

注意 FramePass 不持有任何 backend 资源（renderpass / framebuffer / pipeline
都在 backend 侧），也不持有 draw / dispatch payload。它纯粹是 core 层的
"这条 pass 的 graph 合同是什么，以及如何声明跨 pass attachment 读写"的
描述符。
*/
struct FramePass {
  StringID name;
  RenderTargetDesc target;
  std::vector<FrameGraphRead> reads;
  std::vector<FrameGraphWrite> writes;
  FrameGraphPhase phase = FrameGraphPhase::Material;
  u32 stableOrder = 0;
  ResourceUri shaderUri;
  RenderPassStage stage = RenderPassStage::Raster;
  RenderPassDispatch dispatch = RenderPassDispatch::Draw;
  RenderPassInputContract input;
  std::optional<RenderPathNodeRenderingMode> renderingMode;
  std::vector<RenderPathAttachmentContract> attachments;
  std::vector<RenderPathPayloadContract> payloads;
  RenderState renderState;
  StringID renderPathNodeSignature;
};

[[nodiscard]] StringID
getFramePassRenderPathNodeSignature(const FramePass &pass);

void syncFramePassAttachmentContractsWithTarget(FramePass &pass);

struct CompiledFrameGraphPass {
  StringID name;
  RenderTargetDesc target;
  std::vector<FrameGraphRead> reads;
  std::vector<FrameGraphWrite> writes;
  std::vector<RenderPathPayloadContract> payloads;
  usize sourcePassIndex = 0;
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
`FrameGraph` 是把 RenderPathGraph pass 合同收进一帧列表，并校验 pass 间资源
声明的入口。它的核心职责包括：

- 持有 `vector<FramePass>`：通过 `addPass` 累加，顺序是 declaration / original
  insertion order；真正的执行顺序由 `compile` 输出的 DAG order 决定
- 在 `compile` 时用 `GraphResourceRegistry` 校验 source / target 名称，
  将非 imported source 连接到对应 producer，并按资源依赖 DAG 排序 pass
- 编译排序的稳定兜底顺序是 phase、`stableOrder`、原始插入 index；phase
  约束保证 PreEffect 先于 Material、Material 先于 PostEffect、非 Debug 先于
Debug

注意它仍然不做 attachment 复用，也不持有 backend attachment 资源；这些都留给
backend 执行层。core 层这里只提供 registry-backed 资源依赖图、稳定 pass 顺序和
每条 pass 的 graph 合同。draw / dispatch input 和 pipeline-facing desc 由
FrameGraph::compile() 之后的 render work compiler 处理。
*/
class FrameGraph {
public:
  void addPass(FramePass pass);

  [[nodiscard]] CompiledFrameGraph
  compile(const GraphResourceRegistry &registry) const;
  [[nodiscard]] CompiledFrameGraph compile() const;

  const std::vector<FramePass> &getPasses() const { return m_passes; }
  std::vector<FramePass> &getPasses() { return m_passes; }

private:
  std::vector<FramePass> m_passes;
};

} // namespace LX_core
