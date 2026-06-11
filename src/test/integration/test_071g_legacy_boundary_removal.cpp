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

struct ScanRoot final {
  fs::path path;
  bool required = true;
};

struct ForbiddenToken final {
  std::string text;
};

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
         ext == ".py";
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
              const std::vector<ForbiddenToken> &tokens) {
  std::ifstream in(path);
  const std::string relative = fs::relative(path, repoRoot).generic_string();

  std::string line;
  int lineNumber = 0;
  while (std::getline(in, line)) {
    ++lineNumber;
    for (const ForbiddenToken &token : tokens) {
      if (line.find(token.text) == std::string::npos) {
        continue;
      }
      std::cerr << "[FAIL] forbidden token '" << token.text << "' in "
                << relative << ':' << lineNumber << '\n';
      ++g_failures;
    }
  }
}

void scanRoot(const fs::path &repoRoot, const ScanRoot &root,
              const std::vector<ForbiddenToken> &tokens) {
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
    scanFile(repoRoot, entry.path(), tokens);
  }
  if (root.required) {
    EXPECT(scannedTextFiles > 0, "required scan root produced no text files: " +
                                     root.path.generic_string());
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
  EXPECT(rendererSource.find("RenderEffectResourceParser") != std::string::npos,
         "production renderer must parse the default Forward RenderPathGraph "
         "asset");
}

} // namespace

int main() {
  const fs::path repoRoot = findRepoRoot();
  const std::vector<ScanRoot> roots{
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
  const std::vector<ForbiddenToken> tokens{
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
  };

  for (const ScanRoot &root : roots) {
    scanRoot(repoRoot, root, tokens);
  }
  auditDefaultForwardGraphSource(repoRoot);

  if (g_failures != 0) {
    std::cerr << g_failures
              << " REQ-071-g legacy boundary audit failures\n";
    return 1;
  }
  return 0;
}
