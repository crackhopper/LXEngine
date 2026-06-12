#include "core/asset/material_instance.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/material_loader/material_resource_parser.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

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

namespace fs = std::filesystem;

[[nodiscard]] fs::path makeTempRoot() {
  const fs::path root = fs::temp_directory_path() / "lxe_material_v2_refs";
  fs::remove_all(root);
  fs::create_directories(root / "materials");
  return root;
}

void writeFile(const fs::path &path, const std::string &content) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  out << content;
}

void testParserResourceDependenciesSurviveTableRegistration() {
  SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  auto parsed = parser.parse(table, "assets/materials/car/paint.material", R"(
schema: lxe.material.v2
bsdf:
  type: matte
  source: assets://shaders/glsl/common/materials/matte.contract.glsl
  parameters:
    Kd: { kind: texture, valueType: rgb, uri: ../textures/shared.png }
    normalmap: { kind: texture, valueType: rgb, uri: ../textures/shared.png }
    sigma: { kind: texture, valueType: float, uri: ../textures/shared.png }
)");

  EXPECT(parsed.instance != nullptr, "valid material should parse");
  EXPECT(parsed.diagnostics.empty(),
         "valid material should have no diagnostics");
  EXPECT(parsed.dependencies.size() == 3,
         "texture, normalmap, and sigma envelopes should be reported as "
         "parameter dependencies");
  if (!parsed.instance) {
    return;
  }

  EXPECT(parsed.dependencies[0].uri.string() ==
             "assets/materials/textures/shared.png",
         "first dependency should store canonical texture uri");
  EXPECT(parsed.dependencies[1].uri.string() ==
             "assets/materials/textures/shared.png",
         "second dependency should store canonical texture uri");
  EXPECT(parsed.dependencies.size() == 3 &&
             parsed.dependencies[2].uri.string() ==
                 "assets/materials/textures/shared.png",
         "normalmap dependency should store canonical texture uri");
  EXPECT(parsed.dependencies[0].resourceHandle.isValid(),
         "first dependency should store a typed resource identity handle");
  EXPECT(parsed.dependencies[1].resourceHandle.isValid(),
         "second dependency should store a typed resource identity handle");
  EXPECT(parsed.dependencies.size() == 3 &&
             parsed.dependencies[2].resourceHandle.isValid(),
         "normalmap dependency should store a typed resource identity handle");
  EXPECT(parsed.dependencies[0].resourceHandle ==
             parsed.dependencies[1].resourceHandle,
         "same canonical URI plus resource type should deduplicate handles");
  EXPECT(parsed.dependencies.size() == 3 &&
             parsed.dependencies[0].resourceHandle ==
                 parsed.dependencies[2].resourceHandle,
         "normalmap with the same texture URI should deduplicate handles");

  const MaterialHandle handle =
      table.registerMaterial(std::move(parsed.instance));
  EXPECT(handle.isValid(),
         "parsed material should register into resource table");

  const auto material = table.resolve(handle);
  EXPECT(material.has_value(), "registered material should resolve");
  if (!material.has_value()) {
    return;
  }

  const auto &deps = material->get().getMaterialDependencies();
  EXPECT(deps.size() == 3,
         "registered material should retain dependency metadata");
  EXPECT(deps.size() == 3 && deps[0].resourceHandle == deps[1].resourceHandle &&
             deps[0].resourceHandle == deps[2].resourceHandle,
         "registered dependencies should retain deduplicated handles");

  const auto kd = material->get().getMaterialEnvelope(StringID("Kd"));
  EXPECT(kd.has_value(), "registered material should retain Kd envelope");
  EXPECT(kd.has_value() && kd->get().kind == MaterialEnvelopeKind::Texture,
         "registered Kd envelope should retain texture kind");
  const auto normalmap =
      material->get().getMaterialEnvelope(StringID("normalmap"));
  EXPECT(normalmap.has_value(),
         "registered material should retain normalmap envelope");
  EXPECT(normalmap.has_value() &&
             normalmap->get().kind == MaterialEnvelopeKind::Texture,
         "registered normalmap envelope should retain texture kind");

  const auto graph = table.exportResourceGraph();
  u32 materialCount = 0;
  u32 textureCount = 0;
  u32 materialDependencyCount = 0;
  for (const ResourceMetadata &metadata : graph.resources) {
    if (metadata.type == SceneResourceType::Material &&
        metadata.uri.string() == "assets/materials/car/paint.material") {
      ++materialCount;
      materialDependencyCount = static_cast<u32>(metadata.dependencies.size());
    }
    if (metadata.type == SceneResourceType::Texture &&
        metadata.uri.string() == "assets/materials/textures/shared.png") {
      ++textureCount;
    }
  }
  EXPECT(materialCount == 1,
         "parser should register one material resource identity");
  EXPECT(textureCount == 1,
         "same canonical texture URI should register one texture identity");
  EXPECT(materialDependencyCount == 1,
         "material dependency graph should deduplicate repeated texture edges");
}

