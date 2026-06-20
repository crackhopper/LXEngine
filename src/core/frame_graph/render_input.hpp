#pragma once

#include "core/asset/render_effect.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/platform/types.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"
#include "core/scene/object.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace LX_core {

enum class RenderDomain {
  Realtime,
  Offline,
};

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

struct RenderSceneParticipant final {
  u32 sourceRenderableIndex = u32_max;
  StringID debugId;
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

enum class RayProgramPayload {
  Radiance,
};

struct RayHitShaderProgram final {
  u32 hitShaderIndex = 0;
  StringID materialType;
  ResourceUri uri;
  std::string function;
};

struct PrimitiveHitShader final {
  u32 participantIndex = 0;
  u32 primitiveIndex = 0;
  u32 hitShaderIndex = 0;
};

struct alignas(16) RayPrimitiveHitShaderRecord final {
  u32 hitShaderIndex = 0;
  u32 materialIndex = 0;
  u32 reserved0 = 0;
  u32 reserved1 = 0;
};

struct alignas(16) RayMaterialRecord final {
  u32 hitShaderIndex = 0;
  u32 baseColorTexture = u32_max;
  float metallic = 0.0f;
  float roughness = 1.0f;
  u32 metallicRoughnessTexture = u32_max;
  u32 normalTexture = u32_max;
  u32 occlusionTexture = u32_max;
  u32 emissiveTexture = u32_max;
  Vec4f baseColor{1.0f, 1.0f, 1.0f, 1.0f};
  Vec4f emissive{0.0f, 0.0f, 0.0f, 0.0f};
};

struct RayProgramTable final {
  RayProgramPayload payload = RayProgramPayload::Radiance;
  StringID dispatchFunction;
  std::vector<RayHitShaderProgram> hitShaders;
  std::vector<PrimitiveHitShader> primitiveHitShaders;
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
  u32 sourceRenderableIndex = u32_max;
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
  std::vector<RenderSceneParticipant> sceneParticipants;
};

struct RenderInputBindingPlan final {
  std::vector<DescriptorResourceRef> descriptors;
};

struct RenderInputStats final {
  usize compilerInputCount = 0;
  usize acceptedInputCount = 0;
  usize rejectedInputCount = 0;
  usize submittedDrawCount = 0;
  usize submittedDispatchCount = 0;
  usize fallbackObservedCount = 0;
};

struct RenderInputDesc final {
  struct Readback final {
    std::string name;
    StringID target;
    StringID extentKey;
    StringID binding;
    std::string format;
    RenderPathOutputKind kind = RenderPathOutputKind::Buffer;
    std::string mediaType;
    Vec3u extent{1u, 1u, 1u};
    GpuResourceRef resource;
  };

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
  std::vector<Readback> readbacks;
  std::optional<RayProgramTable> rayProgramTable;
  std::vector<GpuResourceRef> resourceDependencies;
  std::vector<RenderInputDiagnostic> diagnostics;
  RenderInputStats stats;

  [[nodiscard]] bool accepted() const {
    return status == RenderInputStatus::Accepted;
  }
};

struct PreparedFramePassWork final {
  StringID passName;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  std::vector<RenderInputDesc> descs;
};

} // namespace LX_core
