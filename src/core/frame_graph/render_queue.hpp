#pragma once

#include "core/asset/render_effect.hpp"
#include "core/frame_graph/render_work_build_context.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/scene/scene.hpp"
#include <optional>
#include <vector>

namespace LX_core {

struct SceneResourceTableUploadView;

enum class RenderBatchDiagnosticReason {
  ObjectDataSignatureMismatch,
  MaterialTypeSignatureMismatch,
  SourceMaterialRefUnresolved,
  ObjectDrawRecordUnresolved,
  InvalidSourceMaterialRef,
  InvalidDrawRecord,
  MissingMeshRange,
  InvalidMeshRange,
  ZeroIndexCount,
  ZeroInstanceCount,
  GlobalGeometryTableMissing,
  BackendIndirectUnsupported,
  LegacyInputRejected,
};

struct RenderDrawInput final {
  usize inputIndex = 0;
  ObjectHandle object;
  MeshHandle mesh;
  MaterialHandle material;
  u32 primitiveIndex = u32_max;
  StringID debugId;
  Vec3f sortCenter{};
  StringID materialTypeSignature;
};

struct RenderBatchPipelineFacts final {
  StringID materialTypeSignature;
  ShaderProgramSet shaderProgram;
  IShaderSharedPtr shaderInfo;
  RenderState renderState;
  VertexLayout vertexLayout;
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
};

struct RenderBatchGeometryResources final {
  GpuResourceRef vertexBuffer;
  GpuResourceRef indexBuffer;

  [[nodiscard]] bool isValid() const {
    return vertexBuffer.isValid() && indexBuffer.isValid();
  }
};

struct RenderPathNodeContext final {
  StringID pass;
  StringID renderPathNodeSignature;
  std::optional<RenderPathNodeRenderingMode> renderingMode;
  std::optional<RenderPathGeometryContract> geometryContract;
  std::vector<RenderPathAttachmentContract> attachments;
  RenderTargetDesc target;
  DescriptorResourceList sceneResources;
  RenderBatchGeometryResources batchGeometryResources;
  std::vector<RenderBatchPipelineFacts> pipelineFacts;
  StringID objectDataSignature = StringID("BindlessObjectData.v1");
  bool backendIndirectSupported = true;
};

struct PreparedRenderDrawCandidate final {
  usize inputIndex = 0;
  u32 drawRecordIndex = u32_max;
  u32 objectIndex = u32_max;
  u32 materialIndex = u32_max;
  u32 materialRefIndex = u32_max;
  u32 sourceStorageIndex = u32_max;
  u32 sourceLocalMaterialIndex = u32_max;
  u32 meshIndex = u32_max;
  u32 indexCount = 0;
  u32 firstIndex = 0;
  i32 vertexOffset = 0;
  u32 instanceCount = 1;
  StringID objectDataSignature;
  StringID materialTypeSignature;
  StringID finalShaderReflectionIdentity;
  StringID debugId;
  Vec3f sortCenter{};
};

struct RenderBatch final {
  usize batchIndex = 0;
  StringID objectDataSignature;
  StringID materialTypeSignature;
  PipelineKey derivedPipelineKey;
  u32 commandOffset = 0;
  u32 commandCount = 0;
  std::vector<IndexedIndirectDrawCommand> commands;
  std::vector<usize> candidateIndices;
};

struct RenderBatchStats final {
  usize inputDrawCount = 0;
  usize preparedCandidateCount = 0;
  usize batchCount = 0;
  usize drawCount = 0;
  usize indirectCapableDrawCount = 0;
  usize unsupportedDrawCount = 0;
  usize legacyRejectedDrawCount = 0;
  usize fallbackObservedCount = 0;
};

struct RenderBatchDiagnostic final {
  RenderBatchDiagnosticReason reason =
      RenderBatchDiagnosticReason::LegacyInputRejected;
  usize inputIndex = 0;
  std::optional<usize> candidateIndex;
  StringID pass;
  StringID debugId;
  StringID objectDataSignature;
  StringID materialTypeSignature;
  std::optional<PipelineKey> derivedPipelineKey;
  u32 drawRecordIndex = u32_max;
  u32 materialRefIndex = u32_max;
  u32 meshIndex = u32_max;
};

struct RenderPathNodeData final {
  std::vector<RenderDrawInput> drawInputs;
  std::vector<PreparedRenderDrawCandidate> preparedCandidates;
  std::vector<RenderBatchDiagnostic> preparationDiagnostics;
  bool preparationValid = false;
  usize preparedInputCount = 0;
};

struct RenderBatchAnalysis final {
  RenderPathNodeContext context;
  std::vector<PreparedRenderDrawCandidate> candidates;
  std::vector<RenderBatch> batches;
  std::vector<RenderBatchDiagnostic> diagnostics;
  RenderBatchStats stats;

