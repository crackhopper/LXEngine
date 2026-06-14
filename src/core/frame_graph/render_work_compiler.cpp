#include "core/frame_graph/render_work_compiler.hpp"

#include "core/asset/mesh.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace LX_core {
namespace {

[[nodiscard]] StringID shaderUriId(const FramePass &pass) {
  return StringID(pass.shaderUri.string());
}

[[nodiscard]] StringID stablePassDebugId(const FramePass &pass) {
  return pass.name.id != 0 ? pass.name : StringID("<unnamed-pass>");
}

[[nodiscard]] StringID inputPipelineVariant(const FramePass &pass,
                                            const RenderInput &input) {
  if (const auto *draw = dynamic_cast<const RenderDrawInput *>(&input)) {
    if (draw->materialTypeSignature.id != 0) {
      return draw->materialTypeSignature;
    }
    const std::string source =
        draw->source == RenderDrawInputSource::FullscreenTriangle
            ? "fullscreen-triangle"
            : "scene-renderable";
    return StringID("render-input:" + source +
                    ":shader=" + pass.shaderUri.string());
  }
  if (dynamic_cast<const RenderComputeInput *>(&input) != nullptr) {
    return StringID("render-input:compute-dispatch:shader=" +
                    pass.shaderUri.string());
  }
  return StringID("render-input:unsupported:shader=" + pass.shaderUri.string());
}

[[nodiscard]] PipelineKey makePipelineKey(const FramePass &pass,
                                          const RenderInput &input) {
  return PipelineKey::build(inputPipelineVariant(pass, input),
                            getFramePassRenderPathNodeSignature(pass));
}

[[nodiscard]] PrimitiveTopology pipelineTopologyFor(const FramePass &pass) {
  if (pass.input.geometry.has_value()) {
    return pass.input.geometry->topology;
  }
  return PrimitiveTopology::TriangleList;
}

[[nodiscard]] PipelineBuildDesc makePipelineBuildDesc(const FramePass &pass,
                                                      PipelineKey key) {
  PipelineBuildDesc buildDesc;
  buildDesc.type = pass.stage == RenderPassStage::Compute
                       ? PipelineBuildType::Compute
                       : PipelineBuildType::Graphics;
  buildDesc.key = key;
  buildDesc.shaderVariantKey = shaderUriId(pass);
  buildDesc.target = pass.target;
  buildDesc.renderingMode = pass.renderingMode;
  buildDesc.attachments = pass.attachments;
  buildDesc.renderState = pass.renderState;
  buildDesc.topology = pipelineTopologyFor(pass);
  if (buildDesc.type == PipelineBuildType::Compute) {
    buildDesc.pushConstant.size = 0;
    buildDesc.pushConstant.stageFlagsMask =
        static_cast<ShaderStageMask32>(ShaderStage::Compute);
  }
  return buildDesc;
}

[[nodiscard]] RenderInputDiagnostic
makeDiagnostic(const RenderInputDiagnosticCode code, const StringID pass,
               const StringID debugId, std::string message) {
  return RenderInputDiagnostic{
      .code = code,
      .pass = pass,
      .debugId = debugId,
      .message = std::move(message),
  };
}

void reject(RenderInputDesc &desc, RenderInputDiagnosticCode code,
            std::string message) {
  desc.status = RenderInputStatus::Rejected;
  desc.diagnostics.push_back(
      makeDiagnostic(code, desc.pass, desc.debugId, std::move(message)));
}

[[nodiscard]] bool hasSubmittableDrawCommand(const RenderDrawInput &draw) {
  return std::any_of(draw.drawCommands.begin(), draw.drawCommands.end(),
                     [](const RenderDrawCommand &command) {
                       return command.indexCount > 0 &&
                              command.instanceCount > 0;
                     });
}

[[nodiscard]] usize countSubmittableDrawCommands(const RenderDrawInput &draw) {
  return static_cast<usize>(std::count_if(
      draw.drawCommands.begin(), draw.drawCommands.end(),
      [](const RenderDrawCommand &command) {
        return command.indexCount > 0 && command.instanceCount > 0;
      }));
}

[[nodiscard]] bool materialTypeMatches(StringID materialTypeSignature,
                                       std::string_view requestedType) {
  if (materialTypeSignature.id == 0) {
    return false;
  }
  const std::string signature =
      GlobalStringTable::get().toDebugString(materialTypeSignature);
  return signature == requestedType ||
         signature.rfind(std::string(requestedType) + "-", 0) == 0;
}

[[nodiscard]] bool materialTypeAllowed(const RenderDrawInput &draw,
                                       const FramePass &pass) {
  if (pass.input.material.types.empty()) {
    return true;
  }
  if (draw.materialTypeSignature.id == 0) {
    return false;
  }
  return std::any_of(
      pass.input.material.types.begin(), pass.input.material.types.end(),
      [&draw](const std::string &type) {
        return materialTypeMatches(draw.materialTypeSignature, type);
      });
}

[[nodiscard]] std::string stringIdText(StringID id) {
  if (id.id == 0) {
    return {};
  }
  return GlobalStringTable::get().toDebugString(id);
}

[[nodiscard]] bool isDebugObjectClass(std::string_view objectClass) {
  return objectClass == "debug" || objectClass == "debug-only" ||
         objectClass == "debug.mesh";
}

[[nodiscard]] bool objectClassAllowed(const RenderDrawInput &draw,
                                      const FramePass &pass) {
  const std::vector<std::string> &classes = pass.input.object.renderClasses;
  if (classes.empty()) {
    return true;
  }

  const std::string renderType = stringIdText(draw.objectRenderType);
  return std::any_of(classes.begin(), classes.end(),
                     [&draw, &renderType](const std::string &renderClass) {
                       if (isDebugObjectClass(renderClass)) {
                         return draw.debugOnly ||
                                isDebugObjectClass(renderType);
                       }
                       if (!renderType.empty()) {
                         return renderType == renderClass;
                       }
                       return false;
                     });
}

[[nodiscard]] VisibilityLayerMask
resolveVisibleMask(const Scene &scene, const FramePass &pass,
                   const RenderWorkBuildContext::RealtimeOptions &options) {
  VisibilityLayerMask visibleMask = 0;
  if (options.cameraResource.has_value()) {
    visibleMask = options.cameraResource->cullingMask;
  } else {
    const RenderTarget sceneResourceTarget =
        options.sceneResourceTarget.value_or(RenderTarget(pass.target));
    visibleMask = scene.getCombinedCameraCullingMask(sceneResourceTarget);
  }
  if (options.visibleMask.has_value()) {
    visibleMask = *options.visibleMask;
  }
  return visibleMask;
}

[[nodiscard]] MeshHandle resolveObjectMesh(const Scene &scene,
                                           ObjectHandle object) {
  if (!object.isValid()) {
    return {};
  }
  const auto objectResource = scene.resources().resolve(object);
  if (!objectResource.has_value()) {
    return {};
  }
  return objectResource->get().mesh;
}

void fillSceneDrawCommand(const Scene &scene,
                          const ValidatedRenderablePassData &validatedData,
                          RenderDrawInput &draw) {
  if (draw.mesh.isValid()) {
    const auto mesh = scene.resources().resolve(draw.mesh);
    if (mesh.has_value() && mesh->get().getIndexCount() > 0) {
      draw.drawCommands.push_back(RenderDrawCommand{
          .indexCount = mesh->get().getIndexCount(),
          .instanceCount = 1,
          .firstIndex = mesh->get().getIndexOffset(),
      });
      return;
    }
  }

  if (draw.indexBuffer.isValid()) {
    const auto *indexBuffer =
        dynamic_cast<const IndexBuffer *>(&draw.indexBuffer.get());
    if (indexBuffer != nullptr && indexBuffer->indexCount() > 0) {
      draw.drawCommands.push_back(RenderDrawCommand{
          .indexCount = static_cast<u32>(indexBuffer->indexCount()),
          .instanceCount = 1,
      });
    }
  }
}

void fillRenderType(const IRenderable &renderable, RenderDrawInput &draw) {
  draw.debugOnly = renderable.isDebugOnlyRenderable();
  if (const auto renderType = renderable.getRenderType()) {
    draw.objectRenderType = *renderType;
  } else if (draw.debugOnly) {
    draw.objectRenderType = StringID("debug.mesh");
  }
}

void fillRawRenderableResources(const IRenderable &renderable,
                                RenderDrawInput &draw) {
  draw.vertexBuffer = renderable.getVertexBuffer();
  draw.indexBuffer = renderable.getIndexBuffer();
  if (!draw.drawCommands.empty() || !draw.indexBuffer.isValid()) {
    return;
  }
  const auto *indexBuffer =
      dynamic_cast<const IndexBuffer *>(&draw.indexBuffer.get());
  if (indexBuffer != nullptr && indexBuffer->indexCount() > 0) {
    draw.drawCommands.push_back(RenderDrawCommand{
        .indexCount = static_cast<u32>(indexBuffer->indexCount()),
        .instanceCount = 1,
    });
  }
}

void buildFullscreenInput(const FramePass &pass,
                          std::vector<std::unique_ptr<RenderInput>> &out) {
  auto draw = std::make_unique<RenderDrawInput>();
  draw->source = RenderDrawInputSource::FullscreenTriangle;
  draw->pass = pass.name;
  draw->debugId = stablePassDebugId(pass);
  draw->inputIndex = out.size();
  draw->drawCommands.push_back(
      RenderDrawCommand{.indexCount = 3, .instanceCount = 1});
  out.push_back(std::move(draw));
}

void buildSceneRenderableInputs(
    const FramePass &pass, const RenderWorkBuildContext &context,
    std::vector<std::unique_ptr<RenderInput>> &out) {
  if (context.domain() != RenderDomain::Realtime ||
      !context.hasRealtimeScene()) {
    throw std::logic_error(
        "RenderWorkCompiler scene-renderables input requires a realtime scene");
  }

  const Scene &scene = context.realtimeScene();
  const VisibilityLayerMask visibleMask =
      resolveVisibleMask(scene, pass, context.realtimeOptions());

  for (const auto &renderable : scene.getRenderables()) {
    if (!renderable) {
      continue;
    }
    if ((renderable->getVisibilityLayerMask() & visibleMask) == 0) {
      continue;
    }

    auto draw = std::make_unique<RenderDrawInput>();
    draw->source = RenderDrawInputSource::SceneRenderable;
    draw->pass = pass.name;
    draw->debugId = renderable->getDebugId().id != 0
                        ? renderable->getDebugId()
                        : StringID(renderable->getNodeName());
    draw->inputIndex = out.size();
    draw->objectDataSignature = StringID("BindlessObjectData.v1");
    fillRenderType(*renderable, *draw);
    fillRawRenderableResources(*renderable, *draw);

    const auto validated = renderable->getValidatedPassData(pass.name);
    if (validated.has_value()) {
      const ValidatedRenderablePassData &data = validated->get();
      draw->object = data.objectHandle;
      draw->mesh = resolveObjectMesh(scene, data.objectHandle);
      draw->material = data.materialHandle;
      draw->vertexBuffer = data.vertexBuffer;
      draw->indexBuffer = data.indexBuffer;
      draw->primitiveIndex = 0;
      draw->sortCenter = data.sortCenter;
      draw->materialTypeSignature = data.materialTypeSignature;
      fillSceneDrawCommand(scene, data, *draw);
    }

    out.push_back(std::move(draw));
  }
}

void validateFullscreenDesc(const FramePass &pass, const RenderDrawInput &draw,
                            RenderInputDesc &desc) {
  if (pass.stage != RenderPassStage::Raster ||
      pass.dispatch != RenderPassDispatch::Fullscreen) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "fullscreen-triangle input requires raster fullscreen pass");
    return;
  }
  if (draw.source != RenderDrawInputSource::FullscreenTriangle) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "fullscreen pass received non-fullscreen draw input");
    return;
  }
  if (!hasSubmittableDrawCommand(draw)) {
    reject(desc, RenderInputDiagnosticCode::ZeroDrawCount,
           "fullscreen-triangle input has no drawable command");
    return;
  }
  desc.status = RenderInputStatus::Accepted;
}

