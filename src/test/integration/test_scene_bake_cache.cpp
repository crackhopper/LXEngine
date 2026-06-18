#include "core/asset/material_instance.hpp"
#include "core/scene/ibl_bake_manifest.hpp"
#include "core/scene/ibl_bake_keys.hpp"
#include "core/scene/ibl_bake_job.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "editor/commands/command_bus.hpp"
#include "editor/commands/lxe_editor_commands.hpp"
#include "editor/runtime/scene_runtime.hpp"
#include "infra/resource_parsers/ibl_bake_manifest_parser.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace LX_core;
using namespace LX_demo::lxe_editor;

namespace {

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
  }
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

bool diagnosticsContain(const std::vector<std::string> &diagnostics,
                        const std::string &needle) {
  for (const std::string &diagnostic : diagnostics) {
    if (contains(diagnostic, needle)) {
      return true;
    }
  }
  return false;
}

std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "[FAIL] unable to read " << path << '\n';
    std::exit(1);
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

MaterialInstanceUniquePtr makeBakeMaterial(const std::string &type,
                                           const char *sourceUri,
                                           const char *reflectionHash) {
  auto material =
      MaterialInstance::createUnique(MaterialTemplate::create(type));
  material->setBsdfType(type);
  material->setMaterialSourceUri(ResourceUri(sourceUri));
  material->setMaterialSourceReflectionHash(reflectionHash);
  return material;
}

ObjectHandle registerBakeObject(SceneResourceTable &table,
                                MaterialHandle material) {
  ObjectResource object;
  object.material = material;
  return table.registerObject(object);
}

RenderFeature makeBakeEnvironmentFeature(const char *environmentMapUri,
                                         const char *sourceHash = "") {
  RenderFeature feature;
  feature.feature = "environmentLighting";
  feature.parameters["environmentMap"] = RenderFeatureParameter{
      .kind = "textureCube",
      .uri = ResourceUri(environmentMapUri),
      .sourceHash = sourceHash,
      .binding = "SkyboxMap",
      .required = true,
  };
  return feature;
}

std::filesystem::path repoRootForTest() {
  std::filesystem::path root = std::filesystem::current_path();
  if (std::filesystem::exists(root / "assets/materials/pbr.material")) {
    return root;
  }
  if (std::filesystem::exists(root.parent_path() /
                              "assets/materials/pbr.material")) {
    return root.parent_path();
  }
  return root;
}

void writeTextFile(const std::filesystem::path &path,
                   const std::string &text) {
  std::ofstream out(path);
  if (!out) {
    std::cerr << "[FAIL] unable to write " << path << '\n';
    std::exit(1);
  }
  out << text;
}

void testEventSequenceIsMonotonic() {
  IblBakeEventQueue queue;
  const IblBakeJobEvent first =
      queue.push(IblBakeJobEvent{.job = 7,
                                 .phase = IblBakeJobPhase::Queued,
                                 .message = "queued"});
  const IblBakeJobEvent second =
      queue.push(IblBakeJobEvent{.job = 7,
                                 .phase = IblBakeJobPhase::CacheCheck,
                                 .message = "cache check"});
  expect(first.sequence + 1 == second.sequence,
         "event queue sequence should be monotonic");

  const std::vector<IblBakeJobEvent> afterFirst =
      queue.drainSince(first.sequence);
  expect(afterFirst.size() == 1, "drainSince should filter old events");
  expect(afterFirst.front().sequence == second.sequence,
         "drainSince should return later events");
}

void testRunningJobGuardRejectsForce() {
  IblBakeJobService service;

  const IblBakeStartResult start = service.start(false);
  expect(start.ok, "start should return ok");
  expect(start.job != 0, "start should return job id");

  const IblBakeStartResult second = service.start(false);
  expect(!second.ok, "duplicate start should not start a new job");
  expect(second.alreadyRunning, "duplicate start should report running job");
  expect(second.job == start.job, "duplicate start should return running job");

  const IblBakeStartResult forced = service.start(true);
  expect(!forced.ok, "force while running should be rejected");
  expect(forced.rejected, "force while running should report rejection");
  expect(contains(forced.message, "already running"),
         "force rejection should mention running job");
}

