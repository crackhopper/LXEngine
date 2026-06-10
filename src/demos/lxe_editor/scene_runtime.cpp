#include "demos/lxe_editor/scene_runtime.hpp"

#include "core/asset/audio_spectrum_texture.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/ibl_environment.hpp"
#include "core/scene/light.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"
#include "infra/scene_asset/scene_material_loader.hpp"
#include "infra/scene_asset/scene_mesh_loader.hpp"
#include "infra/texture_loader/texture_loader.hpp"
#include "demos/lxe_editor/builtin_asset_catalog.hpp"
#include "demos/lxe_editor/editor_camera_state.hpp"
#include "demos/lxe_editor/project_document.hpp"
#include "demos/lxe_editor/scene_builder.hpp"
#include "demos/lxe_editor/scene_document.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace LX_demo::lxe_editor {
namespace {

using SceneLoadClock = std::chrono::steady_clock;

[[nodiscard]] double elapsedMs(const SceneLoadClock::time_point begin,
                               const SceneLoadClock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

struct SceneLoadTimingStats final {
  double documentMs = 0.0;
  double assetRootMs = 0.0;
  double buildRuntimeMs = 0.0;
  double environmentMs = 0.0;
  double meshMs = 0.0;
  double materialMs = 0.0;
  double sceneRegisterMs = 0.0;
  usize nodeCount = 0;
  usize meshLoadCount = 0;
  usize meshVertexCount = 0;
  usize meshIndexCount = 0;
  usize meshTriangleCount = 0;
  usize materialLoadCount = 0;
  usize materialPrototypeLoadCount = 0;
  usize materialCacheHitCount = 0;
};

struct SceneLoadMaterialCache final {
  std::unordered_map<std::string, LX_core::MaterialInstanceSharedPtr>
      genericMaterialPrototypes;
};

thread_local SceneLoadTimingStats *g_sceneLoadTimingStats = nullptr;
thread_local SceneLoadMaterialCache *g_sceneLoadMaterialCache = nullptr;
thread_local std::optional<
    std::reference_wrapper<const LX_core::SceneRealtimeRenderSettings>>
    g_sceneRealtimeRenderSettings;

void accumulateLoadedMeshStats(const LX_core::MeshSharedPtr &mesh) {
  if (g_sceneLoadTimingStats == nullptr || !mesh) {
    return;
  }
  const usize vertexCount = mesh->getVertexCount();
  const usize indexCount = mesh->getIndexCount();
  g_sceneLoadTimingStats->meshVertexCount += vertexCount;
  g_sceneLoadTimingStats->meshIndexCount += indexCount;
  g_sceneLoadTimingStats->meshTriangleCount += indexCount / 3u;
}

struct ScopedSceneLoadTimingContext final {
  ScopedSceneLoadTimingContext(SceneLoadTimingStats &stats,
                               SceneLoadMaterialCache &cache)
      : previousStats(g_sceneLoadTimingStats),
        previousCache(g_sceneLoadMaterialCache),
        previousRealtimeRenderSettings(g_sceneRealtimeRenderSettings) {
    g_sceneLoadTimingStats = &stats;
    g_sceneLoadMaterialCache = &cache;
  }
  ~ScopedSceneLoadTimingContext() {
    g_sceneLoadTimingStats = previousStats;
    g_sceneLoadMaterialCache = previousCache;
    g_sceneRealtimeRenderSettings = previousRealtimeRenderSettings;
  }

  SceneLoadTimingStats *previousStats = nullptr;
  SceneLoadMaterialCache *previousCache = nullptr;
  std::optional<
      std::reference_wrapper<const LX_core::SceneRealtimeRenderSettings>>
      previousRealtimeRenderSettings;
};

struct ScopedSceneRealtimeRenderSettings final {
  explicit ScopedSceneRealtimeRenderSettings(
      const LX_core::SceneRealtimeRenderSettings &settings)
      : previous(g_sceneRealtimeRenderSettings) {
    g_sceneRealtimeRenderSettings = std::cref(settings);
  }

  ~ScopedSceneRealtimeRenderSettings() {
    g_sceneRealtimeRenderSettings = previous;
  }

  std::optional<
      std::reference_wrapper<const LX_core::SceneRealtimeRenderSettings>>
      previous;
};

struct ScopedAccumulatedTimer final {
  explicit ScopedAccumulatedTimer(double &targetMs)
      : target(targetMs), begin(SceneLoadClock::now()) {}
  ~ScopedAccumulatedTimer() {
    target += elapsedMs(begin, SceneLoadClock::now());
  }

  double &target;
  SceneLoadClock::time_point begin;
};

[[nodiscard]] std::string pathForLog(const std::filesystem::path &path) {
  return path.generic_string();
}

[[nodiscard]] LX_infra::GenericMaterialLoadOptions
currentGenericMaterialLoadOptions() {
  LX_infra::GenericMaterialLoadOptions options;
  if (g_sceneRealtimeRenderSettings.has_value()) {
    const auto &settings = g_sceneRealtimeRenderSettings->get();
    options.forceIbl = settings.ibl;
    options.alphaTransparency =
        settings.alphaTransparency;
    options.technique =
        settings.mode == LX_core::SceneRealtimeRenderMode::Deferred
            ? std::optional<std::string>("Deferred")
            : std::optional<std::string>("Forward");
  }
  return options;
}

[[nodiscard]] std::string
materialCacheKey(const std::filesystem::path &path,
                 const LX_infra::GenericMaterialLoadOptions &options) {
  std::string key = path.is_absolute() ? path.lexically_normal().generic_string()
                                       : std::filesystem::absolute(path)
                                             .lexically_normal()
                                             .generic_string();
  if (options.forceIbl.has_value()) {
    key += *options.forceIbl ? "|ibl=1" : "|ibl=0";
  }
  if (options.alphaTransparency.has_value()) {
    key += *options.alphaTransparency ? "|alpha=1" : "|alpha=0";
  }
  if (options.technique.has_value()) {
    key += "|technique=" + *options.technique;
  }
  return key;
}

[[nodiscard]] LX_core::MaterialInstanceSharedPtr
loadCachedGenericMaterial(const std::filesystem::path &path) {
  const LX_infra::GenericMaterialLoadOptions options =
      currentGenericMaterialLoadOptions();
  if (g_sceneLoadMaterialCache == nullptr) {
    if (g_sceneLoadTimingStats != nullptr) {
      ++g_sceneLoadTimingStats->materialPrototypeLoadCount;
    }
    return LX_infra::loadGenericMaterial(path, options);
  }

  const std::string key = materialCacheKey(path, options);
  auto &prototypes = g_sceneLoadMaterialCache->genericMaterialPrototypes;
  const auto found = prototypes.find(key);
  if (found != prototypes.end()) {
    if (g_sceneLoadTimingStats != nullptr) {
      ++g_sceneLoadTimingStats->materialCacheHitCount;
    }
    return found->second->cloneInstanceData();
  }

  if (g_sceneLoadTimingStats != nullptr) {
    ++g_sceneLoadTimingStats->materialPrototypeLoadCount;
  }
  auto prototype = LX_infra::loadGenericMaterial(path, options);
  if (!prototype) {
    return nullptr;
  }
  auto instance = prototype->cloneInstanceData();
  prototypes.emplace(key, std::move(prototype));
  return instance;
}

constexpr const char *RuntimeDebugDrawNodePrefix = "debug_draw_";
constexpr const char *LegacyCameraHelperNodeName = "helper_camera";
constexpr const char *LegacyLightHelperNodeName = "helper_light";
constexpr const char *BuiltinPrimitivePrefix =
    "builtin://lxe_editor/primitives/";
constexpr const char *BuiltinPatchPrefix = "builtin://lxe_editor/patches/";
constexpr const char *BuiltinPrimitiveMaterial =
    "assets/materials/blinnphong_lit.material";
constexpr const char *BuiltinModelPrefix = "assets/models/builtin/";
constexpr const char *kDefaultGroundMaterial =
    "assets/materials/blinnphong_lit.material";

struct SceneRuntimeData final {
  std::optional<std::filesystem::path> documentPath;
  SceneDocument document;
  std::vector<std::filesystem::path> assetRoots;
  LX_core::SceneSharedPtr scene;
  LX_core::SceneNodeSharedPtr editorCameraNode;
  LX_core::SceneNodeSharedPtr gameCameraNode;
};

[[nodiscard]] bool pathStartsWith(const std::filesystem::path &path,
                                  const std::filesystem::path &prefix) {
  auto pathIt = path.begin();
  auto prefixIt = prefix.begin();
  for (; prefixIt != prefix.end(); ++prefixIt, ++pathIt) {
    if (pathIt == path.end() || *pathIt != *prefixIt) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::filesystem::path
absoluteNormal(const std::filesystem::path &path) {
  return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] std::filesystem::path
stripLeadingAssetsComponent(const std::filesystem::path &uri) {
  std::filesystem::path stripped;
  bool skipped = false;
  for (const auto &component : uri) {
    if (!skipped && component.generic_string() == "assets") {
      skipped = true;
      continue;
    }
    stripped /= component;
  }
  return skipped ? stripped : uri;
}

[[nodiscard]] std::optional<std::filesystem::path>
resolveProjectAssetPath(const std::vector<std::filesystem::path> &assetRoots,
                        const std::filesystem::path &uri) {
  if (uri.empty() || uri.is_absolute()) {
    return std::nullopt;
  }
  const std::filesystem::path assetRelative = stripLeadingAssetsComponent(uri);
  for (const auto &assetRoot : assetRoots) {
    const std::filesystem::path candidate =
        (assetRoot / assetRelative).lexically_normal();
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::filesystem::path resolveRuntimeOrProjectAssetPath(
    const std::vector<std::filesystem::path> &assetRoots,
    const std::filesystem::path &uri) {
  if (uri.empty()) {
    throw std::runtime_error("asset uri is empty");
  }
  if (uri.is_absolute()) {
    return uri;
  }
  if (const auto projectPath = resolveProjectAssetPath(assetRoots, uri);
      projectPath.has_value()) {
    return *projectPath;
  }
  return resolveRuntimePath(uri);
}

[[nodiscard]] LX_core::CombinedTextureSamplerSharedPtr
makeHdrAverageCubeSampler(const LX_core::TextureSharedPtr &hdrTexture,
                          LX_core::StringID bindingName) {
  if (!hdrTexture ||
      hdrTexture->desc().format != LX_core::TextureFormat::RGBA32Float) {
    throw std::runtime_error("expected RGBA32Float HDR environment texture");
  }
  const usize pixelCount = static_cast<usize>(hdrTexture->desc().width) *
                           static_cast<usize>(hdrTexture->desc().height);
  if (pixelCount == 0) {
    throw std::runtime_error("HDR environment texture has no pixels");
  }

  const auto *pixels = static_cast<const float *>(hdrTexture->data());
  float average[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  for (usize i = 0; i < pixelCount; ++i) {
    average[0] += pixels[i * 4 + 0];
    average[1] += pixels[i * 4 + 1];
    average[2] += pixels[i * 4 + 2];
  }
  average[0] /= static_cast<float>(pixelCount);
  average[1] /= static_cast<float>(pixelCount);
  average[2] /= static_cast<float>(pixelCount);

  LX_core::TextureDesc desc;
  desc.width = 1;
  desc.height = 1;
  desc.format = LX_core::TextureFormat::RGBA32Float;
  desc.dimension = LX_core::TextureDimension::TextureCube;
  desc.arrayLayers = 6;

  std::vector<float> cubePixels(6u * 4u);
  for (usize face = 0; face < 6; ++face) {
    cubePixels[face * 4 + 0] = average[0];
    cubePixels[face * 4 + 1] = average[1];
    cubePixels[face * 4 + 2] = average[2];
    cubePixels[face * 4 + 3] = average[3];
  }
  std::vector<u8> bytes(LX_core::expectedTextureByteCount(desc));
  std::memcpy(bytes.data(), cubePixels.data(), bytes.size());

  auto sampler = std::make_shared<LX_core::CombinedTextureSampler>(
      std::make_shared<LX_core::Texture>(desc, std::move(bytes)));
  sampler->setBindingName(bindingName);
  return sampler;
}

[[nodiscard]] LX_core::Vec3f cubeFaceDirection(const u32 face, const float u,
                                               const float v) {
  switch (face) {
  case 0:
    return LX_core::Vec3f{1.0f, -v, -u}.normalized();
  case 1:
    return LX_core::Vec3f{-1.0f, -v, u}.normalized();
  case 2:
    return LX_core::Vec3f{u, 1.0f, v}.normalized();
  case 3:
    return LX_core::Vec3f{u, -1.0f, -v}.normalized();
  case 4:
    return LX_core::Vec3f{u, -v, 1.0f}.normalized();
  default:
    return LX_core::Vec3f{-u, -v, -1.0f}.normalized();
  }
}

void sampleEquirectHdr(const LX_core::Texture &hdrTexture,
                       const LX_core::Vec3f &direction, float *outRgba) {
  constexpr float kPi = 3.14159265358979323846f;
  const auto &desc = hdrTexture.desc();
  const auto *pixels = static_cast<const float *>(hdrTexture.data());

  float u = std::atan2(direction.z, direction.x) / (2.0f * kPi) + 0.5f;
  u = u - std::floor(u);
  const float v = std::asin(std::clamp(direction.y, -1.0f, 1.0f)) / kPi + 0.5f;

  const float x = u * static_cast<float>(desc.width) - 0.5f;
  const float y = v * static_cast<float>(desc.height) - 0.5f;
  const i32 x0 = static_cast<i32>(std::floor(x));
  const i32 y0 = static_cast<i32>(std::floor(y));
  const float tx = x - static_cast<float>(x0);
  const float ty = y - static_cast<float>(y0);

  auto pixelAt = [&](i32 px, i32 py, u32 channel) {
    const i32 wrappedX =
        (px % static_cast<i32>(desc.width) + static_cast<i32>(desc.width)) %
        static_cast<i32>(desc.width);
    const i32 clampedY = std::clamp(py, 0, static_cast<i32>(desc.height) - 1);
    const usize index = (static_cast<usize>(clampedY) * desc.width +
                         static_cast<usize>(wrappedX)) *
                            4u +
                        channel;
    return pixels[index];
  };

  for (u32 channel = 0; channel < 4; ++channel) {
    const float c00 = pixelAt(x0, y0, channel);
    const float c10 = pixelAt(x0 + 1, y0, channel);
    const float c01 = pixelAt(x0, y0 + 1, channel);
    const float c11 = pixelAt(x0 + 1, y0 + 1, channel);
    const float cx0 = c00 * (1.0f - tx) + c10 * tx;
    const float cx1 = c01 * (1.0f - tx) + c11 * tx;
    outRgba[channel] = cx0 * (1.0f - ty) + cx1 * ty;
  }
  outRgba[3] = 1.0f;
}

[[nodiscard]] LX_core::CombinedTextureSamplerSharedPtr
makeHdrEquirectCubeSampler(const LX_core::TextureSharedPtr &hdrTexture,
                           LX_core::StringID bindingName, u32 baseSize,
                           u32 mipLevels = 1, u32 *actualMipLevels = nullptr) {
  if (!hdrTexture ||
      hdrTexture->desc().format != LX_core::TextureFormat::RGBA32Float) {
    throw std::runtime_error("expected RGBA32Float HDR environment texture");
  }

  baseSize = std::max(baseSize, 1u);
  const u32 maxMips = LX_core::maxTextureMipLevels(baseSize, baseSize);
  mipLevels = std::clamp(mipLevels, 1u, maxMips);
  if (actualMipLevels != nullptr) {
    *actualMipLevels = mipLevels;
  }

  LX_core::TextureDesc desc;
  desc.width = baseSize;
  desc.height = baseSize;
  desc.format = LX_core::TextureFormat::RGBA32Float;
  desc.dimension = LX_core::TextureDimension::TextureCube;
  desc.mipLevels = mipLevels;
  desc.arrayLayers = 6;

  std::vector<float> cubePixels;
  cubePixels.reserve(LX_core::expectedTextureByteCount(desc) / sizeof(float));
  for (u32 mip = 0; mip < mipLevels; ++mip) {
    const u32 mipSize = std::max(baseSize >> mip, 1u);
    for (u32 face = 0; face < 6u; ++face) {
      for (u32 y = 0; y < mipSize; ++y) {
        for (u32 x = 0; x < mipSize; ++x) {
          const float u = (2.0f * (static_cast<float>(x) + 0.5f) /
                           static_cast<float>(mipSize)) -
                          1.0f;
          const float v = (2.0f * (static_cast<float>(y) + 0.5f) /
                           static_cast<float>(mipSize)) -
                          1.0f;
          float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
          sampleEquirectHdr(*hdrTexture, cubeFaceDirection(face, u, v), rgba);
          cubePixels.push_back(rgba[0]);
          cubePixels.push_back(rgba[1]);
          cubePixels.push_back(rgba[2]);
          cubePixels.push_back(rgba[3]);
        }
      }
    }
  }

  std::vector<u8> bytes(LX_core::expectedTextureByteCount(desc));
  std::memcpy(bytes.data(), cubePixels.data(), bytes.size());
  auto sampler = std::make_shared<LX_core::CombinedTextureSampler>(
      std::make_shared<LX_core::Texture>(desc, std::move(bytes)));
  sampler->setBindingName(bindingName);
  return sampler;
}

[[nodiscard]] LX_core::CombinedTextureSamplerSharedPtr
makeNeutralBrdfLutSampler() {
  LX_core::TextureDesc desc;
  desc.width = 1;
  desc.height = 1;
  desc.format = LX_core::TextureFormat::RGBA16Float;
  auto sampler = std::make_shared<LX_core::CombinedTextureSampler>(
      std::make_shared<LX_core::Texture>(
          desc, std::vector<u8>(LX_core::expectedTextureByteCount(desc), 0)));
  sampler->setBindingName(LX_core::StringID("BrdfLut"));
  return sampler;
}

[[nodiscard]] LX_core::IblEnvironmentResources
loadEnvironmentResources(const EnvironmentState &environment,
                         const std::vector<std::filesystem::path> &assetRoots,
                         bool iblEnabled) {
  std::optional<ScopedAccumulatedTimer> timer;
  if (g_sceneLoadTimingStats != nullptr) {
    timer.emplace(g_sceneLoadTimingStats->environmentMs);
  }
  LX_core::IblEnvironmentResources resources;
  const auto hdrPath =
      resolveRuntimeOrProjectAssetPath(assetRoots, environment.hdrUri);
  const auto hdrTexture = infra::TextureLoader::loadHdrTexture(hdrPath);
  resources.equirectangularMap =
      std::make_shared<LX_core::CombinedTextureSampler>(hdrTexture);
  resources.equirectangularMap->setBindingName(
      LX_core::StringID("EquirectangularMap"));
  resources.equirectangularMap->setDirty();
  resources.skyboxEnabled = environment.skyboxEnabled;
  resources.skyboxCubemap = makeHdrEquirectCubeSampler(
      hdrTexture, LX_core::StringID("SkyboxMap"), 64u, 1u);
  resources.irradianceCubemap =
      makeHdrAverageCubeSampler(hdrTexture, LX_core::StringID("IrradianceMap"));
  const u32 roughnessMipCount = static_cast<u32>(
      std::max(std::round(environment.roughnessMipCount), 1.0f));
  u32 actualPrefilterMipCount = 1u;
  resources.prefilteredRadianceCubemap = makeHdrEquirectCubeSampler(
      hdrTexture, LX_core::StringID("PrefilteredEnvMap"), 64u,
      roughnessMipCount, &actualPrefilterMipCount);
  resources.brdfLut = makeNeutralBrdfLutSampler();
  resources.environmentUbo = std::make_unique<LX_core::EnvironmentData>(
      iblEnabled ? environment.intensity : 0.0f,
      static_cast<float>(actualPrefilterMipCount));
  return resources;
}

[[nodiscard]] std::vector<std::filesystem::path>
discoverProjectAssetRoots(const std::filesystem::path &scenePath) {
  std::vector<std::filesystem::path> roots;
  std::filesystem::path probe = scenePath.parent_path();
  std::optional<std::filesystem::path> repositoryAssetRoot;
  while (!probe.empty()) {
    if (!repositoryAssetRoot.has_value() &&
        (std::filesystem::exists(probe / "assets") ||
         std::filesystem::exists(probe / "data"))) {
      repositoryAssetRoot = absoluteNormal(probe);
    }
    const std::filesystem::path projectPath = probe / "project.yaml";
    if (std::filesystem::exists(projectPath)) {
      const ProjectDocument project = loadProjectDocument(projectPath);
      const std::filesystem::path projectRoot = absoluteNormal(probe);
      for (const auto &assetRoot : project.assetRoots) {
        if (assetRoot.empty() || assetRoot.is_absolute()) {
          continue;
        }
        const std::filesystem::path resolved =
            (projectRoot / assetRoot).lexically_normal();
        if (pathStartsWith(resolved, projectRoot) &&
            std::filesystem::exists(resolved)) {
          roots.push_back(resolved);
        }
      }
      return roots;
    }
    const auto parent = probe.parent_path();
    if (parent == probe) {
      break;
    }
    probe = parent;
  }
  if (repositoryAssetRoot.has_value()) {
    roots.push_back(*repositoryAssetRoot);
  }
  return roots;
}

[[nodiscard]] bool isRuntimeDebugDrawNodeName(const std::string &nodeName) {
  return nodeName.rfind(RuntimeDebugDrawNodePrefix, 0) == 0;
}

[[nodiscard]] bool
isRuntimeDebugDrawNode(const SceneNodeDocument &nodeDocument) {
  return isRuntimeDebugDrawNodeName(nodeDocument.nodeName);
}

[[nodiscard]] bool
isRuntimeDebugDrawNode(const LX_core::SceneNodeSharedPtr &node) {
  return node && isRuntimeDebugDrawNodeName(node->getNodeName());
}

[[nodiscard]] bool isLegacyEditorHelperName(const std::string &name) {
  return name == LegacyCameraHelperNodeName ||
         name == LegacyLightHelperNodeName;
}

[[nodiscard]] bool isBuiltinPrimitiveMeshUri(const std::string &uri) {
  return uri.rfind(BuiltinPrimitivePrefix, 0) == 0;
}

[[nodiscard]] bool isBuiltinPatchMeshUri(const std::string &uri) {
  return uri.rfind(BuiltinPatchPrefix, 0) == 0;
}

[[nodiscard]] bool isBuiltinModelMeshUri(const std::string &uri) {
  return uri.rfind(BuiltinModelPrefix, 0) == 0;
}

[[nodiscard]] bool isGltfMeshUri(const std::string &uri) {
  std::string extension = std::filesystem::path(uri).extension().string();
  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".gltf" || extension == ".glb";
}

[[nodiscard]] bool isSceneMeshAssetUri(const std::string &uri) {
  std::string extension = std::filesystem::path(uri).extension().string();
  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".obj" || extension == ".gltf" || extension == ".glb";
}

[[nodiscard]] std::filesystem::path
resolveGltfMeshPath(const std::vector<std::filesystem::path> &assetRoots,
                    const std::string &uri) {
  return resolveRuntimeOrProjectAssetPath(assetRoots, uri);
}

[[nodiscard]] std::optional<std::string>
primitiveUriFromNodeName(const std::string &nodeName) {
  constexpr const char *prefix = "primitive_";
  constexpr const char *suffix = "_node";
  if (nodeName.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }
  const usize begin = std::string_view(prefix).size();
  const usize suffixPos = nodeName.find(suffix, begin);
  if (suffixPos == std::string::npos || suffixPos == begin) {
    return std::nullopt;
  }
  const std::string shape = nodeName.substr(begin, suffixPos - begin);
  if (shape != "cube" && shape != "sphere" && shape != "plane" &&
      shape != "cylinder" && shape != "cone") {
    return std::nullopt;
  }
  return std::string(BuiltinPrimitivePrefix) + shape;
}

[[nodiscard]] std::optional<std::string>
patchUriFromNodeName(const std::string &nodeName) {
  constexpr const char *prefix = "patch_";
  constexpr const char *suffix = "_node";
  if (nodeName.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }
  const usize begin = std::string_view(prefix).size();
  const usize suffixPos = nodeName.find(suffix, begin);
  if (suffixPos == std::string::npos || suffixPos == begin) {
    return std::nullopt;
  }
  const std::string shape = nodeName.substr(begin, suffixPos - begin);
  if (shape != "triangle" && shape != "square" && shape != "circle") {
    return std::nullopt;
  }
  return std::string(BuiltinPatchPrefix) + shape;
}

[[nodiscard]] std::optional<std::string>
modelAssetIdFromNodeName(const std::string &nodeName) {
  constexpr const char *prefix = "model_";
  constexpr const char *suffix = "_node";
  if (nodeName.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }
  const usize begin = std::string_view(prefix).size();
  const usize suffixPos = nodeName.find(suffix, begin);
  if (suffixPos == std::string::npos || suffixPos == begin) {
    return std::nullopt;
  }
  return nodeName.substr(begin, suffixPos - begin);
}

[[nodiscard]] BuiltinAssetCatalog loadBuiltinAssetCatalog() {
  BuiltinAssetCatalog catalog;
  catalog.refresh(resolveRuntimePath("assets/models/builtin"));
  return catalog;
}

[[nodiscard]] std::string stripCopySuffix(const std::string &name) {
  const std::string suffix = ".copy";
  const auto suffixPos = name.rfind(suffix);
  if (suffixPos == std::string::npos) {
    return name;
  }
  const usize afterSuffix = suffixPos + suffix.size();
  if (afterSuffix == name.size()) {
    return name.substr(0, suffixPos);
  }
  if (afterSuffix + 4 == name.size() && name[afterSuffix] == '.' &&
      std::isdigit(static_cast<unsigned char>(name[afterSuffix + 1])) &&
      std::isdigit(static_cast<unsigned char>(name[afterSuffix + 2])) &&
      std::isdigit(static_cast<unsigned char>(name[afterSuffix + 3]))) {
    return name.substr(0, suffixPos);
  }
  return name;
}

[[nodiscard]] bool
isLegacyEditorHelperNode(const SceneNodeDocument &nodeDocument) {
  return isLegacyEditorHelperName(nodeDocument.nodeName) ||
         isLegacyEditorHelperName(nodeDocument.name);
}

[[nodiscard]] bool
isLegacyEditorHelperNode(const LX_core::SceneNodeSharedPtr &node) {
  return node && (isLegacyEditorHelperName(node->getNodeName()) ||
                  isLegacyEditorHelperName(node->getName()));
}

[[nodiscard]] std::string jsonEscape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

[[nodiscard]] std::string makeVec3Json(const LX_core::Vec3f &value) {
  std::ostringstream oss;
  oss << "{\"x\":" << value.x << ",\"y\":" << value.y << ",\"z\":" << value.z
      << "}";
  return oss.str();
}

[[nodiscard]] const char *
materialParameterTypeName(const LX_core::MaterialParameterValueType type) {
  switch (type) {
  case LX_core::MaterialParameterValueType::Float:
    return "float";
  case LX_core::MaterialParameterValueType::Int:
    return "int";
  case LX_core::MaterialParameterValueType::Vec3:
    return "Vec3";
  case LX_core::MaterialParameterValueType::Vec4:
    return "Vec4";
  }
  return "unknown";
}

[[nodiscard]] std::string
makeMaterialValueJson(const LX_core::MaterialParameterValue &value) {
  switch (value.type) {
  case LX_core::MaterialParameterValueType::Float:
    return std::to_string(value.floatValue);
  case LX_core::MaterialParameterValueType::Int:
    return std::to_string(value.intValue);
  case LX_core::MaterialParameterValueType::Vec3:
    return "[" + std::to_string(value.vectorValue.x) + "," +
           std::to_string(value.vectorValue.y) + "," +
           std::to_string(value.vectorValue.z) + "]";
  case LX_core::MaterialParameterValueType::Vec4:
    return "[" + std::to_string(value.vectorValue.x) + "," +
           std::to_string(value.vectorValue.y) + "," +
           std::to_string(value.vectorValue.z) + "," +
           std::to_string(value.vectorValue.w) + "]";
  }
  return "null";
}

[[nodiscard]] LX_core::CommandResult makeCommandError(std::string message) {
  return LX_core::CommandResult{false, std::move(message), {}, {}};
}

[[nodiscard]] LX_core::CommandResult makeCommandOk(std::string message,
                                                   std::string structured) {
  return LX_core::CommandResult{
      true, std::move(message), std::move(structured), {}};
}

[[nodiscard]] const std::vector<std::string> &materialPresetUris() {
  static const std::vector<std::string> kPresets = {
      "assets/materials/blinnphong_lit.material",
      "assets/materials/blinnphong_default.material",
      "assets/materials/blinnphong_textured.material",
      "assets/materials/pbr_gold.material",
  };
  return kPresets;
}

[[nodiscard]] bool
isEditorAssignableMaterialFilename(const std::string &filename) {
  return !filename.starts_with(".") && !filename.starts_with("test_");
}

[[nodiscard]] std::vector<std::string> discoverMaterialAssetUris() {
  std::vector<std::string> uris;
  const std::filesystem::path materialsDir =
      resolveRuntimePath("assets/materials");
  std::error_code error;
  if (!std::filesystem::exists(materialsDir, error)) {
    return uris;
  }
  for (const auto &entry :
       std::filesystem::directory_iterator(materialsDir, error)) {
    if (error || !entry.is_regular_file()) {
      continue;
    }
    const auto path = entry.path();
    const std::string filename = path.filename().string();
    if (path.extension() == ".material" &&
        isEditorAssignableMaterialFilename(filename)) {
      uris.push_back("assets/materials/" + filename);
    }
  }
  std::sort(uris.begin(), uris.end());
  return uris;
}

[[nodiscard]] bool isAllowedMaterialPreset(const std::string &uri) {
  const auto &presets = materialPresetUris();
  if (std::find(presets.begin(), presets.end(), uri) != presets.end()) {
    return true;
  }
  const auto assetUris = discoverMaterialAssetUris();
  return std::find(assetUris.begin(), assetUris.end(), uri) != assetUris.end();
}

[[nodiscard]] std::string normalizeMaterialUri(const SceneNodeDocument &node) {
  if (node.materialUri.has_value()) {
    if (*node.materialUri == "builtin://lxe_editor/ground_material") {
      return kDefaultGroundMaterial;
    }
    return *node.materialUri;
  }
  if (node.meshUri == "builtin://lxe_editor/ground_mesh") {
    return kDefaultGroundMaterial;
  }
  return {};
}

void setDocumentNodeMaterialUri(SceneNodeDocument &node,
                                const std::string &uri) {
  if (uri.empty()) {
    node.materialUri.reset();
    return;
  }
  node.materialUri = uri;
}

[[nodiscard]] bool
documentNodeHasMaterialSurface(const SceneNodeDocument &node) {
  return node.meshUri.has_value() || node.materialUri.has_value();
}

[[nodiscard]] bool
materialHasBaseColor(const LX_core::MaterialInstanceSharedPtr &material) {
  if (!material) {
    return false;
  }
  const auto layout =
      material->getParameterBufferLayout(LX_core::StringID("MaterialUBO"));
  if (!layout.has_value()) {
    return false;
  }
  const auto &members = layout->get().members;
  return std::any_of(members.begin(), members.end(), [](const auto &member) {
    return member.name == "baseColor" &&
           member.type == LX_core::ShaderPropertyType::Vec3;
  });
}

[[nodiscard]] const LX_core::MaterialInstance *
activeMaterialForNode(const LX_core::Scene &scene,
                      const LX_core::SceneNode &node) {
  const auto materialComponent =
      node.getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value()) {
    return nullptr;
  }
  if (const auto material = scene.resources().resolve(
          materialComponent->get().getMaterialHandle())) {
    return &material->get();
  }
  return materialComponent->get().getPendingMaterialInstance().get();
}

[[nodiscard]] LX_core::MaterialInstance *
activeMaterialForNode(LX_core::Scene &scene, LX_core::SceneNode &node) {
  const auto materialComponent =
      node.getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value()) {
    return nullptr;
  }
  if (const auto material = scene.resources().resolve(
          materialComponent->get().getMaterialHandle())) {
    return &material->get();
  }
  return materialComponent->get().getPendingMaterialInstance().get();
}

[[nodiscard]] bool
materialHasBaseColor(const LX_core::MaterialInstance *material) {
  if (!material) {
    return false;
  }
  const auto layout =
      material->getParameterBufferLayout(LX_core::StringID("MaterialUBO"));
  if (!layout.has_value()) {
    return false;
  }
  const auto &members = layout->get().members;
  return std::any_of(members.begin(), members.end(), [](const auto &member) {
    return member.name == "baseColor" &&
           member.type == LX_core::ShaderPropertyType::Vec3;
  });
}

void configureProceduralMaterialResources(
    const LX_core::MaterialInstanceSharedPtr &material,
    const ProceduralMaterialState &state) {
  if (!material || !state.enabled || !state.audioChannelBinding.has_value()) {
    return;
  }
  const LX_core::StringID bindingId(*state.audioChannelBinding);
  const auto canonical =
      material->getTemplate()
          ? material->getTemplate()->findCanonicalMaterialBinding(bindingId)
          : std::nullopt;
  if (!canonical.has_value() ||
      canonical->get().type != LX_core::ShaderPropertyType::Texture2D) {
    return;
  }

  LX_core::AudioSpectrumTexture audio(bindingId);
  material->setTexture(bindingId, audio.sampler());
}

void applyMaterialStateOverrides(
    const LX_core::MaterialInstanceSharedPtr &material,
    const MaterialOverrideState &materialOverrides,
    const MaterialOverrideState &nodeOverrides,
    const ProceduralMaterialState &proceduralMaterial) {
  LX_infra::scene_asset::applySceneMaterialOverrides(material,
                                                     materialOverrides);
  LX_infra::scene_asset::applySceneMaterialOverrides(material, nodeOverrides);
  configureProceduralMaterialResources(material, proceduralMaterial);
}

[[nodiscard]] LX_core::MaterialInstanceSharedPtr
loadMaterialForSceneNode(const std::vector<std::filesystem::path> &assetRoots,
                         const std::string &uri,
                         const MaterialOverrideState &materialOverrides,
                         const MaterialOverrideState &nodeOverrides,
                         const ProceduralMaterialState &proceduralMaterial =
                             ProceduralMaterialState{}) {
  std::optional<ScopedAccumulatedTimer> timer;
  if (g_sceneLoadTimingStats != nullptr) {
    timer.emplace(g_sceneLoadTimingStats->materialMs);
    ++g_sceneLoadTimingStats->materialLoadCount;
  }
  LX_infra::scene_io::MaterialBindingDocument binding;
  binding.uri = uri;
  auto material = LX_infra::scene_asset::loadSceneMaterialBinding({
      .meshUri = std::nullopt,
      .binding = std::move(binding),
      .materialOverrides = materialOverrides,
      .nodeMaterialOverrides = nodeOverrides,
      .resolveAssetPath =
          [&assetRoots](const std::string &assetUri) {
            return resolveProjectAssetPath(assetRoots, assetUri)
                .value_or(std::filesystem::path(assetUri));
          },
      .loadGenericMaterial =
          [](const std::filesystem::path &path) {
            return loadCachedGenericMaterial(path);
          },
  });
  configureProceduralMaterialResources(material, proceduralMaterial);
  return material;
}

[[nodiscard]] LX_core::MaterialInstanceSharedPtr
loadEffectiveMaterialForSceneNode(
    const std::vector<std::filesystem::path> &assetRoots,
    const SceneNodeDocument &nodeDocument, const std::string &uri,
    const MaterialOverrideState &materialOverrides,
    const MaterialOverrideState &nodeOverrides,
    const ProceduralMaterialState &proceduralMaterial =
        ProceduralMaterialState{}) {
  if (uri.empty() && nodeDocument.meshUri.has_value() &&
      isGltfMeshUri(*nodeDocument.meshUri)) {
    throw std::runtime_error("glTF scene node requires explicit material uri");
  }
  return loadMaterialForSceneNode(assetRoots, uri, materialOverrides,
                                  nodeOverrides, proceduralMaterial);
}

void applyEffectiveMaterialStateToExisting(
    const LX_core::MaterialInstanceSharedPtr &material,
    const MaterialOverrideState &materialOverrides,
    const MaterialOverrideState &nodeOverrides,
    const ProceduralMaterialState &proceduralMaterial) {
  applyMaterialStateOverrides(material, materialOverrides, nodeOverrides,
                              proceduralMaterial);
}

[[nodiscard]] LX_core::MaterialInstanceSharedPtr
loadEffectiveMaterialForSceneNode(
    const std::vector<std::filesystem::path> &assetRoots,
    const SceneNodeDocument &nodeDocument, const std::string &uri) {
  return loadEffectiveMaterialForSceneNode(
      assetRoots, nodeDocument, uri, nodeDocument.materialOverrides,
      nodeDocument.nodeMaterialOverrides, nodeDocument.proceduralMaterial);
}

[[nodiscard]] LX_core::MaterialInstanceSharedPtr
loadEffectiveMaterialForSceneNode(
    const std::vector<std::filesystem::path> &assetRoots,
    const SceneNodeDocument &nodeDocument,
    const MaterialOverrideState &nodeOverrides) {
  if (nodeDocument.meshUri.has_value() && isGltfMeshUri(*nodeDocument.meshUri) &&
      nodeDocument.materials.empty() && !nodeDocument.materialUri.has_value()) {
    throw std::runtime_error("glTF scene node requires explicit material uri");
  }

  return loadEffectiveMaterialForSceneNode(
      assetRoots, nodeDocument, normalizeMaterialUri(nodeDocument),
      nodeDocument.materialOverrides, nodeOverrides,
      nodeDocument.proceduralMaterial);
}

[[nodiscard]] LX_core::MaterialInstanceSharedPtr loadModelMaterialForSceneNode(
    const std::vector<std::filesystem::path> &assetRoots,
    const std::string &uri, const std::string &albedoTextureUri,
    const MaterialOverrideState &materialOverrides,
    const MaterialOverrideState &nodeOverrides,
    const ProceduralMaterialState &proceduralMaterial =
        ProceduralMaterialState{}) {
  std::optional<ScopedAccumulatedTimer> timer;
  if (g_sceneLoadTimingStats != nullptr) {
    timer.emplace(g_sceneLoadTimingStats->materialMs);
    ++g_sceneLoadTimingStats->materialLoadCount;
  }
  const std::filesystem::path materialPath =
      resolveProjectAssetPath(assetRoots, uri)
          .value_or(std::filesystem::path(uri));
  auto material = loadCachedGenericMaterial(materialPath);
  if (!material) {
    throw std::runtime_error("failed to load material: " + uri);
  }
  bindModelAlbedoTexture(material, albedoTextureUri);
  applyMaterialStateOverrides(material, materialOverrides, nodeOverrides,
                              proceduralMaterial);
  return material;
}

[[nodiscard]] std::filesystem::path
normalizeDocumentPath(const std::filesystem::path &path) {
  if (path.empty()) {
    throw std::runtime_error("scene document path is empty");
  }
  return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] std::reference_wrapper<LX_core::CameraComponent>
requireCameraComponent(const LX_core::SceneNodeSharedPtr &node,
                       const char *nodeLabel) {
  if (!node) {
    throw std::runtime_error(std::string("missing scene node: ") + nodeLabel);
  }
  const auto camera = node->getComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    throw std::runtime_error(std::string("missing camera component on ") +
                             nodeLabel);
  }
  return camera->get();
}

[[nodiscard]] LX_core::SceneNodeSharedPtr
makeCameraNode(const std::string &nodeName, const std::string &displayName,
               const LX_core::VisibilityLayerMask cullingMask) {
  auto node = LX_core::SceneNode::create(nodeName);
  node->setName(displayName);
  const auto camera = node->addComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    throw std::runtime_error("failed to create camera component for " +
                             nodeName);
  }
  camera->get().setTarget(LX_core::RenderTarget{});
  camera->get().setCullingMask(cullingMask);
  return node;
}

[[nodiscard]] LX_core::SceneNodeSharedPtr
makeRenderableNode(const std::string &nodeName, LX_core::MeshSharedPtr mesh,
                   LX_core::MaterialInstanceSharedPtr material) {
  auto node = LX_core::SceneNode::create(nodeName);
  node->addComponent<LX_core::MeshComponent>(std::move(mesh));
  node->addComponent<LX_core::MaterialComponent>(std::move(material));
  return node;
}

[[nodiscard]] LX_infra::scene_asset::GltfMeshAssetLoadResult
loadTimedGltfMeshAsset(const std::filesystem::path &meshPath) {
  std::optional<ScopedAccumulatedTimer> timer;
  if (g_sceneLoadTimingStats != nullptr) {
    timer.emplace(g_sceneLoadTimingStats->meshMs);
    ++g_sceneLoadTimingStats->meshLoadCount;
  }
  auto result = LX_infra::scene_asset::loadGltfMeshAsset(meshPath);
  accumulateLoadedMeshStats(result.mesh);
  return result;
}

[[nodiscard]] LX_core::MeshSharedPtr
loadTimedSceneMeshAsset(const std::filesystem::path &meshPath) {
  std::optional<ScopedAccumulatedTimer> timer;
  if (g_sceneLoadTimingStats != nullptr) {
    timer.emplace(g_sceneLoadTimingStats->meshMs);
    ++g_sceneLoadTimingStats->meshLoadCount;
  }
  auto mesh = LX_infra::scene_asset::loadSceneMeshAsset(meshPath);
  accumulateLoadedMeshStats(mesh);
  return mesh;
}

[[nodiscard]] LX_core::MaterialInstanceSharedPtr loadTaggedMaterialForSceneNode(
    const std::vector<std::filesystem::path> &assetRoots,
    const SceneNodeDocument &nodeDocument,
    const LX_infra::scene_io::MaterialBindingDocument &binding) {
  std::optional<ScopedAccumulatedTimer> timer;
  if (g_sceneLoadTimingStats != nullptr) {
    timer.emplace(g_sceneLoadTimingStats->materialMs);
    ++g_sceneLoadTimingStats->materialLoadCount;
  }
  LX_core::MaterialInstanceSharedPtr material;
  if (binding.source == "gltf") {
    material = LX_infra::scene_asset::loadSceneMaterialBinding({
        .meshUri = nodeDocument.meshUri,
        .binding = binding,
        .materialOverrides = nodeDocument.materialOverrides,
        .nodeMaterialOverrides = nodeDocument.nodeMaterialOverrides,
        .resolveAssetPath =
            [&assetRoots](const std::string &assetUri) {
              return resolveRuntimeOrProjectAssetPath(assetRoots, assetUri);
            },
        .loadGenericMaterial =
            [](const std::filesystem::path &path) {
              return loadCachedGenericMaterial(path);
            },
    });
  } else {
    material = LX_infra::scene_asset::loadSceneMaterialBinding({
        .meshUri = nodeDocument.meshUri,
        .binding = binding,
        .materialOverrides = nodeDocument.materialOverrides,
        .nodeMaterialOverrides = nodeDocument.nodeMaterialOverrides,
        .resolveAssetPath =
            [&assetRoots](const std::string &assetUri) {
              return resolveProjectAssetPath(assetRoots, assetUri)
                  .value_or(std::filesystem::path(assetUri));
            },
        .loadGenericMaterial =
            [](const std::filesystem::path &path) {
              return loadCachedGenericMaterial(path);
            },
    });
  }
  configureProceduralMaterialResources(material,
                                       nodeDocument.proceduralMaterial);
  return material;
}

[[nodiscard]] LX_core::SceneNodeSharedPtr
makeTaggedRenderableNode(const std::string &nodeName,
                         LX_core::MeshSharedPtr mesh,
                         const SceneNodeDocument &nodeDocument,
                         const std::vector<std::filesystem::path> &assetRoots) {
  if (nodeDocument.materials.empty()) {
    throw std::logic_error("tagged renderable node requires materials");
  }

  auto node = LX_core::SceneNode::create(nodeName);
  node->addComponent<LX_core::MeshComponent>(std::move(mesh));

  const auto &firstBinding = nodeDocument.materials.front();
  auto firstMaterial =
      loadTaggedMaterialForSceneNode(assetRoots, nodeDocument, firstBinding);
  auto materialComponent = node->addComponent<LX_core::MaterialComponent>(
      firstBinding.tag, std::move(firstMaterial));
  if (!materialComponent) {
    throw std::runtime_error("failed to attach tagged material component");
  }

  for (usize index = 1; index < nodeDocument.materials.size(); ++index) {
    const auto &binding = nodeDocument.materials[index];
    materialComponent->get().setTaggedMaterial(
        binding.tag,
        loadTaggedMaterialForSceneNode(assetRoots, nodeDocument, binding));
  }
  return node;
}

[[nodiscard]] std::string cameraPathToDisplayName(const std::string &path,
                                                  const std::string &fallback) {
  if (path.empty() || path == "/") {
    return fallback;
  }
  const auto slash = path.find_last_of('/');
  const std::string name =
      slash == std::string::npos ? path : path.substr(slash + 1);
  return name.empty() ? fallback : name;
}

[[nodiscard]] LX_core::Quatf lookAtRotation(const LX_core::Vec3f &eye,
                                            const LX_core::Vec3f &target,
                                            const LX_core::Vec3f &upHint) {
  LX_core::Vec3f forward = (target - eye).normalized();
  if (forward.length2() <= 1e-8f) {
    forward = LX_core::Vec3f{0.0f, 0.0f, -1.0f};
  }
  LX_core::Vec3f up = upHint.normalized();
  if (up.length2() <= 1e-8f) {
    up = LX_core::Vec3f{0.0f, 1.0f, 0.0f};
  }
  const LX_core::Vec3f back = (-forward).normalized();
  LX_core::Vec3f right = up.cross(back);
  if (right.length2() <= 1e-8f) {
    const LX_core::Vec3f fallbackUp = std::abs(forward.y) > 0.99f
                                          ? LX_core::Vec3f{1.0f, 0.0f, 0.0f}
                                          : LX_core::Vec3f{0.0f, 1.0f, 0.0f};
    right = fallbackUp.cross(back);
  }
  right = right.normalized();
  const LX_core::Vec3f correctedUp = back.cross(right).normalized();

  LX_core::Mat4f world = LX_core::Mat4f::identity();
  world(0, 0) = right.x;
  world(1, 0) = right.y;
  world(2, 0) = right.z;
  world(0, 1) = correctedUp.x;
  world(1, 1) = correctedUp.y;
  world(2, 1) = correctedUp.z;
  world(0, 2) = back.x;
  world(1, 2) = back.y;
  world(2, 2) = back.z;
  return LX_core::Transform::fromMat4(world).rotation.normalized();
}

[[nodiscard]] float focusDistance(const LX_core::Vec3f &eye,
                                  const LX_core::Vec3f &target) {
  return std::max((target - eye).length(), 1.0f);
}

[[nodiscard]] SceneDocument makeEmptySceneDocument() {
  SceneDocument document;
  document.setSceneName("Scene");
  document.setGameplayCameraPath("/game_cam");

  auto &rootNode = document.mutableRootNode();
  SceneNodeDocument gameCameraNode;
  gameCameraNode.nodeName = "game_camera";
  gameCameraNode.name = "game_cam";
  gameCameraNode.transform.translation = {0.0f, 2.0f, 6.0f};
  gameCameraNode.transform.rotation = lookAtRotation(
      {0.0f, 2.0f, 6.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  gameCameraNode.camera = CameraNodeState{
      .type = LX_core::CameraType::Perspective,
      .fovY = 45.0f,
      .aspect = 16.0f / 9.0f,
      .nearPlane = 0.1f,
      .farPlane = 1000.0f,
      .focusDistance = focusDistance({0.0f, 2.0f, 6.0f}, {0.0f, 0.0f, 0.0f}),
      .cullingMask = LX_core::Layer_All & ~LX_core::Layer_EditorOverlay &
                     ~Layer_EditorHelper,
  };
  rootNode.children.push_back(std::move(gameCameraNode));

  SceneNodeDocument directionalLightNode;
  directionalLightNode.nodeName = "dir_light_node";
  directionalLightNode.name = "dir_light";
  directionalLightNode.visibilityMask = LX_core::Layer_All;
  directionalLightNode.light = LightNodeState{
      .kind = LightKind::Directional,
      .direction = {-0.3f, -1.0f, -0.5f},
      .color = {1.0f, 0.98f, 0.9f},
      .intensity = 1.0f,
  };
  rootNode.children.push_back(std::move(directionalLightNode));
  return document;
}

[[nodiscard]] LX_core::SceneNodeSharedPtr buildRenderableNodeFromDocument(
    const SceneNodeDocument &nodeDocument,
    const std::vector<std::filesystem::path> &assetRoots) {
  if (!nodeDocument.meshUri.has_value()) {
    return LX_core::SceneNode::create(nodeDocument.nodeName);
  }

  if (*nodeDocument.meshUri == "builtin://lxe_editor/ground_mesh") {
    auto node = buildGroundNode();
    if (auto materialComponent =
            node->getComponent<LX_core::MaterialComponent>();
        materialComponent.has_value()) {
      const std::string uri = normalizeMaterialUri(nodeDocument);
      if (nodeDocument.materialUri.has_value() ||
          !nodeDocument.nodeMaterialOverrides.empty() ||
          !nodeDocument.materialOverrides.empty() ||
          nodeDocument.proceduralMaterial.enabled) {
        auto material =
            loadEffectiveMaterialForSceneNode(assetRoots, nodeDocument, uri);
        materialComponent->get().setMaterialInstance(std::move(material));
      }
    }
    return node;
  }

  if (isGltfMeshUri(*nodeDocument.meshUri)) {
    const std::filesystem::path meshPath =
        resolveGltfMeshPath(assetRoots, *nodeDocument.meshUri);
    if (!nodeDocument.materials.empty()) {
      auto meshAsset = loadTimedGltfMeshAsset(meshPath);
      return makeTaggedRenderableNode(nodeDocument.nodeName,
                                      std::move(meshAsset.mesh), nodeDocument,
                                      assetRoots);
    }
    if (nodeDocument.materialUri.has_value()) {
      auto meshAsset = loadTimedGltfMeshAsset(meshPath);
      const std::string materialUri = normalizeMaterialUri(nodeDocument);
      return makeRenderableNode(nodeDocument.nodeName,
                                std::move(meshAsset.mesh),
                                loadEffectiveMaterialForSceneNode(
                                    assetRoots, nodeDocument, materialUri));
    }

    throw std::runtime_error("glTF scene node requires explicit material uri");
  }

  if (isBuiltinPrimitiveMeshUri(*nodeDocument.meshUri)) {
    auto node =
        buildBuiltinPrimitiveNode(*nodeDocument.meshUri, nodeDocument.nodeName);
    if (auto materialComponent =
            node->getComponent<LX_core::MaterialComponent>();
        materialComponent.has_value()) {
      const std::string uri = normalizeMaterialUri(nodeDocument);
      if (nodeDocument.materialUri.has_value() ||
          !nodeDocument.nodeMaterialOverrides.empty() ||
          !nodeDocument.materialOverrides.empty() ||
          nodeDocument.proceduralMaterial.enabled) {
        auto material =
            loadEffectiveMaterialForSceneNode(assetRoots, nodeDocument, uri);
        materialComponent->get().setMaterialInstance(std::move(material));
      }
    }
    return node;
  }

  if (isBuiltinPatchMeshUri(*nodeDocument.meshUri)) {
    auto node =
        buildBuiltinPatchNode(*nodeDocument.meshUri, nodeDocument.nodeName);
    if (auto materialComponent =
            node->getComponent<LX_core::MaterialComponent>();
        materialComponent.has_value()) {
      const std::string uri = normalizeMaterialUri(nodeDocument);
      if (nodeDocument.materialUri.has_value() ||
          !nodeDocument.nodeMaterialOverrides.empty() ||
          !nodeDocument.materialOverrides.empty() ||
          nodeDocument.proceduralMaterial.enabled) {
        auto material =
            loadEffectiveMaterialForSceneNode(assetRoots, nodeDocument, uri);
        materialComponent->get().setMaterialInstance(std::move(material));
      }
    }
    return node;
  }

  if (isBuiltinModelMeshUri(*nodeDocument.meshUri)) {
    const std::string materialUri = normalizeMaterialUri(nodeDocument);
    const BuiltinAssetCatalog builtinAssets = loadBuiltinAssetCatalog();
    const auto asset = builtinAssets.findByMeshUri(*nodeDocument.meshUri);
    auto node = buildModelAssetNode(
        *nodeDocument.meshUri, materialUri,
        asset ? asset->albedoTextureUri : std::string{}, nodeDocument.nodeName);
    node->setName(nodeDocument.name);
    if (auto materialComponent =
            node->getComponent<LX_core::MaterialComponent>();
        materialComponent.has_value()) {
      if (nodeDocument.materialUri.has_value() ||
          !nodeDocument.nodeMaterialOverrides.empty() ||
          !nodeDocument.materialOverrides.empty() ||
          nodeDocument.proceduralMaterial.enabled) {
        materialComponent->get().setMaterialInstance(
            loadModelMaterialForSceneNode(assetRoots, materialUri,
                                          asset ? asset->albedoTextureUri
                                                : std::string{},
                                          nodeDocument.materialOverrides,
                                          nodeDocument.nodeMaterialOverrides,
                                          nodeDocument.proceduralMaterial));
      }
    }
    return node;
  }

  if (isSceneMeshAssetUri(*nodeDocument.meshUri)) {
    const std::filesystem::path meshPath =
        resolveProjectAssetPath(assetRoots, *nodeDocument.meshUri)
            .value_or(std::filesystem::path(*nodeDocument.meshUri));
    auto mesh = loadTimedSceneMeshAsset(meshPath);
    if (!nodeDocument.materials.empty()) {
      return makeTaggedRenderableNode(nodeDocument.nodeName, std::move(mesh),
                                      nodeDocument, assetRoots);
    }
    if (nodeDocument.materialUri.has_value()) {
      const std::string materialUri = normalizeMaterialUri(nodeDocument);
      return makeRenderableNode(
          nodeDocument.nodeName, std::move(mesh),
          loadEffectiveMaterialForSceneNode(assetRoots, nodeDocument,
                                            materialUri));
    }
    throw std::runtime_error("scene mesh node requires explicit material uri: " +
                             *nodeDocument.meshUri);
  }

  return LX_core::SceneNode::create(nodeDocument.nodeName);
}

void applyNodeIdentityAndTransform(LX_core::SceneNode &node,
                                   const SceneNodeDocument &documentNode) {
  node.setName(documentNode.name);
  node.setLocalTransform(documentNode.transform);
  node.setVisibilityLayerMask(documentNode.visibilityMask);
}

void applyCameraState(LX_core::SceneNode &node,
                      LX_core::CameraComponent &camera,
                      const CameraNodeState &state) {
  const float halfOrthoHeight =
      std::max(state.orthographicHeight, 0.001f) * 0.5f;
  const float halfOrthoWidth = halfOrthoHeight * std::max(state.aspect, 0.001f);
  camera.applyProjectionState(
      state.type, state.fovY, state.aspect, state.nearPlane, state.farPlane,
      -halfOrthoWidth, halfOrthoWidth, -halfOrthoHeight, halfOrthoHeight);
  camera.setTarget(LX_core::RenderTarget{});
  camera.setCullingMask(state.cullingMask & ~Layer_EditorHelper);
  camera.updateMatrices();
}

void configureDirectionalLight(LX_core::DirectionalLight &light,
                               const LightNodeState &state) {
  light.setDirection(state.direction);
  light.setColor(state.color);
  light.setIntensity(state.intensity);
  light.setShadowStrength(state.shadowStrength);
  light.setShadowDistance(state.shadowDistance);
  light.setShadowCascadeCount(state.shadowCascadeCount);
}

void configurePointLight(LX_core::PointLight &light,
                         const LightNodeState &state) {
  light.setColor(state.color);
  light.setIntensity(state.intensity);
  light.setRange(state.range);
}

void configureSpotLight(LX_core::SpotLight &light,
                        const LightNodeState &state) {
  light.setDirection(state.direction);
  light.setColor(state.color);
  light.setIntensity(state.intensity);
  light.setRange(state.range);
  light.setInnerConeDegrees(state.innerConeDegrees);
  light.setOuterConeDegrees(state.outerConeDegrees);
}

void buildSceneNodesRecursive(
    const SceneNodeDocument &nodeDocument,
    const LX_core::SceneNodeSharedPtr &parent,
    const std::shared_ptr<SceneRuntimeData> &runtime,
    std::unordered_map<std::string, LX_core::SceneNodeSharedPtr> &nodesByPath) {
  if (isRuntimeDebugDrawNode(nodeDocument)) {
    std::cerr << "[lxe_editor] skipping runtime-only scene node: "
              << nodeDocument.nodeName << "\n";
    return;
  }
  if (isLegacyEditorHelperNode(nodeDocument)) {
    std::cerr << "[lxe_editor] skipping legacy editor helper scene node: "
              << nodeDocument.nodeName << "\n";
    return;
  }

  if (g_sceneLoadTimingStats != nullptr) {
    ++g_sceneLoadTimingStats->nodeCount;
  }
  const auto nodeBegin = SceneLoadClock::now();

  LX_core::SceneNodeSharedPtr node;
  if (nodeDocument.camera.has_value()) {
    node = makeCameraNode(nodeDocument.nodeName,
                          nodeDocument.name.empty() ? nodeDocument.nodeName
                                                    : nodeDocument.name,
                          nodeDocument.camera->cullingMask);
  } else {
    node = buildRenderableNodeFromDocument(nodeDocument, runtime->assetRoots);
  }

  applyNodeIdentityAndTransform(*node, nodeDocument);
  if (parent) {
    node->setParent(parent);
  }

  if (nodeDocument.camera.has_value()) {
    auto &camera =
        requireCameraComponent(node, nodeDocument.nodeName.c_str()).get();
    applyCameraState(*node, camera, *nodeDocument.camera);
    std::optional<ScopedAccumulatedTimer> timer;
    if (g_sceneLoadTimingStats != nullptr) {
      timer.emplace(g_sceneLoadTimingStats->sceneRegisterMs);
    }
    runtime->scene->addCamera(node);
  } else {
    std::optional<ScopedAccumulatedTimer> timer;
    if (g_sceneLoadTimingStats != nullptr) {
      timer.emplace(g_sceneLoadTimingStats->sceneRegisterMs);
    }
    runtime->scene->addRenderable(node);
  }

  nodesByPath[node->getPath()] = node;

  if (nodeDocument.light.has_value()) {
    switch (nodeDocument.light->kind) {
    case LightKind::Directional: {
      auto light = std::make_shared<LX_core::DirectionalLight>();
      configureDirectionalLight(*light, *nodeDocument.light);
      runtime->scene->attachLight(node, light);
      break;
    }
    case LightKind::Point: {
      auto light = std::make_shared<LX_core::PointLight>();
      configurePointLight(*light, *nodeDocument.light);
      runtime->scene->attachLight(node, light);
      break;
    }
    case LightKind::Spot: {
      auto light = std::make_shared<LX_core::SpotLight>();
      configureSpotLight(*light, *nodeDocument.light);
      runtime->scene->attachLight(node, light);
      break;
    }
    }
  }

  const double nodeMs = elapsedMs(nodeBegin, SceneLoadClock::now());
  if (nodeMs >= 500.0) {
    std::cerr << "[lxe_editor][scene-load] slow node nodeName='"
              << nodeDocument.nodeName << "' name='" << nodeDocument.name
              << "' meshUri='"
              << (nodeDocument.meshUri.has_value() ? *nodeDocument.meshUri
                                                    : std::string{})
              << "' ms=" << nodeMs << "\n";
  }

  for (const auto &childDocument : nodeDocument.children) {
    buildSceneNodesRecursive(childDocument, node, runtime, nodesByPath);
  }
}

[[nodiscard]] std::shared_ptr<SceneRuntimeData>
buildRuntimeFromDocument(const SceneDocument &document,
                         const std::optional<std::filesystem::path> &path,
                         std::vector<std::filesystem::path> assetRoots = {}) {
  LX_core::SceneRealtimeRenderSettings effectiveRealtimeSettings =
      document.realtimeRenderSettings();
  const bool environmentEnabled =
      document.hasEnvironment() && document.environment().enabled;
  effectiveRealtimeSettings.ibl =
      effectiveRealtimeSettings.ibl && environmentEnabled;
  ScopedSceneRealtimeRenderSettings realtimeSettingsScope(
      effectiveRealtimeSettings);

  auto runtime = std::make_shared<SceneRuntimeData>();
  runtime->documentPath = path;
  runtime->document = document;
  runtime->assetRoots = std::move(assetRoots);
  runtime->scene = LX_core::Scene::create(document.sceneName(), nullptr);
  runtime->scene->setRenderSettings(document.renderSettings());
  runtime->scene->setRealtimeRenderSettings(effectiveRealtimeSettings);
  if (environmentEnabled) {
    runtime->scene->resources().setIblEnvironmentResources(
        loadEnvironmentResources(document.environment(), runtime->assetRoots,
                                 effectiveRealtimeSettings.ibl));
  }

  while (!runtime->scene->getLightHandles().empty()) {
    runtime->scene->removeLight(runtime->scene->getLightHandles().front());
  }

  std::unordered_map<std::string, LX_core::SceneNodeSharedPtr> nodesByPath;
  auto rootNode = runtime->scene->getRootNode();
  applyNodeIdentityAndTransform(*rootNode, document.rootNode());
  for (const auto &childDocument : document.rootNode().children) {
    buildSceneNodesRecursive(childDocument, rootNode, runtime, nodesByPath);
  }

  const std::string gameplayPath = document.gameplayCameraPath();
  const auto gameplayNodeIt = nodesByPath.find(gameplayPath);
  if (gameplayNodeIt == nodesByPath.end()) {
    throw std::runtime_error(
        "gameplay camera path not found in scene document: " + gameplayPath);
  }
  runtime->gameCameraNode = gameplayNodeIt->second;

  runtime->editorCameraNode =
      makeCameraNode("editor_camera", "editor_cam", LX_core::Layer_All);
  runtime->editorCameraNode->setVisibilityLayerMask(
      LX_core::Layer_EditorOverlay);
  auto &editorCamera =
      requireCameraComponent(runtime->editorCameraNode, "editor_camera").get();
  auto &gameCamera =
      requireCameraComponent(runtime->gameCameraNode, "game_camera").get();
  if (document.hasEditorCamera()) {
    document.editorCamera().applyTo(*runtime->editorCameraNode, editorCamera);
  } else {
    editorCamera.lookAt(gameCamera.getEyePosition(), gameCamera.getLookTarget(),
                        gameCamera.getUpVector());
    runtime->editorCameraNode->setLocalTransform(
        runtime->gameCameraNode->getLocalTransform());
    editorCamera.applyProjectionState(
        gameCamera.getProjectionType(), gameCamera.getFovY(),
        gameCamera.getAspect(), gameCamera.getNearPlane(),
        gameCamera.getFarPlane(), gameCamera.getLeft(), gameCamera.getRight(),
        gameCamera.getBottom(), gameCamera.getTop());
  }
  runtime->scene->addCamera(runtime->editorCameraNode);

  return runtime;
}

[[nodiscard]] std::shared_ptr<SceneRuntimeData>
requireRuntimeData(const std::shared_ptr<void> &impl) {
  if (!impl) {
    throw std::runtime_error("scene runtime is not loaded");
  }
  return std::static_pointer_cast<SceneRuntimeData>(impl);
}

[[nodiscard]] const SceneNodeDocument *
findDocumentNodeByName(const SceneNodeDocument &node,
                       const std::string &nodeName) {
  if (node.nodeName == nodeName) {
    return &node;
  }
  for (const auto &child : node.children) {
    if (const auto *match = findDocumentNodeByName(child, nodeName)) {
      return match;
    }
  }
  return nullptr;
}

[[nodiscard]] SceneNodeDocument *
findDocumentNodeByName(SceneNodeDocument &node, const std::string &nodeName) {
  if (node.nodeName == nodeName) {
    return &node;
  }
  for (auto &child : node.children) {
    if (auto *match = findDocumentNodeByName(child, nodeName)) {
      return match;
    }
  }
  return nullptr;
}

[[nodiscard]] SceneNodeDocument *
findDocumentNodeForRuntimePath(SceneRuntimeData &runtime,
                               const std::string &path) {
  if (!runtime.scene) {
    return nullptr;
  }
  LX_core::SceneNode *node = runtime.scene->findByPath(path);
  if (!node) {
    return nullptr;
  }
  return findDocumentNodeByName(runtime.document.mutableRootNode(),
                                node->getNodeName());
}

[[nodiscard]] const SceneNodeDocument *
findDocumentNodeForRuntimePath(const SceneRuntimeData &runtime,
                               const std::string &path) {
  if (!runtime.scene) {
    return nullptr;
  }
  LX_core::SceneNode *node = runtime.scene->findByPath(path);
  if (!node) {
    return nullptr;
  }
  return findDocumentNodeByName(runtime.document.rootNode(),
                                node->getNodeName());
}

void forEachDocumentNode(SceneNodeDocument &node,
                         const std::function<void(SceneNodeDocument &)> &fn) {
  fn(node);
  for (auto &child : node.children) {
    forEachDocumentNode(child, fn);
  }
}

void forEachRuntimeNode(const LX_core::SceneNodeSharedPtr &node,
                        const std::function<void(LX_core::SceneNode &)> &fn) {
  if (!node) {
    return;
  }
  fn(*node);
  for (const auto &child : node->getChildren()) {
    forEachRuntimeNode(child, fn);
  }
}

[[nodiscard]] const SceneNodeDocument *
findDocumentNodeByNameOrCopySource(const SceneNodeDocument &node,
                                   const std::string &nodeName) {
  if (const auto *exact = findDocumentNodeByName(node, nodeName)) {
    return exact;
  }
  const std::string sourceName = stripCopySuffix(nodeName);
  if (sourceName == nodeName) {
    return nullptr;
  }
  return findDocumentNodeByName(node, sourceName);
}

[[nodiscard]] CameraNodeState
captureCameraState(const LX_core::CameraComponent &camera) {
  const float orthographicHeight =
      std::max(camera.getTop() - camera.getBottom(), 0.001f);
  const LX_core::Vec3f eye = camera.getEyePosition();
  const LX_core::Vec3f target = camera.getLookTarget();
  return CameraNodeState{
      .type = camera.getProjectionType(),
      .fovY = camera.getFovY(),
      .aspect = camera.getAspect(),
      .nearPlane = camera.getNearPlane(),
      .farPlane = camera.getFarPlane(),
      .orthographicHeight = orthographicHeight,
      .focusDistance = focusDistance(eye, target),
      .cullingMask = camera.getCullingMask(),
  };
}

[[nodiscard]] LightNodeState
captureDirectionalLightState(const LX_core::DirectionalLight &light) {
  return LightNodeState{
      .kind = LightKind::Directional,
      .direction = light.getDirection(),
      .color = light.getColor(),
      .intensity = light.getIntensity(),
      .shadowStrength = light.getShadowParams().z,
      .shadowDistance = light.getShadowDistance(),
      .shadowCascadeCount = light.getShadowCascadeCount(),
  };
}

[[nodiscard]] LightNodeState
capturePointLightState(const LX_core::PointLight &light) {
  return LightNodeState{
      .kind = LightKind::Point,
      .color = light.getColor(),
      .intensity = light.getIntensity(),
      .range = light.getRange(),
  };
}

[[nodiscard]] LightNodeState
captureSpotLightState(const LX_core::SpotLight &light) {
  return LightNodeState{
      .kind = LightKind::Spot,
      .direction = light.getDirection(),
      .color = light.getColor(),
      .intensity = light.getIntensity(),
      .range = light.getRange(),
      .innerConeDegrees = light.getInnerConeDegrees(),
      .outerConeDegrees = light.getOuterConeDegrees(),
  };
}

[[nodiscard]] SceneDocument
captureSceneDocument(const std::shared_ptr<SceneRuntimeData> &runtime) {
  SceneDocument document;
  document.setSceneName(runtime->scene ? runtime->scene->getSceneName()
                                       : "Scene");
  document.setGameplayCameraPath(runtime->gameCameraNode
                                     ? runtime->gameCameraNode->getPath()
                                     : "/game_cam");
  document.setRenderSettings(runtime->scene ? runtime->scene->renderSettings()
                                            : runtime->document.renderSettings());
  document.setRealtimeRenderSettings(
      runtime->document.realtimeRenderSettings());
  if (runtime->document.hasEnvironment()) {
    document.setEnvironment(runtime->document.environment());
  }
  if (runtime->document.hasRenderProfileDocument()) {
    document.setRenderProfileDocument(
        runtime->document.renderProfileDocument());
  }
  const BuiltinAssetCatalog builtinAssets = loadBuiltinAssetCatalog();

  auto captureNode =
      [&](const auto &self,
          const LX_core::SceneNodeSharedPtr &node) -> SceneNodeDocument {
    SceneNodeDocument entry;
    entry.nodeName = node->getNodeName();
    entry.name = node->getName();
    entry.transform = node->getLocalTransform();
    entry.visibilityMask = node->getVisibilityLayerMask();

    if (const auto *existing = findDocumentNodeByNameOrCopySource(
            runtime->document.rootNode(), node->getNodeName())) {
      entry.visibilityMask = existing->visibilityMask;
      entry.meshUri = existing->meshUri;
      entry.materialUri = existing->materialUri;
      entry.proceduralMaterial = existing->proceduralMaterial;
      entry.nodeMaterialOverrides = existing->nodeMaterialOverrides;
      entry.materialOverrides = existing->materialOverrides;
    } else if (node->getName() == "ground") {
      entry.meshUri = "builtin://lxe_editor/ground_mesh";
      entry.materialUri = kDefaultGroundMaterial;
    } else if (const auto primitiveUri =
                   primitiveUriFromNodeName(node->getNodeName())) {
      entry.meshUri = *primitiveUri;
      entry.materialUri = BuiltinPrimitiveMaterial;
    } else if (const auto patchUri =
                   patchUriFromNodeName(node->getNodeName())) {
      entry.meshUri = *patchUri;
      entry.materialUri = BuiltinPrimitiveMaterial;
    } else if (const auto assetId =
                   modelAssetIdFromNodeName(node->getNodeName())) {
      if (const auto asset = builtinAssets.findByAssetId(*assetId)) {
        entry.meshUri = asset->meshUri;
        entry.materialUri = asset->defaultMaterialUri;
      }
    }

    if (const auto camera = node->getComponent<LX_core::CameraComponent>();
        camera.has_value()) {
      entry.camera = captureCameraState(camera->get());
    }

    if (const auto light = runtime->scene->getLight(*node)) {
      const LX_core::LightBase &lightRef = light->get();
      if (const auto *directional =
              dynamic_cast<const LX_core::DirectionalLight *>(&lightRef)) {
        entry.light = captureDirectionalLightState(*directional);
      } else if (const auto *point =
                     dynamic_cast<const LX_core::PointLight *>(&lightRef)) {
        entry.light = capturePointLightState(*point);
      } else if (const auto *spot =
                     dynamic_cast<const LX_core::SpotLight *>(&lightRef)) {
        entry.light = captureSpotLightState(*spot);
      }
    }

    for (const auto &child : node->getChildren()) {
      if (!child || child == runtime->editorCameraNode ||
          isRuntimeDebugDrawNode(child) || isLegacyEditorHelperNode(child)) {
        continue;
      }
      entry.children.push_back(self(self, child));
    }

    return entry;
  };

  auto &rootEntry = document.mutableRootNode();
  if (runtime->scene && runtime->scene->getRootNode()) {
    const auto &rootNode = runtime->scene->getRootNode();
    rootEntry.nodeName = rootNode->getNodeName();
    rootEntry.name = rootNode->getName();
    rootEntry.parentPath.clear();
    rootEntry.transform = rootNode->getLocalTransform();
    rootEntry.visibilityMask = rootNode->getVisibilityLayerMask();
    rootEntry.meshUri.reset();
    rootEntry.materialUri.reset();
    rootEntry.proceduralMaterial = ProceduralMaterialState{};
    rootEntry.nodeMaterialOverrides = MaterialOverrideState{};
    rootEntry.materialOverrides = MaterialOverrideState{};
    rootEntry.camera.reset();
    rootEntry.light.reset();
    rootEntry.children.clear();

    for (const auto &child : rootNode->getChildren()) {
      if (!child || child == runtime->editorCameraNode ||
          isRuntimeDebugDrawNode(child) || isLegacyEditorHelperNode(child)) {
        continue;
      }
      rootEntry.children.push_back(captureNode(captureNode, child));
    }
  }

  auto &editorCamera =
      requireCameraComponent(runtime->editorCameraNode, "editor_camera").get();
  document.setEditorCamera(
      EditorCameraState::captureFrom(*runtime->editorCameraNode, editorCamera));
  return document;
}

} // namespace

void SceneRuntime::createEmptyScene() {
  m_impl = buildRuntimeFromDocument(makeEmptySceneDocument(), std::nullopt);
}

void SceneRuntime::loadFromDocumentPath(const std::filesystem::path &path) {
  SceneLoadTimingStats timing;
  SceneLoadMaterialCache materialCache;
  ScopedSceneLoadTimingContext timingContext(timing, materialCache);
  const auto totalBegin = SceneLoadClock::now();
  const std::filesystem::path normalizedPath = normalizeDocumentPath(path);
  SceneDocument document;
  {
    const auto begin = SceneLoadClock::now();
    document = loadSceneDocument(normalizedPath);
    timing.documentMs = elapsedMs(begin, SceneLoadClock::now());
  }
  std::vector<std::filesystem::path> assetRoots;
  {
    const auto begin = SceneLoadClock::now();
    assetRoots = discoverProjectAssetRoots(normalizedPath);
    timing.assetRootMs = elapsedMs(begin, SceneLoadClock::now());
  }
  {
    const auto begin = SceneLoadClock::now();
    m_impl = buildRuntimeFromDocument(document, normalizedPath,
                                      std::move(assetRoots));
    timing.buildRuntimeMs = elapsedMs(begin, SceneLoadClock::now());
  }
  const double totalMs = elapsedMs(totalBegin, SceneLoadClock::now());
  std::cerr << "[lxe_editor][scene-load] path='"
            << pathForLog(normalizedPath) << "' totalMs=" << totalMs
            << " documentMs=" << timing.documentMs
            << " assetRootMs=" << timing.assetRootMs
            << " buildRuntimeMs=" << timing.buildRuntimeMs
            << " environmentMs=" << timing.environmentMs
            << " nodeCount=" << timing.nodeCount
            << " meshLoadCount=" << timing.meshLoadCount
            << " meshVertexCount=" << timing.meshVertexCount
            << " meshIndexCount=" << timing.meshIndexCount
            << " meshTriangleCount=" << timing.meshTriangleCount
            << " meshMs=" << timing.meshMs
            << " materialLoadCount=" << timing.materialLoadCount
            << " materialPrototypeLoadCount="
            << timing.materialPrototypeLoadCount
            << " materialCacheHitCount=" << timing.materialCacheHitCount
            << " materialMs=" << timing.materialMs
            << " sceneRegisterMs=" << timing.sceneRegisterMs << "\n";
}

void SceneRuntime::saveToCurrentDocumentPath() {
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->documentPath.has_value()) {
    throw std::runtime_error("scene runtime has no current document path");
  }
  saveToDocumentPath(*runtime->documentPath);
}

void SceneRuntime::saveToDocumentPath(const std::filesystem::path &path) {
  const auto runtime = requireRuntimeData(m_impl);
  const std::filesystem::path normalizedPath = normalizeDocumentPath(path);
  SceneDocument document = captureSceneDocument(runtime);
  saveSceneDocument(normalizedPath, document);
  runtime->document = std::move(document);
  runtime->documentPath = normalizedPath;
}

std::optional<std::filesystem::path> SceneRuntime::documentPath() const {
  const auto runtime = requireRuntimeData(m_impl);
  return runtime->documentPath;
}

const SceneDocument &SceneRuntime::document() const {
  return requireRuntimeData(m_impl)->document;
}

LX_core::SceneSharedPtr SceneRuntime::scene() const {
  return requireRuntimeData(m_impl)->scene;
}

LX_core::SceneNodeSharedPtr SceneRuntime::editorCameraNode() const {
  return requireRuntimeData(m_impl)->editorCameraNode;
}

LX_core::SceneNodeSharedPtr SceneRuntime::gameCameraNode() const {
  return requireRuntimeData(m_impl)->gameCameraNode;
}

std::optional<std::string>
SceneRuntime::materialUriForNode(const std::string &path) const {
  const auto runtime = requireRuntimeData(m_impl);
  const auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return std::nullopt;
  }
  if (!documentNode->meshUri.has_value() &&
      !documentNode->materialUri.has_value()) {
    return std::nullopt;
  }
  return normalizeMaterialUri(*documentNode);
}

std::optional<LX_core::Vec3f>
SceneRuntime::nodeMaterialBaseColorForNode(const std::string &path) const {
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->scene) {
    return std::nullopt;
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return std::nullopt;
  }
  const LX_core::MaterialInstance *material =
      activeMaterialForNode(*runtime->scene, *node);
  if (!material) {
    return std::nullopt;
  }
  const auto value = material->readParameterValue(
      LX_core::StringID("MaterialUBO"), LX_core::StringID("baseColor"));
  if (!value.has_value() ||
      value->type != LX_core::MaterialParameterValueType::Vec3) {
    return std::nullopt;
  }
  return LX_core::Vec3f{value->vectorValue.x, value->vectorValue.y,
                        value->vectorValue.z};
}

bool SceneRuntime::nodeMaterialBaseColorEditable(
    const std::string &path) const {
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->scene) {
    return false;
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return false;
  }
  return materialHasBaseColor(activeMaterialForNode(*runtime->scene, *node));
}