[[nodiscard]] bool
geometryContractMatches(const FramePass &pass,
                        const RenderWorkBuildContext &context,
                        const RenderDrawInput &draw) {
  if (!pass.input.geometry.has_value()) {
    return true;
  }
  if (draw.mesh.isValid() && context.hasRealtimeScene()) {
    const Scene &scene = context.realtimeScene();
    const auto mesh = scene.resources().resolve(draw.mesh);
    if (mesh.has_value()) {
      return mesh->get().getIndexBuffer().getTopology() ==
             pass.input.geometry->topology;
    }
  }
  if (draw.indexBuffer.isValid()) {
    const auto *indexBuffer =
        dynamic_cast<const IndexBuffer *>(&draw.indexBuffer.get());
    if (indexBuffer == nullptr) {
      return true;
    }
    return indexBuffer->getTopology() == pass.input.geometry->topology;
  }
  return true;
}

void validateSceneRenderableDesc(const FramePass &pass,
                                 const RenderWorkBuildContext &context,
                                 const RenderDrawInput &draw,
                                 RenderInputDesc &desc) {
  if (pass.stage != RenderPassStage::Raster ||
      pass.dispatch != RenderPassDispatch::Draw) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "scene-renderables input requires raster draw pass");
    return;
  }
  if (draw.source != RenderDrawInputSource::SceneRenderable) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "scene-renderables pass received non-scene draw input");
    return;
  }
  if (!objectClassAllowed(draw, pass)) {
    reject(desc, RenderInputDiagnosticCode::ObjectClassRejected,
           "renderable object type does not match input contract");
    return;
  }
  if (pass.input.material.required && !draw.material.isValid()) {
    reject(desc, RenderInputDiagnosticCode::MaterialRequired,
           "scene renderable is missing required validated material data");
    return;
  }
  if (!materialTypeAllowed(draw, pass)) {
    reject(desc, RenderInputDiagnosticCode::MaterialTypeRejected,
           "scene renderable material type does not match input contract");
    return;
  }
  if (draw.material.isValid() && !draw.mesh.isValid()) {
    reject(desc, RenderInputDiagnosticCode::MissingMesh,
           "scene renderable material pass has no resolved mesh");
    return;
  }
  if (!geometryContractMatches(pass, context, draw)) {
    reject(desc, RenderInputDiagnosticCode::GeometryContractMismatch,
           "scene renderable mesh topology does not match geometry contract");
    return;
  }
  if (!hasSubmittableDrawCommand(draw)) {
    reject(desc, RenderInputDiagnosticCode::ZeroDrawCount,
           "scene renderable input has no drawable command");
    return;
  }
  desc.status = RenderInputStatus::Accepted;
}