void testBakeCommandsUseJobService() {
  CommandBus bus;
  IblBakeJobService service;
  registerBakeCommands(bus, service);

  const CommandResult start = bus.dispatch("bake ibl start");
  expect(start.ok, "bake ibl start should succeed");
  expect(contains(start.message, "started bake job 1"),
         "start command should return job id");

  const CommandResult forced = bus.dispatch("bake ibl start --force");
  expect(!forced.ok, "force start while running should fail");
  expect(contains(forced.message, "already running"),
         "force command should explain running job");

  const CommandResult status = bus.dispatch("bake job status 1");
  expect(status.ok, "status command should succeed");
  expect(contains(status.message, "phase="),
         "status command should include phase");
  expect(contains(status.message, "progress="),
         "status command should include progress");

  const CommandResult logs = bus.dispatch("bake job logs 1 0");
  expect(logs.ok, "logs command should succeed");
  expect(contains(logs.message, "[1] info queued"),
         "logs command should include queued event");

  const CommandResult cancel = bus.dispatch("bake job cancel 1");
  expect(cancel.ok, "cancel command should succeed");
  expect(contains(cancel.message, "cancel pending"),
         "cancel command should report cancel pending");
}

void testSceneReferencedAssetsWithoutBakeMarkersProduceNoItems() {
  SceneResourceTable table;
  const MaterialHandle material = table.registerMaterialInstance(
      ResourceUri("memory://materials/standard-a"),
      makeBakeMaterial("standard-pbr", "memory://materials/standard-a",
                       "hash-a"));
  (void)registerBakeObject(table, material);
  (void)table.registerRenderFeature(
      ResourceUri("memory://features/env-a"),
      makeBakeEnvironmentFeature("memory://env/a.hdr"));

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.items.empty(),
         "scene-referenced assets without bake markers must not create work");
}

void testStandardPbrMaterialBakeItemsAreDeduplicated() {
  SceneResourceTable table;
  const MaterialHandle first = table.registerMaterialInstance(
      ResourceUri("memory://materials/standard-a"),
      makeBakeMaterial("standard-pbr", "memory://materials/standard-a",
                       "hash-a"));
  const MaterialHandle second = table.registerMaterialInstance(
      ResourceUri("memory://materials/standard-b"),
      makeBakeMaterial("standard-pbr", "memory://materials/standard-b",
                       "hash-b"));
  table.resolve(second)->get().setAuthoringMetadata(
      std::unordered_map<std::string, std::string>{{"brdfModel", "custom"}});
  const ObjectHandle firstObject = registerBakeObject(table, first);
  const ObjectHandle secondObject = registerBakeObject(table, second);
  table.setObjectIblBakeMarker(firstObject, SceneIblBakeMarker{.enabled = true});
  table.setObjectIblBakeMarker(secondObject,
                               SceneIblBakeMarker{.enabled = true});

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.materialItems.size() == 1,
         "duplicate standard-pbr material bake keys should collapse");
  expect(collection.items.size() == 1,
         "deduplicated material key should produce one bake item");
  const auto *key =
      std::get_if<MaterialIblBakeKey>(&collection.materialItems.front().key);
  expect(key != nullptr, "material bake item should expose material key");
  expect(key->materialType == "standard-pbr",
         "material bake key should include material type");
  expect(key->bsdfModel == "ggx-smith",
         "standard-pbr material bake model should be type-derived");
}

void testReleasedObjectMarkerDoesNotLeakToReusedSlot() {
  SceneResourceTable table;
  const MaterialHandle material = table.registerMaterialInstance(
      ResourceUri("memory://materials/standard"),
      makeBakeMaterial("standard-pbr", "memory://materials/standard",
                       "hash"));
  const ObjectHandle firstObject = registerBakeObject(table, material);
  table.setObjectIblBakeMarker(firstObject, SceneIblBakeMarker{.enabled = true});
  table.release(firstObject);

  const ObjectHandle secondObject = registerBakeObject(table, material);
  expect(secondObject.index == firstObject.index,
         "test setup should reuse released object slot");

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.items.empty(),
         "released object bake marker must not leak to reused slot");
}

