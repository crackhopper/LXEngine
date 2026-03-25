#include "filesystem_tools.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
bool cdToWhereShadersExist(const std::string& shaderName) {
  fs::path p = fs::current_path();
  for (int i = 0; i < 8; ++i) {
    if (fs::exists(p / "shaders" / "glsl" / (shaderName + ".vert.spv")) &&
        fs::exists(p / "shaders" / "glsl" / (shaderName + ".frag.spv"))) {
      fs::current_path(p);
      return true;
    }
    if (fs::exists(p / "build" / "shaders" / "glsl" / "blinnphong_0.vert.spv") &&
        fs::exists(p / "build" / "shaders" / "glsl" / "blinnphong_0.frag.spv")) {
      fs::current_path(p / "build");
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