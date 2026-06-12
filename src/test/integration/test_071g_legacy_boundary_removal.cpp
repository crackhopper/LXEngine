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

struct ScanRoot final {
  fs::path path;
  bool required = true;
};

struct ForbiddenToken final {
  std::string text;
};

struct ForbiddenPath final {
  fs::path path;
};

struct AllowedLegacyMention final {
  std::string token;
  std::string pathContains;
};

bool contains(std::string_view text, std::string_view needle) {
  return text.find(needle) != std::string_view::npos;
}

bool hasSkippedPathComponent(const fs::path &path) {
  for (const fs::path &component : path) {
    if (component == "external" || component == "generated" ||
        component == "third_party") {
      return true;
    }
  }
  return false;
}

bool isTextFile(const fs::path &path) {
  const std::string ext = path.extension().string();
  return ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" ||
         ext == ".frag" || ext == ".vert" || ext == ".glsl" ||
         ext == ".yaml" || ext == ".yml" || ext == ".material" ||
         ext == ".py" || ext == ".txt";
}

bool isRepoRoot(const fs::path &path) {
  return fs::exists(path /
                    "src/test/integration/"
                    "test_071g_legacy_boundary_removal.cpp");
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
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

void scanFile(const fs::path &repoRoot, const fs::path &path,
              const std::vector<ForbiddenToken> &tokens,
              const std::vector<AllowedLegacyMention> &allowlist) {
  std::ifstream in(path);
  const std::string relative = fs::relative(path, repoRoot).generic_string();
  if (relative == "src/test/integration/test_071g_legacy_boundary_removal.cpp") {
    return;
  }

  std::string line;
  int lineNumber = 0;
  while (std::getline(in, line)) {
    ++lineNumber;
    for (const ForbiddenToken &token : tokens) {
      if (line.find(token.text) == std::string::npos) {
        continue;
      }
      bool allowed = false;
      for (const AllowedLegacyMention &allow : allowlist) {
        if (allow.token == token.text &&
            contains(relative, allow.pathContains)) {
          allowed = true;
          break;
        }
      }
      if (allowed) {
        continue;
      }
      std::cerr << "[FAIL] forbidden token '" << token.text << "' in "
                << relative << ':' << lineNumber << '\n';
      ++g_failures;
    }
  }
}

void scanRoot(const fs::path &repoRoot, const ScanRoot &root,
              const std::vector<ForbiddenToken> &tokens,
              const std::vector<AllowedLegacyMention> &allowlist) {
  const fs::path absolute = repoRoot / root.path;
  if (!fs::exists(absolute)) {
    EXPECT(!root.required, "required scan root missing: " +
                               root.path.generic_string());
    return;
  }
  int scannedTextFiles = 0;
  for (fs::recursive_directory_iterator it(absolute), end; it != end; ++it) {
    const fs::directory_entry &entry = *it;
    const fs::path relative = fs::relative(entry.path(), repoRoot);
    if (entry.is_directory() && hasSkippedPathComponent(relative)) {
      it.disable_recursion_pending();
      continue;
    }
    if (hasSkippedPathComponent(relative)) {
      continue;
    }
    if (!entry.is_regular_file() || !isTextFile(entry.path())) {
      continue;
    }
    ++scannedTextFiles;
    scanFile(repoRoot, entry.path(), tokens, allowlist);
  }
  if (root.required) {
    EXPECT(scannedTextFiles > 0, "required scan root produced no text files: " +
                                     root.path.generic_string());
  }
}

void auditRemovedLegacyFiles(const fs::path &repoRoot,
                             const std::vector<ForbiddenPath> &paths) {
  for (const ForbiddenPath &path : paths) {
    EXPECT(!fs::exists(repoRoot / path.path),
           "superseded legacy file still exists: " +
               path.path.generic_string());
  }
}

void auditDefaultForwardGraphSource(const fs::path &repoRoot) {
  const fs::path rendererPath =
      repoRoot / "src/backend/vulkan/vulkan_realtime_renderer.cpp";
  const std::string rendererSource = readTextFile(rendererPath);

  EXPECT(rendererSource.find("makeDefaultForwardRenderPathGraph") ==
             std::string::npos,
         "production renderer must not contain a built-in Forward graph "
         "factory");
  EXPECT(rendererSource.find("assets/render_paths/"
                             "forward_main.render-path.yaml") !=
             std::string::npos,
         "production renderer must name the default Forward RenderPathGraph "
         "asset");
  EXPECT(rendererSource.find("RenderPathGraphResourceParser") !=
             std::string::npos,
         "production renderer must parse the default Forward RenderPathGraph "
         "asset");
}

} // namespace

