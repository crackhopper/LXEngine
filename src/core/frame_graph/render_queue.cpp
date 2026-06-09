#include "core/frame_graph/render_queue.hpp"

#include "core/asset/mesh.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/scene_descriptor_resource_resolver.hpp"
#include "core/offline/offline_scene_storage_resources.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <unordered_set>

namespace LX_core {

namespace {

/*
@source_analysis.section makeItemFromValidatedData：把 validated 结构数据翻译成
RenderWorkItem 这是一个 anonymous-namespace
内的纯字段拷贝函数，存在的理由是把"翻译"这件事 和"过滤 + 入队"分开：`build`
只关心条件判断和 sceneResources 追加， 逐字段拷贝从这里走。

不做合并、不做校验，因为 `ValidatedRenderablePassData` 的命名已经承诺了
"pass-level validation 已经完成"。这里把 pass 内可验证的结构事实转成 backend
消费的 `RenderWorkItem`；target-dependent 的 `pipelineKey` 不在 validated
数据里缓存，而是在 `build` 拿到 target 后再组合。
*/
RenderWorkItem
makeItemFromValidatedData(const ValidatedRenderablePassData &data) {
  RenderWorkItem item;
  item.domain = RenderDomain::Realtime;
  item.kind = RenderWorkKind::RasterDraw;
  item.raster.vertexBuffer = data.vertexBuffer;
  item.raster.indexBuffer = data.indexBuffer;
  item.raster.drawData = data.drawData;
  item.shaderInfo = data.shaderInfo;
  item.renderState = data.renderState;
  item.pass = data.pass;
  item.objectSignature = data.objectSignature;
  item.materialSignature = data.materialSignature;
  return item;
}

} // namespace

void RenderWorkQueue::addItem(RenderWorkItem item) {
  m_items.push_back(std::move(item));
}

void RenderWorkQueue::clearItems() { m_items.clear(); }

void RenderWorkQueue::sort() {
  std::stable_sort(m_items.begin(), m_items.end(),
                   [](const RenderWorkItem &a, const RenderWorkItem &b) {
                     return a.pipelineKey.id.id < b.pipelineKey.id.id;
                   });
}

RenderWorkItem makeOfflineComputeItem(offline::OfflineRenderJob &job,
                                      StringID pass, const RenderTarget &target,
                                      IShaderSharedPtr shader) {
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
  item.pipelineKey =
      PipelineKey::build(item.objectSignature, item.materialSignature,
                         item.target.getPipelineSignature());
  return item;
}

std::vector<PipelineBuildDesc>
RenderWorkQueue::collectUniquePipelineBuildDescs() const {
  std::unordered_set<PipelineKey, PipelineKey::Hash> seen;
  std::vector<PipelineBuildDesc> out;
  out.reserve(m_items.size());
  for (const auto &item : m_items) {
    if (!seen.insert(item.pipelineKey).second)
      continue;
    out.push_back(PipelineBuildDesc::fromRenderWorkItem(item));
  }
  return out;
}

void RenderWorkQueue::build(const RenderWorkBuildContext &context,
                            StringID pass, const RenderTarget &target) {
  if (context.domain() == RenderDomain::Offline) {
    clearItems();
    if (pass == Pass_OfflineRayTrace) {
      m_items.push_back(makeOfflineComputeItem(
          context.offlineJob(), pass, target,
          context.offlineJob().offlineShader));
    }
    return;
  }

  const Scene &scene = context.realtimeScene();
  const auto &options = context.realtimeOptions();
  DescriptorResourceList sceneResources;
  VisibilityLayerMask visibleMask = 0;
  if (options.cameraResource.has_value()) {
    sceneResources = scene.getSceneLevelResources(pass, *options.cameraResource);
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

  buildRealtime(scene, pass, target, std::move(sceneResources), visibleMask);
}

void RenderWorkQueue::buildRealtime(const Scene &scene, StringID pass,
                                    const RenderTarget &target,
                                    DescriptorResourceList sceneResources,
                                    VisibilityLayerMask visibleMask) {
  clearItems();

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

    RenderWorkItem item = makeItemFromValidatedData(validated->get());
    item.target = target.toDesc();
    item.debugId = renderable->getDebugId();
    item.pipelineKey =
        PipelineKey::build(item.objectSignature, item.materialSignature,
                           item.target.getPipelineSignature());

    item.descriptorResources =
        buildSceneDescriptorResources(SceneDescriptorResourceContext{
            .scene = scene,
            .renderable = validated->get(),
            .pass = pass,
            .target = target,
            .sceneResources = sceneResources,
        });

    m_items.push_back(std::move(item));
  }

  sort();
}

} // namespace LX_core
