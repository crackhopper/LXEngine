#include "core/frame_graph/render_queue.hpp"

#include "core/asset/render_effect.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/offline/offline_scene_storage_resources.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/scene_resource_table_upload_view.hpp"

#include <algorithm>
#include <iterator>
#include <span>
#include <string_view>
#include <unordered_set>

namespace LX_core {

namespace {

[[nodiscard]] RenderBatchDiagnostic makePreparationDiagnostic(
    const RenderPathNodeContext &context, const RenderDrawInput &input,
    const RenderBatchDiagnosticReason reason,
    const u32 drawRecordIndex = u32_max,
    const u32 materialRefIndex = u32_max, const u32 meshIndex = u32_max) {
  RenderBatchDiagnostic diagnostic;
  diagnostic.reason = reason;
  diagnostic.inputIndex = input.inputIndex;
  diagnostic.pass = context.pass;
  diagnostic.debugId = input.debugId;
  diagnostic.objectDataSignature = context.objectDataSignature;
  diagnostic.materialTypeSignature = input.materialTypeSignature;
  diagnostic.drawRecordIndex = drawRecordIndex;
  diagnostic.materialRefIndex = materialRefIndex;
  diagnostic.meshIndex = meshIndex;
  return diagnostic;
}

void invalidatePreparedDrawData(RenderPathNodeData &data) {
  data.preparedCandidates.clear();
  data.preparationDiagnostics.clear();
  data.preparationValid = false;
  data.preparedInputCount = 0;
}

template <typename Entry, typename Handle>
[[nodiscard]] std::optional<u32>
findTypedIndex(std::span<const Entry> entries, Handle handle) {
  if (!handle.isValid()) {
    return std::nullopt;
  }
  const auto it = std::find_if(
      entries.begin(), entries.end(), [handle](const Entry &entry) {
        return entry.handle == handle && entry.typedIndex != u32_max;
      });
  if (it == entries.end()) {
    return std::nullopt;
  }
  return it->typedIndex;
}

[[nodiscard]] std::optional<u32>
findObjectIndex(const SceneResourceTableUploadView &view,
                const ObjectHandle handle) {
  return findTypedIndex(view.objectIndexByHandle, handle);
}

[[nodiscard]] std::optional<u32>
findMaterialIndex(const SceneResourceTableUploadView &view,
                  const MaterialHandle handle) {
  return findTypedIndex(view.materialIndexByHandle, handle);
}

[[nodiscard]] std::optional<u32>
findMaterialRefIndex(const SceneResourceTableUploadView &view,
                     const MaterialHandle handle) {
  return findTypedIndex(view.materialRefIndexByHandle, handle);
}

[[nodiscard]] bool hasGlobalIndexRange(const SceneResourceTableUploadView &view,
                                       const SceneGpuMeshRecord &mesh) {
  if (mesh.indexCount == 0) {
    return true;
  }
  if (mesh.indexOffset >= view.indices.size()) {
    return false;
  }
  return mesh.indexCount <= view.indices.size() - mesh.indexOffset;
}

[[nodiscard]] bool hasSourceLocalRecord(
    const SceneResourceTableUploadView &view,
    const SceneGpuMaterialRefRecord &materialRef) {
  if (materialRef.sourceStorageIndex >= view.sourceMaterialStorages.size()) {
    return false;
  }
  const SceneSourceLocalMaterialStorageView &storage =
      view.sourceMaterialStorages[materialRef.sourceStorageIndex];
  if (materialRef.sourceLocalMaterialIndex >= storage.recordCount) {
    return false;
  }
  if (storage.recordOffset >= view.sourceMaterialRecords.size()) {
    return false;
  }
  return static_cast<usize>(storage.recordOffset) +
             materialRef.sourceLocalMaterialIndex <
         view.sourceMaterialRecords.size();
}

[[nodiscard]] u32
resolveCandidateInstanceCount(const SceneGpuObjectRecord &object) {
  return object.visible == 0 ? 0u : 1u;
}

[[nodiscard]] std::optional<PreparedRenderDrawCandidate> prepareDrawCandidate(
    const RenderPathNodeContext &context, const RenderDrawInput &input,
    const SceneResourceTableUploadView &view,
    std::vector<RenderBatchDiagnostic> &diagnostics) {
  if (!context.backendIndirectSupported) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input, RenderBatchDiagnosticReason::BackendIndirectUnsupported));
    return std::nullopt;
  }

  const std::optional<u32> objectIndex = findObjectIndex(view, input.object);
  if (!objectIndex.has_value()) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input,
        RenderBatchDiagnosticReason::ObjectDrawRecordUnresolved));
    return std::nullopt;
  }

  const u32 drawRecordIndex = *objectIndex;
  if (drawRecordIndex >= view.draws.size()) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input, RenderBatchDiagnosticReason::InvalidDrawRecord,
        drawRecordIndex));
    return std::nullopt;
  }

  const SceneGpuDrawRecord &draw = view.draws[drawRecordIndex];
  if (draw.objectIndex != *objectIndex) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input, RenderBatchDiagnosticReason::InvalidDrawRecord,
        drawRecordIndex));
    return std::nullopt;
  }
  if (*objectIndex >= view.objects.size()) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input,
        RenderBatchDiagnosticReason::ObjectDrawRecordUnresolved,
        drawRecordIndex));
    return std::nullopt;
  }

  const u32 meshIndex = draw.meshIndex;
  if (meshIndex >= view.meshes.size()) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input, RenderBatchDiagnosticReason::MissingMeshRange,
        drawRecordIndex, draw.materialRefIndex, meshIndex));
    return std::nullopt;
  }

  const SceneGpuMeshRecord &mesh = view.meshes[meshIndex];
  if (mesh.indexCount == 0) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input, RenderBatchDiagnosticReason::ZeroIndexCount,
        drawRecordIndex, draw.materialRefIndex, meshIndex));
    return std::nullopt;
  }
  if (!hasGlobalIndexRange(view, mesh)) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input, RenderBatchDiagnosticReason::GlobalGeometryTableMissing,
        drawRecordIndex, draw.materialRefIndex, meshIndex));
    return std::nullopt;
  }

  const u32 instanceCount =
      resolveCandidateInstanceCount(view.objects[*objectIndex]);
  if (instanceCount == 0) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input, RenderBatchDiagnosticReason::ZeroInstanceCount,
        drawRecordIndex, draw.materialRefIndex, meshIndex));
    return std::nullopt;
  }

  const std::optional<u32> materialIndex =
      findMaterialIndex(view, input.material);
  if (draw.materialIndex != u32_max) {
    if (!materialIndex.has_value() || *materialIndex != draw.materialIndex) {
      diagnostics.push_back(makePreparationDiagnostic(
          context, input, RenderBatchDiagnosticReason::InvalidDrawRecord,
          drawRecordIndex, draw.materialRefIndex, meshIndex));
      return std::nullopt;
    }
  }

  const u32 materialRefIndex = draw.materialRefIndex;
  if (materialRefIndex == u32_max) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input, RenderBatchDiagnosticReason::SourceMaterialRefUnresolved,
        drawRecordIndex, materialRefIndex, meshIndex));
    return std::nullopt;
  }
  if (materialRefIndex >= view.materialRefs.size()) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input, RenderBatchDiagnosticReason::InvalidSourceMaterialRef,
        drawRecordIndex, materialRefIndex, meshIndex));
    return std::nullopt;
  }

  const std::optional<u32> resolvedMaterialRefIndex =
      findMaterialRefIndex(view, input.material);
  if (!resolvedMaterialRefIndex.has_value() ||
      *resolvedMaterialRefIndex != materialRefIndex) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input, RenderBatchDiagnosticReason::InvalidSourceMaterialRef,
        drawRecordIndex, materialRefIndex, meshIndex));
    return std::nullopt;
  }

  const SceneGpuMaterialRefRecord &materialRef =
      view.materialRefs[materialRefIndex];
  if (materialRef.sourceStorageIndex == u32_max ||
      materialRef.sourceLocalMaterialIndex == u32_max ||
      !hasSourceLocalRecord(view, materialRef)) {
    diagnostics.push_back(makePreparationDiagnostic(
        context, input, RenderBatchDiagnosticReason::InvalidSourceMaterialRef,
        drawRecordIndex, materialRefIndex, meshIndex));
    return std::nullopt;
  }

  PreparedRenderDrawCandidate candidate;
  candidate.inputIndex = input.inputIndex;
  candidate.drawRecordIndex = drawRecordIndex;
  candidate.objectIndex = *objectIndex;
  if (materialIndex.has_value()) {
    candidate.materialIndex = *materialIndex;
  }
  candidate.materialRefIndex = materialRefIndex;
  candidate.sourceStorageIndex = materialRef.sourceStorageIndex;
  candidate.sourceLocalMaterialIndex = materialRef.sourceLocalMaterialIndex;
  candidate.meshIndex = meshIndex;
  candidate.indexCount = mesh.indexCount;
  candidate.firstIndex = mesh.indexOffset;
  candidate.vertexOffset = static_cast<i32>(mesh.vertexOffset);
  candidate.instanceCount = instanceCount;
  candidate.objectDataSignature = context.objectDataSignature;
  candidate.materialTypeSignature = input.materialTypeSignature;
  candidate.debugId = input.debugId;
  candidate.sortCenter = input.sortCenter;
  return candidate;
}