int main() {
  const fs::path repoRoot = findRepoRoot();
  const std::vector<ScanRoot> productionRoots{
      {"src/core"},
      {"src/infra"},
      {"src/backend"},
      {"src/demos/lxe_editor"},
      {"assets/materials"},
      {"assets/shaders"},
      {"assets/scenes"},
      {"assets/render_paths", false},
      {"assets/effects", false},
      {"data/scenes", false},
      {"src/tools/lxe_pbrt_scene_convert", false},
  };
  const std::vector<ScanRoot> testRoots{
      {"src/test"},
  };
  const std::vector<ForbiddenToken> productionTokens{
      {"defaultTechnique"},
      {"MaterialUBO"},
      {"baseColorFactor"},
      {"metallicFactor"},
      {"roughnessFactor"},
      {"materialTag"},
      {"setActiveMaterialTag"},
      {"activeMaterialTag"},
      {"BindlessSubmissionDecisionKind::LegacyPerItem"},
      {"LegacyPerItem"},
      {"raster.drawData"},
      {"PerDrawData"},
      {"vkCmdPushConstants"},
      {"m_pushConstants"},
      {"PushConstantSnapshot"},
      {"pushConstants"},
      {"makeDefaultForwardRenderPathGraph"},
      {"MaterialPassContract"},
      {"MaterialTechnique"},
      {"MaterialTechniqueSet"},
      {"FrameGraphMaterialTechniqueInput"},
      {"buildFrameGraphFromSourceTargetContracts"},
      {"TechniqueValidator"},
      {"MaterialPassContractParser"},
      {"RenderEffectResourceParser"},
      {"test_technique_pass_contract"},
      {"test_render_effect_resource_parser"},
      {"MaterialParams"},
      {"RenderPassContractSet"},
      {"RenderPassContractLibrary"},
  };
  const std::vector<ForbiddenToken> testTokens{
      {"defaultTechnique"},
      {"MaterialUBO"},
      {"MaterialParams"},
      {"activeTechnique"},
      {"MaterialTechniqueSet"},
      {"TechniqueValidator"},
      {"MaterialPassContractParser"},
      {"RenderEffectResourceParser"},
      {"test_technique_pass_contract"},
      {"test_render_effect_resource_parser"},
      {"materialTag"},
      {"LegacyPerItem"},
      {"makeDefaultForwardRenderPathGraph"},
      {"RenderPassContractSet"},
      {"RenderPassContractLibrary"},
  };
  const std::vector<AllowedLegacyMention> allowlist{
      {"MaterialParams", "src/core/frame_graph/render_validation_contract.cpp"},
      {"defaultTechnique", "test_material_v2_resource_dependencies.cpp"},
      {"defaultTechnique", "test_default_material_asset_audit.cpp"},
      {"MaterialUBO", "test_material_v2_parser.cpp"},
      {"MaterialUBO", "test_071_bridge_audit.cpp"},
      {"MaterialUBO", "test_071g_legacy_boundary_removal.cpp"},
      {"MaterialParams", "test_071_bridge_audit.cpp"},
      {"materialTag", "test_071g_legacy_boundary_removal.cpp"},
      {"materialTag", "test_gltf_scene_asset_loader.cpp"},
      {"materialTag", "test_lxe_pbrt_scene_convert.py"},
      {"activeTechnique", "test_render_validation_profile.cpp"},
      {"setActiveMaterialTag", "test_071g_legacy_boundary_removal.cpp"},
      {"activeMaterialTag", "test_071g_legacy_boundary_removal.cpp"},
      {"activeMaterialTag", "test_gltf_scene_asset_loader.cpp"},
      {"BindlessSubmissionDecisionKind::LegacyPerItem",
       "test_071_bridge_audit.cpp"},
      {"BindlessSubmissionDecisionKind::LegacyPerItem",
       "test_071g_legacy_boundary_removal.cpp"},
      {"LegacyPerItem", "test_071_bridge_audit.cpp"},
      {"LegacyPerItem", "test_071g_legacy_boundary_removal.cpp"},
      {"raster.drawData", "test_071_bridge_audit.cpp"},
      {"raster.drawData", "test_071g_legacy_boundary_removal.cpp"},
      {"PerDrawData", "test_071_bridge_audit.cpp"},
      {"PerDrawData", "test_071g_legacy_boundary_removal.cpp"},
      {"vkCmdPushConstants", "test_071_bridge_audit.cpp"},
      {"vkCmdPushConstants", "test_071g_legacy_boundary_removal.cpp"},
      {"m_pushConstants", "test_071_bridge_audit.cpp"},
      {"m_pushConstants", "test_071g_legacy_boundary_removal.cpp"},
      {"PushConstantSnapshot", "test_071_bridge_audit.cpp"},
      {"PushConstantSnapshot", "test_071g_legacy_boundary_removal.cpp"},
      {"pushConstants", "test_071_bridge_audit.cpp"},
      {"pushConstants", "test_071g_legacy_boundary_removal.cpp"},
      {"makeDefaultForwardRenderPathGraph",
       "test_default_forward_render_path_graph_source.cpp"},
      {"makeDefaultForwardRenderPathGraph",
       "test_071g_legacy_boundary_removal.cpp"},
      {"MaterialPassContract", "test_071g_legacy_boundary_removal.cpp"},
      {"MaterialTechnique", "test_071g_legacy_boundary_removal.cpp"},
      {"MaterialTechniqueSet", "test_071g_legacy_boundary_removal.cpp"},
      {"FrameGraphMaterialTechniqueInput",
       "test_071g_legacy_boundary_removal.cpp"},
      {"buildFrameGraphFromSourceTargetContracts",
       "test_071g_legacy_boundary_removal.cpp"},
      {"TechniqueValidator", "test_071g_legacy_boundary_removal.cpp"},
      {"MaterialPassContractParser",
       "test_071g_legacy_boundary_removal.cpp"},
      {"RenderEffectResourceParser",
       "test_071g_legacy_boundary_removal.cpp"},
      {"test_technique_pass_contract",
       "test_071g_legacy_boundary_removal.cpp"},
      {"test_render_effect_resource_parser",
       "test_071g_legacy_boundary_removal.cpp"},
      {"MaterialParams", "test_071g_legacy_boundary_removal.cpp"},
      {"RenderPassContractSet", "test_071g_legacy_boundary_removal.cpp"},
      {"RenderPassContractLibrary", "test_071g_legacy_boundary_removal.cpp"},
  };
  const std::vector<ForbiddenPath> legacyFiles{
      {"src/core/asset/material_technique_set.cpp"},
      {"src/core/asset/material_technique_set.hpp"},
      {"src/core/frame_graph/technique_validator.cpp"},
      {"src/core/frame_graph/technique_validator.hpp"},
      {"src/infra/resource_parsers/material_pass_contract_parser.cpp"},
      {"src/infra/resource_parsers/material_pass_contract_parser.hpp"},
      {"src/infra/resource_parsers/render_effect_resource_parser.cpp"},
      {"src/infra/resource_parsers/render_effect_resource_parser.hpp"},
      {"src/test/integration/test_technique_pass_contract.cpp"},
      {"src/test/integration/test_render_effect_resource_parser.cpp"},
  };

  auditRemovedLegacyFiles(repoRoot, legacyFiles);
  for (const ScanRoot &root : productionRoots) {
    scanRoot(repoRoot, root, productionTokens, allowlist);
  }
  for (const ScanRoot &root : testRoots) {
    scanRoot(repoRoot, root, testTokens, allowlist);
  }
  auditDefaultForwardGraphSource(repoRoot);

  if (g_failures != 0) {
    std::cerr << g_failures
              << " REQ-071-g legacy boundary audit failures\n";
    return 1;
  }
  return 0;
}