std::vector<std::string> SceneRuntime::materialPresets() const {
  std::vector<std::string> out = materialPresetUris();
  const auto discovered = discoverMaterialAssetUris();
  out.insert(out.end(), discovered.begin(), discovered.end());
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::optional<LX_core::MaterialParameterValue>
SceneRuntime::nodeMaterialParameterForNode(const std::string &path,
                                           const std::string &binding,
                                           const std::string &member) const {
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->scene) {
    return std::nullopt;
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return std::nullopt;
  }
  const LX_core::MaterialInstance *material =
      activeMaterialForNode(*runtime->scene, *node);
  if (!material) {
    return std::nullopt;
  }
  return material->readParameterValue(LX_core::StringID(binding),
                                      LX_core::StringID(member));
}

std::vector<RuntimeMaterialParameterValue>
SceneRuntime::nodeMaterialParametersForNode(const std::string &path) const {
  std::vector<RuntimeMaterialParameterValue> out;
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->scene) {
    return out;
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return out;
  }
  const LX_core::MaterialInstance *material =
      activeMaterialForNode(*runtime->scene, *node);
  if (!material) {
    return out;
  }
  if (!material->getTemplate()) {
    return out;
  }
  const auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  const auto isRuntimeOwned = [&](const std::string &binding,
                                  const std::string &member) {
    if (!documentNode || !documentNode->proceduralMaterial.enabled) {
      return false;
    }
    const ProceduralMaterialState &state = documentNode->proceduralMaterial;
    if (binding != state.binding) {
      return false;
    }
    return member == state.timeMember || member == state.resolutionMember ||
           (state.audioBandsMember.has_value() &&
            member == *state.audioBandsMember);
  };
  for (const auto &[bindingId, binding] :
       material->getTemplate()->getCanonicalMaterialBindings()) {
    if (binding.type != LX_core::ShaderPropertyType::UniformBuffer &&
        binding.type != LX_core::ShaderPropertyType::StorageBuffer) {
      continue;
    }
    for (const auto &member : binding.members) {
      if (member.type != LX_core::ShaderPropertyType::Float &&
          member.type != LX_core::ShaderPropertyType::Int &&
          member.type != LX_core::ShaderPropertyType::Vec3 &&
          member.type != LX_core::ShaderPropertyType::Vec4) {
        continue;
      }
      const auto value = material->readParameterValue(
          bindingId, LX_core::StringID(member.name));
      if (!value.has_value()) {
        continue;
      }
      out.push_back(RuntimeMaterialParameterValue{
          .binding = binding.name,
          .member = member.name,
          .value = *value,
          .runtimeOwned = isRuntimeOwned(binding.name, member.name)});
    }
  }
  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
    return a.binding + "." + a.member < b.binding + "." + b.member;
  });
  return out;
}

