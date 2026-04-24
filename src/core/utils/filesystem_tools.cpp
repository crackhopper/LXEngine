#include "filesystem_tools.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
constexpr int kMaxSearchDepth = 8;
std::optional<fs::path> g_runtimeRoot;

bool hasRuntimeAssetDirs(const fs::path &root) {
  return fs::exists(root / "materials") && fs::exists(root / "assets") &&
         fs::exists(root / "shaders" / "glsl");
}

bool hasShaderOutputsAtRoot(const fs::path& root, const std::string& shaderName) {
  return fs::exists(root / "shaders" / "glsl" / (shaderName + ".vert.spv")) &&
         fs::exists(root / "shaders" / "glsl" / (shaderName + ".frag.spv"));
}

bool hasShaderOutputsAtBuildDir(const fs::path& root, const std::string& shaderName) {
  return fs::exists(root / "build" / "shaders" / "glsl" /
                        (shaderName + ".vert.spv")) &&
         fs::exists(root / "build" / "shaders" / "glsl" /
                        (shaderName + ".frag.spv"));
}

bool hasRuntimeResourceLayout(const fs::path& root, const std::string& shaderName) {
  return hasRuntimeAssetDirs(root) &&
         (hasShaderOutputsAtRoot(root, shaderName) ||
          hasShaderOutputsAtBuildDir(root, shaderName));
}

std::string describeRuntimeResourceLayout(const fs::path& root,
                                          const std::string& shaderName) {
  std::ostringstream oss;
  oss << "materials=" << (fs::exists(root / "materials") ? "yes" : "no")
      << ", assets=" << (fs::exists(root / "assets") ? "yes" : "no")
      << ", shaders/glsl=" << (hasShaderOutputsAtRoot(root, shaderName) ? "yes" : "no")
      << ", build/shaders/glsl="
      << (hasShaderOutputsAtBuildDir(root, shaderName) ? "yes" : "no");
  return oss.str();
}

std::optional<fs::path> validatedRuntimeRoot(const fs::path &candidate) {
  if (candidate.empty())
    return std::nullopt;
  const fs::path absolute = fs::absolute(candidate);
  if (!hasRuntimeAssetDirs(absolute))
    return std::nullopt;
  return absolute;
}

std::optional<fs::path> findRuntimeRootFromHint(const fs::path &hintPath) {
  fs::path start = hintPath.empty() ? fs::current_path() : fs::absolute(hintPath);
  if (fs::is_regular_file(start))
    start = start.parent_path();

  if (const char *envRoot = std::getenv("LX_RUNTIME_ROOT")) {
    if (*envRoot) {
      if (auto validated = validatedRuntimeRoot(envRoot))
        return validated;
    }
  }

  if (auto validated = validatedRuntimeRoot(start))
    return validated;

  fs::path probe = start;
  for (int i = 0; i < kMaxSearchDepth; ++i) {
    if (auto validated = validatedRuntimeRoot(probe))
      return validated;
    const auto parent = probe.parent_path();
    if (parent == probe)
      break;
    probe = parent;
  }
  return std::nullopt;
}

fs::path requireRuntimeRoot() {
  if (g_runtimeRoot)
    return *g_runtimeRoot;
  if (auto discovered = findRuntimeRootFromHint(fs::current_path())) {
    g_runtimeRoot = *discovered;
    return *g_runtimeRoot;
  }
  throw std::runtime_error(
      "runtime asset root not initialized; expected root containing assets/, "
      "materials/, and shaders/glsl/ (or set LX_RUNTIME_ROOT)");
}
} // namespace

bool initializeRuntimeAssetRoot() {
  return initializeRuntimeAssetRoot(fs::current_path());
}

bool initializeRuntimeAssetRoot(const fs::path &hintPath) {
  auto discovered = findRuntimeRootFromHint(hintPath);
  if (!discovered)
    return false;
  g_runtimeRoot = *discovered;
  return true;
}

fs::path getRuntimeAssetRoot() { return requireRuntimeRoot(); }

fs::path resolveRuntimePath(const fs::path &relativePath) {
  if (relativePath.is_absolute())
    return relativePath;
  return requireRuntimeRoot() / relativePath;
}

fs::path getRuntimeShaderSourceDir() {
  return requireRuntimeRoot() / "shaders" / "glsl";
}

fs::path getRuntimeShaderBinaryDir() {
  const fs::path root = requireRuntimeRoot();
  const fs::path packagedDir = root / "shaders" / "glsl";
  const fs::path devDir = root / "build" / "shaders" / "glsl";
  if (fs::exists(packagedDir))
    return packagedDir;
  return devDir;
}

bool cdToWhereResourcesCouldFound(const std::string& shaderName) {
  const fs::path start = fs::current_path();
  fs::path p = start;
  std::vector<std::string> probes;
  for (int i = 0; i < kMaxSearchDepth; ++i) {
    probes.push_back(p.string() + " [" +
                     describeRuntimeResourceLayout(p, shaderName) + "]");
    if (hasRuntimeResourceLayout(p, shaderName)) {
      g_runtimeRoot = fs::absolute(p);
      fs::current_path(p);
      std::cerr << "[filesystem] cdToWhereResourcesCouldFound(\"" << shaderName
                << "\") => working directory: " << fs::current_path().string()
                << '\n';
      return true;
    }
    const auto parent = p.parent_path();
    if (parent == p) break;
    p = parent;
  }
  std::cerr << "[filesystem] cdToWhereResourcesCouldFound(\"" << shaderName
            << "\") failed. start directory: " << start.string() << '\n';
  for (const auto& probe : probes) {
    std::cerr << "  probed: " << probe << '\n';
  }
  return false;
}

bool cdToWhereShadersExist(const std::string& shaderName) {
  return cdToWhereResourcesCouldFound(shaderName);
}

bool cdToWhereAssetsExist(const std::string& subpath) {
  if (initializeRuntimeAssetRoot()) {
    const fs::path root = requireRuntimeRoot();
    if (fs::exists(root / "assets" / subpath)) {
      fs::current_path(root);
      return true;
    }
  }

  fs::path p = fs::current_path();
  for (int i = 0; i < kMaxSearchDepth; ++i) {
    if (fs::exists(p / "assets" / subpath)) {
      g_runtimeRoot = fs::absolute(p);
      fs::current_path(p);
      return true;
    }
    const auto parent = p.parent_path();
    if (parent == p) break;
    p = parent;
  }
  return false;
}

std::string getShaderPath(const std::string& shaderName,
                          const std::string &stageSuffix) {
  auto path1 = getRuntimeShaderBinaryDir() / (shaderName + "." + stageSuffix);
  if (fs::exists(path1)) {
    return path1.string();
  }
  auto path2 = getRuntimeAssetRoot() / "build" / "shaders" / "glsl" /
               (shaderName + "." + stageSuffix);
  if (fs::exists(path2)) {
    return path2.string();
  }
  return "";
}

std::vector<char> readFile(const std::string &filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    throw std::runtime_error("failed to open file!");
  }

  size_t fileSize = (size_t)file.tellg();
  std::vector<char> buffer(fileSize);
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();
  return buffer;
}