  [[nodiscard]] bool ok() const {
    return diagnostics.empty() && stats.fallbackObservedCount == 0;
  }
};

/*
@source_analysis.section RenderWorkQueue：RenderPathNode 级 draw 输入与 batch analysis
收口 RenderWorkQueue 是 per-node/per-pass 的，不是全局的。realtime geometry 的
正向数据由 `RenderPathNodeContext` + `RenderPathNodeData` 表达：上游只写入
handle/ref 级 `RenderDrawInput`，typed object/draw/material/mesh index 由后续
preparation 通过 `SceneResourceTableUploadView` 解析。

`compileIndirectBatches()` 不再返回旧的 vector batch，而是返回
`RenderBatchAnalysis`。analysis 同时携带 batches、diagnostics 和 stats，让 backend
和测试能区分 "可 indirect 提交" 与 "被明确拒绝"。旧 `RenderWorkItem` 容器只保留给
compute/offline 等非 geometry dispatch，不能作为 realtime geometry 的正向 batch
输入。
*/
class RenderWorkQueue {
public:
  void addItem(RenderWorkItem item);
  void clearItems();
  void setNodeContext(RenderPathNodeContext context);
  void addDrawInput(RenderDrawInput input);
  void prepareDrawInputs(const SceneResourceTableUploadView &uploadView);

  void sort();
  void sort(const std::optional<Vec3f> &cameraEye);

  const std::vector<RenderWorkItem> &getItems() const {
    return m_nonGeometryDispatchItems;
  }
  std::vector<RenderWorkItem> &getItems() { return m_nonGeometryDispatchItems; }
  const RenderPathNodeData &nodeData() const { return m_nodeData; }
  const std::optional<RenderPathNodeContext> &nodeContext() const {
    return m_context;
  }
  const RenderBatchAnalysis &lastBatchAnalysis() const {
    return m_lastBatchAnalysis;
  }

  std::vector<PipelineBuildDesc> collectUniquePipelineBuildDescs() const;
  RenderBatchAnalysis compileIndirectBatches() const;

  void build(const RenderWorkBuildContext &context, StringID pass,
             const RenderTarget &target, StringID renderPathNodeSignature,
             std::optional<RenderPathGeometryContract> geometryContract,
             std::optional<RenderPathNodeRenderingMode> renderingMode =
                 std::nullopt,
             std::vector<RenderPathAttachmentContract> attachments = {});

private:
  void buildRealtime(const Scene &scene, StringID pass,
                     const RenderTarget &target,
                     StringID renderPathNodeSignature,
                     std::optional<RenderPathGeometryContract> geometryContract,
                     std::optional<RenderPathNodeRenderingMode> renderingMode,
                     std::vector<RenderPathAttachmentContract> attachments,
                     DescriptorResourceList sceneResources,
                     VisibilityLayerMask visibleMask,
                     std::optional<Vec3f> cameraEye);

  std::optional<RenderPathNodeContext> m_context;
  RenderPathNodeData m_nodeData;
  mutable RenderBatchAnalysis m_lastBatchAnalysis;
  std::vector<RenderWorkItem> m_nonGeometryDispatchItems;
};

/*
@source_analysis.section sort：非 geometry dispatch 的旧排序入口
`RenderWorkQueue::sort` 当前只作用于 `m_nonGeometryDispatchItems`。realtime geometry
的 batch locality / depth policy 应在后续 RenderBatch preparation/compiler 阶段处理，
不能继续通过旧 `RenderWorkItem` pipelineKey 排序表达。
*/

/*
@source_analysis.section collectUniquePipelineBuildDescs：预构建去重
这一步暂时只覆盖非 geometry dispatch items。realtime geometry 的 pipeline lookup
将从 `RenderBatchAnalysis` 的 batch identity 与 node context 派生，不再由 per-draw
`RenderWorkItem` 去重驱动。
*/

/*
@source_analysis.section build：REQ-009 两轴筛选与 scene-level 资源拼接
`RenderWorkQueue::build` 是 RenderWorkQueue 唯一面向 Scene 的入口，也是把
"scene 视角" 翻译成 "pass 视角" 的收口点。三步固定流程：

1. 取 scene-level 资源：`scene.getSceneLevelResources(pass, target)` —
这一步本身 就是 REQ-009 的两轴筛选（camera 按 target 匹配，light 按 pass
匹配）的产物。
2. 取该 target 上所有 camera 的 OR-combined 可见性掩码，作为本 pass
的可见性下界。
3. 遍历 `scene.getRenderables()`，对每个 renderable 串联三个独立条件：
   `supportsPass(pass)`、`getVisibilityLayerMask() & visibleMask != 0`、
   `getValidatedPassData(pass)` 返回非空。三者同时满足才入队。

对 realtime geometry，入队结果是 `RenderDrawInput`，不是 descriptor-resource 绑定好的
`RenderWorkItem`。scene-level resources 仍由调用边界传入，但真实 table/range 解析推迟到
`prepareDrawInputs()`。`build` 自身不做增量 — 每次调用都先 `clearItems()`，重建语义优先于
增量正确性。
*/

} // namespace LX_core