std::optional<bool>
SceneRuntime::proceduralMaterialEnabledForNode(const std::string &path) const {
  const auto runtime = requireRuntimeData(m_impl);
  const auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode || !documentNodeHasMaterialSurface(*documentNode)) {
    return std::nullopt;
  }
  return documentNode->proceduralMaterial.enabled;
}

std::vector<std::string>
SceneRuntime::updateProceduralMaterials(const float totalTime,
                                        const LX_core::Vec2f &resolution) {
  std::vector<std::string> diagnostics;
  const auto runtime = requireRuntimeData(m_impl);
  if (!runtime->scene) {
    diagnostics.push_back("scene runtime is not loaded");
    return diagnostics;
  }

  const auto writeRequiredFloat = [&](LX_core::MaterialInstance &material,
                                      const ProceduralMaterialState &state,
                                      const std::string &path) {
    const auto member = material.findParameterMember(
        LX_core::StringID(state.binding), LX_core::StringID(state.timeMember));
    if (!member.has_value()) {
      diagnostics.push_back("procedural time member missing on " + path + ": " +
                            state.binding + "." + state.timeMember);
      return;
    }
    if (member->get().type != LX_core::ShaderPropertyType::Float) {
      diagnostics.push_back("procedural time member is not Float on " + path +
                            ": " + state.binding + "." + state.timeMember);
      return;
    }
    material.setParameter(LX_core::StringID(state.binding),
                          LX_core::StringID(state.timeMember), totalTime);
  };

  const auto writeRequiredResolution = [&](LX_core::MaterialInstance &material,
                                           const ProceduralMaterialState &state,
                                           const std::string &path) {
    const auto member =
        material.findParameterMember(LX_core::StringID(state.binding),
                                     LX_core::StringID(state.resolutionMember));
    if (!member.has_value()) {
      diagnostics.push_back("procedural resolution member missing on " + path +
                            ": " + state.binding + "." +
                            state.resolutionMember);
      return;
    }
    if (member->get().type != LX_core::ShaderPropertyType::Vec4) {
      diagnostics.push_back("procedural resolution member is not Vec4 on " +
                            path + ": " + state.binding + "." +
                            state.resolutionMember);
      return;
    }
    const float width = std::max(resolution.x, 1.0f);
    const float height = std::max(resolution.y, 1.0f);
    material.setParameter(
        LX_core::StringID(state.binding),
        LX_core::StringID(state.resolutionMember),
        LX_core::Vec4f{width, height, 1.0f / width, 1.0f / height});
  };

  const auto writeOptionalAudioBands =
      [&](LX_core::MaterialInstance &material,
          const ProceduralMaterialState &state) {
        if (!state.audioBandsMember.has_value()) {
          return;
        }
        const auto member = material.findParameterMember(
            LX_core::StringID(state.binding),
            LX_core::StringID(*state.audioBandsMember));
        if (!member.has_value()) {
          return;
        }
        if (member->get().type != LX_core::ShaderPropertyType::Vec4) {
          diagnostics.push_back("procedural audioBands member is not Vec4: " +
                                state.binding + "." + *state.audioBandsMember);
          return;
        }
        const float bass = 0.45f + 0.25f * std::sin(totalTime * 1.7f);
        const float mid = 0.35f + 0.20f * std::sin(totalTime * 2.3f + 0.4f);
        material.setParameter(LX_core::StringID(state.binding),
                              LX_core::StringID(*state.audioBandsMember),
                              LX_core::Vec4f{bass, mid, 0.0f, 0.0f});
      };

  const auto writeOptionalAudioChannel =
      [&](LX_core::Scene &scene, LX_core::MaterialInstance &material,
          const ProceduralMaterialState &state) {
        if (!state.audioChannelBinding.has_value()) {
          return;
        }
        const LX_core::StringID bindingId(*state.audioChannelBinding);
        const LX_core::TextureHandle textureHandle =
            material.getTextureHandle(bindingId);
        if (!textureHandle.isValid()) {
          return;
        }
        const auto sampler = scene.resources().resolve(textureHandle);
        if (!sampler.has_value() || !sampler->get().texture()) {
          return;
        }
        sampler->get().update(LX_core::AudioSpectrumTexture::makeFakePixels(
            sampler->get().texture()->desc().width, totalTime));
      };

  forEachRuntimeNode(
      runtime->scene->getRootNode(), [&](LX_core::SceneNode &node) {
        auto *documentNode = findDocumentNodeByName(
            runtime->document.mutableRootNode(), node.getNodeName());
        if (!documentNode || !documentNode->proceduralMaterial.enabled) {
          return;
        }
        LX_core::MaterialInstance *material =
            activeMaterialForNode(*runtime->scene, node);
        if (!material) {
          diagnostics.push_back("procedural node has no material: " +
                                node.getPath());
          return;
        }
        writeRequiredFloat(*material, documentNode->proceduralMaterial,
                           node.getPath());
        writeRequiredResolution(*material, documentNode->proceduralMaterial,
                                node.getPath());
        writeOptionalAudioBands(*material, documentNode->proceduralMaterial);
        writeOptionalAudioChannel(*runtime->scene, *material,
                                  documentNode->proceduralMaterial);
        material->syncGpuData();
      });

  return diagnostics;
}

