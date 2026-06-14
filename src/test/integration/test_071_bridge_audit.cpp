#include "core/frame_graph/render_queue.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace LX_core;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

struct AuditResource final : public IGpuResource {
  AuditResource(ResourceType type, StringID bindingName, u32 byteSize)
      : type(type), bindingName(bindingName), bytes(byteSize, 0) {}

  ResourceType getType() const override { return type; }
  const void *getRawData() const override { return bytes.data(); }
  u32 getByteSize() const override { return static_cast<u32>(bytes.size()); }
  StringID getBindingName() const override { return bindingName; }

  ResourceType type = ResourceType::None;
  StringID bindingName;
  std::vector<u8> bytes;
};

std::filesystem::path repoRoot() {
  return std::filesystem::path(__FILE__).parent_path().parent_path()
      .parent_path().parent_path();
}

std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void testProductionCodeHasNoOldRealtimeRasterDtoTokens() {
  const std::array<std::string, 3> forbiddenTokens = {
      std::string("Raster") + "Draw", std::string("Raster") + "Batch",
      std::string("raster") + "Batch"};
  const std::array<std::filesystem::path, 2> roots = {
      repoRoot() / "src/core", repoRoot() / "src/backend"};

  bool foundForbiddenToken = false;
  for (const auto &root : roots) {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto extension = entry.path().extension().string();
      if (extension != ".cpp" && extension != ".hpp" && extension != ".h") {
        continue;
      }
      const std::string text = readTextFile(entry.path());
      for (const auto &token : forbiddenTokens) {
        if (text.find(token) != std::string::npos) {
          std::cerr << "[FAIL] old realtime raster DTO token '" << token
                    << "' remains in production file "
                    << entry.path().lexically_relative(repoRoot()).string()
                    << '\n';
          foundForbiddenToken = true;
        }
      }
    }
  }

  EXPECT(!foundForbiddenToken,
         "production code must not expose old realtime raster DTO names");
}

RenderWorkItem makeLegacyDirectRasterAuditItem(const IGpuResource &vertex,
                                   const IGpuResource &index,
                                   const IGpuResource &camera,
                                   const IGpuResource &material,
                                   u32 firstIndex) {
  RenderWorkItem item;
  item.domain = RenderDomain::Realtime;
  item.kind = RenderWorkKind::DirectRasterPass;
  item.pass = StringID("Forward");
  RenderTargetDesc target;
  target.role = RenderTargetRole::Swapchain;
  target.colorFormat = ImageFormat::BGRA8;
  target.depthFormat = ImageFormat::D32Float;
  item.target = target;
  item.debugId = StringID("071.audit.draw");
  item.objectSignature = StringID("071.audit.mesh");
  item.materialSignature = StringID("071.audit.material");
  item.materialTypeVariant = StringID("071.audit.materialTypeVariant");
  item.renderPathNodeSignature = StringID("071.audit.renderPathNode");
  item.pipelineKey =
      PipelineKey::build(item.materialTypeVariant,
                         item.renderPathNodeSignature);
  item.directRaster.vertexBuffer = GpuResourceRef{vertex};
  item.directRaster.indexBuffer = GpuResourceRef{index};
  item.directRaster.indexCount = 3;
  item.directRaster.firstIndex = firstIndex;
  item.directRaster.instanceCount = 1;
  item.descriptorResources.emplace_back(camera);
  item.descriptorResources.emplace_back(material);
  return item;
}