void testSceneRuntimeRegistersObjectBakeMarker() {
  const std::filesystem::path scenePath =
      repoRootForTest() / ".tmp_scene_bake_runtime_marker.scene.yaml";
  writeTextFile(scenePath, R"(
scene:
  name: Scene Bake Runtime Marker
  gameplayCameraPath: /camera
root:
  nodeName: scene_root
  name: ''
  children:
    - nodeName: camera
      name: camera
      camera:
        type: perspective
        fovY: 45.0
        aspect: 1.0
        nearPlane: 0.1
        farPlane: 100.0
        cullingMask: 4294967295
    - nodeName: cube
      name: cube
      mesh:
        uri: builtin://lxe_editor/primitives/cube
      material:
        uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material
      bake:
        ibl:
          enabled: true
)");

  SceneRuntime runtime;
  runtime.loadFromDocumentPath(scenePath);
  std::filesystem::remove(scenePath);

  const auto scene = runtime.scene();
  expect(scene != nullptr, "runtime should load scene");
  const IblBakeItemCollection collection =
      scene->resources().collectIblBakeItems();

  expect(collection.materialItems.size() == 1,
         "runtime object bake.ibl marker should create one material item");
}

void testDifferentEnvironmentKeysProduceDistinctItems() {
  SceneResourceTable table;
  const RenderFeatureHandle first = table.registerRenderFeature(
      ResourceUri("memory://features/env-a"),
      makeBakeEnvironmentFeature("memory://env/a.hdr"));
  const RenderFeatureHandle second = table.registerRenderFeature(
      ResourceUri("memory://features/env-b"),
      makeBakeEnvironmentFeature("memory://env/b.hdr"));
  table.addEnvironmentIblBakeRequest(first);
  table.addEnvironmentIblBakeRequest(second);

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.environmentItems.size() == 2,
         "different environment input uri/hash should produce two keys");
  const auto *firstKey =
      std::get_if<EnvironmentIblBakeKey>(&collection.environmentItems[0].key);
  const auto *secondKey =
      std::get_if<EnvironmentIblBakeKey>(&collection.environmentItems[1].key);
  expect(firstKey != nullptr && secondKey != nullptr,
         "environment bake items should expose environment keys");
  expect(firstKey->environmentMapUri != secondKey->environmentMapUri,
         "environment bake keys should include environment map uri");
}

void testSameEnvironmentSourceIsDeduplicatedAcrossFeatures() {
  SceneResourceTable table;
  const RenderFeatureHandle first = table.registerRenderFeature(
      ResourceUri("memory://features/env-a"),
      makeBakeEnvironmentFeature("memory://env/shared.hdr"));
  const RenderFeatureHandle second = table.registerRenderFeature(
      ResourceUri("memory://features/env-b"),
      makeBakeEnvironmentFeature("memory://env/shared.hdr"));
  table.addEnvironmentIblBakeRequest(first);
  table.addEnvironmentIblBakeRequest(second);

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.environmentItems.size() == 1,
         "same environment input URI should deduplicate across features");
  const auto *key =
      std::get_if<EnvironmentIblBakeKey>(&collection.environmentItems[0].key);
  expect(key != nullptr, "environment bake item should expose environment key");
  expect(key->environmentMapUri == ResourceUri("memory://env/shared.hdr"),
         "environment bake key should preserve environment source uri");
}

