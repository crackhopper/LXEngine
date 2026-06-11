#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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
  return fs::exists(path /
                    "src/backend/vulkan/vulkan_realtime_renderer.cpp") &&
         fs::exists(path /
                    "assets/render_paths/forward_main.render-path.yaml");
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

} // namespace

int main() {
  const fs::path repoRoot = findRepoRoot();
  const fs::path rendererPath =
      repoRoot / "src/backend/vulkan/vulkan_realtime_renderer.cpp";
  const std::string rendererSource = readTextFile(rendererPath);

  EXPECT(!rendererSource.empty(), "renderer source must be readable");
  EXPECT(rendererSource.find("makeDefaultForwardRenderPathGraph") ==
             std::string::npos,
         "production renderer must not contain the removed built-in Forward "
         "graph factory");
  EXPECT(rendererSource.find("assets/render_paths/"
                             "forward_main.render-path.yaml") !=
             std::string::npos,
         "production renderer must name the default Forward RenderPathGraph "
         "asset");
  EXPECT(rendererSource.find("RenderEffectResourceParser") != std::string::npos,
         "production renderer must use RenderEffectResourceParser for the "
         "default Forward RenderPathGraph asset");
  EXPECT(rendererSource.find(".parse(kDefaultForwardRenderPathGraphAsset") !=
             std::string::npos,
         "production renderer must parse the named default Forward "
         "RenderPathGraph asset");

  if (g_failures != 0) {
    std::cerr << g_failures
              << " default Forward RenderPathGraph source checks failed\n";
    return 1;
  }
  return 0;
}
