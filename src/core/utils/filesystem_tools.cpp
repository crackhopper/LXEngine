#include "filesystem_tools.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
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
  return fs::exists(root / "materials") && fs::exists(root / "assets") &&
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
} // namespace

bool cdToWhereResourcesCouldFound(const std::string& shaderName) {
  const fs::path start = fs::current_path();
  fs::path p = start;
  std::vector<std::string> probes;
  for (int i = 0; i < 8; ++i) {
    probes.push_back(p.string() + " [" +
                     describeRuntimeResourceLayout(p, shaderName) + "]");
    if (hasRuntimeResourceLayout(p, shaderName)) {
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
  fs::path p = fs::current_path();
  for (int i = 0; i < 8; ++i) {
    if (fs::exists(p / "assets" / subpath)) {
      fs::current_path(p);
      return true;
    }
    const auto parent = p.parent_path();
    if (parent == p) break;
    p = parent;
  }
  return false;
}

std::string getShaderPath(const std::string& shaderName){
  auto path1 = fs::current_path() / "shaders" / "glsl" / (shaderName + ".spv");
  if (fs::exists(path1)) {
    return path1.string();
  }
  auto path2 = fs::current_path() / "build" / "shaders" / "glsl" / (shaderName + ".spv");
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