void testMixMaterialRefReadsTargetHeaderWithoutFullParse() {
  const fs::path root = makeTempRoot();
  const fs::path owner = root / "materials" / "mix.material";
  const fs::path leaf = root / "materials" / "leaf.material";
  writeFile(leaf, R"(
schema: lxe.material.v2
bsdf:
  type: matte
  source: assets://shaders/glsl/common/materials/matte.contract.glsl
)");

  SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  auto parsed = parser.parse(table, owner.generic_string(), R"(
schema: lxe.material.v2
bsdf:
  type: mix
  source: assets://shaders/glsl/common/materials/mix.contract.glsl
  parameters:
    namedmaterial1: { kind: materialRef, uri: leaf.material }
    namedmaterial2: { kind: materialRef, uri: leaf.material }
    amount: { kind: float, value: 0.35 }
)");

  EXPECT(parsed.instance != nullptr,
         "mix material should accept non-mix material reference header");
  EXPECT(parsed.diagnostics.empty(),
         "header-only material reference validation should not require target "
         "parameters");
  EXPECT(parsed.dependencies.size() == 2,
         "mix material refs should remain parameter dependencies");
  EXPECT(parsed.dependencies.size() == 2 &&
             parsed.dependencies[0].resourceHandle ==
                 parsed.dependencies[1].resourceHandle,
         "same material reference URI should deduplicate header handles");
}

void testMixMaterialRefRejectsTargetMixHeader() {
  const fs::path root = makeTempRoot();
  const fs::path owner = root / "materials" / "mix.material";
  const fs::path child = root / "materials" / "child_mix.material";
  writeFile(child, R"(
schema: lxe.material.v2
bsdf:
  type: mix
  source: assets://shaders/glsl/common/materials/mix.contract.glsl
)");

  SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  auto parsed = parser.parse(table, owner.generic_string(), R"(
schema: lxe.material.v2
bsdf:
  type: mix
  source: assets://shaders/glsl/common/materials/mix.contract.glsl
  parameters:
    namedmaterial1: { kind: materialRef, uri: child_mix.material }
    namedmaterial2: { kind: materialRef, uri: child_mix.material }
    amount: { kind: float, value: 0.35 }
)");

  EXPECT(parsed.instance == nullptr,
         "mix material should reject materialRef whose target header is mix");
  EXPECT(!parsed.diagnostics.empty(),
         "rejected nested mix material reference should emit diagnostics");
}

void testMixMaterialRefRejectsTargetHeaderWithoutSource() {
  const fs::path root = makeTempRoot();
  const fs::path owner = root / "materials" / "mix.material";
  const fs::path leaf = root / "materials" / "leaf_without_source.material";
  writeFile(leaf, R"(
schema: lxe.material.v2
bsdf:
  type: matte
)");

  SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  auto parsed = parser.parse(table, owner.generic_string(), R"(
schema: lxe.material.v2
bsdf:
  type: mix
  source: assets://shaders/glsl/common/materials/mix.contract.glsl
  parameters:
    namedmaterial1: { kind: materialRef, uri: leaf_without_source.material }
    namedmaterial2: { kind: materialRef, uri: leaf_without_source.material }
    amount: { kind: float, value: 0.35 }
)");

  EXPECT(parsed.instance == nullptr,
         "mix material should reject materialRef target header without source");
  EXPECT(!parsed.diagnostics.empty(),
         "materialRef target header without source should emit diagnostics");
  bool mentionsSource = false;
  for (const std::string &diagnostic : parsed.diagnostics) {
    mentionsSource =
        mentionsSource || diagnostic.find("bsdf.source") != std::string::npos;
  }
  EXPECT(mentionsSource,
         "materialRef target header diagnostic should name bsdf.source");
}

void testMixMaterialRefRejectsNamedStringReference() {
  const fs::path root = makeTempRoot();
  const fs::path owner = root / "materials" / "mix.material";

  SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  auto parsed = parser.parse(table, owner.generic_string(), R"(
schema: lxe.material.v2
bsdf:
  type: mix
  source: assets://shaders/glsl/common/materials/mix.contract.glsl
  parameters:
    namedmaterial1: { kind: materialRef, uri: named:matte_base }
    namedmaterial2: { kind: materialRef, uri: named:clearcoat }
    amount: { kind: float, value: 0.35 }
)");

  EXPECT(parsed.instance == nullptr,
         "mix material should reject named-string materialRef values");
  EXPECT(!parsed.diagnostics.empty(),
         "rejected named-string materialRef should emit diagnostics");
}

