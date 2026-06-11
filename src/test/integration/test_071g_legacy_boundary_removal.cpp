#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

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

bool isSkippedDirectory(const fs::path &path) {
  const std::string generic = path.generic_string();
  return generic.find("/external/") != std::string::npos ||
         generic.find("/generated/") != std::string::npos ||
         generic.find("/third_party/") != std::string::npos;
}

bool isTextFile(const fs::path &path) {
  const std::string ext = path.extension().string();
  return ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" ||
         ext == ".frag" || ext == ".vert" || ext == ".glsl" ||
         ext == ".yaml" || ext == ".yml" || ext == ".material" ||
         ext == ".py";
}

std::string readFile(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

void scanFile(const fs::path &repoRoot, const fs::path &path,
              const std::vector<ForbiddenToken> &tokens) {
  const std::string text = readFile(path);
  const std::string relative = fs::relative(path, repoRoot).generic_string();
  for (const ForbiddenToken &token : tokens) {
    if (text.find(token.text) == std::string::npos) {
      continue;
    }
    std::cerr << "[FAIL] forbidden token '" << token.text << "' in "
              << relative << '\n';
    ++g_failures;
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
  for (const fs::directory_entry &entry :
       fs::recursive_directory_iterator(absolute)) {
    if (entry.is_directory() && isSkippedDirectory(entry.path())) {
      continue;
    }
    if (!entry.is_regular_file() || !isTextFile(entry.path())) {
      continue;
    }
    scanFile(repoRoot, entry.path(), tokens);
  }
}

} // namespace

int main() {
  const fs::path repoRoot = fs::current_path();
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
      {"makeDefaultForwardRenderPathGraph"},
  };

  for (const ScanRoot &root : roots) {
    scanRoot(repoRoot, root, tokens);
  }

  if (g_failures != 0) {
    std::cerr << g_failures
              << " REQ-071-g legacy boundary audit failures\n";
    return 1;
  }
  return 0;
}