LX_core::CommandResult
SceneRuntime::setNodeMaterialUri(const std::string &path,
                                 const std::string &uri) {
  if (!isAllowedMaterialPreset(uri)) {
    return makeCommandError("unsupported material preset: " + uri);
  }

  const auto runtime = requireRuntimeData(m_impl);
  auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return makeCommandError("node not found: " + path);
  }

  try {
    auto material = loadEffectiveMaterialForSceneNode(runtime->assetRoots,
                                                      *documentNode, uri);
    auto materialComponent = node->getComponent<LX_core::MaterialComponent>();
    if (materialComponent.has_value()) {
      materialComponent->get().setMaterialInstance(std::move(material));
    } else {
      node->addComponent<LX_core::MaterialComponent>(std::move(material));
    }
  } catch (const std::exception &error) {
    return makeCommandError(std::string("failed to set materialUri: ") +
                            error.what());
  }
  setDocumentNodeMaterialUri(*documentNode, uri);

  return makeCommandOk("materialUri updated",
                       "{\"path\":\"" + jsonEscape(path) +
                           "\",\"materialUri\":\"" + jsonEscape(uri) + "\"}");
}

LX_core::CommandResult
SceneRuntime::setNodeMaterialBaseColor(const std::string &path,
                                       const LX_core::Vec3f &color) {
  const auto runtime = requireRuntimeData(m_impl);
  auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return makeCommandError("node not found: " + path);
  }
  auto materialComponent = node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value()) {
    return makeCommandError("node has no material component: " + path);
  }

  const std::string uri = normalizeMaterialUri(*documentNode);
  try {
    MaterialOverrideState nodeOverrides = documentNode->nodeMaterialOverrides;
    nodeOverrides.baseColor = color;
    auto material = loadEffectiveMaterialForSceneNode(
        runtime->assetRoots, *documentNode, nodeOverrides);
    if (!materialHasBaseColor(material)) {
      return makeCommandError(
          "material does not expose MaterialUBO.baseColor: " + uri);
    }
    materialComponent->get().setMaterialInstance(std::move(material));
    if (!uri.empty()) {
      setDocumentNodeMaterialUri(*documentNode, uri);
    }
    documentNode->nodeMaterialOverrides = nodeOverrides;
  } catch (const std::exception &error) {
    return makeCommandError(
        std::string("failed to set node material baseColor: ") + error.what());
  }

  return makeCommandOk("node material baseColor updated",
                       "{\"path\":\"" + jsonEscape(path) +
                           "\",\"baseColor\":" + makeVec3Json(color) + "}");
}