void testMaterialRefDiagnosticsIncludeParserAndResourceContext() {
  const fs::path root = makeTempRoot();
  const fs::path owner = root / "materials" / "mix.material";

  SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  auto parsed = parser.parse(table, owner.generic_string(), R"(
schema: lxe.material.v2
bsdf:
  type: mix
  source: assets://shaders/glsl/common/materials/mix.contract.glsl
  parameters:
    namedmaterial1: { kind: materialRef, uri: missing.material }
    namedmaterial2: { kind: materialRef, uri: missing.material }
    amount: { kind: float, value: 0.35 }
)");

  EXPECT(parsed.instance == nullptr,
         "missing materialRef header should fail material parse");
  EXPECT(!parsed.diagnostics.empty(),
         "missing materialRef header should emit diagnostics");
  if (parsed.diagnostics.empty()) {
    return;
  }
  const std::string &diagnostic = parsed.diagnostics.front();
  EXPECT(diagnostic.find(owner.generic_string()) != std::string::npos,
         "diagnostic should include owner material URI");
  EXPECT(diagnostic.find("bsdf.parameters.namedmaterial1") != std::string::npos,
         "diagnostic should include parameter path");
  EXPECT(diagnostic.find(
             (root / "materials" / "missing.material").generic_string()) !=
             std::string::npos,
         "diagnostic should include target resource URI");
  EXPECT(diagnostic.find("MaterialResourceParser") != std::string::npos,
         "diagnostic should include parser name");
}

void testGenericMaterialLoaderWritesDependenciesIntoCallerTable() {
  const fs::path root = makeTempRoot();
  const fs::path materialPath = root / "materials" / "loader_v2.material";
  writeFile(materialPath, R"(
schema: lxe.material.v2
bsdf:
  type: matte
  source: assets://shaders/glsl/common/materials/matte.contract.glsl
  parameters:
    Kd: { kind: texture, valueType: rgb, uri: ../textures/shared.png }
    normalmap: { kind: texture, valueType: rgb, uri: ../textures/shared.png }
    sigma: { kind: float, value: 0.0 }
)");

  SceneResourceTable table;
  auto material = LX_infra::loadGenericMaterial(materialPath, table);
  EXPECT(material != nullptr,
         "generic material loader should load v2 envelope material");
  if (!material) {
    return;
  }

  const auto &deps = material->getMaterialDependencies();
  EXPECT(deps.size() == 2,
         "loaded material should retain texture dependency handles");
  EXPECT(deps.size() == 2 && deps[0].resourceHandle.isValid() &&
             deps[0].resourceHandle == deps[1].resourceHandle,
         "same canonical texture URI should deduplicate dependency handles in "
         "the caller table");

  const auto graph = table.exportResourceGraph();
  u32 textureCount = 0;
  bool graphOwnsDependencyHandle = false;
  bool materialHasDependencyEdge = false;
  for (u32 i = 0; i < graph.resources.size(); ++i) {
    const ResourceMetadata &metadata = graph.resources[i];
    if (metadata.type == SceneResourceType::Texture &&
        metadata.uri.string().find("textures/shared.png") !=
            std::string::npos) {
      ++textureCount;
      graphOwnsDependencyHandle =
          deps.size() == 2 && graph.handles[i] == deps[0].resourceHandle;
    }
    if (metadata.type == SceneResourceType::Material &&
        metadata.uri.string().find("loader_v2.material") != std::string::npos) {
      materialHasDependencyEdge =
          metadata.dependencyHandles.size() == 1 && deps.size() == 2 &&
          metadata.dependencyHandles.front() == deps[0].resourceHandle;
    }
  }

  EXPECT(textureCount == 1,
         "caller table should contain one deduplicated texture resource");
  EXPECT(graphOwnsDependencyHandle,
         "material dependency handle should belong to caller table graph");
  EXPECT(materialHasDependencyEdge,
         "caller table material metadata should point at deduplicated texture "
         "handle");
}

void testGenericMaterialLoaderRejectsMaterialLocalTechniqueFiles() {
  const fs::path root = makeTempRoot();
  const fs::path materialPath =
      root / "materials" / "legacy_technique.material";
  writeFile(materialPath, R"(
shader: techniques/Forward/pbr
defaultTechnique: Forward
techniques:
  Forward:
    passes:
      Forward:
        shader: techniques/Forward/pbr
        stage: raster
        dispatch: draw
        sources: [geometry.vertex, geometry.index, material.bsdf, camera.ubo]
        targets: [hdr.color]
        renderState:
          cullMode: Back
          depthTest: true
          depthWrite: true
          depthOp: LessEqual
)");

  SceneResourceTable table;
  bool rejected = false;
  std::string message;
  try {
    (void)LX_infra::loadGenericMaterial(materialPath, table);
  } catch (const std::exception &error) {
    rejected = true;
    message = error.what();
  }

  EXPECT(
      rejected,
      "generic material loader should reject material-local technique files");
  EXPECT(message.find("lxe.material.v2") != std::string::npos,
         "legacy material rejection should name the required v2 schema");
}

} // namespace

int main() {
  testParserResourceDependenciesSurviveTableRegistration();
  testMixMaterialRefReadsTargetHeaderWithoutFullParse();
  testMixMaterialRefRejectsTargetMixHeader();
  testMixMaterialRefRejectsTargetHeaderWithoutSource();
  testMixMaterialRefRejectsNamedStringReference();
  testMaterialRefDiagnosticsIncludeParserAndResourceContext();
  testGenericMaterialLoaderWritesDependenciesIntoCallerTable();
  testGenericMaterialLoaderRejectsMaterialLocalTechniqueFiles();
  if (g_failures != 0) {
    std::cerr << g_failures << " material v2 dependency checks failed\n";
    return 1;
  }
  return 0;
}