[[nodiscard]] StringID makeNamedMaterialTypeVariant(std::string_view name,
                                                    StringID shaderSignature) {
  auto &tbl = GlobalStringTable::get();
  StringID fields[] = {
      tbl.Intern(name),
      tbl.Intern("<non-bsdf-source-uri>"),
      tbl.Intern("<non-bsdf-reflection-hash>"),
      tbl.Intern("<non-bsdf-source-signature>"),
      shaderSignature,
  };
  return tbl.compose(TypeTag::MaterialTypeVariant, fields);
}

[[nodiscard]] std::optional<Vec3f>
resolveSortCameraEye(const Scene &scene,
                     const RenderWorkBuildContext::RealtimeOptions &options,
                     const RenderTarget &target) {
  if (options.cameraResource.has_value()) {
    return options.cameraResource->pose.eye;
  }

  const RenderTarget &sceneResourceTarget =
      options.sceneResourceTarget.value_or(target);
  for (const auto &cameraNode : scene.getCameras()) {
    if (!cameraNode) {
      continue;
    }
    const auto camera = cameraNode->getComponent<CameraComponent>();
    if (!camera.has_value() || !camera->get().isActive() ||
        !camera->get().matchesTarget(sceneResourceTarget)) {
      continue;
    }
    return camera->get().getEyePosition();
  }
  return std::nullopt;
}

} // namespace