LX_core::CommandResult SceneRuntime::setNodeMaterialParameter(
    const std::string &path, const std::string &binding,
    const std::string &member, const LX_core::MaterialParameterValue &value) {
  if (binding == "MaterialUBO" && member == "baseColor") {
    if (value.type != LX_core::MaterialParameterValueType::Vec3) {
      return makeCommandError(
          "MaterialUBO.baseColor requires Vec3 material parameter value");
    }
    return setNodeMaterialBaseColor(path, LX_core::Vec3f{value.vectorValue.x,
                                                         value.vectorValue.y,
                                                         value.vectorValue.z});
  }

  const auto runtime = requireRuntimeData(m_impl);
  auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return makeCommandError("node not found: " + path);
  }
  auto materialComponent = node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value()) {
    return makeCommandError("node has no material component: " + path);
  }

  const std::string uri = normalizeMaterialUri(*documentNode);
  const std::string key = binding + "." + member;
  try {
    MaterialOverrideState nodeOverrides = documentNode->nodeMaterialOverrides;
    nodeOverrides.parameters[key] = value;
    auto material = loadEffectiveMaterialForSceneNode(
        runtime->assetRoots, *documentNode, nodeOverrides);
    const auto reflectedMember = material->findParameterMember(
        LX_core::StringID(binding), LX_core::StringID(member));
    if (!reflectedMember.has_value()) {
      return makeCommandError("material parameter not found: " + key);
    }
    materialComponent->get().setMaterialInstance(std::move(material));
    if (!uri.empty()) {
      setDocumentNodeMaterialUri(*documentNode, uri);
    }
    documentNode->nodeMaterialOverrides = std::move(nodeOverrides);
  } catch (const std::exception &error) {
    return makeCommandError(
        std::string("failed to set node material parameter: ") + error.what());
  }

  return makeCommandOk(
      "node material parameter updated",
      "{\"path\":\"" + jsonEscape(path) + "\",\"binding\":\"" +
          jsonEscape(binding) + "\",\"member\":\"" + jsonEscape(member) +
          "\",\"type\":\"" + materialParameterTypeName(value.type) +
          "\",\"value\":" + makeMaterialValueJson(value) + "}");
}

