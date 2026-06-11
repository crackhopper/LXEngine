#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    ++g_failures;
  }
}

[[nodiscard]] fs::path projectRoot() {
  return fs::path(__FILE__).parent_path().parent_path().parent_path()
      .parent_path();
}

[[nodiscard]] bool hasRootKey(const YAML::Node &root, std::string_view key) {
  const YAML::Node node = root[std::string(key)];
  return node && node.IsDefined();
}

void auditDefaultMaterialAssets() {
  const fs::path materialsDir = projectRoot() / "assets" / "materials";
  expect(fs::exists(materialsDir),
         "assets/materials directory should exist for default material audit");

  const std::vector<std::string> legacyRootKeys{
      "shader", "defaultTechnique", "techniques", "parameters", "resources"};

  std::size_t materialCount = 0;
  for (const fs::directory_entry &entry : fs::directory_iterator(materialsDir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".material") {
      continue;
    }

    ++materialCount;
    YAML::Node root;
    try {
      root = YAML::LoadFile(entry.path().string());
    } catch (const YAML::Exception &error) {
      expect(false, entry.path().string() + ": YAML parse failed: " +
                        error.what());
      continue;
    }

    expect(root.IsMap(), entry.path().string() + ": root should be a map");
    if (!root.IsMap()) {
      continue;
    }

    expect(hasRootKey(root, "schema") &&
               root["schema"].as<std::string>() == "lxe.material.v2",
           entry.path().string() +
               ": default material assets must use schema lxe.material.v2");

    for (const std::string &key : legacyRootKeys) {
      expect(!hasRootKey(root, key),
             entry.path().string() + ": forbidden legacy root key '" + key +
                 "'");
    }
  }

  expect(materialCount > 0,
         "default material audit should cover at least one .material asset");
}

} // namespace

int main() {
  auditDefaultMaterialAssets();

  if (g_failures != 0) {
    std::cerr << g_failures << " default material asset audit failure(s)\n";
    return 1;
  }

  std::cout << "Default material asset audit passed\n";
  return 0;
}