void RenderWorkQueue::addItem(RenderWorkItem item) {
  m_nonGeometryDispatchItems.push_back(std::move(item));
  invalidatePreparedDrawData(m_nodeData);
  m_lastBatchAnalysis = RenderBatchAnalysis{};
}

void RenderWorkQueue::clearItems() {
  m_context.reset();
  m_nodeData = RenderPathNodeData{};
  m_lastBatchAnalysis = RenderBatchAnalysis{};
  m_nonGeometryDispatchItems.clear();
}

void RenderWorkQueue::setNodeContext(RenderPathNodeContext context) {
  m_context = std::move(context);
  invalidatePreparedDrawData(m_nodeData);
  m_lastBatchAnalysis = RenderBatchAnalysis{};
}

void RenderWorkQueue::addDrawInput(RenderDrawInput input) {
  m_nodeData.drawInputs.push_back(std::move(input));
  invalidatePreparedDrawData(m_nodeData);
  m_lastBatchAnalysis = RenderBatchAnalysis{};
}

void RenderWorkQueue::prepareDrawInputs(
    const SceneResourceTableUploadView &uploadView) {
  m_nodeData.preparedCandidates.clear();
  m_nodeData.preparationDiagnostics.clear();
  if (!m_context.has_value()) {
    for (const RenderDrawInput &input : m_nodeData.drawInputs) {
      RenderPathNodeContext emptyContext;
      m_nodeData.preparationDiagnostics.push_back(makePreparationDiagnostic(
          emptyContext, input, RenderBatchDiagnosticReason::LegacyInputRejected));
    }
    m_nodeData.preparationValid = true;
    m_nodeData.preparedInputCount = m_nodeData.drawInputs.size();
    m_lastBatchAnalysis = RenderBatchAnalysis{};
    return;
  }

  for (const RenderDrawInput &input : m_nodeData.drawInputs) {
    std::vector<RenderBatchDiagnostic> diagnostics;
    std::optional<PreparedRenderDrawCandidate> candidate =
        prepareDrawCandidate(*m_context, input, uploadView, diagnostics);
    m_nodeData.preparationDiagnostics.insert(
        m_nodeData.preparationDiagnostics.end(),
        std::make_move_iterator(diagnostics.begin()),
        std::make_move_iterator(diagnostics.end()));
    if (candidate.has_value()) {
      m_nodeData.preparedCandidates.push_back(std::move(*candidate));
    }
  }
  m_nodeData.preparationValid = true;
  m_nodeData.preparedInputCount = m_nodeData.drawInputs.size();
  m_lastBatchAnalysis = RenderBatchAnalysis{};
}