void testMigratedQueueRejectsIncompleteCoverageWithoutFallback() {
  AuditResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  AuditResource index(ResourceType::IndexBuffer, StringID{}, 48);
  AuditResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"), 64);
  AuditResource material(ResourceType::UniformBuffer,
                         StringID("MaterialParams"), 64);

  RenderWorkQueue queue;
  RenderWorkItem item = makeLegacyDirectRasterAuditItem(vertex, index, camera, material, 0);
  item.directRaster.indexBuffer = GpuResourceRef{};
  queue.addItem(std::move(item));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(!result.ok,
         "migrated validation queue must reject incomplete batch coverage");
  EXPECT(!result.diagnostics.empty(),
         "migrated validation queue must explain rejected incomplete coverage");

  const BindlessSubmissionDecision decision = decideBindlessSubmission(
      queue, StringID("Forward"), false, true);
  EXPECT(decision.kind ==
             BindlessSubmissionDecisionKind::StrictValidationRejected,
         "renderer decision should reject incomplete migrated work without "
         "falling back to per-item submission");
}

void testMigratedQueueRejectsLegacyRenderWorkItems() {
  AuditResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  AuditResource index(ResourceType::IndexBuffer, StringID{}, 48);
  AuditResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"), 64);
  AuditResource material(ResourceType::UniformBuffer,
                         StringID("MaterialParams"), 64);

  RenderWorkQueue queue;
  queue.addItem(makeLegacyDirectRasterAuditItem(vertex, index, camera, material, 0));
  queue.addItem(makeLegacyDirectRasterAuditItem(vertex, index, camera, material, 3));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(!result.ok,
         "legacy RenderWorkItem geometry should not pass bindless audit");
  EXPECT(result.coveredItemCount == 0,
         "legacy RenderWorkItem geometry should not count as covered");
  EXPECT(result.diagnostics.size() == 2,
         "audit should diagnose every legacy RenderWorkItem");

  const BindlessSubmissionDecision decision = decideBindlessSubmission(
      queue, StringID("Forward"), true, true);
  EXPECT(decision.kind ==
             BindlessSubmissionDecisionKind::StrictValidationRejected,
         "renderer decision should reject legacy RenderWorkItem geometry");
}

void testMaterialV2ValidationRejectsLegacyMaterialDescriptor() {
  AuditResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  AuditResource index(ResourceType::IndexBuffer, StringID{}, 48);
  AuditResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"), 64);
  AuditResource material(ResourceType::UniformBuffer, StringID("MaterialUBO"),
                         64);

  RenderWorkQueue queue;
  RenderWorkItem item = makeLegacyDirectRasterAuditItem(vertex, index, camera, material, 0);
  item.directRaster.materialIndex = 0;
  item.directRaster.drawRecordIndex = 0;
  queue.addItem(std::move(item));

  const MaterialV2ValidationResult result =
      validateMaterialV2StrictQueue(queue, StringID("Forward"));
  EXPECT(!result.ok,
         "Material v2 validation should reject legacy MaterialUBO bindings");
  EXPECT(!result.diagnostics.empty(),
         "legacy material rejection should include diagnostics");
  if (!result.diagnostics.empty()) {
    const auto &diagnostic = result.diagnostics.front();
    EXPECT(diagnostic.pass == StringID("Forward"),
           "diagnostic should name the rejected pass");
    EXPECT(diagnostic.debugId == StringID("071.audit.draw"),
           "diagnostic should preserve draw debug identity");
    EXPECT(diagnostic.objectSignature == StringID("071.audit.mesh"),
           "diagnostic should preserve object identity");
    EXPECT(diagnostic.materialSignature == StringID("071.audit.material"),
           "diagnostic should preserve material identity");
    EXPECT(diagnostic.bindingName == StringID("MaterialUBO"),
           "diagnostic should name the forbidden binding");
    EXPECT(diagnostic.reason.find("MaterialUBO") != std::string::npos,
           "diagnostic should name the forbidden legacy binding");
  }
}

