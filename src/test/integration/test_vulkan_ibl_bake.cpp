#include "core/asset/texture.hpp"
#include "core/frame_graph/frame_graph_executor.hpp"
#include "core/scene/ibl_bake_keys.hpp"
#include "core/scene/ibl_bake_service.hpp"
#include "editor/commands/command_bus.hpp"
#include "editor/commands/lxe_editor_commands.hpp"
#include "infra/resource_parsers/ibl_bake_manifest_parser.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace LX_core;
using namespace LX_demo::lxe_editor;

namespace {

constexpr std::string_view kMaterialSourceUri =
    "memory://materials/standard-pbr.material";
constexpr std::string_view kMaterialSourceHash = "sha256:standard-pbr";
constexpr std::string_view kNeutralEnvironmentUri =
    "assets/env/khronos/neutral/ggx/specular.ktx2";
constexpr std::string_view kNeutralEnvironmentHash =
    "sha256:f4766016d86d33019dbe56b42e93d5de4f8926f6724771b8a6c7218db35539c5";

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
  }
}

std::string joinDiagnostics(const std::vector<std::string> &diagnostics) {
  std::string text;
  for (usize i = 0; i < diagnostics.size(); ++i) {
    if (i != 0u) {
      text += "; ";
    }
    text += diagnostics[i];
  }
  return text;
}

std::filesystem::path repoRootForTest() {
  std::filesystem::path root = std::filesystem::current_path();
  while (!root.empty()) {
    if (std::filesystem::exists(root / "CMakeLists.txt") &&
        std::filesystem::exists(root / "src/core/scene") &&
        std::filesystem::exists(root / "assets/render_paths")) {
      return root;
    }
    const std::filesystem::path parent = root.parent_path();
    if (parent == root) {
      break;
    }
    root = parent;
  }
  return std::filesystem::current_path();
}

std::filesystem::path makeTempDir(const std::string &name) {
  const std::filesystem::path path = repoRootForTest() / (".tmp_" + name);
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "[FAIL] unable to read " << path << '\n';
    std::exit(1);
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

std::vector<u8> readBinaryFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    std::cerr << "[FAIL] unable to read " << path << '\n';
    std::exit(1);
  }
  const std::streamsize size = in.tellg();
  expect(size > 0, "binary payload should be non-empty");
  std::vector<u8> bytes(static_cast<usize>(size));
  in.seekg(0, std::ios::beg);
  in.read(reinterpret_cast<char *>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
  return bytes;
}

void writeTextFile(const std::filesystem::path &path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out) {
    std::cerr << "[FAIL] unable to write " << path << '\n';
    std::exit(1);
  }
  out << text;
}

template <typename T> void writeLe(std::vector<u8> &bytes, usize offset, T value) {
  for (usize i = 0; i < sizeof(T); ++i) {
    bytes[offset + i] = static_cast<u8>((value >> (i * 8u)) & 0xffu);
  }
}