void RenderWorkQueue::sort() { sort(std::nullopt); }

void RenderWorkQueue::sort(const std::optional<Vec3f> &cameraEye) {
  std::stable_sort(
      m_nonGeometryDispatchItems.begin(), m_nonGeometryDispatchItems.end(),
      [cameraEye](const RenderWorkItem &a, const RenderWorkItem &b) {
        const bool aTransparent = a.renderState.blendEnable;
        const bool bTransparent = b.renderState.blendEnable;
        if (aTransparent != bTransparent) {
          return !aTransparent;
        }
        if (cameraEye.has_value() && aTransparent && bTransparent) {
          const float aDistance = (a.sortCenter - *cameraEye).length2();
          const float bDistance = (b.sortCenter - *cameraEye).length2();
          if (aDistance != bDistance) {
            return aDistance > bDistance;
          }
        }
        return a.pipelineKey.id.id < b.pipelineKey.id.id;
      });
}

RenderWorkItem makeOfflineComputeItem(offline::OfflineRenderJob &job,
                                      StringID pass, const RenderTarget &target,
                                      IShaderSharedPtr shader,
                                      StringID renderPathNodeSignature) {
  offline::OfflineSceneStorageResources storageResources =
      offline::buildOfflineSceneStorageResources(job);
  RenderWorkItem item;
  item.domain = RenderDomain::Offline;
  item.kind = RenderWorkKind::ComputeDispatch;
  item.pass = pass;
  item.target = target.toDesc();
  item.compute.groupCountX = (job.output.width + 7u) / 8u;
  item.compute.groupCountY = (job.output.height + 7u) / 8u;
  item.compute.groupCountZ = 1u;
  item.shaderInfo = std::move(shader);
  item.descriptorResources = std::move(storageResources.descriptorResources);
  item.debugId = StringID("OfflineRayTraceDispatch");
  item.objectSignature = StringID("OfflineSceneGpuData");
  item.materialSignature = StringID("OfflinePrimaryRayCompute");
  item.materialTypeVariant = makeNamedMaterialTypeVariant(
      "offline-primary-ray", item.shaderProgram.getPipelineSignature());
  item.renderPathNodeSignature = renderPathNodeSignature;
  item.pipelineKey =
      PipelineKey::build(item.materialTypeVariant, item.renderPathNodeSignature);
  return item;
}

std::vector<PipelineBuildDesc>
RenderWorkQueue::collectUniquePipelineBuildDescs() const {
  std::unordered_set<PipelineKey, PipelineKey::Hash> seen;
  std::vector<PipelineBuildDesc> out;
  out.reserve(m_nonGeometryDispatchItems.size());
  for (const auto &item : m_nonGeometryDispatchItems) {
    if (!seen.insert(item.pipelineKey).second)
      continue;
    out.push_back(PipelineBuildDesc::fromRenderWorkItem(item));
  }
  return out;
}

RenderBatchAnalysis RenderWorkQueue::compileIndirectBatches() const {
  RenderBatchAnalysis analysis;
  if (m_context.has_value()) {
    analysis.context = *m_context;
  }
  analysis.stats.inputDrawCount = m_nodeData.drawInputs.size();
  if (!m_nodeData.preparationValid ||
      m_nodeData.preparedInputCount != m_nodeData.drawInputs.size()) {
    for (const RenderDrawInput &drawInput : m_nodeData.drawInputs) {
      analysis.diagnostics.push_back(makePreparationDiagnostic(
          analysis.context, drawInput,
          RenderBatchDiagnosticReason::GlobalGeometryTableMissing));
    }
    analysis.stats.unsupportedDrawCount = analysis.diagnostics.size();
    m_lastBatchAnalysis = analysis;
    return analysis;
  }
  analysis.candidates = m_nodeData.preparedCandidates;
  analysis.diagnostics = m_nodeData.preparationDiagnostics;
  analysis.stats.preparedCandidateCount = analysis.candidates.size();
  analysis.stats.unsupportedDrawCount = analysis.diagnostics.size();
  m_lastBatchAnalysis = analysis;
  return analysis;
}