LX_core::CommandResult
SceneRuntime::clearNodeMaterialParameter(const std::string &path,
                                         const std::string &binding,
                                         const std::string &member) {
  const auto runtime = requireRuntimeData(m_impl);
  auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return makeCommandError("node not found: " + path);
  }
  auto materialComponent = node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value()) {
    return makeCommandError("node has no material component: " + path);
  }

  const std::string uri = normalizeMaterialUri(*documentNode);
  const std::string key = binding + "." + member;
  if (binding == "MaterialUBO" && member == "baseColor") {
    if (!documentNode->nodeMaterialOverrides.baseColor.has_value()) {
      return makeCommandError("node has no baseColor override: " + path);
    }
    try {
      MaterialOverrideState nodeOverrides = documentNode->nodeMaterialOverrides;
      nodeOverrides.baseColor.reset();
      auto material = loadEffectiveMaterialForSceneNode(
          runtime->assetRoots, *documentNode, nodeOverrides);
      materialComponent->get().setMaterialInstance(std::move(material));
      if (!uri.empty()) {
        setDocumentNodeMaterialUri(*documentNode, uri);
      }
      documentNode->nodeMaterialOverrides = std::move(nodeOverrides);
    } catch (const std::exception &error) {
      return makeCommandError(
          std::string("failed to clear node material baseColor: ") +
          error.what());
    }
    return makeCommandOk(
        "node material baseColor override cleared",
        "{\"path\":\"" + jsonEscape(path) +
            "\",\"binding\":\"MaterialUBO\",\"member\":\"baseColor\"}");
  }
  if (documentNode->nodeMaterialOverrides.parameters.find(key) ==
      documentNode->nodeMaterialOverrides.parameters.end()) {
    return makeCommandError("node has no material parameter override: " + key);
  }
  try {
    MaterialOverrideState nodeOverrides = documentNode->nodeMaterialOverrides;
    nodeOverrides.parameters.erase(key);
    auto material = loadEffectiveMaterialForSceneNode(
        runtime->assetRoots, *documentNode, nodeOverrides);
    materialComponent->get().setMaterialInstance(std::move(material));
    if (!uri.empty()) {
      setDocumentNodeMaterialUri(*documentNode, uri);
    }
    documentNode->nodeMaterialOverrides = std::move(nodeOverrides);
  } catch (const std::exception &error) {
    return makeCommandError(
        std::string("failed to clear node material parameter: ") +
        error.what());
  }

  return makeCommandOk("node material parameter override cleared",
                       "{\"path\":\"" + jsonEscape(path) + "\",\"binding\":\"" +
                           jsonEscape(binding) + "\",\"member\":\"" +
                           jsonEscape(member) + "\"}");
}

