#pragma once

#include "core/asset/render_effect.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/platform/types.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"
#include "core/scene/object.hpp"

#include <optional>
#include <string>
#include <vector>

namespace LX_core {

enum class RenderInputKind { Draw, Compute };
enum class RenderInputStatus { Accepted, Rejected };
enum class RenderDrawInputSource { SceneRenderable, FullscreenTriangle };

enum class RenderInputDiagnosticCode {
  UnsupportedInputContract,
  ObjectClassRejected,
  MaterialRequired,
  MaterialTypeRejected,
  MissingMesh,
  GeometryContractMismatch,
  MissingShaderReflection,
  MissingPipelineFacts,
  MissingBinding,
  MissingResource,
  ZeroDrawCount,
  BackendUnsupported,
};

struct RenderInputDiagnostic final {
  RenderInputDiagnosticCode code =
      RenderInputDiagnosticCode::UnsupportedInputContract;
  StringID pass;
  StringID debugId;
  std::string message;
};

struct RenderDrawCommand final {
  u32 indexCount = 0;
  u32 instanceCount = 1;
  u32 firstIndex = 0;
  i32 vertexOffset = 0;
  u32 firstInstance = 0;
};

struct RenderInput {
  virtual ~RenderInput() = default;
  [[nodiscard]] virtual RenderInputKind kind() const = 0;

  StringID pass;
  StringID debugId;
  usize inputIndex = 0;
};

struct RenderDrawInput final : RenderInput {
  [[nodiscard]] RenderInputKind kind() const override {
    return RenderInputKind::Draw;
  }

  RenderDrawInputSource source = RenderDrawInputSource::SceneRenderable;
  ObjectHandle object;
  MeshHandle mesh;
  MaterialHandle material;
  GpuResourceRef vertexBuffer;
  GpuResourceRef indexBuffer;
  u32 primitiveIndex = u32_max;
  Vec3f sortCenter{};
  StringID objectDataSignature;
  StringID objectRenderType;
  StringID materialTypeSignature;
  bool debugOnly = false;
  std::vector<RenderDrawCommand> drawCommands;
};

struct RenderComputeInput final : RenderInput {
  [[nodiscard]] RenderInputKind kind() const override {
    return RenderInputKind::Compute;
  }

  u32 groupCountX = 1;
  u32 groupCountY = 1;
  u32 groupCountZ = 1;
  std::optional<StringID> readbackResource;
};

struct RenderInputBindingPlan final {
  std::vector<DescriptorResourceRef> descriptors;
};

struct RenderInputStats final {
  usize inputCount = 0;
  usize acceptedInputCount = 0;
  usize rejectedInputCount = 0;
  usize submittedDrawCount = 0;
  usize submittedDispatchCount = 0;
  usize fallbackObservedCount = 0;
};

struct RenderInputDesc final {
  RenderInputStatus status = RenderInputStatus::Rejected;
  usize inputIndex = 0;
  StringID pass;
  StringID debugId;
  PipelineKey pipelineKey;
  PipelineBuildDesc pipelineBuildDesc;
  StringID shaderUri;
  StringID shaderVariantKey;
  StringID reflectionIdentity;
  RenderInputBindingPlan bindingPlan;
  std::vector<GpuResourceRef> resourceDependencies;
  std::vector<RenderInputDiagnostic> diagnostics;
  RenderInputStats stats;

  [[nodiscard]] bool accepted() const {
    return status == RenderInputStatus::Accepted;
  }
};

} // namespace LX_core