void testSameEnvironmentUriWithDifferentHashProducesDistinctItems() {
  SceneResourceTable table;
  const RenderFeatureHandle first = table.registerRenderFeature(
      ResourceUri("memory://features/env-a"),
      makeBakeEnvironmentFeature("memory://env/shared.hdr", "hash-a"));
  const RenderFeatureHandle second = table.registerRenderFeature(
      ResourceUri("memory://features/env-b"),
      makeBakeEnvironmentFeature("memory://env/shared.hdr", "hash-b"));
  table.addEnvironmentIblBakeRequest(first);
  table.addEnvironmentIblBakeRequest(second);

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.environmentItems.size() == 2,
         "same environment input URI with different hash should produce two "
         "keys");
  const auto *firstKey =
      std::get_if<EnvironmentIblBakeKey>(&collection.environmentItems[0].key);
  const auto *secondKey =
      std::get_if<EnvironmentIblBakeKey>(&collection.environmentItems[1].key);
  expect(firstKey != nullptr && secondKey != nullptr,
         "hashed environment bake items should expose environment keys");
  expect(firstKey->environmentMapUri == secondKey->environmentMapUri,
         "hash test should hold environment URI constant");
  expect(firstKey->sourceHash != secondKey->sourceHash,
         "environment bake keys should include source hash");
}

void testUnsupportedMaterialTypeProducesWarning() {
  SceneResourceTable table;
  const MaterialHandle material = table.registerMaterialInstance(
      ResourceUri("memory://materials/custom"),
      makeBakeMaterial("custom-brdf", "memory://materials/custom",
                       "custom-hash"));
  const ObjectHandle object = registerBakeObject(table, material);
  table.setObjectIblBakeMarker(object, SceneIblBakeMarker{.enabled = true});

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.materialItems.empty(),
         "unsupported material type should not create material bake item");
  expect(!collection.warnings.empty(),
         "unsupported material type should produce warning");
  expect(contains(collection.warnings.front().message,
                  "unsupported material type"),
         "unsupported material warning should explain type rejection");
}

void testIblManifestDerivesMipCount() {
  expect(deriveIblBakeMipCount(256) == 9,
         "256px IBL bake should derive 9 mip levels");
}

void testEnvironmentManifestRejectsUnknownField() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed = parser.parseEnvironmentManifest(
      ResourceUri("memory://env-manifest"), R"(
schema: lxe.environment-ibl-bake.v1
source:
  uri: memory://env/source.hdr
  hash: sha256:env
bake:
  diffuse:
    basis: sh9
  specular:
    format: RGBA16Float
    resolution: 256
    mips: 9
    roughness: alpha-squared
    layout: cubemap
    faces: 6
outputs:
  diffuse:
    file: diffuse_sh9.yaml
  specular:
    file: specular_prefilter.ktx2
unexpected: true
)");

  expect(!parsed.manifest.has_value(),
         "unknown environment manifest field should reject manifest");
  expect(diagnosticsContain(parsed.diagnostics, "unexpected"),
         "unknown environment manifest diagnostic should name field");
}

void testEnvironmentManifestRejectsWrongMipCount() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed = parser.parseEnvironmentManifest(
      ResourceUri("memory://env-manifest"), R"(
schema: lxe.environment-ibl-bake.v1
source:
  uri: memory://env/source.hdr
  hash: sha256:env
bake:
  diffuse:
    basis: sh9
  specular:
    format: RGBA16Float
    resolution: 256
    mips: 8
    roughness: alpha-squared
    layout: cubemap
    faces: 6
outputs:
  diffuse:
    file: diffuse_sh9.yaml
  specular:
    file: specular_prefilter.ktx2
)");

  expect(!parsed.manifest.has_value(),
         "wrong environment manifest mip count should reject manifest");
  expect(diagnosticsContain(parsed.diagnostics, "bake.specular.mips"),
         "wrong mip diagnostic should name bake.specular.mips");
}

void testSh9PayloadRejectsEightCoefficients() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed = parser.parseSh9IrradiancePayload(
      ResourceUri("memory://diffuse-sh9"), R"(
schema: lxe.sh9.v1
space: world
basis: real-sh
order: 2
layout: rgb-interleaved
coefficients:
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
)");

  expect(!parsed.payload.has_value(),
         "SH9 payload with eight coefficients should reject");
  expect(diagnosticsContain(parsed.diagnostics,
                            "coefficients must contain 9"),
         "SH9 coefficient diagnostic should explain required count");
}