LX_core::CommandResult
SceneRuntime::setNodeProceduralMaterialEnabled(const std::string &path,
                                               const bool enabled) {
  const auto runtime = requireRuntimeData(m_impl);
  auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  if (!documentNodeHasMaterialSurface(*documentNode)) {
    return makeCommandError("node has no material surface: " + path);
  }
  LX_core::SceneNode *node = runtime->scene->findByPath(path);
  if (!node) {
    return makeCommandError("node not found: " + path);
  }
  auto materialComponent = node->getComponent<LX_core::MaterialComponent>();
  if (!materialComponent.has_value()) {
    return makeCommandError("node has no material component: " + path);
  }

  const std::string uri = normalizeMaterialUri(*documentNode);
  try {
    ProceduralMaterialState proceduralState = documentNode->proceduralMaterial;
    proceduralState.enabled = enabled;
    auto material = loadEffectiveMaterialForSceneNode(
        runtime->assetRoots, *documentNode, uri,
        documentNode->materialOverrides, documentNode->nodeMaterialOverrides,
        proceduralState);
    materialComponent->get().setMaterialInstance(std::move(material));
    setDocumentNodeMaterialUri(*documentNode, uri);
    documentNode->proceduralMaterial = std::move(proceduralState);
  } catch (const std::exception &error) {
    return makeCommandError(
        std::string("failed to set proceduralMaterial.enabled: ") +
        error.what());
  }

  return makeCommandOk(std::string("proceduralMaterial.enabled updated"),
                       "{\"path\":\"" + jsonEscape(path) + "\",\"enabled\":" +
                           (enabled ? "true" : "false") + "}");
}