std::vector<u8> makeKtx2Payload(TextureDesc desc, u8 seed) {
  constexpr std::array<u8, 12> kIdentifier = {
      0xABu, 0x4Bu, 0x54u, 0x58u, 0x20u, 0x32u,
      0x30u, 0xBBu, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
  constexpr u32 kVkFormatR16G16Sfloat = 83u;
  constexpr u32 kVkFormatR16G16B16A16Sfloat = 97u;
  const u32 vkFormat = desc.format == TextureFormat::RG16Float
                           ? kVkFormatR16G16Sfloat
                           : kVkFormatR16G16B16A16Sfloat;
  const u32 faceCount = desc.dimension == TextureDimension::TextureCube ? 6u : 1u;
  desc.arrayLayers = faceCount;

  const usize headerBytes = 80u + static_cast<usize>(desc.mipLevels) * 24u;
  std::vector<u8> bytes(headerBytes, 0u);
  std::copy(kIdentifier.begin(), kIdentifier.end(), bytes.begin());
  writeLe<u32>(bytes, 12u, vkFormat);
  writeLe<u32>(bytes, 16u, 2u);
  writeLe<u32>(bytes, 20u, desc.width);
  writeLe<u32>(bytes, 24u, desc.height);
  writeLe<u32>(bytes, 28u, 0u);
  writeLe<u32>(bytes, 32u, 0u);
  writeLe<u32>(bytes, 36u, faceCount);
  writeLe<u32>(bytes, 40u, desc.mipLevels);
  writeLe<u32>(bytes, 44u, 0u);

  usize dataOffset = headerBytes;
  u32 mipWidth = desc.width;
  u32 mipHeight = desc.height;
  const usize bytesPerPixel = textureBytesPerPixel(desc.format);
  for (u32 mip = 0; mip < desc.mipLevels; ++mip) {
    const usize levelBytes =
        static_cast<usize>(std::max(mipWidth, 1u)) *
        static_cast<usize>(std::max(mipHeight, 1u)) * bytesPerPixel *
        static_cast<usize>(faceCount);
    const usize indexOffset = 80u + static_cast<usize>(mip) * 24u;
    writeLe<u64>(bytes, indexOffset + 0u, static_cast<u64>(dataOffset));
    writeLe<u64>(bytes, indexOffset + 8u, static_cast<u64>(levelBytes));
    writeLe<u64>(bytes, indexOffset + 16u, static_cast<u64>(levelBytes));
    bytes.insert(bytes.end(), levelBytes,
                 static_cast<u8>(seed + static_cast<u8>(mip)));
    dataOffset += levelBytes;
    mipWidth = std::max(mipWidth / 2u, 1u);
    mipHeight = std::max(mipHeight / 2u, 1u);
  }
  return bytes;
}

Sh9IrradiancePayload makeSh9Payload(u8 seed) {
  Sh9IrradiancePayload payload;
  for (u32 i = 0; i < payload.coefficients.size(); ++i) {
    const float value = static_cast<float>(seed + i + 1u) * 0.01f;
    payload.coefficients[i] = Vec3f{value, value + 0.1f, value + 0.2f};
  }
  return payload;
}

std::vector<u8> toBytes(const std::string &text) {
  return std::vector<u8>(text.begin(), text.end());
}

TextureDesc specularDesc() {
  TextureDesc desc;
  desc.width = 256;
  desc.height = 256;
  desc.format = TextureFormat::RGBA16Float;
  desc.content = TextureContent::Environment;
  desc.dimension = TextureDimension::TextureCube;
  desc.mipLevels = deriveIblBakeMipCount(desc.width);
  desc.arrayLayers = 6;
  return desc;
}

TextureDesc brdfDesc() {
  TextureDesc desc;
  desc.width = 256;
  desc.height = 256;
  desc.format = TextureFormat::RG16Float;
  desc.content = TextureContent::Data;
  desc.dimension = TextureDimension::Texture2D;
  desc.mipLevels = 1;
  desc.arrayLayers = 1;
  return desc;
}

IblBakeItem makeEnvironmentItem() {
  return IblBakeItem{
      .id = 1,
      .kind = IblBakeItemKind::EnvironmentLight,
      .key =
          EnvironmentIblBakeKey{
              .environmentMapUri = ResourceUri(std::string(kNeutralEnvironmentUri)),
              .sourceHash = std::string(kNeutralEnvironmentHash),
              .sourceKind = EnvironmentIblBakeSourceKind::TextureCube,
          },
      .bakeRenderPathUri = ResourceUri(
          "assets/render_paths/bake_environment_ibl.render-path.yaml"),
  };
}

IblBakeItem makeMaterialItem() {
  return IblBakeItem{
      .id = 2,
      .kind = IblBakeItemKind::MaterialBrdf,
      .key = MaterialIblBakeKey{.materialType = "standard-pbr",
                                .bsdfModel = "ggx-smith"},
      .bakeRenderPathUri = ResourceUri(
          "assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml"),
  };
}

const FrameGraphExecutionPayload *
findOutput(const FrameGraphExecutionResult &execution,
           const std::string &name) {
  const auto it =
      std::find_if(execution.outputs.begin(), execution.outputs.end(),
                   [&](const FrameGraphExecutionPayload &payload) {
                     return payload.name == name;
                   });
  return it == execution.outputs.end() ? nullptr : &*it;
}

std::string payloadText(const FrameGraphExecutionPayload &payload) {
  return std::string(reinterpret_cast<const char *>(payload.bytes.data()),
                     payload.bytes.size());
}

class SyntheticIblFrameGraphExecutor final : public FrameGraphExecutor {
public:
  [[nodiscard]] FrameGraphExecutionResult
  execute(const FrameGraphExecutionRequest &) override {
    ++executeCount;
    const u8 seed = static_cast<u8>(0x20u + executeCount);
    if (fail) {
      return FrameGraphExecutionResult{.ok = false,
                                       .diagnostics = {"synthetic bake failed"}};
    }

    LX_infra::IblBakeManifestParser parser;
    FrameGraphExecutionResult result;
    result.ok = true;
    if (!omitDiffuse) {
      result.outputs.push_back(FrameGraphExecutionPayload{
          .name = "diffuse_sh9",
          .mediaType = "application/x-yaml",
          .bytes = toBytes(parser.writeSh9IrradiancePayload(makeSh9Payload(seed))),
      });
    }
    if (!omitSpecular) {
      result.outputs.push_back(FrameGraphExecutionPayload{
          .name = "specular_prefilter",
          .mediaType = "image/ktx2",
          .bytes = makeKtx2Payload(specularDesc(), seed),
      });
    }
    if (!omitBrdf) {
      result.outputs.push_back(FrameGraphExecutionPayload{
          .name = "brdf_lut",
          .mediaType = "image/ktx2",
          .bytes = makeKtx2Payload(brdfDesc(), static_cast<u8>(seed + 17u)),
      });
    }
    return result;
  }

  int executeCount = 0;
  bool fail = false;
  bool omitDiffuse = false;
  bool omitSpecular = false;
  bool omitBrdf = false;
};

class RecordingActivationSink final : public IblBakeActivationSink {
public:
  [[nodiscard]] IblBakeActivationResult
  activate(std::span<const IblBakeItem> items) override {
    ++activateCount;
    lastActivatedIds.clear();
    for (const IblBakeItem &item : items) {
      lastActivatedIds.push_back(item.id);
    }
    return IblBakeActivationResult::success("activated");
  }

  int activateCount = 0;
  std::vector<BakeItemId> lastActivatedIds;
};

class InlineActivationDispatcher final : public IblBakeActivationDispatcher {
public:
  [[nodiscard]] IblBakeActivationResult
  dispatchActivation(IblBakeActivationSink &sink,
                     std::span<const IblBakeItem> items) override {
    ++dispatchCount;
    return sink.activate(items);
  }

  int dispatchCount = 0;
};

class OutputFileCacheStore final : public IblBakeCacheStore {
public:
  explicit OutputFileCacheStore(std::filesystem::path root)
      : m_root(std::move(root)) {}

  [[nodiscard]] std::filesystem::path itemDir(const IblBakeItem &item) const {
    if (item.kind == IblBakeItemKind::EnvironmentLight) {
      return m_root / "environment";
    }
    return m_root / "material-standard-pbr";
  }

  [[nodiscard]] IblBakeCacheCheckResult
  check(const IblBakeItem &item) override {
    ++checkCount;
    const std::filesystem::path manifestPath = itemDir(item) / "manifest.yaml";
    if (!std::filesystem::exists(manifestPath)) {
      return IblBakeCacheCheckResult::missing("manifest missing");
    }

    if (item.kind == IblBakeItemKind::EnvironmentLight) {
      const auto parsed = m_parser.parseEnvironmentManifest(
          ResourceUri(manifestPath.generic_string()), readTextFile(manifestPath));
      if (!parsed.manifest.has_value()) {
        return IblBakeCacheCheckResult::invalid(
            joinDiagnostics(parsed.diagnostics));
      }
      const auto *key = std::get_if<EnvironmentIblBakeKey>(&item.key);
      if (key == nullptr) {
        return IblBakeCacheCheckResult::invalid("environment key required");
      }
      const IblBakeValidationResult source = validateIblBakeManifestSource(
          *parsed.manifest, key->environmentMapUri, key->sourceHash,
          key->sourceKind);
      if (!source.ok) {
        return IblBakeCacheCheckResult::invalid(
            joinDiagnostics(source.diagnostics));
      }
      const IblBakeValidationResult payloads =
          m_parser.validateEnvironmentPayloadFiles(manifestPath,
                                                   *parsed.manifest);
      if (!payloads.ok) {
        return IblBakeCacheCheckResult::invalid(
            joinDiagnostics(payloads.diagnostics));
      }
      return IblBakeCacheCheckResult::hit("valid environment payloads");
    }

    const auto parsed = m_parser.parseMaterialManifest(
        ResourceUri(manifestPath.generic_string()), readTextFile(manifestPath));
    if (!parsed.manifest.has_value()) {
      return IblBakeCacheCheckResult::invalid(
          joinDiagnostics(parsed.diagnostics));
    }
    const IblBakeValidationResult payloads =
        m_parser.validateMaterialPayloadFiles(manifestPath, *parsed.manifest);
    if (!payloads.ok) {
      return IblBakeCacheCheckResult::invalid(
          joinDiagnostics(payloads.diagnostics));
    }
    return IblBakeCacheCheckResult::hit("valid material payloads");
  }

  [[nodiscard]] IblBakeCacheWriteResult
  write(const IblBakeItem &item,
        const FrameGraphExecutionResult &execution) override {
    ++writeCount;
    return item.kind == IblBakeItemKind::EnvironmentLight
               ? writeEnvironment(item, execution)
               : writeMaterial(item, execution);
  }

  int checkCount = 0;
  int writeCount = 0;

private:
  [[nodiscard]] IblBakeCacheWriteResult fail(std::string message) const {
    return IblBakeCacheWriteResult::failure(std::move(message));
  }

  [[nodiscard]] IblBakeCacheWriteResult
  commitBytes(const std::filesystem::path &path,
              const std::vector<u8> &bytes) const {
    const auto result = m_parser.writeAtomically(
        path, std::string_view(reinterpret_cast<const char *>(bytes.data()),
                               bytes.size()));
    if (!result.ok) {
      return fail(joinDiagnostics(result.diagnostics));
    }
    return IblBakeCacheWriteResult::success();
  }

  [[nodiscard]] IblBakeCacheWriteResult
  commitText(const std::filesystem::path &path, const std::string &text) const {
    const auto result = m_parser.writeAtomically(path, text);
    if (!result.ok) {
      return fail(joinDiagnostics(result.diagnostics));
    }
    return IblBakeCacheWriteResult::success();
  }

  [[nodiscard]] IblBakeCacheWriteResult
  writeEnvironment(const IblBakeItem &item,
                   const FrameGraphExecutionResult &execution) const {
    const auto *key = std::get_if<EnvironmentIblBakeKey>(&item.key);
    if (key == nullptr) {
      return fail("environment key required");
    }
    const FrameGraphExecutionPayload *diffuse =
        findOutput(execution, "diffuse_sh9");
    const FrameGraphExecutionPayload *specular =
        findOutput(execution, "specular_prefilter");
    if (diffuse == nullptr || diffuse->bytes.empty()) {
      return fail("missing diffuse_sh9 output payload");
    }
    if (specular == nullptr || specular->bytes.empty()) {
      return fail("missing specular_prefilter output payload");
    }
    const auto parsedDiffuse = m_parser.parseSh9IrradiancePayload(
        ResourceUri("memory://execution/diffuse_sh9.yaml"),
        payloadText(*diffuse));
    if (!parsedDiffuse.payload.has_value()) {
      return fail(joinDiagnostics(parsedDiffuse.diagnostics));
    }
    const auto parsedSpecular = m_parser.parseKtx2Payload(
        ResourceUri("memory://execution/specular_prefilter.ktx2"),
        std::span<const u8>(specular->bytes.data(), specular->bytes.size()));
    if (!parsedSpecular.info.has_value()) {
      return fail(joinDiagnostics(parsedSpecular.diagnostics));
    }

    EnvironmentIblBakeManifest manifest;
    manifest.sourceUri = key->environmentMapUri;
    manifest.sourceHash = key->sourceHash;
    manifest.sourceKind = key->sourceKind;
    manifest.diffuseFile = "diffuse_sh9.yaml";
    manifest.specularFile = "specular_prefilter.ktx2";
    manifest.specularResolution = 256;
    manifest.specularMips = deriveIblBakeMipCount(manifest.specularResolution);
    const std::filesystem::path dir = itemDir(item);
    IblBakeCacheWriteResult written =
        commitBytes(dir / manifest.diffuseFile, diffuse->bytes);
    if (!written.ok) {
      return written;
    }
    written = commitBytes(dir / manifest.specularFile, specular->bytes);
    if (!written.ok) {
      return written;
    }
    return commitText(dir / "manifest.yaml",
                      m_parser.writeEnvironmentManifest(manifest));
  }

  [[nodiscard]] IblBakeCacheWriteResult
  writeMaterial(const IblBakeItem &item,
                const FrameGraphExecutionResult &execution) const {
    const auto *key = std::get_if<MaterialIblBakeKey>(&item.key);
    if (key == nullptr) {
      return fail("material key required");
    }
    const FrameGraphExecutionPayload *brdf = findOutput(execution, "brdf_lut");
    if (brdf == nullptr || brdf->bytes.empty()) {
      return fail("missing brdf_lut output payload");
    }
    const auto parsedBrdf = m_parser.parseKtx2Payload(
        ResourceUri("memory://execution/brdf_lut.ktx2"),
        std::span<const u8>(brdf->bytes.data(), brdf->bytes.size()));
    if (!parsedBrdf.info.has_value()) {
      return fail(joinDiagnostics(parsedBrdf.diagnostics));
    }

    MaterialIblBakeManifest manifest;
    manifest.materialType = key->materialType;
    manifest.materialSourceUri = ResourceUri(std::string(kMaterialSourceUri));
    manifest.materialSourceHash = std::string(kMaterialSourceHash);
    manifest.brdfModel = key->bsdfModel;
    manifest.brdfFile = "brdf_lut.ktx2";
    const std::filesystem::path dir = itemDir(item);
    IblBakeCacheWriteResult written =
        commitBytes(dir / manifest.brdfFile, brdf->bytes);
    if (!written.ok) {
      return written;
    }
    return commitText(dir / "manifest.yaml",
                      m_parser.writeMaterialManifest(manifest));
  }

  std::filesystem::path m_root;
  LX_infra::IblBakeManifestParser m_parser;
};

std::optional<IblBakeJobStatus> waitForStoppedJob(IblBakeJobService &service,
                                                  BakeJobId job) {
  for (int i = 0; i < 200; ++i) {
    const auto status = service.status(job);
    if (status.has_value() && !status->running) {
      return status;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return service.status(job);
}

void expectKtx2(const std::filesystem::path &path, TextureFormat format,
                TextureDimension dimension, u32 width, u32 mips,
                u32 faces) {
  LX_infra::IblBakeManifestParser parser;
  const std::vector<u8> bytes = readBinaryFile(path);
  const auto parsed =
      parser.parseKtx2Payload(ResourceUri(path.generic_string()), bytes);
  expect(parsed.info.has_value(), "KTX2 payload should parse");
  expect(parsed.info->format == format, "KTX2 format should match");
  expect(parsed.info->dimension == dimension, "KTX2 dimension should match");
  expect(parsed.info->width == width && parsed.info->height == width,
         "KTX2 size should match");
  expect(parsed.info->mipLevels == mips, "KTX2 mip count should match");
  expect(parsed.info->faceCount == faces, "KTX2 face count should match");
}

void expectSh9(const std::filesystem::path &path) {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed = parser.parseSh9IrradiancePayload(
      ResourceUri(path.generic_string()), readTextFile(path));
  expect(parsed.payload.has_value(), "SH9 payload should parse");
}

void expectValidatedOutputs(const OutputFileCacheStore &cache,
                            const IblBakeItem &environment,
                            const IblBakeItem &material) {
  const std::filesystem::path envDir = cache.itemDir(environment);
  const std::filesystem::path materialDir = cache.itemDir(material);
  expect(std::filesystem::exists(envDir / "manifest.yaml"),
         "environment manifest should exist");
  expect(std::filesystem::exists(materialDir / "manifest.yaml"),
         "material manifest should exist");
  expectSh9(envDir / "diffuse_sh9.yaml");
  expectKtx2(envDir / "specular_prefilter.ktx2",
             TextureFormat::RGBA16Float, TextureDimension::TextureCube, 256,
             9, 6);
  expectKtx2(materialDir / "brdf_lut.ktx2", TextureFormat::RG16Float,
             TextureDimension::Texture2D, 256, 1, 1);
}

void testBakeRenderPathsUseGenericReadbacks() {
  const std::vector<std::pair<std::filesystem::path, usize>> cases = {
      {repoRootForTest() / "assets/render_paths/"
                               "bake_environment_ibl.render-path.yaml",
       2},
      {repoRootForTest() / "assets/render_paths/"
                               "bake_standard_pbr_brdf_lut.render-path.yaml",
       1},
  };

  for (const auto &[path, expectedReadbackCount] : cases) {
    const std::string text = readTextFile(path);
    expect(text.find("payloads:") == std::string::npos,
           "bake render-path assets must not author legacy payloads");
    usize readbackCount = 0;
    std::string::size_type offset = 0;
    while ((offset = text.find("readbacks:", offset)) != std::string::npos) {
      ++readbackCount;
      offset += std::string("readbacks:").size();
    }
    expect(readbackCount == expectedReadbackCount,
           "bake render-path assets should author generic readbacks");
  }
}

void testNegativeBaselineCacheStillConsumesBareExecutionPayloads() {
  const std::filesystem::path root =
      makeTempDir("vulkan_ibl_bake_legacy_payload_baseline");
  const IblBakeItem environment = makeEnvironmentItem();
  OutputFileCacheStore cache(root);
  LX_infra::IblBakeManifestParser parser;

  FrameGraphExecutionResult execution;
  execution.ok = true;
  execution.outputs.push_back(FrameGraphExecutionPayload{
      .name = "diffuse_sh9",
      .mediaType = "application/x-yaml",
      .bytes = toBytes(parser.writeSh9IrradiancePayload(makeSh9Payload(0x31))),
  });
  execution.outputs.push_back(FrameGraphExecutionPayload{
      .name = "specular_prefilter",
      .mediaType = "image/ktx2",
      .bytes = makeKtx2Payload(specularDesc(), 0x41),
  });

  const IblBakeCacheWriteResult written = cache.write(environment, execution);
  expect(written.ok,
         "negative baseline: IBL cache still accepts executor outputs by "
         "payload name/mediaType/bytes without graph readback metadata");
  const std::filesystem::path envDir = cache.itemDir(environment);
  expect(std::filesystem::exists(envDir / "manifest.yaml"),
         "negative baseline: bare execution payloads should still publish an "
         "environment manifest");
  expectSh9(envDir / "diffuse_sh9.yaml");
  expectKtx2(envDir / "specular_prefilter.ktx2",
             TextureFormat::RGBA16Float, TextureDimension::TextureCube, 256,
             9, 6);
  std::filesystem::remove_all(root);
}

void testBakeCommandWritesPayloadsCacheHitsAndForceReplaces() {
  const std::filesystem::path root = makeTempDir("vulkan_ibl_bake_output");
  const IblBakeItem environment = makeEnvironmentItem();
  const IblBakeItem material = makeMaterialItem();
  auto cache = std::make_shared<OutputFileCacheStore>(root);
  auto executor = std::make_shared<SyntheticIblFrameGraphExecutor>();
  auto activation = std::make_shared<RecordingActivationSink>();
  auto dispatcher = std::make_shared<InlineActivationDispatcher>();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {environment, material},
      .cacheStore = cache,
      .executor = executor,
      .activation = activation,
      .activationDispatcher = dispatcher,
  });
  CommandBus bus;
  registerBakeCommands(bus, service);

  const CommandResult first = bus.dispatch("bake ibl start");
  expect(first.ok, "first bake command should start");
  const auto firstStatus = waitForStoppedJob(service, 1);
  expect(firstStatus.has_value(), "first bake should report final status");
  expect(firstStatus->phase == IblBakeJobPhase::Complete,
         "first bake should complete");
  expect(executor->executeCount == 2,
         "first bake should execute environment and material items");
  expect(cache->writeCount == 2, "first bake should write both cache items");
  expect(activation->activateCount == 1,
         "first bake should activate written payloads");
  expectValidatedOutputs(*cache, environment, material);
  const std::vector<u8> firstSpecular =
      readBinaryFile(cache->itemDir(environment) / "specular_prefilter.ktx2");

  const CommandResult second = bus.dispatch("bake ibl start");
  expect(second.ok, "second bake command should start");
  const auto secondStatus = waitForStoppedJob(service, 2);
  expect(secondStatus.has_value(), "second bake should report final status");
  expect(secondStatus->phase == IblBakeJobPhase::Complete,
         "second bake should complete from cache");
  expect(executor->executeCount == 2, "cache hit should skip GPU bake");
  expect(cache->writeCount == 2, "cache hit should not rewrite payloads");
  expect(activation->activateCount == 2,
         "cache hit should still activate cached payloads");
  const std::vector<u8> secondSpecular =
      readBinaryFile(cache->itemDir(environment) / "specular_prefilter.ktx2");
  expect(firstSpecular == secondSpecular,
         "cache-hit run should leave payload bytes unchanged");

  const CommandResult forced = bus.dispatch("bake ibl start --force");
  expect(forced.ok, "forced bake command should start when idle");
  const auto forcedStatus = waitForStoppedJob(service, 3);
  expect(forcedStatus.has_value(), "forced bake should report final status");
  expect(forcedStatus->phase == IblBakeJobPhase::Complete,
         "forced bake should complete");
  expect(executor->executeCount == 4, "force should rebake both items");
  expect(cache->writeCount == 4, "force should rewrite both cache items");
  expect(activation->activateCount == 3,
         "force should activate replacement payloads");
  expectValidatedOutputs(*cache, environment, material);
  const std::vector<u8> forcedSpecular =
      readBinaryFile(cache->itemDir(environment) / "specular_prefilter.ktx2");
  expect(firstSpecular != forcedSpecular,
         "force should atomically replace payload bytes");
  std::filesystem::remove_all(root);
}

void testBakeCommandWritesInspectableNeutralSmokeArtifacts() {
  const std::filesystem::path root =
      std::filesystem::current_path() /
      "test-output/ibl_bake_smoke/neutral/bake-cache";
  std::filesystem::remove_all(root);

  const IblBakeItem environment = makeEnvironmentItem();
  const IblBakeItem material = makeMaterialItem();
  auto cache = std::make_shared<OutputFileCacheStore>(root);
  auto executor = std::make_shared<SyntheticIblFrameGraphExecutor>();
  auto activation = std::make_shared<RecordingActivationSink>();
  auto dispatcher = std::make_shared<InlineActivationDispatcher>();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {environment, material},
      .cacheStore = cache,
      .executor = executor,
      .activation = activation,
      .activationDispatcher = dispatcher,
  });
  CommandBus bus;
  registerBakeCommands(bus, service);

  const CommandResult started = bus.dispatch("bake ibl start --force");
  expect(started.ok, "neutral smoke bake command should start");
  const auto status = waitForStoppedJob(service, 1);
  expect(status.has_value(), "neutral smoke bake should report final status");
  expect(status->phase == IblBakeJobPhase::Complete,
         "neutral smoke bake should complete");
  expectValidatedOutputs(*cache, environment, material);

  const std::filesystem::path manifestPath =
      cache->itemDir(environment) / "manifest.yaml";
  const std::string manifest = readTextFile(manifestPath);
  expect(manifest.find(std::string(kNeutralEnvironmentUri)) !=
             std::string::npos,
         "neutral smoke manifest should record the neutral KTX2 source URI");
  expect(manifest.find("kind: textureCube") != std::string::npos,
         "neutral smoke manifest should record textureCube source kind");
  expect(manifest.find(std::string(kNeutralEnvironmentHash)) !=
             std::string::npos,
         "neutral smoke manifest should record the neutral KTX2 source hash");

  const std::filesystem::path environmentDir = cache->itemDir(environment);
  const std::filesystem::path materialDir = cache->itemDir(material);
  const std::filesystem::path diffusePath = environmentDir / "diffuse_sh9.yaml";
  const std::filesystem::path specularPath =
      environmentDir / "specular_prefilter.ktx2";
  const std::filesystem::path brdfPath = materialDir / "brdf_lut.ktx2";
  expect(std::filesystem::exists(diffusePath),
         "neutral smoke should leave diffuse SH output");
  expect(std::filesystem::exists(specularPath),
         "neutral smoke should leave specular prefilter output");
  expect(std::filesystem::exists(brdfPath),
         "neutral smoke should leave BRDF LUT output");

  const std::filesystem::path summaryPath = root.parent_path() / "summary.txt";
  writeTextFile(summaryPath,
                "neutral IBL bake smoke artifacts\nroot: " +
                    std::filesystem::absolute(root).generic_string() +
                    "\nenvironment_manifest: " +
                    std::filesystem::absolute(manifestPath).generic_string() +
                    "\ndiffuse_sh9: " +
                    std::filesystem::absolute(diffusePath).generic_string() +
                    "\nspecular_prefilter: " +
                    std::filesystem::absolute(specularPath).generic_string() +
                    "\nmaterial_manifest: " +
                    std::filesystem::absolute(materialDir / "manifest.yaml")
                        .generic_string() +
                    "\nbrdf_lut: " +
                    std::filesystem::absolute(brdfPath).generic_string() +
                    "\n");

  std::cout << "[smoke] neutral IBL bake artifacts: "
            << std::filesystem::absolute(root).generic_string() << '\n';
}

