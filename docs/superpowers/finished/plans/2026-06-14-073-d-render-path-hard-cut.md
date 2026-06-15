# 073-d RenderPath Shader URI And Material Source Hard Cut Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `REQ-073-d` so realtime Material source rendering uses `render_paths/...`, deletes old `techniques/Forward|Deferred` implementation paths, rejects old URIs, and prepares pipelines explicitly after scene resources and material source variants are complete.

**Architecture:** Add one shared RenderPath shader URI resolver and make both graph resource parsing and material source variant resolution use it. Migrate Forward/Deferred shader files and default graph assets to `render_paths/...`, delete old positive tests and compatibility branches, and harden preparation with strict material-source validation and an explicit pipeline preparation phase. Keep OfflineRT `techniques/OfflineRT` out of this slice because `REQ-073-g/h` owns OfflineRT graph migration.

**Tech Stack:** C++20, CMake/Ninja, yaml-cpp, shaderc/glslc, Vulkan backend, existing LXEngine simple integration-test harness.

---

## File Structure

- Create `src/infra/resource_parsers/render_path_shader_resolver.hpp`:
  Public URI resolver interface shared by parser adapters and source variant resolver.
- Create `src/infra/resource_parsers/render_path_shader_resolver.cpp`:
  Implements `render_paths/...` lookup, allowed utility shader lookup, and legacy URI diagnostics.
- Modify `src/infra/CMakeLists.txt`:
  Add the resolver implementation to the infra library.
- Modify `src/infra/resource_parsers/render_resource_scene_parser_adapters.cpp`:
  Remove the local legacy resolver and call the shared resolver.
- Modify `src/infra/resource_parsers/material_source_variant_resolver.cpp`:
  Remove the duplicate local legacy resolver and call the shared resolver.
- Move `assets/shaders/glsl/techniques/Forward/` to `assets/shaders/glsl/render_paths/Forward/`.
- Move `assets/shaders/glsl/techniques/Deferred/` to `assets/shaders/glsl/render_paths/Deferred/`.
- Modify `assets/shaders/CMakeLists.txt` and `assets/shaders/README.md`:
  Replace the old runtime layout documentation with `render_paths/...`.
- Modify `assets/render_paths/*.render-path.yaml`:
  Use `render_paths/...` for realtime material, shadow, and deferred lighting pass shaders.
- Modify `src/demos/lxe_editor/scene_runtime.cpp`:
  Remove old runtime Forward PBR material-pass injection.
- Modify `src/infra/material_loader/generic_material_loader.hpp`:
  Remove the unused old `technique` option.
- Modify `src/backend/vulkan/vulkan_realtime_renderer.cpp`:
  Name the explicit post-load pipeline preparation phase.
- Create `src/test/integration/test_073d_render_path_hard_cut.cpp`:
  Focused 073-d audit and rejection tests.
- Modify `src/test/CMakeLists.txt`:
  Register the new test binary and `LXE_SOURCE_DIR`.
- Modify existing positive tests:
  `test_render_resource_parsers.cpp`, `test_render_path_graph_pass_contract.cpp`,
  `test_material_source_variant_pipeline.cpp`, `test_shader_compiler.cpp`,
  `test_vulkan_shader.cpp`, `test_default_forward_render_path_graph_source.cpp`,
  `test_default_deferred_render_path_graph_source.cpp`,
  `test_bindless_validation_contract.cpp`, and `test_071g_legacy_boundary_removal.cpp`.
- Modify `src/core/utils/filesystem_tools.cpp`:
  Detect current shader outputs under `render_paths/...`.

## Progress

- [ ] Task 0: Add 073-d characterization tests.
- [ ] Task 1: Add shared RenderPath shader URI resolver.
- [ ] Task 2: Migrate Forward/Deferred shader source trees.
- [ ] Task 3: Migrate RenderPathGraph assets and source-dependent tests.
- [ ] Task 4: Remove runtime material-source pass injection.
- [ ] Task 5: Add explicit pipeline preparation phase.
- [ ] Task 6: Harden strict material-source validation.
- [ ] Task 7: Delete old positive fixtures and tighten rg audits.
- [ ] Task 8: Run focused and broad verification.
- [ ] Task 9: Commit requirement implementation status update.

---

## Task 0: Add 073-d Characterization Tests