void RenderWorkQueue::build(const RenderWorkBuildContext &context,
                            StringID pass, const RenderTarget &target,
                            StringID renderPathNodeSignature,
                            std::optional<RenderPathGeometryContract>
                                geometryContract,
                            std::optional<RenderPathNodeRenderingMode>
                                renderingMode,
                            std::vector<RenderPathAttachmentContract>
                                attachments) {
  if (context.domain() == RenderDomain::Offline) {
    clearItems();
    if (pass == Pass_OfflineRayTrace) {
      m_nonGeometryDispatchItems.push_back(
          makeOfflineComputeItem(context.offlineJob(), pass, target,
                                 context.offlineJob().offlineShader,
                                 renderPathNodeSignature));
    }
    return;
  }

  const Scene &scene = context.realtimeScene();
  const auto &options = context.realtimeOptions();
  DescriptorResourceList sceneResources;
  VisibilityLayerMask visibleMask = 0;
  if (options.cameraResource.has_value()) {
    sceneResources =
        scene.getSceneLevelResources(pass, *options.cameraResource);
    visibleMask = options.cameraResource->cullingMask;
  } else {
    const RenderTarget &sceneResourceTarget =
        options.sceneResourceTarget.value_or(target);
    sceneResources = scene.getSceneLevelResources(pass, sceneResourceTarget);
    visibleMask = scene.getCombinedCameraCullingMask(sceneResourceTarget);
  }
  if (options.visibleMask.has_value()) {
    visibleMask = *options.visibleMask;
  }
  if (visibleMask == 0 && pass == Pass_Shadow) {
    visibleMask = VisibilityMask_All;
  }

  const std::optional<Vec3f> cameraEye =
      resolveSortCameraEye(scene, options, target);
  buildRealtime(scene, pass, target, renderPathNodeSignature, geometryContract,
                renderingMode, std::move(attachments),
                std::move(sceneResources), visibleMask, cameraEye);
}

void RenderWorkQueue::buildRealtime(const Scene &scene, StringID pass,
                                    const RenderTarget &target,
                                    StringID renderPathNodeSignature,
                                    std::optional<RenderPathGeometryContract>
                                        geometryContract,
                                    std::optional<RenderPathNodeRenderingMode>
                                        renderingMode,
                                    std::vector<RenderPathAttachmentContract>
                                        attachments,
                                    DescriptorResourceList sceneResources,
                                    VisibilityLayerMask visibleMask,
                                    std::optional<Vec3f> cameraEye) {
  clearItems();
  setNodeContext(RenderPathNodeContext{
      .pass = pass,
      .renderPathNodeSignature = renderPathNodeSignature,
      .renderingMode = renderingMode,
      .geometryContract = geometryContract,
      .attachments = std::move(attachments),
      .target = target.toDesc(),
      .objectDataSignature = StringID("BindlessObjectData.v1"),
      .backendIndirectSupported = true,
  });

  for (const auto &renderable : scene.getRenderables()) {
    if (!renderable)
      continue;
    if (renderable->isDebugOnlyRenderable() && pass != Pass_DebugOverlay)
      continue;
    if (!renderable->supportsPass(pass))
      continue;
    if ((renderable->getVisibilityLayerMask() & visibleMask) == 0)
      continue;
    auto validated = renderable->getValidatedPassData(pass);
    if (!validated)
      continue;

    const auto &validatedData = validated->get();
    MeshHandle mesh;
    if (validatedData.objectHandle.isValid()) {
      if (const auto objectResource =
              scene.resources().resolve(validatedData.objectHandle)) {
        mesh = objectResource->get().mesh;
      }
    }
    addDrawInput(RenderDrawInput{
        .inputIndex = m_nodeData.drawInputs.size(),
        .object = validatedData.objectHandle,
        .mesh = mesh,
        .material = validatedData.materialHandle,
        .debugId = renderable->getDebugId(),
        .sortCenter = validatedData.sortCenter,
        .materialTypeSignature = validatedData.materialTypeVariant,
    });
  }

  (void)sceneResources;
  (void)cameraEye;
  prepareDrawInputs(scene.resources().buildUploadView());
}

} // namespace LX_core