void testSh9PayloadRejectsWrongLayout() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed = parser.parseSh9IrradiancePayload(
      ResourceUri("memory://diffuse-sh9"), R"(
schema: lxe.sh9.v1
space: world
basis: real-sh
order: 2
layout: bgr-interleaved
coefficients:
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
)");

  expect(!parsed.payload.has_value(),
         "SH9 payload with wrong layout should reject");
  expect(diagnosticsContain(parsed.diagnostics, "layout"),
         "SH9 wrong layout diagnostic should name layout");
}

void testSh9PayloadRejectsNonNumericCoefficient() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed = parser.parseSh9IrradiancePayload(
      ResourceUri("memory://diffuse-sh9"), R"(
schema: lxe.sh9.v1
space: world
basis: real-sh
order: 2
layout: rgb-interleaved
coefficients:
  - [not-a-number, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
)");

  expect(!parsed.payload.has_value(),
         "SH9 payload with non-numeric coefficient should reject");
  expect(diagnosticsContain(parsed.diagnostics, "coefficients[0]"),
         "SH9 non-numeric coefficient diagnostic should name coefficient");
}

void testMaterialManifestRejectsWrongBrdfSize() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed = parser.parseMaterialManifest(
      ResourceUri("memory://material-manifest"), R"(
schema: lxe.material-ibl-bake.v1
material:
  uri: memory://materials/standard-pbr.material
  type: standard-pbr
  hash: sha256:material
bake:
  brdf:
    model: ggx-smith
    format: RG16Float
    size: 128
outputs:
  brdf:
    file: brdf_lut.ktx2
)");

  expect(!parsed.manifest.has_value(),
         "wrong material BRDF size should reject manifest");
  expect(diagnosticsContain(parsed.diagnostics, "brdf.size"),
         "wrong BRDF size diagnostic should name brdf.size");
}

void testEnvironmentManifestRejectsWrongSourceHashAgainstExpectedKey() {
  EnvironmentIblBakeManifest manifest;
  manifest.sourceUri = ResourceUri("memory://env/source.hdr");
  manifest.sourceHash = "sha256:old";
  manifest.diffuseFile = "diffuse_sh9.yaml";
  manifest.specularFile = "specular_prefilter.ktx2";

  const auto result = validateIblBakeManifestSource(
      manifest, ResourceUri("memory://env/source.hdr"), "sha256:new");

  expect(!result.ok, "environment manifest wrong source hash should reject");
  expect(diagnosticsContain(result.diagnostics, "source.hash"),
         "wrong environment source hash diagnostic should name source.hash");
}

void testEnvironmentManifestRejectsMissingPayloadFiles() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed = parser.parseEnvironmentManifest(
      ResourceUri("memory://env-manifest"), R"(
schema: lxe.environment-ibl-bake.v1
source:
  uri: memory://env/source.hdr
  hash: sha256:env
bake:
  diffuse:
    basis: sh9
  specular:
    format: RGBA16Float
    resolution: 256
    mips: 9
    roughness: alpha-squared
    layout: cubemap
    faces: 6
outputs:
  diffuse:
    file: missing_diffuse_sh9.yaml
  specular:
    file: missing_specular_prefilter.ktx2
)");
  expect(parsed.manifest.has_value(),
         "manifest with referenced payload files should parse before payload "
         "existence validation");

  const auto result = parser.validateEnvironmentPayloadFiles(
      repoRootForTest() / ".tmp_env_manifest.yaml", *parsed.manifest);

  expect(!result.ok,
         "environment manifest with missing payload files should reject");
  expect(diagnosticsContain(result.diagnostics, "outputs.diffuse.file"),
         "missing diffuse payload diagnostic should name output field");
}