**Files:**
- Create: `src/test/integration/test_073d_render_path_hard_cut.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 0.1: Register the new test target**

Modify `src/test/CMakeLists.txt`.

Add `test_073d_render_path_hard_cut` to `TEST_INTEGRATION_EXE_LIST` immediately after `test_material_source_variant_pipeline`.

Add this compile definition block next to the other `LXE_SOURCE_DIR` blocks:

```cmake
if(TARGET test_073d_render_path_hard_cut)
  target_compile_definitions(test_073d_render_path_hard_cut
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()
```

- [ ] **Step 0.2: Write the failing 073-d test file**

Create `src/test/integration/test_073d_render_path_hard_cut.cpp` with this content:

```cpp
#include "core/asset/render_effect.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"
#include "infra/resource_parsers/render_resource_scene_parser_adapters.hpp"
#include "infra/resource_parsers/scene_resource_parser_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

#ifndef LXE_SOURCE_DIR
#define LXE_SOURCE_DIR ""
#endif

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

bool isRepoRoot(const fs::path &path) {
  return fs::exists(path / "assets/render_paths/forward_main.render-path.yaml") &&
         fs::exists(path / "src/demos/lxe_editor/scene_runtime.cpp");
}

fs::path findRepoRoot() {
  const fs::path configured{LXE_SOURCE_DIR};
  if (!configured.empty() && isRepoRoot(configured)) {
    return fs::canonical(configured);
  }
  fs::path probe = fs::absolute(__FILE__);
  for (fs::path current = probe.parent_path(); !current.empty();
       current = current.parent_path()) {
    if (isRepoRoot(current)) {
      return fs::canonical(current);
    }
    if (current == current.root_path()) {
      break;
    }
  }
  return fs::current_path();
}

std::string readTextFile(const fs::path &path) {
  std::ifstream in(path);
  EXPECT(in.is_open(), "failed to open " + path.generic_string());
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

template <typename Parsed>
bool hasDiagnosticContaining(const Parsed &parsed, std::string_view needle) {
  for (const std::string &diagnostic : parsed.diagnostics) {
    if (diagnostic.find(needle) != std::string::npos) {
      return true;
    }
  }
  for (const auto &diagnostic : parsed.metadata.diagnostics) {
    if (diagnostic.message.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

LX_core::ResourceUri writeTempGraph(const std::string &name,
                                    const std::string &contents) {
  const fs::path path = fs::temp_directory_path() / name;
  std::ofstream out(path);
  out << contents;
  return LX_core::ResourceUri("file://" + path.generic_string());
}

void testDefaultGraphAssetsUseRenderPathShaderUris(const fs::path &repoRoot) {
  const fs::path assets[] = {
      repoRoot / "assets/render_paths/forward_main.render-path.yaml",
      repoRoot / "assets/render_paths/forward_bloom.render-path.yaml",
      repoRoot / "assets/render_paths/deferred_main.render-path.yaml",
      repoRoot / "assets/render_paths/deferred_bloom.render-path.yaml",
  };
  LX_infra::RenderPathGraphResourceParser parser;
  for (const fs::path &path : assets) {
    const std::string text = readTextFile(path);
    EXPECT(text.find("techniques/") == std::string::npos,
           path.generic_string() + " must not contain techniques/ URIs");
    const auto parsed = parser.parse(path.generic_string(), text);
    EXPECT(parsed.renderPathGraph.has_value(),
           path.generic_string() + " should parse");
    if (!parsed.renderPathGraph.has_value()) {
      continue;
    }
    for (const auto &pass : parsed.renderPathGraph->passes) {
      if (pass.sources.end() != std::find(pass.sources.begin(),
                                          pass.sources.end(),
                                          "material.bsdf") ||
          pass.id == "Shadow" || pass.id == "DeferredLighting") {
        EXPECT(pass.shaderUri.string().rfind("render_paths/", 0) == 0,
               path.generic_string() + " pass " + pass.id +
                   " must use render_paths/... shader URI");
      }
    }
  }
}

void testLegacyTechniqueUriRejectedByResourceParser() {
  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  LX_core::SceneResourceTable table;
  const LX_core::ResourceUri graphUri = writeTempGraph(
      "lxe_073d_legacy_shader_uri.render-path.yaml", R"yaml(
schema: lxe.render-path-graph.v1
name: LegacyShaderUri
renderPath: Forward
passes:
  - id: Forward
    stage: raster
    dispatch: draw
    shader: techniques/Forward/pbr
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
    geometry:
      vertex: position-only
      topology: triangle-list
    sources: [geometry.vertex, geometry.index, material.bsdf, scene.camera]
    targets: [hdr.color]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)yaml");

  const auto parsed = registry.parse(
      table, LX_core::SceneResourceType::RenderPathGraph, graphUri,
      LX_infra::SceneResourceParseContext{});

  EXPECT(!parsed.identity.isValid() ||
             parsed.metadata.state == LX_core::ResourceState::Failed,
         "legacy techniques/... shader URI should fail graph resource parse");
  EXPECT(hasDiagnosticContaining(parsed, "legacy shader URI"),
         "diagnostic should identify legacy shader URI");
  EXPECT(hasDiagnosticContaining(parsed, "render_paths/"),
         "diagnostic should name expected render_paths/... namespace");
  EXPECT(hasDiagnosticContaining(parsed, "techniques/Forward/pbr"),
         "diagnostic should include rejected URI");
}

void testRenderPathShaderUriResolvesThroughResourceParser() {
  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  LX_core::SceneResourceTable table;
  const LX_core::ResourceUri graphUri = writeTempGraph(
      "lxe_073d_render_path_shader_uri.render-path.yaml", R"yaml(
schema: lxe.render-path-graph.v1
name: RenderPathShaderUri
renderPath: Forward
passes:
  - id: Shadow
    stage: raster
    dispatch: draw
    shader: render_paths/Forward/shadow_depth_only
    rendering:
      mode: dynamic
      attachments:
        - target: shadow.main
          format: D32Float
          samples: 1
          layers: 1
          depth: true
    geometry:
      vertex: position-only
      topology: triangle-list
    sources: [geometry.vertex, geometry.index, scene.camera]
    targets: [shadow.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)yaml");

  const auto parsed = registry.parse(
      table, LX_core::SceneResourceType::RenderPathGraph, graphUri,
      LX_infra::SceneResourceParseContext{});

  EXPECT(parsed.diagnostics.empty(),
         "render_paths/... shader URI should resolve without diagnostics");
  EXPECT(parsed.identity.isValid(),
         "render_paths/... graph should register successfully");
  EXPECT(table.shaderCount() == 1,
         "render_paths/... graph should register one shader descriptor");
}

void testOldForwardDeferredShaderDirectoriesRemoved(const fs::path &repoRoot) {
  EXPECT(!fs::exists(repoRoot / "assets/shaders/glsl/techniques/Forward"),
         "old techniques/Forward shader directory must be deleted");
  EXPECT(!fs::exists(repoRoot / "assets/shaders/glsl/techniques/Deferred"),
         "old techniques/Deferred shader directory must be deleted");
  EXPECT(fs::exists(repoRoot / "assets/shaders/glsl/render_paths/Forward"),
         "render_paths/Forward shader directory must exist");
  EXPECT(fs::exists(repoRoot / "assets/shaders/glsl/render_paths/Deferred"),
         "render_paths/Deferred shader directory must exist");
}

void testRuntimeDoesNotInjectOldForwardMaterialPass(const fs::path &repoRoot) {
  const std::string sceneRuntime =
      readTextFile(repoRoot / "src/demos/lxe_editor/scene_runtime.cpp");
  const std::string genericMaterialLoader =
      readTextFile(repoRoot / "src/infra/material_loader/generic_material_loader.hpp");
  EXPECT(sceneRuntime.find("ensureRealtimeForwardSurfacePass") ==
             std::string::npos,
         "scene runtime must not inject old realtime Forward material passes");
  EXPECT(sceneRuntime.find("techniques/Forward/pbr") == std::string::npos,
         "scene runtime must not compile old techniques/Forward/pbr shaders");
  EXPECT(sceneRuntime.find("|technique=") == std::string::npos,
         "scene runtime material cache key must not use old technique terms");
  EXPECT(genericMaterialLoader.find("technique") == std::string::npos,
         "GenericMaterialLoadOptions must not expose old technique option");
}

void testProductionOldTokenAudit(const fs::path &repoRoot) {
  const struct Root final {
    const char *path;
  } roots[] = {
      {"assets/render_paths"},
      {"assets/shaders"},
      {"src/core"},
      {"src/infra/resource_parsers"},
      {"src/demos/lxe_editor"},
      {"src/backend/vulkan"},
  };
  const std::string forbidden[] = {
      "techniques/Forward",
      "techniques/Deferred",
      "defaultTechnique",
      "MaterialTechnique",
      "MaterialTechniqueSet",
  };
  for (const Root &root : roots) {
    const fs::path absolute = repoRoot / root.path;
    if (!fs::exists(absolute)) {
      continue;
    }
    for (fs::recursive_directory_iterator it(absolute), end; it != end; ++it) {
      if (!it->is_regular_file()) {
        continue;
      }
      const std::string ext = it->path().extension().string();
      if (ext != ".cpp" && ext != ".hpp" && ext != ".yaml" &&
          ext != ".yml" && ext != ".glsl" && ext != ".frag" &&
          ext != ".vert" && ext != ".md" && ext != ".txt") {
        continue;
      }
      const std::string text = readTextFile(it->path());
      for (std::string_view token : forbidden) {
        EXPECT(text.find(token) == std::string::npos,
               fs::relative(it->path(), repoRoot).generic_string() +
                   " contains forbidden old token " + std::string(token));
      }
    }
  }
}

} // namespace

int main() {
  const fs::path repoRoot = findRepoRoot();
  testDefaultGraphAssetsUseRenderPathShaderUris(repoRoot);
  testLegacyTechniqueUriRejectedByResourceParser();
  testRenderPathShaderUriResolvesThroughResourceParser();
  testOldForwardDeferredShaderDirectoriesRemoved(repoRoot);
  testRuntimeDoesNotInjectOldForwardMaterialPass(repoRoot);
  testProductionOldTokenAudit(repoRoot);
  if (g_failures != 0) {
    std::cerr << g_failures << " 073-d render path hard-cut checks failed\n";
    return 1;
  }
  return 0;
}
```

- [ ] **Step 0.3: Build and run the new test to verify it fails**

Run:

```bash
cmake --build build --target test_073d_render_path_hard_cut
./build/src/test/test_073d_render_path_hard_cut
```

Expected: FAIL. The failure should mention current `techniques/...` graph assets, existing old shader directories, current resolver behavior, or runtime pass injection.

- [ ] **Step 0.4: Commit the failing characterization test**

Run:

```bash
git add src/test/CMakeLists.txt src/test/integration/test_073d_render_path_hard_cut.cpp
git commit -m "Add 073-d render path hard cut tests"
```

---

## Task 1: Add Shared RenderPath Shader URI Resolver

**Files:**
- Create: `src/infra/resource_parsers/render_path_shader_resolver.hpp`
- Create: `src/infra/resource_parsers/render_path_shader_resolver.cpp`
- Modify: `src/infra/CMakeLists.txt`
- Modify: `src/infra/resource_parsers/render_resource_scene_parser_adapters.cpp`
- Modify: `src/infra/resource_parsers/material_source_variant_resolver.cpp`

- [ ] **Step 1.1: Add the resolver header**

Create `src/infra/resource_parsers/render_path_shader_resolver.hpp`:

```cpp
#pragma once

#include "core/resource/resource_uri.hpp"

#include <string>
#include <vector>

namespace LX_infra {

struct RenderPathShaderSourceResolveResult final {
  std::vector<LX_core::ResourceUri> sourceUris;
  std::vector<std::string> diagnostics;

  [[nodiscard]] bool success() const {
    return !sourceUris.empty() && diagnostics.empty();
  }
};

[[nodiscard]] RenderPathShaderSourceResolveResult
resolveRenderPathShaderSourceUris(const LX_core::ResourceUri &graphUri,
                                  const std::string &passId,
                                  const LX_core::ResourceUri &shaderUri);

} // namespace LX_infra
```

- [ ] **Step 1.2: Add the resolver implementation**

Create `src/infra/resource_parsers/render_path_shader_resolver.cpp`:

```cpp
#include "infra/resource_parsers/render_path_shader_resolver.hpp"

#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

namespace LX_infra {
namespace {

[[nodiscard]] std::filesystem::path pathFromUri(const LX_core::ResourceUri &uri) {
  const std::string &text = uri.string();
  constexpr std::string_view assetsPrefix = "assets://";
  constexpr std::string_view filePrefix = "file://";
  if (text.rfind(assetsPrefix, 0) == 0) {
    return std::filesystem::path("assets") / text.substr(assetsPrefix.size());
  }
  if (text.rfind(filePrefix, 0) == 0) {
    return std::filesystem::path(text.substr(filePrefix.size()));
  }
  return std::filesystem::path(text);
}

[[nodiscard]] bool resourceFileExists(const LX_core::ResourceUri &uri) {
  return std::filesystem::exists(pathFromUri(uri));
}

[[nodiscard]] LX_core::ResourceUri makeShaderUri(const std::string &path) {
  return LX_core::ResourceUri("assets/shaders/glsl/" + path);
}

[[nodiscard]] bool isAllowedRootUtilityShader(std::string_view shader) {
  return shader == "post_process" || shader == "debug_overlay" ||
         shader == "bloom_threshold" || shader == "bloom_blur_h" ||
         shader == "bloom_blur_v" || shader == "skybox";
}

[[nodiscard]] std::string resolverPrefix(const LX_core::ResourceUri &graphUri,
                                         const std::string &passId,
                                         const LX_core::ResourceUri &shaderUri) {
  std::ostringstream out;
  out << "RenderPathGraph '" << graphUri.string() << "' pass '" << passId
      << "' shader '" << shaderUri.string() << "'";
  return out.str();
}

void addMissingDiagnostic(RenderPathShaderSourceResolveResult &result,
                          const LX_core::ResourceUri &graphUri,
                          const std::string &passId,
                          const LX_core::ResourceUri &shaderUri,
                          const std::string &base) {
  result.diagnostics.push_back(
      resolverPrefix(graphUri, passId, shaderUri) +
      " failed to resolve shader source; searched assets/shaders/glsl/" +
      base + ".vert, " + base + ".frag, and " + base + ".comp");
}

[[nodiscard]] bool appendStagePair(RenderPathShaderSourceResolveResult &result,
                                   const std::string &base) {
  const LX_core::ResourceUri vert = makeShaderUri(base + ".vert");
  const LX_core::ResourceUri frag = makeShaderUri(base + ".frag");
  if (!resourceFileExists(vert) || !resourceFileExists(frag)) {
    return false;
  }
  result.sourceUris = {vert, frag};
  return true;
}

[[nodiscard]] bool appendComputeStage(RenderPathShaderSourceResolveResult &result,
                                      const std::string &base) {
  const LX_core::ResourceUri comp = makeShaderUri(base + ".comp");
  if (!resourceFileExists(comp)) {
    return false;
  }
  result.sourceUris = {comp};
  return true;
}

} // namespace

RenderPathShaderSourceResolveResult
resolveRenderPathShaderSourceUris(const LX_core::ResourceUri &graphUri,
                                  const std::string &passId,
                                  const LX_core::ResourceUri &shaderUri) {
  RenderPathShaderSourceResolveResult result;
  const std::string shader = shaderUri.string();

  if (shader.rfind("techniques/", 0) == 0) {
    result.diagnostics.push_back(
        resolverPrefix(graphUri, passId, shaderUri) +
        " rejected legacy shader URI; expected render_paths/... and no "
        "techniques/... compatibility alias; resolver search path is "
        "assets/shaders/glsl/render_paths/");
    return result;
  }

  if (shader.rfind("render_paths/", 0) == 0) {
    if (appendStagePair(result, shader) || appendComputeStage(result, shader)) {
      return result;
    }
    addMissingDiagnostic(result, graphUri, passId, shaderUri, shader);
    return result;
  }

  if (isAllowedRootUtilityShader(shader)) {
    if (appendStagePair(result, shader) || appendComputeStage(result, shader)) {
      return result;
    }
    addMissingDiagnostic(result, graphUri, passId, shaderUri, shader);
    return result;
  }

  const LX_core::ResourceUri directUri =
      shader.rfind("assets/shaders/glsl/", 0) == 0
          ? LX_core::ResourceUri(shader)
          : makeShaderUri(shader);
  if (resourceFileExists(directUri)) {
    result.sourceUris = {directUri};
    return result;
  }

  result.diagnostics.push_back(
      resolverPrefix(graphUri, passId, shaderUri) +
      " uses unsupported shader URI form; expected render_paths/... for "
      "RenderPath shaders or an allowed root-level utility shader");
  return result;
}

} // namespace LX_infra
```

- [ ] **Step 1.3: Add the resolver source to the infra library**

In `src/infra/CMakeLists.txt`, add this line in the resource parser source list, next to `material_source_variant_resolver.cpp`:

```cmake
    resource_parsers/render_path_shader_resolver.cpp
```

- [ ] **Step 1.4: Replace the parser-adapter local resolver**

In `src/infra/resource_parsers/render_resource_scene_parser_adapters.cpp`:

Add include:

```cpp
#include "infra/resource_parsers/render_path_shader_resolver.hpp"
```

Delete the local `resourceFileExists(...)` function and the local `resolveShaderSourceUris(...)` function.

In `parseRenderPathGraphIntoTable(...)`, replace:

```cpp
const auto shaderSourceUris = resolveShaderSourceUris(pass.shaderUri);
if (!shaderSourceUris.has_value()) {
```

with:

```cpp
const RenderPathShaderSourceResolveResult shaderSourceUris =
    resolveRenderPathShaderSourceUris(canonicalUri, pass.id, pass.shaderUri);
if (!shaderSourceUris.success()) {
```

In the failure metadata block, copy all resolver diagnostics:

```cpp
for (const std::string &diagnostic : shaderSourceUris.diagnostics) {
  failedShader.diagnostics.push_back(LX_core::ResourceDiagnostic{
      .ownerUri = canonicalUri,
      .resourceUri = pass.shaderUri,
      .parserName = kRenderPathGraphParserName,
      .message = diagnostic,
  });
}
```

Return `makeFailedParse(...)` with `shaderSourceUris.diagnostics` rather than the generic old missing message:

```cpp
return makeFailedParse(
    table, LX_core::SceneResourceType::RenderPathGraph, context.ownerUri,
    canonicalUri, kRenderPathGraphParserName, shaderSourceUris.diagnostics);
```

Replace later uses of `*shaderSourceUris` with `shaderSourceUris.sourceUris`.

- [ ] **Step 1.5: Replace the material-source resolver local resolver**

In `src/infra/resource_parsers/material_source_variant_resolver.cpp`:

Add include:

```cpp
#include "infra/resource_parsers/render_path_shader_resolver.hpp"
```

Delete the local `resourceFileExists(...)` function and the local `resolveShaderSourceUris(...)` function.

In `resolveMaterialSourceVariants(...)`, replace:

```cpp
const auto sourceUris = resolveShaderSourceUris(pass.shaderUri);
if (!sourceUris.has_value()) {
  result.diagnostics.push_back(
      "MaterialSourceVariantResolver graph=" + graphUri.string() +
      " pass=" + pass.id + " failed to resolve Shader '" +
      pass.shaderUri.string() + "'");
  return result;
}
```

with:

```cpp
const RenderPathShaderSourceResolveResult sourceUris =
    resolveRenderPathShaderSourceUris(graphUri, pass.id, pass.shaderUri);
if (!sourceUris.success()) {
  result.diagnostics.insert(result.diagnostics.end(),
                            sourceUris.diagnostics.begin(),
                            sourceUris.diagnostics.end());
  return result;
}
```

Replace later uses of `*sourceUris` with `sourceUris.sourceUris`.

- [ ] **Step 1.6: Build and run resolver tests**

Run:

```bash
cmake --build build --target test_073d_render_path_hard_cut test_render_resource_parsers test_material_source_variant_pipeline
./build/src/test/test_073d_render_path_hard_cut
./build/src/test/test_render_resource_parsers
./build/src/test/test_material_source_variant_pipeline
```

Expected now: the new legacy URI rejection diagnostic passes, but the suite still fails until assets and shader files are migrated.

- [ ] **Step 1.7: Commit the shared resolver**

Run:

```bash
git add src/infra/CMakeLists.txt \
  src/infra/resource_parsers/render_path_shader_resolver.hpp \
  src/infra/resource_parsers/render_path_shader_resolver.cpp \
  src/infra/resource_parsers/render_resource_scene_parser_adapters.cpp \
  src/infra/resource_parsers/material_source_variant_resolver.cpp
git commit -m "Add RenderPath shader URI resolver"
```

---

## Task 2: Migrate Forward/Deferred Shader Source Trees

**Files:**
- Move: `assets/shaders/glsl/techniques/Forward/` to `assets/shaders/glsl/render_paths/Forward/`
- Move: `assets/shaders/glsl/techniques/Deferred/` to `assets/shaders/glsl/render_paths/Deferred/`
- Modify: `assets/shaders/CMakeLists.txt`
- Modify: `assets/shaders/README.md`
- Modify: `src/core/utils/filesystem_tools.cpp`

- [ ] **Step 2.1: Move shader directories with git**

Run:

```bash
mkdir -p assets/shaders/glsl/render_paths
git mv assets/shaders/glsl/techniques/Forward assets/shaders/glsl/render_paths/Forward
git mv assets/shaders/glsl/techniques/Deferred assets/shaders/glsl/render_paths/Deferred
```

Expected:

```bash
test -d assets/shaders/glsl/render_paths/Forward
test -d assets/shaders/glsl/render_paths/Deferred
test ! -d assets/shaders/glsl/techniques/Forward
test ! -d assets/shaders/glsl/techniques/Deferred
```

- [ ] **Step 2.2: Update shader build comments**

In `assets/shaders/CMakeLists.txt`, replace the opening runtime asset layout comment with:

```cmake
# Runtime asset layout:
#   assets/shaders/glsl/render_paths/Forward/<shaderName>.vert
#   assets/shaders/glsl/render_paths/Forward/<shaderName>.frag
#   <build>/assets/shaders/glsl/render_paths/Forward/<shaderName>.vert.spv
#   <build>/assets/shaders/glsl/render_paths/Forward/<shaderName>.frag.spv
```

- [ ] **Step 2.3: Update shader README**

In `assets/shaders/README.md`, replace references to `assets/shaders/glsl/techniques/Forward` and `assets/shaders/glsl/techniques/Deferred` with `assets/shaders/glsl/render_paths/Forward` and `assets/shaders/glsl/render_paths/Deferred`.

Keep any `techniques/OfflineRT` text only if the sentence explicitly says OfflineRT migration is owned by `REQ-073-g/h`.

- [ ] **Step 2.4: Update runtime shader-output discovery**

In `src/core/utils/filesystem_tools.cpp`, change `hasCurrentShaderOutputsAtRoot(...)` to:

```cpp
bool hasCurrentShaderOutputsAtRoot(const fs::path &root) {
  return hasShaderOutputsAtRoot(root, "render_paths/Forward/pbr") &&
         hasShaderOutputsAtRoot(root, "post_process") &&
         hasShaderOutputsAtRoot(root,
                                "render_paths/Deferred/deferred_lighting") &&
         hasShaderOutputsAtRoot(root, "render_paths/Deferred/pbr_gbuffer");
}
```

- [ ] **Step 2.5: Build shader targets**

Run:

```bash
cmake --build build --target CompileShaders CompileMaterialSourceShaderVariants
```

Expected: shader compilation now writes default realtime Forward/Deferred outputs under `build/assets/shaders/glsl/render_paths/...`.

- [ ] **Step 2.6: Commit shader tree migration**

Run:

```bash
git add assets/shaders src/core/utils/filesystem_tools.cpp
git commit -m "Migrate realtime shaders to render_paths"
```

---

## Task 3: Migrate RenderPathGraph Assets And Positive Tests

**Files:**
- Modify: `assets/render_paths/forward_main.render-path.yaml`
- Modify: `assets/render_paths/forward_bloom.render-path.yaml`
- Modify: `assets/render_paths/deferred_main.render-path.yaml`
- Modify: `assets/render_paths/deferred_bloom.render-path.yaml`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`
- Modify: `src/test/integration/test_render_path_graph_pass_contract.cpp`
- Modify: `src/test/integration/test_material_source_variant_pipeline.cpp`
- Modify: `src/test/integration/test_vulkan_shader.cpp`
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 3.1: Update default graph assets**

Apply these shader URI replacements in all four files under `assets/render_paths/`:

```text
techniques/Forward/shadow_depth_only -> render_paths/Forward/shadow_depth_only
techniques/Forward/pbr -> render_paths/Forward/pbr
techniques/Deferred/gbuffer -> render_paths/Deferred/pbr_gbuffer
techniques/Deferred/pbr_gbuffer -> render_paths/Deferred/pbr_gbuffer
deferred_lighting -> render_paths/Deferred/deferred_lighting
```

Leave root-level utility shader URIs unchanged for these non-material fullscreen/debug passes:

```text
post_process
debug_overlay
bloom_threshold
bloom_blur_h
bloom_blur_v
```

- [ ] **Step 3.2: Update render resource parser positive fixtures**

In `src/test/integration/test_render_resource_parsers.cpp`, rename `testParserAdapterResolvesCurrentGraphShaderUriForms()` to:

```cpp
void testParserAdapterResolvesRenderPathShaderUriForms()
```

Within that test, replace the positive shader URIs with:

```yaml
shader: render_paths/Forward/pbr
shader: render_paths/Deferred/pbr_gbuffer
shader: render_paths/Deferred/deferred_lighting
```

Keep root utility shader fixtures as:

```yaml
shader: post_process
shader: bloom_threshold
shader: bloom_blur_h
shader: bloom_blur_v
```

Update the call in `main()`:

```cpp
testParserAdapterResolvesRenderPathShaderUriForms();
```

- [ ] **Step 3.3: Update RenderPathGraph pass contract tests**

In `src/test/integration/test_render_path_graph_pass_contract.cpp`, replace all positive `shader: techniques/Forward/surface_lit` fixture lines with:

```yaml
shader: render_paths/Forward/surface_lit
```

Update the retained assertion:

```cpp
EXPECT(pass.shaderUri == "render_paths/Forward/surface_lit",
       "shader uri should be retained");
```

The file does not need an actual `surface_lit` shader because this parser-only test does not resolve shader files.

- [ ] **Step 3.4: Update material source variant tests**

In `src/test/integration/test_material_source_variant_pipeline.cpp`, replace:

```text
techniques/Forward/pbr -> render_paths/Forward/pbr
techniques/Forward/shadow_depth_only -> render_paths/Forward/shadow_depth_only
```

Replace source-path checks:

```cpp
LXE_SOURCE_DIR / "assets" / "shaders" / "glsl" /
"techniques" / "Forward" / "pbr.frag"
```

with:

```cpp
LXE_SOURCE_DIR / "assets" / "shaders" / "glsl" /
"render_paths" / "Forward" / "pbr.frag"
```

Update `ShaderResourceMetadata` URI assertions from:

```cpp
LX_core::ResourceUri("techniques/Forward/pbr")
```

to:

```cpp
LX_core::ResourceUri("render_paths/Forward/pbr")
```

- [ ] **Step 3.5: Update Vulkan shader smoke test**

In `src/test/integration/test_vulkan_shader.cpp`, replace both `VulkanShader::create(...)` calls:

```cpp
auto vertShader = LX_core::backend::VulkanShader::create(
    *device, "render_paths/Forward/pbr", VK_SHADER_STAGE_VERTEX_BIT);
auto fragShader = LX_core::backend::VulkanShader::create(
    *device, "render_paths/Forward/pbr", VK_SHADER_STAGE_FRAGMENT_BIT);
```

- [ ] **Step 3.6: Update shader compiler tests**

In `src/test/integration/test_shader_compiler.cpp`, apply these replacements outside OfflineRT tests:

```text
shaderDir / "techniques" / "Forward" -> shaderDir / "render_paths" / "Forward"
shaderDir / "techniques" / "Deferred" -> shaderDir / "render_paths" / "Deferred"
```

Do not replace:

```text
shaderDir / "techniques" / "OfflineRT"
```

because OfflineRT migration belongs to `REQ-073-g/h`.

Update user-facing failure text that says `techniques` for Forward/Deferred to say `render_paths`.

- [ ] **Step 3.7: Run focused parser and shader tests**

Run:

```bash
cmake --build build --target \
  test_073d_render_path_hard_cut \
  test_render_resource_parsers \
  test_render_path_graph_pass_contract \
  test_material_source_variant_pipeline \
  test_vulkan_shader \
  test_shader_compiler
./build/src/test/test_073d_render_path_hard_cut
./build/src/test/test_render_resource_parsers
./build/src/test/test_render_path_graph_pass_contract
./build/src/test/test_material_source_variant_pipeline
./build/src/test/test_shader_compiler
```

For `test_vulkan_shader`, use xvfb because it is labeled `requires_video_device`:

```bash
xvfb-run -a ./build/src/test/test_vulkan_shader
```

Expected: tests pass except failures caused by runtime material pass injection and rg audits, which are handled in later tasks.

- [ ] **Step 3.8: Commit asset and test URI migration**

Run:

```bash
git add assets/render_paths src/test/integration/test_render_resource_parsers.cpp \
  src/test/integration/test_render_path_graph_pass_contract.cpp \
  src/test/integration/test_material_source_variant_pipeline.cpp \
  src/test/integration/test_vulkan_shader.cpp \
  src/test/integration/test_shader_compiler.cpp
git commit -m "Migrate RenderPath graph tests to render_paths"
```

---

## Task 4: Remove Runtime Material-Source Pass Injection

**Files:**
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Modify: `src/infra/material_loader/generic_material_loader.hpp`
- Modify: `src/infra/material_loader/generic_material_loader.cpp`

- [ ] **Step 4.1: Remove unused technique option from generic material loader**

In `src/infra/material_loader/generic_material_loader.hpp`, replace `GenericMaterialLoadOptions` with:

```cpp
struct GenericMaterialLoadOptions final {
  std::optional<bool> forceIbl;
  std::optional<bool> alphaTransparency;
};
```

In `src/infra/material_loader/generic_material_loader.cpp`, keep:

```cpp
(void)options;
```

No other code in this file should mention `technique`.

- [ ] **Step 4.2: Remove old realtime shader cache and helper declarations**

In `src/demos/lxe_editor/scene_runtime.cpp`, delete:

```cpp
std::unordered_map<std::string, LX_core::IShaderSharedPtr>
    g_realtimeSurfaceShaders;
```

Delete these functions completely:

```cpp
realtimeSurfaceShaderVariants(...)
realtimeSurfaceShaderCacheKey(...)
loadRealtimeForwardSurfaceShader(...)
ensureRealtimeForwardSurfacePass(...)
```

Remove now-unused includes:

```cpp
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"
```

- [ ] **Step 4.3: Remove technique assignment in current material load options**

In `currentGenericMaterialLoadOptions()`, keep only IBL and alpha settings:

```cpp
[[nodiscard]] LX_infra::GenericMaterialLoadOptions
currentGenericMaterialLoadOptions() {
  LX_infra::GenericMaterialLoadOptions options;
  if (g_sceneRealtimeRenderSettings.has_value()) {
    const auto &settings = g_sceneRealtimeRenderSettings->get();
    options.forceIbl = settings.ibl;
    options.alphaTransparency = settings.alphaTransparency;
  }
  return options;
}
```

- [ ] **Step 4.4: Remove pass injection calls**

In `loadCachedGenericMaterial(...)`, delete both calls:

```cpp
ensureRealtimeForwardSurfacePass(material, options);
ensureRealtimeForwardSurfacePass(prototype, options);
```

Keep texture dependency resolution:

```cpp
resolveMaterialTextureDependencies(*material, resourceTable);
resolveMaterialTextureDependencies(*prototype, resourceTable);
```

- [ ] **Step 4.5: Remove technique from material cache key**

In `materialCacheKey(...)`, delete:

```cpp
if (options.technique.has_value()) {
  key += "|technique=" + *options.technique;
}
```

- [ ] **Step 4.6: Run runtime gate test**

Run:

```bash
cmake --build build --target test_073d_render_path_hard_cut test_gltf_scene_asset_loader test_material_v2_resource_dependencies
./build/src/test/test_073d_render_path_hard_cut
./build/src/test/test_gltf_scene_asset_loader
./build/src/test/test_material_v2_resource_dependencies
```

Expected: the runtime injection checks in `test_073d_render_path_hard_cut` pass.

- [ ] **Step 4.7: Commit runtime gate removal**

Run:

```bash
git add src/demos/lxe_editor/scene_runtime.cpp \
  src/infra/material_loader/generic_material_loader.hpp \
  src/infra/material_loader/generic_material_loader.cpp
git commit -m "Remove legacy realtime material pass injection"
```

---

## Task 5: Add Explicit Pipeline Preparation Phase

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/test/integration/test_073d_render_path_hard_cut.cpp`

- [ ] **Step 5.1: Add source-scan coverage for the named phase**

In `testRuntimeDoesNotInjectOldForwardMaterialPass(...)` in `src/test/integration/test_073d_render_path_hard_cut.cpp`, append:

```cpp
const std::string renderer =
    readTextFile(repoRoot / "src/backend/vulkan/vulkan_realtime_renderer.cpp");
EXPECT(renderer.find("preparePipelinesForLoadedScene") != std::string::npos,
       "renderer should name the explicit post-load pipeline preparation phase");
EXPECT(renderer.find("collectAllPipelineBuildDescs") != std::string::npos,
       "renderer should collect stable PipelineBuildDesc values before "
       "pipeline preparation");
```

Run:

```bash
cmake --build build --target test_073d_render_path_hard_cut
./build/src/test/test_073d_render_path_hard_cut
```

Expected: FAIL until the renderer has the named phase.

- [ ] **Step 5.2: Add the named preparation helper**

In `src/backend/vulkan/vulkan_realtime_renderer.cpp`, add this private method in the renderer class near the existing frame graph setup helpers:

```cpp
void preparePipelinesForLoadedScene() {
  const std::vector<LX_core::PipelineBuildDesc> pipelineDescs =
      m_frameGraph.collectAllPipelineBuildDescs();
  resourceManager().preloadPipelines(pipelineDescs);
}
```

Replace the existing inline preload block:

```cpp
// Pre-build every pipeline the scene needs. Runtime cache misses still
// work via getOrCreatePipeline(item) but emit a warning log.
auto infos = m_frameGraph.collectAllPipelineBuildDescs();
resourceManager().preloadPipelines(infos);
```

with:

```cpp
// Explicit pipeline preparation happens only after scene resources, material
// source variants, FrameGraph queues, upload resources, and final shader
// reflection are ready. Future pipeline cache package loading belongs inside
// this phase and must validate the same PipelineBuildDesc identities.
preparePipelinesForLoadedScene();
```

- [ ] **Step 5.3: Run the named preparation test**

Run:

```bash
cmake --build build --target test_073d_render_path_hard_cut test_pipeline_build_info test_pipeline_cache
./build/src/test/test_073d_render_path_hard_cut
./build/src/test/test_pipeline_build_info
./build/src/test/test_pipeline_cache
```

Expected: PASS.

- [ ] **Step 5.4: Commit pipeline preparation naming**

Run:

```bash
git add src/backend/vulkan/vulkan_realtime_renderer.cpp \
  src/test/integration/test_073d_render_path_hard_cut.cpp
git commit -m "Name post-load pipeline preparation phase"
```

---

## Task 6: Harden Strict Material-Source Validation

**Files:**
- Modify: `src/core/frame_graph/render_validation_contract.cpp`
- Modify: `src/test/integration/test_bindless_validation_contract.cpp`

- [ ] **Step 6.1: Add validation tests for final identities and typed indices**

In `src/test/integration/test_bindless_validation_contract.cpp`, add this test helper after `makeMigratedDraw(...)`:

```cpp
struct ShaderWithBindings final : IShader {
  explicit ShaderWithBindings(std::vector<ShaderResourceBinding> bindings)
      : m_bindings(std::move(bindings)) {}

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }
  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const auto &candidate : m_bindings) {
      if (candidate.set == set && candidate.binding == binding) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const auto &candidate : m_bindings) {
      if (candidate.name == name) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }
  usize getProgramHash() const override { return 73u; }

  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

ShaderResourceBinding storageBinding(const char *name) {
  ShaderResourceBinding binding;
  binding.name = name;
  binding.type = ShaderPropertyType::StorageBuffer;
  return binding;
}
```

Add these tests before `main()`:

```cpp
void testMaterialV2StrictRejectsMissingFinalIdentity() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  RenderWorkQueue queue;
  RenderWorkItem item = makeMigratedDraw(vertex, index);
  item.shaderInfo = std::make_shared<ShaderWithBindings>(
      std::vector<ShaderResourceBinding>{storageBinding("SceneMaterials")});
  item.materialTypeVariant = StringID{};
  item.pipelineKey = PipelineKey{};
  item.raster.materialIndex = 0;
  item.raster.drawRecordIndex = 0;
  queue.addItem(std::move(item));

  const MaterialV2ValidationResult result =
      validateMaterialV2StrictQueue(queue, StringID("Forward"));
  EXPECT(!result.ok,
         "strict Material source validation should reject missing final "
         "MaterialTypeVariant/PipelineKey identity");
}

void testMaterialV2StrictRejectsMissingTypedSourceRef() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  RenderWorkQueue queue;
  RenderWorkItem item = makeMigratedDraw(vertex, index);
  item.shaderInfo = std::make_shared<ShaderWithBindings>(
      std::vector<ShaderResourceBinding>{storageBinding("SceneMaterialRefs"),
                                         storageBinding("SceneDraws")});
  item.raster.drawRecordIndex = 0;
  item.raster.materialRefIndex = u32_max;
  queue.addItem(std::move(item));

  const MaterialV2ValidationResult result =
      validateMaterialV2StrictQueue(queue, StringID("Forward"));
  EXPECT(!result.ok,
         "strict Material source validation should reject missing typed source "
         "material ref index");
}
```

Call both tests in `main()`:

```cpp
testMaterialV2StrictRejectsMissingFinalIdentity();
testMaterialV2StrictRejectsMissingTypedSourceRef();
```

Run:

```bash
cmake --build build --target test_bindless_validation_contract
./build/src/test/test_bindless_validation_contract
```

Expected: FAIL until validation is hardened.

- [ ] **Step 6.2: Harden validation**

In `src/core/frame_graph/render_validation_contract.cpp`, add checks inside `validateMaterialV2StrictQueue(...)` after `if (item.kind != RenderWorkKind::RasterDraw) continue;`:

```cpp
if (!item.shaderInfo) {
  addMaterialV2Diagnostic(result, i, item, pass, StringID("FinalShader"),
                          "Material v2 validation requires final shader "
                          "reflection");
}
if (item.materialTypeVariant.id == 0) {
  addMaterialV2Diagnostic(result, i, item, pass,
                          StringID("MaterialTypeVariant"),
                          "Material v2 validation requires final "
                          "MaterialTypeVariant");
}
if (item.renderPathNodeSignature.id == 0) {
  addMaterialV2Diagnostic(result, i, item, pass,
                          StringID("RenderPathNodeSignature"),
                          "Material v2 validation requires "
                          "RenderPathNodeSignature");
}
if (item.pipelineKey.id.id == 0) {
  addMaterialV2Diagnostic(result, i, item, pass, StringID("PipelineKey"),
                          "Material v2 validation requires PipelineKey built "
                          "from material type variant and RenderPathNode "
                          "signature");
}
```

Replace the existing source-ref check:

```cpp
if (!shaderConsumesBinding(item.shaderInfo, "SceneMaterials") &&
    item.raster.materialRefIndex == u32_max) {
```

with:

```cpp
if (shaderConsumesBinding(item.shaderInfo, "SceneMaterialRefs") &&
    item.raster.materialRefIndex == u32_max) {
```

Keep the existing `SceneMaterials` and `SceneDraws` checks.

- [ ] **Step 6.3: Run validation tests**

Run:

```bash
cmake --build build --target test_bindless_validation_contract test_073d_render_path_hard_cut
./build/src/test/test_bindless_validation_contract
./build/src/test/test_073d_render_path_hard_cut
```

Expected: PASS.

- [ ] **Step 6.4: Commit validation hardening**

Run:

```bash
git add src/core/frame_graph/render_validation_contract.cpp \
  src/test/integration/test_bindless_validation_contract.cpp
git commit -m "Harden material source preparation validation"
```

---

## Task 7: Delete Old Positive Fixtures And Tighten rg Audits

**Files:**
- Modify: `src/test/integration/test_071g_legacy_boundary_removal.cpp`
- Modify: `src/test/integration/test_073d_render_path_hard_cut.cpp`
- Modify: `notes/requirements/073-d-render-path-shader-uri-migration-and-terminology-hard-cut.md`

- [ ] **Step 7.1: Tighten old-token allowlists**

In `src/test/integration/test_071g_legacy_boundary_removal.cpp`, do not allow `techniques/Forward` or `techniques/Deferred` as positive fixture strings.

If the file has broad test allowlist entries for old technique files, remove those entries and keep only inline rejection/audit tests. The retained old-token mentions should be comments or strings inside named rejection tests.

Add these tokens to the production forbidden token list:

```cpp
{"techniques/Forward"},
{"techniques/Deferred"},
{"GenericMaterialLoadOptions final {\n  std::optional<bool> forceIbl;\n  std::optional<bool> alphaTransparency;\n  std::optional<std::string> technique;"},
```

If the third multi-line token is awkward in this test harness, use this single-line token instead:

```cpp
{"std::optional<std::string> technique"},
```

- [ ] **Step 7.2: Update the 073-d requirement implementation status**

In `notes/requirements/073-d-render-path-shader-uri-migration-and-terminology-hard-cut.md`, change the status section from:

```markdown
## 实施状态

未实施。
```

to:

```markdown
## 实施状态

实施中。Superpowers 设计见
`docs/superpowers/specs/2026-06-14-073-d-render-path-shader-uri-material-source-hard-cut-design.md`，
实施计划见
`docs/superpowers/plans/2026-06-14-073-d-render-path-hard-cut.md`。

本轮硬切要求删除旧 `techniques/Forward` / `techniques/Deferred` 实现、
resolver alias、runtime pass 注入 helper 和旧正向测试；只保留窄负测中的内联旧
URI 字符串，用来证明当前 parser 会拒绝旧路径。
```

- [ ] **Step 7.3: Run rg audits manually**

Run these exact commands:

```bash
rg -n "techniques/Forward|techniques/Deferred|defaultTechnique|MaterialTechnique|MaterialTechniqueSet|std::optional<std::string> technique" \
  assets src/core src/infra src/backend src/demos src/test
```

Expected: only named rejection/audit tests may mention `techniques/Forward` or `techniques/Deferred`. There must be no production hits.

Run:

```bash
find assets/shaders/glsl -maxdepth 3 -type d | sort
```

Expected includes:

```text
assets/shaders/glsl/render_paths/Deferred
assets/shaders/glsl/render_paths/Forward
assets/shaders/glsl/techniques/OfflineRT
```

Expected excludes:

```text
assets/shaders/glsl/techniques/Deferred
assets/shaders/glsl/techniques/Forward
```

- [ ] **Step 7.4: Run audit tests**

Run:

```bash
cmake --build build --target test_073d_render_path_hard_cut test_071g_legacy_boundary_removal
./build/src/test/test_073d_render_path_hard_cut
./build/src/test/test_071g_legacy_boundary_removal
```

Expected: PASS.

- [ ] **Step 7.5: Commit audit tightening**

Run:

```bash
git add src/test/integration/test_071g_legacy_boundary_removal.cpp \
  src/test/integration/test_073d_render_path_hard_cut.cpp \
  notes/requirements/073-d-render-path-shader-uri-migration-and-terminology-hard-cut.md
git commit -m "Tighten 073-d legacy path audits"
```

---

## Task 8: Run Focused And Broad Verification

**Files:**
- No file edits in this task.

- [ ] **Step 8.1: Build the focused targets**

Run:

```bash
cmake --build build --target \
  CompileShaders \
  CompileMaterialSourceShaderVariants \
  test_073d_render_path_hard_cut \
  test_render_resource_parsers \
  test_render_path_graph_pass_contract \
  test_material_source_variant_pipeline \
  test_shader_compiler \
  test_bindless_validation_contract \
  test_071g_legacy_boundary_removal \
  test_pipeline_build_info \
  test_pipeline_cache \
  test_vulkan_shader \
  lxe_editor
```

Expected: build succeeds without new warnings in touched targets.

- [ ] **Step 8.2: Run headless focused tests**

Run:

```bash
./build/src/test/test_073d_render_path_hard_cut
./build/src/test/test_render_resource_parsers
./build/src/test/test_render_path_graph_pass_contract
./build/src/test/test_material_source_variant_pipeline
./build/src/test/test_shader_compiler
./build/src/test/test_bindless_validation_contract
./build/src/test/test_071g_legacy_boundary_removal
./build/src/test/test_pipeline_build_info
./build/src/test/test_pipeline_cache
```

Expected: all pass.

- [ ] **Step 8.3: Run Vulkan shader smoke under xvfb**

Run:

```bash
xvfb-run -a ./build/src/test/test_vulkan_shader
```

Expected: PASS or a skip message caused only by missing video/Vulkan environment. A failure caused by missing `render_paths/Forward/pbr` shader output blocks completion.

- [ ] **Step 8.4: Run CTest labels**

Run:

```bash
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
```

Expected: headless auto tests pass.

Run video tests only if the environment supports xvfb and Vulkan:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

Expected: requires-video tests pass or expose an environment-only Vulkan/SDL failure unrelated to 073-d. Record the exact failing test and message if this occurs.

- [ ] **Step 8.5: Run final rg audit**

Run:

```bash
rg -n "techniques/Forward|techniques/Deferred|defaultTechnique|MaterialTechnique|MaterialTechniqueSet|std::optional<std::string> technique" \
  assets src/core src/infra src/backend src/demos src/test
```

Expected: no production hits. Remaining hits must be in named negative/audit tests and must assert rejection.

- [ ] **Step 8.6: Commit any verification-only doc adjustment**

If Task 8 exposes no file edits, do not create an empty commit.

---

## Task 9: Close Implementation Notes For This Slice

**Files:**
- Modify: `notes/requirements/073-d-render-path-shader-uri-migration-and-terminology-hard-cut.md`
- Modify: `docs/superpowers/plans/2026-06-14-073-d-render-path-hard-cut.md`

- [ ] **Step 9.1: Update implementation status with verification evidence**

In the `## 实施状态` section of `notes/requirements/073-d-render-path-shader-uri-migration-and-terminology-hard-cut.md`, append:

```markdown

已完成本轮 hard cut：

- 默认 Forward / Deferred shader source tree 迁移到 `assets/shaders/glsl/render_paths/`。
- 默认 RenderPathGraph asset 使用 `render_paths/...`，旧 `techniques/Forward` /
  `techniques/Deferred` URI 被 parser 拒绝。
- `scene_runtime` 不再注入旧 `techniques/Forward/pbr` pass。
- pipeline preparation 在 scene resources 和 material source variants 完成后显式执行。
- `rg` audit 确认 production code 和普通正向测试不再保留旧 Forward/Deferred technique 路径。

验证命令：

```bash
cmake --build build --target CompileShaders CompileMaterialSourceShaderVariants test_073d_render_path_hard_cut test_render_resource_parsers test_render_path_graph_pass_contract test_material_source_variant_pipeline test_shader_compiler test_bindless_validation_contract test_071g_legacy_boundary_removal test_pipeline_build_info test_pipeline_cache test_vulkan_shader lxe_editor
./build/src/test/test_073d_render_path_hard_cut
./build/src/test/test_render_resource_parsers
./build/src/test/test_render_path_graph_pass_contract
./build/src/test/test_material_source_variant_pipeline
./build/src/test/test_shader_compiler
./build/src/test/test_bindless_validation_contract
./build/src/test/test_071g_legacy_boundary_removal
./build/src/test/test_pipeline_build_info
./build/src/test/test_pipeline_cache
rg -n "techniques/Forward|techniques/Deferred|defaultTechnique|MaterialTechnique|MaterialTechniqueSet|std::optional<std::string> technique" assets src/core src/infra src/backend src/demos src/test
```
```

If a command could not run because of environment limits, replace the corresponding line with the exact attempted command and error.

- [ ] **Step 9.2: Update plan progress**

In this plan, mark completed tasks with `[x]`.

- [ ] **Step 9.3: Commit status update**

Run:

```bash
git add notes/requirements/073-d-render-path-shader-uri-migration-and-terminology-hard-cut.md \
  docs/superpowers/plans/2026-06-14-073-d-render-path-hard-cut.md
git commit -m "Close 073-d render path hard cut status"
```

---

## Final Completion Checklist

Before reporting completion:

- [ ] `assets/shaders/glsl/techniques/Forward` does not exist.
- [ ] `assets/shaders/glsl/techniques/Deferred` does not exist.
- [ ] `assets/shaders/glsl/techniques/OfflineRT` may still exist for `REQ-073-g/h`.
- [ ] Default graph assets contain no `techniques/...`.
- [ ] `scene_runtime.cpp` contains no `ensureRealtimeForwardSurfacePass` and no `techniques/Forward/pbr`.
- [ ] Parser diagnostics reject old URIs with `legacy shader URI` and `render_paths/...`.
- [ ] Pipeline preparation has a named `preparePipelinesForLoadedScene()` phase.
- [ ] Focused tests and rg audit commands have been run and recorded.
