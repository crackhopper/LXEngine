#include "core/frame_graph/render_queue.hpp"

#include "core/asset/render_effect.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/offline/offline_scene_storage_resources.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_set>

namespace LX_core {

namespace {

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
  m_lastBatchAnalysis = RenderBatchAnalysis{};
}

void RenderWorkQueue::addDrawInput(RenderDrawInput input) {
  m_nodeData.drawInputs.push_back(std::move(input));
  m_lastBatchAnalysis = RenderBatchAnalysis{};
}

void RenderWorkQueue::prepareDrawInputs(
    const SceneResourceTableUploadView &uploadView) {
  (void)uploadView;
  m_nodeData.preparedCandidates.clear();
  m_nodeData.preparationDiagnostics.clear();
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
  for (usize i = 0; i < m_nodeData.drawInputs.size(); ++i) {
    const RenderDrawInput &drawInput = m_nodeData.drawInputs[i];
    RenderBatchDiagnostic diagnostic;
    diagnostic.reason = RenderBatchDiagnosticReason::GlobalGeometryTableMissing;
    diagnostic.inputIndex = drawInput.inputIndex;
    diagnostic.pass = analysis.context.pass;
    diagnostic.debugId = drawInput.debugId;
    diagnostic.objectDataSignature = analysis.context.objectDataSignature;
    diagnostic.materialTypeSignature = drawInput.materialTypeSignature;
    analysis.diagnostics.push_back(std::move(diagnostic));
  }
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