void testAtomicEnvironmentManifestCommitKeepsOldFileOnInvalidManifest() {
  const std::filesystem::path path =
      repoRootForTest() / ".tmp_invalid_ibl_manifest.yaml";
  writeTextFile(path, "old-manifest");

  LX_infra::IblBakeManifestParser parser;
  EnvironmentIblBakeManifest manifest;
  manifest.sourceUri = ResourceUri("memory://env/source.hdr");
  manifest.sourceHash = "sha256:env";
  manifest.specularResolution = 256;
  manifest.specularMips = 8;
  manifest.diffuseFile = "diffuse_sh9.yaml";
  manifest.specularFile = "specular_prefilter.ktx2";

  const auto result = parser.writeEnvironmentManifestAtomically(path, manifest);

  expect(!result.ok, "invalid environment manifest should not be committed");
  expect(readTextFile(path) == "old-manifest",
         "invalid atomic manifest commit should keep old file");
  std::filesystem::remove(path);
}

void testAtomicEnvironmentManifestCommitWritesValidManifest() {
  const std::filesystem::path path =
      repoRootForTest() / ".tmp_valid_ibl_manifest.yaml";
  const std::filesystem::path tempPath =
      path.parent_path() / (path.filename().generic_string() + ".tmp");
  std::filesystem::remove(path);
  std::filesystem::remove(tempPath);

  LX_infra::IblBakeManifestParser parser;
  EnvironmentIblBakeManifest manifest;
  manifest.sourceUri = ResourceUri("memory://env/source.hdr");
  manifest.sourceHash = "sha256:env";
  manifest.specularResolution = 256;
  manifest.specularMips = deriveIblBakeMipCount(manifest.specularResolution);
  manifest.diffuseFile = "diffuse_sh9.yaml";
  manifest.specularFile = "specular_prefilter.ktx2";

  const auto result = parser.writeEnvironmentManifestAtomically(path, manifest);

  expect(result.ok, "valid environment manifest should be committed");
  expect(contains(readTextFile(path), "lxe.environment-ibl-bake.v1"),
         "valid atomic commit should write manifest contents");
  expect(!std::filesystem::exists(tempPath),
         "successful atomic manifest commit should not leave temp file");
  std::filesystem::remove(path);
}

void testAtomicManifestCommitCleansTempOnRenameFailure() {
  const std::filesystem::path path =
      repoRootForTest() / ".tmp_ibl_manifest_target_dir";
  const std::filesystem::path tempPath =
      path.parent_path() / (path.filename().generic_string() + ".tmp");
  std::filesystem::remove_all(path);
  std::filesystem::remove(tempPath);
  std::filesystem::create_directory(path);

  LX_infra::IblBakeManifestParser parser;
  const auto result = parser.writeAtomically(path, "manifest");

  expect(!result.ok, "atomic commit to existing directory should fail");
  expect(!std::filesystem::exists(tempPath),
         "failed atomic manifest rename should remove temp file");
  std::filesystem::remove_all(path);
}

} // namespace

int main() {
  testEventSequenceIsMonotonic();
  testRunningJobGuardRejectsForce();
  testBakeCommandsUseJobService();
  testSceneReferencedAssetsWithoutBakeMarkersProduceNoItems();
  testStandardPbrMaterialBakeItemsAreDeduplicated();
  testReleasedObjectMarkerDoesNotLeakToReusedSlot();
  testSceneRuntimeRegistersObjectBakeMarker();
  testDifferentEnvironmentKeysProduceDistinctItems();
  testSameEnvironmentSourceIsDeduplicatedAcrossFeatures();
  testSameEnvironmentUriWithDifferentHashProducesDistinctItems();
  testUnsupportedMaterialTypeProducesWarning();
  testIblManifestDerivesMipCount();
  testEnvironmentManifestRejectsUnknownField();
  testEnvironmentManifestRejectsWrongMipCount();
  testSh9PayloadRejectsEightCoefficients();
  testSh9PayloadRejectsWrongLayout();
  testSh9PayloadRejectsNonNumericCoefficient();
  testMaterialManifestRejectsWrongBrdfSize();
  testEnvironmentManifestRejectsWrongSourceHashAgainstExpectedKey();
  testEnvironmentManifestRejectsMissingPayloadFiles();
  testAtomicEnvironmentManifestCommitKeepsOldFileOnInvalidManifest();
  testAtomicEnvironmentManifestCommitWritesValidManifest();
  testAtomicManifestCommitCleansTempOnRenameFailure();
  return 0;
}