void validateComputeDesc(const FramePass &pass, const RenderComputeInput &input,
                         RenderInputDesc &desc) {
  if (pass.stage != RenderPassStage::Compute ||
      pass.dispatch != RenderPassDispatch::Compute) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "compute-dispatch input requires compute pass");
    return;
  }
  if (input.groupCountX == 0 || input.groupCountY == 0 ||
      input.groupCountZ == 0) {
    reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
           "compute-dispatch input has zero dispatch group dimension");
    return;
  }
  desc.status = RenderInputStatus::Accepted;
}

void updateStats(std::vector<RenderInputDesc> &descs,
                 const std::vector<std::unique_ptr<RenderInput>> &inputs) {
  RenderInputStats stats;
  stats.inputCount = inputs.size();
  for (const RenderInputDesc &desc : descs) {
    if (desc.accepted()) {
      ++stats.acceptedInputCount;
    } else {
      ++stats.rejectedInputCount;
    }
  }
  for (const RenderInputDesc &desc : descs) {
    if (!desc.accepted() || desc.inputIndex >= inputs.size() ||
        !inputs[desc.inputIndex]) {
      continue;
    }
    const RenderInput &input = *inputs[desc.inputIndex];
    if (const auto *draw = dynamic_cast<const RenderDrawInput *>(&input)) {
      stats.submittedDrawCount += countSubmittableDrawCommands(*draw);
    } else if (dynamic_cast<const RenderComputeInput *>(&input) != nullptr) {
      ++stats.submittedDispatchCount;
    }
  }
  for (RenderInputDesc &desc : descs) {
    desc.stats = stats;
  }
}

} // namespace

