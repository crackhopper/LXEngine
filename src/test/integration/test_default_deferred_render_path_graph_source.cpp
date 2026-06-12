#include "core/frame_graph/frame_graph_build_plan.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/frame_graph/pass.hpp"
#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
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
  return fs::exists(path / "src/backend/vulkan/vulkan_realtime_renderer.cpp") &&
         fs::exists(path / "assets/render_paths/forward_main.render-path.yaml");
}

fs::path findRepoRootFromSourceFile() {
  fs::path sourceFile = fs::absolute(__FILE__);
  if (!fs::exists(sourceFile)) {
    return {};
  }

  for (fs::path current = sourceFile.parent_path(); !current.empty();
       current = current.parent_path()) {
    if (isRepoRoot(current)) {
      return fs::canonical(current);
    }
    if (current == current.root_path()) {
      break;
    }
  }
  return {};
}

fs::path findRepoRoot() {
  const fs::path configured{LXE_SOURCE_DIR};
  if (!configured.empty() && isRepoRoot(configured)) {
    return fs::canonical(configured);
  }

  const fs::path fromSourceFile = findRepoRootFromSourceFile();
  if (!fromSourceFile.empty()) {
    return fromSourceFile;
  }

  return fs::current_path();
}

std::string readTextFile(const fs::path &path) {
  std::ifstream in(path);
  EXPECT(in.is_open(), "failed to open " + path.generic_string());
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

bool containsAfter(const std::string &source, const std::string &needle,
                   std::size_t offset) {
  return source.find(needle, offset) != std::string::npos;
}

bool hasGraphPassMutationAfterBuild(const std::string &source) {
  const std::size_t buildCall =
      source.find("buildFrameGraphFromRenderPathGraph");
  if (buildCall == std::string::npos) {
    return false;
  }
  for (const std::string &needle : {
           "pass.reads =",
           "pass.reads.insert",
           "pass.writes =",
           "pass.shaderUri =",
           "pass.stage =",
           "pass.dispatch =",
       }) {
    if (containsAfter(source, needle, buildCall)) {
      return true;
    }
  }
  return false;
}

std::string sourceBetweenGraphBuildAndFrameGraphBuild(
    const std::string &source) {
  const std::size_t buildCall =
      source.find("buildFrameGraphFromRenderPathGraph");
  if (buildCall == std::string::npos) {
    return {};
  }
  const std::size_t frameGraphBuild = source.find("m_frameGraph.build(", buildCall);
  if (frameGraphBuild == std::string::npos) {
    return source.substr(buildCall);
  }
  return source.substr(buildCall, frameGraphBuild - buildCall);
}

bool constructsLegacyPostProcessOrBloomPassAfterGraphBuild(
    const std::string &source) {
  const std::string graphBuildScope =
      sourceBetweenGraphBuildAndFrameGraphBuild(source);
  for (const std::string &needle : {
           "m_frameGraph.addPass(LX_core::FramePass{\n"
           "          LX_core::Pass_BloomThreshold",
           "m_frameGraph.addPass(LX_core::FramePass{\n"
           "          LX_core::Pass_BloomBlurH",
           "m_frameGraph.addPass(LX_core::FramePass{\n"
           "          LX_core::Pass_BloomBlurV",
           "m_frameGraph.addPass(LX_core::FramePass{\n"
           "          LX_core::Pass_PostProcess",
       }) {
    if (graphBuildScope.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool constructsUndocumentedShadowOrDebugOverlayRuntimePass(
    const std::string &source) {
  for (const std::string &needle : {
           "LX_core::FramePass{LX_core::Pass_Shadow",
           "LX_core::FramePass debugOverlayPass",
           "LX_core::FramePass{LX_core::Pass_DebugOverlay",
       }) {
    if (source.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

LX_core::RenderPathGraph loadRenderPathGraphAsset(const fs::path &path) {
  const std::string yamlText = readTextFile(path);
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse(path.generic_string(), yamlText);
  EXPECT(parsed.renderPathGraph.has_value(),
         "RenderPathGraph asset should parse: " + path.generic_string());
  if (!parsed.renderPathGraph.has_value()) {
    for (const std::string &diagnostic : parsed.diagnostics) {
      std::cerr << "[diag] " << diagnostic << '\n';
    }
    return {};
  }
  EXPECT(parsed.diagnostics.empty(),
         "RenderPathGraph asset should not emit diagnostics: " +
             path.generic_string());
  return *parsed.renderPathGraph;
}

std::vector<std::string> passNames(const LX_core::FrameGraph &graph) {
  std::vector<std::string> names;
  for (const LX_core::FramePass &pass : graph.getPasses()) {
    names.push_back(LX_core::GlobalStringTable::get().toDebugString(pass.name));
  }
  return names;
}

const LX_core::FramePass *findPass(const LX_core::FrameGraph &graph,
                                   LX_core::StringID passName) {
  for (const LX_core::FramePass &pass : graph.getPasses()) {
    if (pass.name == passName) {
      return &pass;
    }
  }
  return nullptr;
}

std::vector<std::string> readResourceNames(const LX_core::FramePass &pass) {
  std::vector<std::string> names;
  for (const LX_core::FrameGraphRead &read : pass.reads) {
    names.push_back(
        LX_core::GlobalStringTable::get().toDebugString(read.resource));
  }
  return names;
}

std::vector<LX_core::StringID>
readBindingNames(const LX_core::FramePass &pass) {
  std::vector<LX_core::StringID> names;
  for (const LX_core::FrameGraphRead &read : pass.reads) {
    names.push_back(read.bindingName);
  }
  return names;
}

} // namespace

int main() {
  const fs::path repoRoot = findRepoRoot();
  const fs::path rendererPath =
      repoRoot / "src/backend/vulkan/vulkan_realtime_renderer.cpp";
  const fs::path deferredAssetPath =
      repoRoot / "assets/render_paths/deferred_main.render-path.yaml";
  const fs::path deferredBloomAssetPath =
      repoRoot / "assets/render_paths/deferred_bloom.render-path.yaml";
  const std::string rendererSource = readTextFile(rendererPath);

  EXPECT(!rendererSource.empty(), "renderer source must be readable");
  EXPECT(fs::exists(deferredAssetPath),
         "default Deferred RenderPathGraph asset must exist");
  EXPECT(rendererSource.find("assets/render_paths/"
                             "deferred_main.render-path.yaml") !=
             std::string::npos,
         "production renderer must name the default Deferred RenderPathGraph "
         "asset");
  EXPECT(rendererSource.find(".parse(assetPath") != std::string::npos,
         "production renderer must parse the selected RenderPathGraph asset "
         "path");
  EXPECT(rendererSource.find("buildFrameGraphFromRenderPathGraph") !=
             std::string::npos,
         "production renderer must build renderer passes from RenderPathGraph");
  EXPECT(!hasGraphPassMutationAfterBuild(rendererSource),
         "renderer must not rewrite graph-built pass dependency or shader "
         "semantics after buildFrameGraphFromRenderPathGraph");
  EXPECT(!constructsLegacyPostProcessOrBloomPassAfterGraphBuild(rendererSource),
         "renderer must not construct Bloom/PostProcess passes with "
         "m_frameGraph.addPass after buildFrameGraphFromRenderPathGraph");
  EXPECT(!constructsUndocumentedShadowOrDebugOverlayRuntimePass(rendererSource),
         "renderer must not construct Shadow or DebugOverlay as undocumented "
         "runtime FramePass branches");
  EXPECT(rendererSource.find("expandGraphDeclaredShadowCascadePass") !=
             std::string::npos,
         "temporary shadow cascade fan-out must be named as graph-declared "
         "dynamic expansion");

  const LX_core::RenderPathGraph deferredGraphAsset =
      loadRenderPathGraphAsset(deferredAssetPath);
  if (!deferredGraphAsset.passes.empty()) {
    const LX_core::FrameGraph graph = LX_core::buildFrameGraphFromRenderPathGraph(
        deferredGraphAsset, LX_core::GraphResourceRegistry::makeDefault());
    const std::vector<std::string> expectedOrder{
        "Shadow", "Deferred", "DeferredLighting", "PostProcess",
        "DebugOverlay"};
    EXPECT(passNames(graph) == expectedOrder,
           "Deferred no-bloom graph order must be Shadow -> Deferred -> "
           "DeferredLighting -> PostProcess -> DebugOverlay");
    const LX_core::FramePass *deferredLighting =
        findPass(graph, LX_core::Pass_DeferredLighting);
    EXPECT(deferredLighting != nullptr,
           "Deferred no-bloom graph must include DeferredLighting");
    if (deferredLighting != nullptr) {
      const std::vector<LX_core::StringID> expectedBindings{
          LX_core::StringID("GBufferAlbedoAlpha"),
          LX_core::StringID("GBufferNormalRoughness"),
          LX_core::StringID("GBufferMaterial"),
          LX_core::StringID("GBufferDepth")};
      EXPECT(readBindingNames(*deferredLighting) == expectedBindings,
             "DeferredLighting reads must carry GBuffer shader binding names");
    }
    const LX_core::FramePass *postProcess =
        findPass(graph, LX_core::Pass_PostProcess);
    EXPECT(postProcess != nullptr,
           "Deferred no-bloom graph must include PostProcess");
    if (postProcess != nullptr) {
      const std::vector<std::string> expectedReads{"hdr.color",
                                                   "feature.toneMapping"};
      EXPECT(readResourceNames(*postProcess) == expectedReads,
             "Deferred no-bloom PostProcess reads must come from graph sources");
      const std::vector<LX_core::StringID> expectedBindings{
          LX_core::StringID("SceneColor"), LX_core::StringID{}};
      EXPECT(readBindingNames(*postProcess) == expectedBindings,
             "Deferred no-bloom PostProcess reads must carry shader binding "
             "names");
    }
  }

  EXPECT(fs::exists(deferredBloomAssetPath),
         "Deferred bloom RenderPathGraph asset must exist");
  const LX_core::RenderPathGraph deferredBloomGraph =
      loadRenderPathGraphAsset(deferredBloomAssetPath);
  if (!deferredBloomGraph.passes.empty()) {
    const LX_core::FrameGraph graph = LX_core::buildFrameGraphFromRenderPathGraph(
        deferredBloomGraph, LX_core::GraphResourceRegistry::makeDefault());
    const std::vector<std::string> expectedOrder{
        "Shadow",     "Deferred",       "DeferredLighting", "BloomThreshold",
        "BloomBlurH", "BloomBlurV",     "PostProcess",      "DebugOverlay"};
    EXPECT(passNames(graph) == expectedOrder,
           "Deferred bloom graph order must be Shadow -> Deferred -> "
           "DeferredLighting -> BloomThreshold -> BloomBlurH -> BloomBlurV -> "
           "PostProcess -> DebugOverlay");
    const LX_core::FramePass *deferredLighting =
        findPass(graph, LX_core::Pass_DeferredLighting);
    EXPECT(deferredLighting != nullptr,
           "Deferred bloom graph must include DeferredLighting");
    if (deferredLighting != nullptr) {
      const std::vector<LX_core::StringID> expectedBindings{
          LX_core::StringID("GBufferAlbedoAlpha"),
          LX_core::StringID("GBufferNormalRoughness"),
          LX_core::StringID("GBufferMaterial"),
          LX_core::StringID("GBufferDepth")};
      EXPECT(readBindingNames(*deferredLighting) == expectedBindings,
             "Deferred bloom DeferredLighting reads must carry GBuffer shader "
             "binding names");
    }
    const LX_core::FramePass *postProcess =
        findPass(graph, LX_core::Pass_PostProcess);
    EXPECT(postProcess != nullptr, "Deferred bloom graph must include PostProcess");
    if (postProcess != nullptr) {
      const std::vector<std::string> expectedReads{"hdr.color", "bloom.blur",
                                                   "feature.toneMapping"};
      EXPECT(readResourceNames(*postProcess) == expectedReads,
             "Deferred bloom PostProcess reads must come from graph sources");
      const std::vector<LX_core::StringID> expectedBindings{
          LX_core::StringID("SceneColor"), LX_core::StringID("BloomColor"),
          LX_core::StringID{}};
      EXPECT(readBindingNames(*postProcess) == expectedBindings,
             "Deferred bloom PostProcess reads must carry shader binding names");
    }
  }

  const std::size_t deferredBranch = rendererSource.find("if (deferredMode)");
  EXPECT(deferredBranch != std::string::npos,
         "renderer source should keep an explicit Deferred mode branch");
  if (deferredBranch != std::string::npos) {
    EXPECT(!containsAfter(rendererSource,
                          "m_frameGraph.addPass(\n"
                          "          LX_core::FramePass{"
                          "LX_core::Pass_Deferred",
                          deferredBranch),
           "Deferred mode must not hard-code Pass_Deferred via "
           "m_frameGraph.addPass");
    EXPECT(!containsAfter(rendererSource,
                          "m_frameGraph.addPass(LX_core::FramePass{\n"
                          "          LX_core::Pass_DeferredLighting",
                          deferredBranch),
           "Deferred mode must not hard-code Pass_DeferredLighting via "
           "m_frameGraph.addPass");
  }

  if (g_failures != 0) {
    std::cerr << g_failures
              << " default Deferred RenderPathGraph source checks failed\n";
    return 1;
  }
  return 0;
}