void testMissingExecutionPayloadFailsWithoutManifestCommit() {
  const std::filesystem::path root =
      makeTempDir("vulkan_ibl_bake_missing_payload");
  const IblBakeItem environment = makeEnvironmentItem();
  auto cache = std::make_shared<OutputFileCacheStore>(root);
  auto executor = std::make_shared<SyntheticIblFrameGraphExecutor>();
  executor->omitSpecular = true;
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {environment},
      .cacheStore = cache,
      .executor = executor,
  });
  CommandBus bus;
  registerBakeCommands(bus, service);

  const CommandResult started = bus.dispatch("bake ibl start");
  expect(started.ok, "missing-payload bake should start");
  const auto status = waitForStoppedJob(service, 1);
  expect(status.has_value(), "missing-payload bake should report final status");
  expect(status->phase == IblBakeJobPhase::Failed,
         "missing execution payload should fail cache write");
  expect(!std::filesystem::exists(cache->itemDir(environment) / "manifest.yaml"),
         "failed write must not publish a manifest");
  std::filesystem::remove_all(root);
}

void testMetadataOnlyCacheIsInvalid() {
  const std::filesystem::path root =
      makeTempDir("vulkan_ibl_bake_metadata_only");
  const IblBakeItem environment = makeEnvironmentItem();
  OutputFileCacheStore cache(root);
  const std::filesystem::path dir = cache.itemDir(environment);
  std::filesystem::create_directories(dir);
  writeTextFile(dir / "diffuse_sh9.yaml", "payload\n");
  writeTextFile(dir / "specular_prefilter.ktx2", "payload\n");

  EnvironmentIblBakeManifest manifest;
  const auto *key = std::get_if<EnvironmentIblBakeKey>(&environment.key);
  expect(key != nullptr, "test setup should expose environment key");
  manifest.sourceUri = key->environmentMapUri;
  manifest.sourceHash = key->sourceHash;
  manifest.sourceKind = key->sourceKind;
  manifest.diffuseFile = "diffuse_sh9.yaml";
  manifest.specularFile = "specular_prefilter.ktx2";
  LX_infra::IblBakeManifestParser parser;
  writeTextFile(dir / "manifest.yaml",
                parser.writeEnvironmentManifest(manifest));

  const IblBakeCacheCheckResult checked = cache.check(environment);
  expect(checked.state == IblBakeCacheState::Invalid,
         "metadata-only cache payloads should be invalid");
  std::filesystem::remove_all(root);
}

} // namespace

int main() {
  testBakeRenderPathsUseGenericReadbacks();
  testNegativeBaselineCacheStillConsumesBareExecutionPayloads();
  testBakeCommandWritesPayloadsCacheHitsAndForceReplaces();
  testBakeCommandWritesInspectableNeutralSmokeArtifacts();
  testMissingExecutionPayloadFailsWithoutManifestCommit();
  testMetadataOnlyCacheIsInvalid();
  return 0;
}
