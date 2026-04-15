#include "infra/material_loader/blinn_phong_material_loader.hpp"
#include "core/frame_graph/pass.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace LX_infra {

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fatalLoader(const std::string &reason) {
  std::cerr << "FATAL [BlinnPhongMaterialLoader] reason=" << reason
            << std::endl;
  std::terminate();
}

bool isVariantEnabled(const std::vector<LX_core::ShaderVariant> &variants,
                      const char *macroName) {
  for (const auto &variant : variants) {
    if (variant.macroName == macroName) {
      return variant.enabled;
    }
  }
  return false;
}

std::vector<LX_core::ShaderVariant>
normalizeForwardVariants(const std::vector<LX_core::ShaderVariant> &input) {
  static const std::vector<LX_core::ShaderVariant> kDefaults = {
      LX_core::ShaderVariant{"USE_VERTEX_COLOR", false},
      LX_core::ShaderVariant{"USE_UV", false},
      LX_core::ShaderVariant{"USE_LIGHTING", true},
      LX_core::ShaderVariant{"USE_NORMAL_MAP", false},
      LX_core::ShaderVariant{"USE_SKINNING", false},
  };

  std::unordered_map<std::string, bool> enabled;
  enabled.reserve(kDefaults.size());
  for (const auto &variant : kDefaults) {
    enabled.emplace(variant.macroName, variant.enabled);
  }

  for (const auto &variant : input) {
    auto it = enabled.find(variant.macroName);
    if (it == enabled.end()) {
      fatalLoader("unsupported variant '" + variant.macroName + "'");
    }
    it->second = variant.enabled;
  }

  std::vector<LX_core::ShaderVariant> normalized = kDefaults;
  for (auto &variant : normalized) {
    variant.enabled = enabled.at(variant.macroName);
  }
  return normalized;
}

void validateForwardVariants(const std::vector<LX_core::ShaderVariant> &variants) {
  const bool useLighting = isVariantEnabled(variants, "USE_LIGHTING");
  const bool useUv = isVariantEnabled(variants, "USE_UV");
  const bool useNormalMap = isVariantEnabled(variants, "USE_NORMAL_MAP");
  const bool useSkinning = isVariantEnabled(variants, "USE_SKINNING");

  if (useNormalMap && !useLighting) {
    fatalLoader("USE_NORMAL_MAP requires USE_LIGHTING");
  }
  if (useNormalMap && !useUv) {
    fatalLoader("USE_NORMAL_MAP requires USE_UV");
  }
  if (useSkinning && !useLighting) {
    fatalLoader("USE_SKINNING requires USE_LIGHTING");
  }
}

} // namespace

LX_core::MaterialInstance::Ptr
loadBlinnPhongMaterial(LX_core::ResourcePassFlag passFlag,
                       std::vector<LX_core::ShaderVariant> variants) {
  const std::string baseName = "blinnphong_0";
  variants = normalizeForwardVariants(variants);
  validateForwardVariants(variants);

  // Walk up from the current working directory to locate shaders/glsl.
  fs::path cwd = fs::current_path();
  fs::path glslDir;
  for (int i = 0; i < 4; ++i) {
    fs::path candidate = cwd / "shaders" / "glsl";
    if (fs::exists(candidate / (baseName + ".vert")) &&
        fs::exists(candidate / (baseName + ".frag"))) {
      glslDir = std::move(candidate);
      break;
    }
    fs::path parent = cwd.parent_path();
    if (parent == cwd)
      break;
    cwd = parent;
  }
  if (glslDir.empty()) {
    throw std::runtime_error(
        "blinnphong GLSL sources not found (expected .../shaders/glsl/)");
  }

  const fs::path vert = glslDir / (baseName + ".vert");
  const fs::path frag = glslDir / (baseName + ".frag");

  auto compiled = ShaderCompiler::compileProgram(vert, frag, variants);
  if (!compiled.success) {
    throw std::runtime_error("blinnphong compile failed: " +
                             compiled.errorMessage);
  }

  auto bindings = ShaderReflector::reflect(compiled.stages);
  auto vertexInputs = ShaderReflector::reflectVertexInputs(compiled.stages);
  auto shader = std::make_shared<CompiledShader>(std::move(compiled.stages),
                                                 bindings, vertexInputs,
                                                 baseName);

  auto tmpl = LX_core::MaterialTemplate::create(baseName, shader);

  LX_core::ShaderProgramSet programSet;
  programSet.shaderName = baseName;
  programSet.variants = variants;
  programSet.shader = shader;

  LX_core::RenderPassEntry entry;
  entry.shaderSet = programSet;
  entry.renderState = LX_core::RenderState{};
  entry.buildCache(); // populates per-entry bindingCache for backend lookups
  tmpl->setPass(LX_core::Pass_Forward, std::move(entry));
  tmpl->buildBindingCache();

  auto mat = LX_core::MaterialInstance::create(tmpl, passFlag);

  // Seed default uniform values matching the previous BlinnPhongMaterialUBO
  // defaults. Member names come from the GLSL declaration in
  // `shaders/glsl/blinnphong_0.frag::MaterialUBO`.
  mat->setVec3(LX_core::StringID("baseColor"),
               LX_core::Vec3f{0.8f, 0.8f, 0.8f});
  mat->setFloat(LX_core::StringID("shininess"), 12.0f);
  mat->setFloat(LX_core::StringID("specularIntensity"), 1.0f);
  mat->setInt(LX_core::StringID("enableAlbedo"),
              isVariantEnabled(variants, "USE_UV") ? 1 : 0);
  mat->setInt(LX_core::StringID("enableNormal"),
              isVariantEnabled(variants, "USE_NORMAL_MAP") ? 1 : 0);
  mat->updateUBO();

  return mat;
}

} // namespace LX_infra