LX_core::CommandResult
SceneRuntime::applyMaterialOverride(const std::string &path,
                                    const std::string &field) {
  if (field != "baseColor") {
    return makeCommandError("unknown material override field: " + field);
  }

  const auto runtime = requireRuntimeData(m_impl);
  auto *documentNode = findDocumentNodeForRuntimePath(*runtime, path);
  if (!documentNode) {
    return makeCommandError("scene document node not found: " + path);
  }
  const auto color = documentNode->nodeMaterialOverrides.baseColor;
  if (!color.has_value()) {
    return makeCommandError("node has no baseColor override: " + path);
  }

  const std::string uri = normalizeMaterialUri(*documentNode);
  usize updatedDocuments = 0;
  forEachDocumentNode(runtime->document.mutableRootNode(),
                      [&](SceneNodeDocument &candidate) {
                        if (!documentNodeHasMaterialSurface(candidate)) {
                          return;
                        }
                        if (normalizeMaterialUri(candidate) != uri) {
                          return;
                        }
                        setDocumentNodeMaterialUri(candidate, uri);
                        candidate.materialOverrides.baseColor = *color;
                        ++updatedDocuments;
                      });

  usize updatedRuntimeNodes = 0;
  forEachRuntimeNode(
      runtime->scene->getRootNode(), [&](LX_core::SceneNode &node) {
        auto *candidateDocument = findDocumentNodeByName(
            runtime->document.mutableRootNode(), node.getNodeName());
        if (!candidateDocument ||
            !documentNodeHasMaterialSurface(*candidateDocument) ||
            normalizeMaterialUri(*candidateDocument) != uri) {
          return;
        }
        const auto materialComponent =
            node.getComponent<LX_core::MaterialComponent>();
        if (!materialComponent.has_value()) {
          return;
        }
        const auto effectiveNodeOverride =
            candidateDocument->nodeMaterialOverrides.baseColor;
        try {
          auto material = loadEffectiveMaterialForSceneNode(
              runtime->assetRoots, *candidateDocument, uri,
              candidateDocument->materialOverrides,
              MaterialOverrideState{.baseColor = effectiveNodeOverride},
              candidateDocument->proceduralMaterial);
          materialComponent->get().setMaterialInstance(std::move(material));
          ++updatedRuntimeNodes;
        } catch (const std::exception &error) {
          std::cerr << "[lxe_editor] failed to apply material override to "
                    << node.getPath() << ": " << error.what() << "\n";
        }
      });

  return makeCommandOk(
      "material baseColor override applied",
      "{\"materialUri\":\"" + jsonEscape(uri) +
          "\",\"updatedDocuments\":" + std::to_string(updatedDocuments) +
          ",\"updatedRuntimeNodes\":" + std::to_string(updatedRuntimeNodes) +
          ",\"baseColor\":" + makeVec3Json(*color) + "}");
}

} // namespace LX_demo::lxe_editor