void RenderWorkCompiler::buildInputs(
    const FramePass &pass, const RenderWorkBuildContext &context,
    std::vector<std::unique_ptr<RenderInput>> &outInputs) const {
  switch (pass.input.kind) {
  case RenderPassInputKind::FullscreenTriangle:
    buildFullscreenInput(pass, outInputs);
    return;
  case RenderPassInputKind::SceneRenderables:
    buildSceneRenderableInputs(pass, context, outInputs);
    return;
  case RenderPassInputKind::ComputeDispatch: {
    auto compute = std::make_unique<RenderComputeInput>();
    compute->pass = pass.name;
    compute->debugId = stablePassDebugId(pass);
    compute->inputIndex = outInputs.size();
    outInputs.push_back(std::move(compute));
    return;
  }
  }
}

std::vector<RenderInputDesc> RenderWorkCompiler::prepare(
    const FramePass &pass, const RenderWorkBuildContext &context,
    const std::vector<std::unique_ptr<RenderInput>> &inputs) const {
  std::vector<RenderInputDesc> descs;
  descs.reserve(inputs.size());

  for (usize index = 0; index < inputs.size(); ++index) {
    RenderInputDesc desc;
    desc.inputIndex = index;
    desc.pass = pass.name;
    desc.debugId = stablePassDebugId(pass);
    desc.shaderUri = shaderUriId(pass);

    if (!inputs[index]) {
      reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
             "render input pointer is null");
      descs.push_back(std::move(desc));
      continue;
    }

    const RenderInput &input = *inputs[index];
    desc.inputIndex = index;
    desc.pass = input.pass.id != 0 ? input.pass : pass.name;
    desc.debugId =
        input.debugId.id != 0 ? input.debugId : stablePassDebugId(pass);
    desc.pipelineKey = makePipelineKey(pass, input);
    desc.pipelineBuildDesc = makePipelineBuildDesc(pass, desc.pipelineKey);
    desc.shaderVariantKey = desc.pipelineBuildDesc.shaderVariantKey;

    if (const auto *draw = dynamic_cast<const RenderDrawInput *>(&input)) {
      if (pass.input.kind == RenderPassInputKind::FullscreenTriangle) {
        validateFullscreenDesc(pass, *draw, desc);
      } else if (pass.input.kind == RenderPassInputKind::SceneRenderables) {
        validateSceneRenderableDesc(pass, context, *draw, desc);
      } else {
        reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
               "draw input does not match pass input contract");
      }
    } else if (const auto *compute =
                   dynamic_cast<const RenderComputeInput *>(&input)) {
      if (pass.input.kind == RenderPassInputKind::ComputeDispatch) {
        validateComputeDesc(pass, *compute, desc);
      } else {
        reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
               "compute input does not match pass input contract");
      }
    } else {
      reject(desc, RenderInputDiagnosticCode::UnsupportedInputContract,
             "unknown render input type");
    }

    descs.push_back(std::move(desc));
  }

  updateStats(descs, inputs);
  return descs;
}

} // namespace LX_core