void testMaterialV2ValidationRejectsTypedIndexFallback() {
  AuditResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  AuditResource index(ResourceType::IndexBuffer, StringID{}, 48);
  AuditResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"), 64);
  AuditResource material(ResourceType::UniformBuffer,
                         StringID("SceneMaterials"), 64);

  RenderWorkQueue queue;
  RenderWorkItem item = makeLegacyDirectRasterAuditItem(vertex, index, camera, material, 0);
  item.directRaster.materialIndex = u32_max;
  item.directRaster.drawRecordIndex = u32_max;
  queue.addItem(std::move(item));

  const MaterialV2ValidationResult result =
      validateMaterialV2StrictQueue(queue, StringID("Forward"));
  EXPECT(!result.ok,
         "Material v2 validation should reject missing typed material/draw "
         "indices");
  EXPECT(!result.diagnostics.empty(),
         "typed-index fallback rejection should include diagnostics");
  bool namesMaterialIndex = false;
  bool namesDrawIndex = false;
  bool namesFinalShader = false;
  for (const auto &diagnostic : result.diagnostics) {
    EXPECT(diagnostic.pass == StringID("Forward"),
           "typed-index diagnostic should name the rejected pass");
    EXPECT(diagnostic.objectSignature == StringID("071.audit.mesh"),
           "typed-index diagnostic should preserve object identity");
    EXPECT(diagnostic.materialSignature == StringID("071.audit.material"),
           "typed-index diagnostic should preserve material identity");
    if (diagnostic.bindingName == StringID("SceneMaterials") &&
        diagnostic.reason.find("typed SceneMaterials index") !=
            std::string::npos) {
      namesMaterialIndex = true;
    }
    if (diagnostic.bindingName == StringID("SceneDraws") &&
        diagnostic.reason.find("typed SceneDraws index") != std::string::npos) {
      namesDrawIndex = true;
    }
    if (diagnostic.bindingName == StringID("FinalShader") &&
        diagnostic.reason.find("resolved final shader") != std::string::npos) {
      namesFinalShader = true;
    }
  }
  EXPECT(!namesMaterialIndex,
         "legacy RenderWorkItem validation should not infer SceneMaterials "
         "consumption without final shader reflection");
  EXPECT(namesDrawIndex,
         "typed-index diagnostic should name the missing SceneDraws index");
  EXPECT(namesFinalShader,
         "typed-index diagnostic should require final shader reflection");
}

void testMaterialV2ValidationRejectsLegacyTypedSceneDataWithoutFinalShader() {
  AuditResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  AuditResource index(ResourceType::IndexBuffer, StringID{}, 48);
  AuditResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"), 64);
  AuditResource material(ResourceType::StorageBuffer,
                         StringID("SceneMaterials"), 64);

  RenderWorkQueue queue;
  RenderWorkItem item = makeLegacyDirectRasterAuditItem(vertex, index, camera, material, 0);
  item.directRaster.materialIndex = 0;
  item.directRaster.drawRecordIndex = 0;
  queue.addItem(std::move(item));

  const MaterialV2ValidationResult result =
      validateMaterialV2StrictQueue(queue, StringID("Forward"));
  EXPECT(!result.ok,
         "Task 2 should not preserve a positive legacy RenderWorkItem "
         "geometry path for typed SceneMaterials data");
  const bool namesFinalShader =
      std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                  [](const MaterialV2ValidationDiagnostic &diagnostic) {
                    return diagnostic.bindingName == StringID("FinalShader");
                  });
  EXPECT(namesFinalShader,
         "legacy typed SceneMaterials item should still require final shader "
         "reflection");
}

} // namespace

int main() {
  testProductionCodeHasNoOldRealtimeRasterDtoTokens();
  testMigratedQueueRejectsIncompleteCoverageWithoutFallback();
  testMigratedQueueRejectsLegacyRenderWorkItems();
  testMaterialV2ValidationRejectsLegacyMaterialDescriptor();
  testMaterialV2ValidationRejectsTypedIndexFallback();
  testMaterialV2ValidationRejectsLegacyTypedSceneDataWithoutFinalShader();
  if (g_failures != 0) {
    std::cerr << g_failures << " REQ-071 bridge audit checks failed\n";
    return 1;
  }
  return 0;
}
