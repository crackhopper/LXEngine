#include "core/asset/material_instance.hpp"
#include "core/scene/ibl_bake_keys.hpp"
#include "core/scene/ibl_bake_manifest.hpp"
#include "core/scene/ibl_bake_service.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/resource_parsers/ibl_bake_manifest_parser.hpp"
#include "editor/commands/command_bus.hpp"
#include "editor/commands/lxe_editor_commands.hpp"
#include "editor/runtime/scene_runtime.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

using namespace LX_core;
using namespace LX_demo::lxe_editor;

namespace {

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
  }
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

bool diagnosticsContain(const std::vector<std::string> &diagnostics,
                        const std::string &needle) {
  for (const std::string &diagnostic : diagnostics) {
    if (contains(diagnostic, needle)) {
      return true;
    }
  }
  return false;
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

std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "[FAIL] unable to read " << path << '\n';
    std::exit(1);
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

MaterialInstanceUniquePtr makeBakeMaterial(const std::string &type,
                                           const char *sourceUri,
                                           const char *reflectionHash) {
  auto material =
      MaterialInstance::createUnique(MaterialTemplate::create(type));
  material->setBsdfType(type);
  material->setMaterialSourceUri(ResourceUri(sourceUri));
  material->setMaterialSourceReflectionHash(reflectionHash);
  return material;
}

Sh9IrradiancePayload sh9PayloadFixture(float scale = 1.0f) {
  Sh9IrradiancePayload payload;
  for (u32 i = 0; i < payload.coefficients.size(); ++i) {
    const float value = scale * static_cast<float>(i + 1u);
    payload.coefficients[i] = Vec3f{value, value + 0.25f, value + 0.5f};
  }
  return payload;
}

CombinedTextureSamplerSharedPtr makeTexturePayload(TextureDesc desc,
                                                   StringID binding) {
  auto sampler =
      std::make_shared<CombinedTextureSampler>(std::make_shared<Texture>(
          desc, std::vector<u8>(expectedTextureByteCount(desc), 0x7fu)));
  sampler->setBindingName(binding);
  return sampler;
}

CombinedTextureSamplerSharedPtr specularPrefilteredPayloadFixture() {
  TextureDesc desc;
  desc.width = 4;
  desc.height = 4;
  desc.format = TextureFormat::RGBA16Float;
  desc.content = TextureContent::Environment;
  desc.dimension = TextureDimension::TextureCube;
  desc.mipLevels = 3;
  desc.arrayLayers = 6;
  return makeTexturePayload(desc, StringID("PrefilteredEnvMap"));
}

CombinedTextureSamplerSharedPtr brdfLutPayloadFixture() {
  TextureDesc desc;
  desc.width = 256;
  desc.height = 256;
  desc.format = TextureFormat::RG16Float;
  desc.content = TextureContent::Data;
  desc.dimension = TextureDimension::Texture2D;
  desc.mipLevels = 1;
  desc.arrayLayers = 1;
  return makeTexturePayload(desc, StringID("BrdfLut"));
}

template <typename T> void writeLe(std::vector<u8> &bytes, usize offset, T value) {
  for (usize i = 0; i < sizeof(T); ++i) {
    bytes[offset + i] = static_cast<u8>((value >> (i * 8u)) & 0xffu);
  }
}

std::vector<u8> makeKtx2Payload(TextureDesc desc, u8 seed = 0x31u) {
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
    const usize levelIndexOffset = 80u + static_cast<usize>(mip) * 24u;
    writeLe<u64>(bytes, levelIndexOffset + 0u, static_cast<u64>(dataOffset));
    writeLe<u64>(bytes, levelIndexOffset + 8u, static_cast<u64>(levelBytes));
    writeLe<u64>(bytes, levelIndexOffset + 16u, static_cast<u64>(levelBytes));
    bytes.insert(bytes.end(), levelBytes,
                 static_cast<u8>(seed + static_cast<u8>(mip)));
    dataOffset += levelBytes;
    mipWidth = std::max(mipWidth / 2u, 1u);
    mipHeight = std::max(mipHeight / 2u, 1u);
  }
  return bytes;
}

IblEnvironmentActivationPayload iblActivationPayloadFixture(u64 generation) {
  IblEnvironmentActivationPayload payload;
  payload.generation = generation;
  payload.diffuseSh = sh9PayloadFixture();
  payload.specularPrefilteredCubemap = specularPrefilteredPayloadFixture();
  payload.standardPbrBrdfLut = brdfLutPayloadFixture();
  return payload;
}

ObjectHandle registerBakeObject(SceneResourceTable &table,
                                MaterialHandle material) {
  ObjectResource object;
  object.material = material;
  return table.registerObject(object);
}

RenderFeature makeBakeEnvironmentFeature(const char *environmentMapUri,
                                         const char *sourceHash = "") {
  RenderFeature feature;
  feature.feature = "environmentLighting";
  feature.parameters["environmentMap"] = RenderFeatureParameter{
      .kind = "textureCube",
      .uri = ResourceUri(environmentMapUri),
      .sourceHash = sourceHash,
      .binding = "SkyboxMap",
      .required = true,
  };
  return feature;
}

std::filesystem::path repoRootForTest() {
  std::filesystem::path root = std::filesystem::current_path();
  if (std::filesystem::exists(root / "assets/materials/pbr.material")) {
    return root;
  }
  if (std::filesystem::exists(root.parent_path() /
                              "assets/materials/pbr.material")) {
    return root.parent_path();
  }
  return root;
}

void writeTextFile(const std::filesystem::path &path, const std::string &text) {
  std::ofstream out(path);
  if (!out) {
    std::cerr << "[FAIL] unable to write " << path << '\n';
    std::exit(1);
  }
  out << text;
}

void writeBinaryFile(const std::filesystem::path &path,
                     const std::vector<u8> &bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    std::cerr << "[FAIL] unable to write " << path << '\n';
    std::exit(1);
  }
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

std::filesystem::path makeTempDir(const std::string &name) {
  const std::filesystem::path path = repoRootForTest() / (".tmp_" + name);
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

IblBakeItem makeEnvironmentBakeItem(BakeItemId id = 1) {
  return IblBakeItem{
      .id = id,
      .kind = IblBakeItemKind::EnvironmentLight,
      .key =
          EnvironmentIblBakeKey{
              .environmentMapUri = ResourceUri("memory://env/service.hdr"),
              .sourceHash = "sha256:env-service",
          },
      .bakeRenderPathUri = ResourceUri(
          "assets/render_paths/bake_environment_ibl.render-path.yaml"),
  };
}

class TempIblBakeCacheStore final : public IblBakeCacheStore {
public:
  explicit TempIblBakeCacheStore(std::filesystem::path root)
      : m_root(std::move(root)) {}

  void writeValidCache(const IblBakeItem &item) {
    const std::filesystem::path dir = itemDir(item);
    std::filesystem::create_directories(dir);
    writeTextFile(dir / "diffuse_sh9.yaml",
                  m_parser.writeSh9IrradiancePayload(sh9PayloadFixture()));
    TextureDesc specularDesc;
    specularDesc.width = 256;
    specularDesc.height = 256;
    specularDesc.format = TextureFormat::RGBA16Float;
    specularDesc.content = TextureContent::Environment;
    specularDesc.dimension = TextureDimension::TextureCube;
    specularDesc.mipLevels = deriveIblBakeMipCount(specularDesc.width);
    specularDesc.arrayLayers = 6;
    writeBinaryFile(dir / "specular_prefilter.ktx2",
                    makeKtx2Payload(specularDesc));
    writeTextFile(dir / "manifest.yaml",
                  m_parser.writeEnvironmentManifest(makeManifest(item)));
  }

  void writeInvalidCache(const IblBakeItem &item, const std::string &reason) {
    const std::filesystem::path dir = itemDir(item);
    std::filesystem::create_directories(dir);
    writeTextFile(dir / "diffuse_sh9.yaml",
                  m_parser.writeSh9IrradiancePayload(sh9PayloadFixture()));
    TextureDesc specularDesc;
    specularDesc.width = 256;
    specularDesc.height = 256;
    specularDesc.format = TextureFormat::RGBA16Float;
    specularDesc.content = TextureContent::Environment;
    specularDesc.dimension = TextureDimension::TextureCube;
    specularDesc.mipLevels = deriveIblBakeMipCount(specularDesc.width);
    specularDesc.arrayLayers = 6;
    writeBinaryFile(dir / "specular_prefilter.ktx2",
                    makeKtx2Payload(specularDesc));
    EnvironmentIblBakeManifest manifest = makeManifest(item);
    manifest.sourceHash = "sha256:wrong-" + reason;
    writeTextFile(dir / "manifest.yaml",
                  m_parser.writeEnvironmentManifest(manifest));
  }

  void blockNextCheck() {
    std::lock_guard lock(m_mutex);
    m_blockCheck = true;
    m_checkEntered = false;
    m_releaseCheck = false;
  }

  bool waitUntilCheckEntered() {
    std::unique_lock lock(m_mutex);
    return m_cv.wait_for(lock, std::chrono::seconds(2),
                         [&] { return m_checkEntered; });
  }

  void releaseBlockedCheck() {
    {
      std::lock_guard lock(m_mutex);
      m_releaseCheck = true;
    }
    m_cv.notify_all();
  }

  [[nodiscard]] IblBakeCacheCheckResult
  check(const IblBakeItem &item) override {
    {
      std::unique_lock lock(m_mutex);
      if (m_blockCheck) {
        m_checkEntered = true;
        m_cv.notify_all();
        m_cv.wait(lock, [&] { return m_releaseCheck; });
        m_blockCheck = false;
      }
    }

    ++checkCount;
    const std::filesystem::path dir = itemDir(item);
    const std::filesystem::path manifest = dir / "manifest.yaml";
    if (!std::filesystem::exists(manifest)) {
      return IblBakeCacheCheckResult::missing("manifest missing");
    }
    const auto parsed = m_parser.parseEnvironmentManifest(
        ResourceUri(manifest.generic_string()), readTextFile(manifest));
    if (!parsed.manifest.has_value()) {
      return IblBakeCacheCheckResult::invalid(
          joinDiagnostics(parsed.diagnostics));
    }
    const auto *key = std::get_if<EnvironmentIblBakeKey>(&item.key);
    if (key == nullptr) {
      return IblBakeCacheCheckResult::invalid("environment key required");
    }
    const auto sourceValidation = validateIblBakeManifestSource(
        *parsed.manifest, key->environmentMapUri, key->sourceHash);
    if (!sourceValidation.ok) {
      return IblBakeCacheCheckResult::invalid(
          joinDiagnostics(sourceValidation.diagnostics));
    }
    const auto payloadValidation =
        m_parser.validateEnvironmentPayloadFiles(manifest, *parsed.manifest);
    if (!payloadValidation.ok) {
      return IblBakeCacheCheckResult::invalid(
          joinDiagnostics(payloadValidation.diagnostics));
    }
    return IblBakeCacheCheckResult::hit(
        "valid manifest and environment payloads");
  }

  [[nodiscard]] IblBakeCacheWriteResult
  write(const IblBakeItem &item, const FrameGraphExecutionResult &) override {
    ++writeCount;
    if (failWrites) {
      return IblBakeCacheWriteResult::failure("cache write failed by test");
    }
    writeValidCache(item);
    return IblBakeCacheWriteResult::success();
  }

  int checkCount = 0;
  int writeCount = 0;
  bool failWrites = false;

private:
  [[nodiscard]] EnvironmentIblBakeManifest
  makeManifest(const IblBakeItem &item) const {
    const auto *key = std::get_if<EnvironmentIblBakeKey>(&item.key);
    expect(key != nullptr, "temp cache store expects environment bake item");
    EnvironmentIblBakeManifest manifest;
    manifest.sourceUri = key->environmentMapUri;
    manifest.sourceHash = key->sourceHash;
    manifest.specularResolution = 256;
    manifest.specularMips = deriveIblBakeMipCount(manifest.specularResolution);
    manifest.diffuseFile = "diffuse_sh9.yaml";
    manifest.specularFile = "specular_prefilter.ktx2";
    return manifest;
  }

  [[nodiscard]] std::filesystem::path itemDir(const IblBakeItem &item) const {
    return m_root / ("item-" + std::to_string(item.id));
  }

  std::filesystem::path m_root;
  LX_infra::IblBakeManifestParser m_parser;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_blockCheck = false;
  bool m_checkEntered = false;
  bool m_releaseCheck = false;
};

class FakeFrameGraphExecutor final : public FrameGraphExecutor {
public:
  [[nodiscard]] FrameGraphExecutionResult
  execute(const FrameGraphExecutionRequest &) override {
    ++executeCount;
    if (fail) {
      return FrameGraphExecutionResult{.ok = false,
                                       .diagnostics = {"gpu bake failed"}};
    }
    return FrameGraphExecutionResult{.ok = true};
  }

  int executeCount = 0;
  bool fail = false;
};

class FakeActivationSink final : public IblBakeActivationSink {
public:
  [[nodiscard]] IblBakeActivationResult
  activate(std::span<const IblBakeItem> items) override {
    ++activateCount;
    activatedItemIds.clear();
    for (const IblBakeItem &item : items) {
      activatedItemIds.push_back(item.id);
    }
    if (fail) {
      return IblBakeActivationResult::failure("activation failed by test");
    }
    return IblBakeActivationResult::success("activated");
  }

  int activateCount = 0;
  bool fail = false;
  std::vector<BakeItemId> activatedItemIds;
};

class FakeActivationDispatcher final : public IblBakeActivationDispatcher {
public:
  [[nodiscard]] IblBakeActivationResult
  dispatchActivation(IblBakeActivationSink &sink,
                     std::span<const IblBakeItem> items) override {
    ++dispatchCount;
    return sink.activate(items);
  }

  int dispatchCount = 0;
};

class TableActivationSink final : public IblBakeActivationSink {
public:
  explicit TableActivationSink(SceneResourceTable &table) : m_table(table) {}

  [[nodiscard]] IblBakeActivationResult
  activate(std::span<const IblBakeItem>) override {
    ++activateCount;
    IblEnvironmentActivationResult activated =
        m_table.activateIblEnvironment(std::move(nextPayload));
    if (!activated.ok) {
      return IblBakeActivationResult::failure(activated.message);
    }
    return IblBakeActivationResult::success("activated");
  }

  IblEnvironmentActivationPayload nextPayload;
  int activateCount = 0;

private:
  SceneResourceTable &m_table;
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

bool logsContain(const std::vector<IblBakeJobEvent> &events,
                 const std::string &needle) {
  for (const IblBakeJobEvent &event : events) {
    if (contains(event.message, needle)) {
      return true;
    }
  }
  return false;
}

bool phasesContainInOrder(const std::vector<IblBakeJobEvent> &events,
                          const std::vector<IblBakeJobPhase> &phases) {
  usize phaseIndex = 0;
  for (const IblBakeJobEvent &event : events) {
    if (phaseIndex < phases.size() && event.phase == phases[phaseIndex]) {
      ++phaseIndex;
    }
  }
  return phaseIndex == phases.size();
}

void testEventSequenceIsMonotonic() {
  IblBakeEventQueue queue;
  const IblBakeJobEvent first = queue.push(IblBakeJobEvent{
      .job = 7, .phase = IblBakeJobPhase::Queued, .message = "queued"});
  const IblBakeJobEvent second =
      queue.push(IblBakeJobEvent{.job = 7,
                                 .phase = IblBakeJobPhase::CacheCheck,
                                 .message = "cache check"});
  expect(first.sequence + 1 == second.sequence,
         "event queue sequence should be monotonic");

  const std::vector<IblBakeJobEvent> afterFirst =
      queue.drainSince(first.sequence);
  expect(afterFirst.size() == 1, "drainSince should filter old events");
  expect(afterFirst.front().sequence == second.sequence,
         "drainSince should return later events");
}

void testRunningJobGuardRejectsForce() {
  const std::filesystem::path root = makeTempDir("ibl_running_guard");
  const IblBakeItem item = makeEnvironmentBakeItem();
  auto cache = std::make_shared<TempIblBakeCacheStore>(root);
  cache->blockNextCheck();
  auto executor = std::make_shared<FakeFrameGraphExecutor>();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
  });

  const IblBakeStartResult start = service.start(false);
  expect(start.ok, "start should return ok");
  expect(start.job != 0, "start should return job id");
  expect(cache->waitUntilCheckEntered(),
         "running guard should reach blocking cache check");

  const IblBakeStartResult second = service.start(false);
  expect(!second.ok, "duplicate start should not start a new job");
  expect(second.alreadyRunning, "duplicate start should report running job");
  expect(second.job == start.job, "duplicate start should return running job");

  const IblBakeStartResult forced = service.start(true);
  expect(!forced.ok, "force while running should be rejected");
  expect(forced.rejected, "force while running should report rejection");
  expect(contains(forced.message, "already running"),
         "force rejection should mention running job");
  const IblBakeCancelResult cancelled = service.cancel(start.job);
  expect(cancelled.ok, "running guard cleanup should cancel job");
  cache->releaseBlockedCheck();
  (void)waitForStoppedJob(service, start.job);
  std::filesystem::remove_all(root);
}

void testBakeCommandsUseJobService() {
  const std::filesystem::path root = makeTempDir("ibl_command_service");
  const IblBakeItem item = makeEnvironmentBakeItem();
  auto cache = std::make_shared<TempIblBakeCacheStore>(root);
  cache->blockNextCheck();
  auto executor = std::make_shared<FakeFrameGraphExecutor>();
  CommandBus bus;
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
  });
  registerBakeCommands(bus, service);

  const CommandResult start = bus.dispatch("bake ibl start");
  expect(start.ok, "bake ibl start should succeed");
  expect(contains(start.message, "started bake job 1"),
         "start command should return job id");
  expect(cache->waitUntilCheckEntered(),
         "command service should reach blocking cache check");

  const CommandResult forced = bus.dispatch("bake ibl start --force");
  expect(!forced.ok, "force start while running should fail");
  expect(contains(forced.message, "already running"),
         "force command should explain running job");

  const CommandResult status = bus.dispatch("bake job status 1");
  expect(status.ok, "status command should succeed");
  expect(contains(status.message, "phase="),
         "status command should include phase");
  expect(contains(status.message, "progress="),
         "status command should include progress");

  const CommandResult logs = bus.dispatch("bake job logs 1 0");
  expect(logs.ok, "logs command should succeed");
  expect(contains(logs.message, "[1] info queued"),
         "logs command should include queued event");

  const CommandResult cancel = bus.dispatch("bake job cancel 1");
  expect(cancel.ok, "cancel command should succeed");
  expect(contains(cancel.message, "cancel pending"),
         "cancel command should report cancel pending");
  cache->releaseBlockedCheck();
  (void)waitForStoppedJob(service, 1);
  std::filesystem::remove_all(root);
}

void testIblBakeServiceCacheHitSkipsGpuBakeAndActivates() {
  const std::filesystem::path root = makeTempDir("ibl_service_cache_hit");
  const IblBakeItem item = makeEnvironmentBakeItem();
  auto cache = std::make_shared<TempIblBakeCacheStore>(root);
  cache->writeValidCache(item);
  auto executor = std::make_shared<FakeFrameGraphExecutor>();
  auto activation = std::make_shared<FakeActivationSink>();
  auto dispatcher = std::make_shared<FakeActivationDispatcher>();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
      .activation = activation,
      .activationDispatcher = dispatcher,
  });

  const IblBakeStartResult start = service.startBake(false);
  expect(start.ok, "cache-hit bake start should succeed");
  const auto status = waitForStoppedJob(service, start.job);
  expect(status.has_value(), "cache-hit job should report final status");
  expect(status->phase == IblBakeJobPhase::Complete,
         "cache-hit job should complete");
  expect(executor->executeCount == 0, "cache hit should skip GPU bake");
  expect(cache->writeCount == 0, "cache hit should not rewrite cache");
  expect(dispatcher->dispatchCount == 1,
         "cache hit should dispatch activation");
  expect(activation->activateCount == 1, "cache hit should activate payloads");
  expect(activation->activatedItemIds.size() == 1 &&
             activation->activatedItemIds.front() == item.id,
         "cache hit should activate cached item");
  expect(logsContain(service.logs(start.job, 0), "cache hit"),
         "cache-hit logs should record cache hit");
  std::filesystem::remove_all(root);
}

void testIblBakeServiceInvalidCacheRebakesAndWritesCache() {
  const std::filesystem::path root = makeTempDir("ibl_service_invalid_cache");
  const IblBakeItem item = makeEnvironmentBakeItem();
  auto cache = std::make_shared<TempIblBakeCacheStore>(root);
  cache->writeInvalidCache(item, "manifest schema mismatch");
  auto executor = std::make_shared<FakeFrameGraphExecutor>();
  auto activation = std::make_shared<FakeActivationSink>();
  auto dispatcher = std::make_shared<FakeActivationDispatcher>();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
      .activation = activation,
      .activationDispatcher = dispatcher,
  });

  const IblBakeStartResult start = service.startBake(false);
  expect(start.ok, "invalid-cache bake start should succeed");
  const auto status = waitForStoppedJob(service, start.job);
  expect(status.has_value(), "invalid-cache job should report final status");
  expect(status->phase == IblBakeJobPhase::Complete,
         "invalid-cache job should complete after rebake");
  expect(executor->executeCount == 1,
         "invalid cache should execute graph bake");
  expect(cache->writeCount == 1, "invalid cache should be replaced");
  const std::vector<IblBakeJobEvent> logs = service.logs(start.job, 0);
  expect(logsContain(logs, "invalid cache: source.hash"),
         "invalid cache log should include exact invalid reason");
  expect(phasesContainInOrder(
             logs, {IblBakeJobPhase::CacheCheck, IblBakeJobPhase::Filter,
                    IblBakeJobPhase::WriteCache, IblBakeJobPhase::Activate,
                    IblBakeJobPhase::Complete}),
         "rebake job should move through cache-check, filter, write-cache, "
         "activate, complete");
  std::filesystem::remove_all(root);
}

void testIblBakeServiceActivationRequiresDispatcher() {
  const std::filesystem::path root =
      makeTempDir("ibl_service_activation_dispatcher_required");
  const IblBakeItem item = makeEnvironmentBakeItem();
  auto cache = std::make_shared<TempIblBakeCacheStore>(root);
  cache->writeValidCache(item);
  auto executor = std::make_shared<FakeFrameGraphExecutor>();
  auto activation = std::make_shared<FakeActivationSink>();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
      .activation = activation,
  });

  const IblBakeStartResult start = service.startBake(false);
  expect(start.ok, "activation dispatcher test should start");
  const auto status = waitForStoppedJob(service, start.job);
  expect(status.has_value(),
         "activation dispatcher test should report final status");
  expect(status->phase == IblBakeJobPhase::ActivationFailed,
         "activation sink without dispatcher should fail activation");
  expect(activation->activateCount == 0,
         "service must not call activation sink directly from worker");
  expect(logsContain(service.logs(start.job, 0), "activation dispatcher"),
         "activation failure should name missing dispatcher");
  std::filesystem::remove_all(root);
}

void testIblBakeServiceFailedActivationPreservesActiveSceneIblGeneration() {
  const std::filesystem::path root =
      makeTempDir("ibl_service_failed_activation_preserves_generation");
  const IblBakeItem item = makeEnvironmentBakeItem();
  auto cache = std::make_shared<TempIblBakeCacheStore>(root);
  cache->writeValidCache(item);
  auto executor = std::make_shared<FakeFrameGraphExecutor>();
  auto dispatcher = std::make_shared<FakeActivationDispatcher>();
  SceneResourceTable table;
  const IblEnvironmentActivationResult initial =
      table.activateIblEnvironment(iblActivationPayloadFixture(11));
  expect(initial.ok, "initial table activation should succeed");

  auto activation = std::make_shared<TableActivationSink>(table);
  activation->nextPayload = iblActivationPayloadFixture(12);
  activation->nextPayload.standardPbrBrdfLut.reset();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
      .activation = activation,
      .activationDispatcher = dispatcher,
  });

  const IblBakeStartResult start = service.startBake(false);
  expect(start.ok, "failed-activation test should start");
  const auto status = waitForStoppedJob(service, start.job);
  expect(status.has_value(),
         "failed-activation test should report final status");
  expect(status->phase == IblBakeJobPhase::ActivationFailed,
         "invalid table activation should fail the job activation phase");
  expect(activation->activateCount == 1,
         "service should dispatch the table-backed activation sink");
  expect(table.buildUploadView().activeIblGeneration == 11,
         "failed service activation must preserve active scene IBL "
         "generation");
  std::filesystem::remove_all(root);
}

void testIblBakeServiceForceIgnoresValidCacheWhenIdle() {
  const std::filesystem::path root = makeTempDir("ibl_service_force");
  const IblBakeItem item = makeEnvironmentBakeItem();
  auto cache = std::make_shared<TempIblBakeCacheStore>(root);
  cache->writeValidCache(item);
  auto executor = std::make_shared<FakeFrameGraphExecutor>();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
  });

  const IblBakeStartResult start = service.startBake(true);
  expect(start.ok, "force bake should start when idle");
  const auto status = waitForStoppedJob(service, start.job);
  expect(status.has_value(), "force job should report final status");
  expect(status->phase == IblBakeJobPhase::Complete,
         "force job should complete");
  expect(executor->executeCount == 1, "force should execute graph bake");
  expect(cache->writeCount == 1, "force should rewrite cache");
  expect(logsContain(service.logs(start.job, 0), "force rebake"),
         "force job should log cache bypass");
  std::filesystem::remove_all(root);
}

void testIblBakeServiceRestartsAfterCompletedWorker() {
  const std::filesystem::path root = makeTempDir("ibl_service_restart");
  const IblBakeItem item = makeEnvironmentBakeItem();
  auto cache = std::make_shared<TempIblBakeCacheStore>(root);
  cache->writeValidCache(item);
  auto executor = std::make_shared<FakeFrameGraphExecutor>();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
  });

  const IblBakeStartResult first = service.startBake(false);
  expect(first.ok, "first restart test job should start");
  const auto firstStatus = waitForStoppedJob(service, first.job);
  expect(firstStatus.has_value() &&
             firstStatus->phase == IblBakeJobPhase::Complete,
         "first restart test job should complete");

  const IblBakeStartResult second = service.startBake(false);
  expect(second.ok, "second restart test job should start after completion");
  expect(second.job != first.job, "second restart test job should get new id");
  const auto secondStatus = waitForStoppedJob(service, second.job);
  expect(secondStatus.has_value() &&
             secondStatus->phase == IblBakeJobPhase::Complete,
         "second restart test job should complete");
  std::filesystem::remove_all(root);
}

void testIblBakeServiceRejectsDuplicateRunningJob() {
  const std::filesystem::path root =
      makeTempDir("ibl_service_duplicate_running");
  const IblBakeItem item = makeEnvironmentBakeItem();
  auto cache = std::make_shared<TempIblBakeCacheStore>(root);
  cache->blockNextCheck();
  auto executor = std::make_shared<FakeFrameGraphExecutor>();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
  });

  const IblBakeStartResult start = service.startBake(false);
  expect(start.ok, "first blocking bake start should succeed");
  expect(cache->waitUntilCheckEntered(),
         "blocking cache check should be reached");

  const IblBakeStartResult duplicate = service.startBake(false);
  expect(!duplicate.ok, "duplicate start should not create another job");
  expect(duplicate.alreadyRunning,
         "duplicate start should report already running");
  expect(duplicate.job == start.job,
         "duplicate start should return running job id");

  const IblBakeStartResult forced = service.startBake(true);
  expect(!forced.ok, "force start should be rejected while running");
  expect(forced.rejected, "force duplicate should report rejection");
  expect(contains(forced.message, "already running"),
         "force duplicate should explain running job");

  cache->releaseBlockedCheck();
  const auto status = waitForStoppedJob(service, start.job);
  expect(status.has_value(), "blocking job should finish after release");
  std::filesystem::remove_all(root);
}

void testIblBakeServiceCancelStopsBeforeGpuBake() {
  const std::filesystem::path root = makeTempDir("ibl_service_cancel");
  const IblBakeItem item = makeEnvironmentBakeItem();
  auto cache = std::make_shared<TempIblBakeCacheStore>(root);
  cache->blockNextCheck();
  auto executor = std::make_shared<FakeFrameGraphExecutor>();
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
  });

  const IblBakeStartResult start = service.startBake(false);
  expect(start.ok, "cancel test bake start should succeed");
  expect(cache->waitUntilCheckEntered(),
         "cancel test should reach blocking cache check");
  const IblBakeCancelResult cancelled = service.cancel(start.job);
  expect(cancelled.ok, "running bake should accept cancellation");
  cache->releaseBlockedCheck();

  const auto status = waitForStoppedJob(service, start.job);
  expect(status.has_value(), "cancelled job should report final status");
  expect(status->phase == IblBakeJobPhase::CancelPending,
         "cancelled job should remain in cancel-pending phase");
  expect(status->cancelRequested,
         "cancelled job status should record cancellation");
  expect(executor->executeCount == 0,
         "cancel before filter should skip GPU bake");
  expect(logsContain(service.logs(start.job, 0), "cancel"),
         "cancelled job logs should include cancellation");
  std::filesystem::remove_all(root);
}

void testIblBakeServiceExecutorFailureCanRetry() {
  const std::filesystem::path root = makeTempDir("ibl_service_retry");
  const IblBakeItem item = makeEnvironmentBakeItem();
  auto cache = std::make_shared<TempIblBakeCacheStore>(root);
  auto executor = std::make_shared<FakeFrameGraphExecutor>();
  executor->fail = true;
  IblBakeJobService service(IblBakeJobServiceConfig{
      .items = {item},
      .cacheStore = cache,
      .executor = executor,
  });

  const IblBakeStartResult failedStart = service.startBake(false);
  expect(failedStart.ok, "failing bake should start");
  const auto failed = waitForStoppedJob(service, failedStart.job);
  expect(failed.has_value(), "failed job should report final status");
  expect(failed->phase == IblBakeJobPhase::Failed,
         "executor failure should fail job");
  expect(executor->executeCount == 1,
         "executor failure should execute graph once");
  expect(cache->writeCount == 0, "failed executor should not write cache");
  expect(logsContain(service.logs(failedStart.job, 0), "gpu bake failed"),
         "failure logs should include executor diagnostic");

  executor->fail = false;
  const IblBakeStartResult retryStart = service.startBake(false);
  expect(retryStart.ok, "retry after failure should start");
  const auto retried = waitForStoppedJob(service, retryStart.job);
  expect(retried.has_value(), "retry job should report final status");
  expect(retried->phase == IblBakeJobPhase::Complete,
         "retry should complete after executor recovers");
  expect(executor->executeCount == 2, "retry should execute graph again");
  expect(cache->writeCount == 1, "successful retry should write cache");
  std::filesystem::remove_all(root);
}

void testSceneReferencedAssetsWithoutBakeMarkersProduceNoItems() {
  SceneResourceTable table;
  const MaterialHandle material = table.registerMaterialInstance(
      ResourceUri("memory://materials/standard-a"),
      makeBakeMaterial("standard-pbr", "memory://materials/standard-a",
                       "hash-a"));
  (void)registerBakeObject(table, material);
  (void)table.registerRenderFeature(
      ResourceUri("memory://features/env-a"),
      makeBakeEnvironmentFeature("memory://env/a.hdr"));

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.items.empty(),
         "scene-referenced assets without bake markers must not create work");
}

void testStandardPbrMaterialBakeItemsAreDeduplicated() {
  SceneResourceTable table;
  const MaterialHandle first = table.registerMaterialInstance(
      ResourceUri("memory://materials/standard-a"),
      makeBakeMaterial("standard-pbr", "memory://materials/standard-a",
                       "hash-a"));
  const MaterialHandle second = table.registerMaterialInstance(
      ResourceUri("memory://materials/standard-b"),
      makeBakeMaterial("standard-pbr", "memory://materials/standard-b",
                       "hash-b"));
  table.resolve(second)->get().setAuthoringMetadata(
      std::unordered_map<std::string, std::string>{{"brdfModel", "custom"}});
  const ObjectHandle firstObject = registerBakeObject(table, first);
  const ObjectHandle secondObject = registerBakeObject(table, second);
  table.setObjectIblBakeMarker(firstObject,
                               SceneIblBakeMarker{.enabled = true});
  table.setObjectIblBakeMarker(secondObject,
                               SceneIblBakeMarker{.enabled = true});

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.materialItems.size() == 1,
         "duplicate standard-pbr material bake keys should collapse");
  expect(collection.items.size() == 1,
         "deduplicated material key should produce one bake item");
  const auto *key =
      std::get_if<MaterialIblBakeKey>(&collection.materialItems.front().key);
  expect(key != nullptr, "material bake item should expose material key");
  expect(key->materialType == "standard-pbr",
         "material bake key should include material type");
  expect(key->bsdfModel == "ggx-smith",
         "standard-pbr material bake model should be type-derived");
}

void testReleasedObjectMarkerDoesNotLeakToReusedSlot() {
  SceneResourceTable table;
  const MaterialHandle material = table.registerMaterialInstance(
      ResourceUri("memory://materials/standard"),
      makeBakeMaterial("standard-pbr", "memory://materials/standard", "hash"));
  const ObjectHandle firstObject = registerBakeObject(table, material);
  table.setObjectIblBakeMarker(firstObject,
                               SceneIblBakeMarker{.enabled = true});
  table.release(firstObject);

  const ObjectHandle secondObject = registerBakeObject(table, material);
  expect(secondObject.index == firstObject.index,
         "test setup should reuse released object slot");

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.items.empty(),
         "released object bake marker must not leak to reused slot");
}

void testSceneRuntimeRegistersObjectBakeMarker() {
  const std::filesystem::path scenePath =
      repoRootForTest() / ".tmp_scene_bake_runtime_marker.scene.yaml";
  writeTextFile(scenePath, R"(
scene:
  name: Scene Bake Runtime Marker
  gameplayCameraPath: /camera
root:
  nodeName: scene_root
  name: ''
  children:
    - nodeName: camera
      name: camera
      camera:
        type: perspective
        fovY: 45.0
        aspect: 1.0
        nearPlane: 0.1
        farPlane: 100.0
        cullingMask: 4294967295
    - nodeName: cube
      name: cube
      mesh:
        uri: builtin://lxe_editor/primitives/cube
      material:
        uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material
      bake:
        ibl:
          enabled: true
)");

  SceneRuntime runtime;
  runtime.loadFromDocumentPath(scenePath);
  std::filesystem::remove(scenePath);

  const auto scene = runtime.scene();
  expect(scene != nullptr, "runtime should load scene");
  const IblBakeItemCollection collection =
      scene->resources().collectIblBakeItems();

  expect(collection.materialItems.size() == 1,
         "runtime object bake.ibl marker should create one material item");
}

void testDifferentEnvironmentKeysProduceDistinctItems() {
  SceneResourceTable table;
  const RenderFeatureHandle first = table.registerRenderFeature(
      ResourceUri("memory://features/env-a"),
      makeBakeEnvironmentFeature("memory://env/a.hdr"));
  const RenderFeatureHandle second = table.registerRenderFeature(
      ResourceUri("memory://features/env-b"),
      makeBakeEnvironmentFeature("memory://env/b.hdr"));
  table.addEnvironmentIblBakeRequest(first);
  table.addEnvironmentIblBakeRequest(second);

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.environmentItems.size() == 2,
         "different environment input uri/hash should produce two keys");
  const auto *firstKey =
      std::get_if<EnvironmentIblBakeKey>(&collection.environmentItems[0].key);
  const auto *secondKey =
      std::get_if<EnvironmentIblBakeKey>(&collection.environmentItems[1].key);
  expect(firstKey != nullptr && secondKey != nullptr,
         "environment bake items should expose environment keys");
  expect(firstKey->environmentMapUri != secondKey->environmentMapUri,
         "environment bake keys should include environment map uri");
}

void testSameEnvironmentSourceIsDeduplicatedAcrossFeatures() {
  SceneResourceTable table;
  const RenderFeatureHandle first = table.registerRenderFeature(
      ResourceUri("memory://features/env-a"),
      makeBakeEnvironmentFeature("memory://env/shared.hdr"));
  const RenderFeatureHandle second = table.registerRenderFeature(
      ResourceUri("memory://features/env-b"),
      makeBakeEnvironmentFeature("memory://env/shared.hdr"));
  table.addEnvironmentIblBakeRequest(first);
  table.addEnvironmentIblBakeRequest(second);

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.environmentItems.size() == 1,
         "same environment input URI should deduplicate across features");
  const auto *key =
      std::get_if<EnvironmentIblBakeKey>(&collection.environmentItems[0].key);
  expect(key != nullptr, "environment bake item should expose environment key");
  expect(key->environmentMapUri == ResourceUri("memory://env/shared.hdr"),
         "environment bake key should preserve environment source uri");
}

void testSameEnvironmentUriWithDifferentHashProducesDistinctItems() {
  SceneResourceTable table;
  const RenderFeatureHandle first = table.registerRenderFeature(
      ResourceUri("memory://features/env-a"),
      makeBakeEnvironmentFeature("memory://env/shared.hdr", "hash-a"));
  const RenderFeatureHandle second = table.registerRenderFeature(
      ResourceUri("memory://features/env-b"),
      makeBakeEnvironmentFeature("memory://env/shared.hdr", "hash-b"));
  table.addEnvironmentIblBakeRequest(first);
  table.addEnvironmentIblBakeRequest(second);

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.environmentItems.size() == 2,
         "same environment input URI with different hash should produce two "
         "keys");
  const auto *firstKey =
      std::get_if<EnvironmentIblBakeKey>(&collection.environmentItems[0].key);
  const auto *secondKey =
      std::get_if<EnvironmentIblBakeKey>(&collection.environmentItems[1].key);
  expect(firstKey != nullptr && secondKey != nullptr,
         "hashed environment bake items should expose environment keys");
  expect(firstKey->environmentMapUri == secondKey->environmentMapUri,
         "hash test should hold environment URI constant");
  expect(firstKey->sourceHash != secondKey->sourceHash,
         "environment bake keys should include source hash");
}

void testUnsupportedMaterialTypeProducesWarning() {
  SceneResourceTable table;
  const MaterialHandle material = table.registerMaterialInstance(
      ResourceUri("memory://materials/custom"),
      makeBakeMaterial("custom-brdf", "memory://materials/custom",
                       "custom-hash"));
  const ObjectHandle object = registerBakeObject(table, material);
  table.setObjectIblBakeMarker(object, SceneIblBakeMarker{.enabled = true});

  const IblBakeItemCollection collection = table.collectIblBakeItems();

  expect(collection.materialItems.empty(),
         "unsupported material type should not create material bake item");
  expect(!collection.warnings.empty(),
         "unsupported material type should produce warning");
  expect(contains(collection.warnings.front().message,
                  "unsupported material type"),
         "unsupported material warning should explain type rejection");
}

void testIblManifestDerivesMipCount() {
  expect(deriveIblBakeMipCount(256) == 9,
         "256px IBL bake should derive 9 mip levels");
}

void testEnvironmentManifestRejectsUnknownField() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed =
      parser.parseEnvironmentManifest(ResourceUri("memory://env-manifest"), R"(
schema: lxe.environment-ibl-bake.v1
source:
  uri: memory://env/source.hdr
  hash: sha256:env
bake:
  diffuse:
    basis: sh9
  specular:
    format: RGBA16Float
    resolution: 256
    mips: 9
    roughness: alpha-squared
    layout: cubemap
    faces: 6
outputs:
  diffuse:
    file: diffuse_sh9.yaml
  specular:
    file: specular_prefilter.ktx2
unexpected: true
)");

  expect(!parsed.manifest.has_value(),
         "unknown environment manifest field should reject manifest");
  expect(diagnosticsContain(parsed.diagnostics, "unexpected"),
         "unknown environment manifest diagnostic should name field");
}

void testEnvironmentManifestRejectsWrongMipCount() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed =
      parser.parseEnvironmentManifest(ResourceUri("memory://env-manifest"), R"(
schema: lxe.environment-ibl-bake.v1
source:
  uri: memory://env/source.hdr
  hash: sha256:env
bake:
  diffuse:
    basis: sh9
  specular:
    format: RGBA16Float
    resolution: 256
    mips: 8
    roughness: alpha-squared
    layout: cubemap
    faces: 6
outputs:
  diffuse:
    file: diffuse_sh9.yaml
  specular:
    file: specular_prefilter.ktx2
)");

  expect(!parsed.manifest.has_value(),
         "wrong environment manifest mip count should reject manifest");
  expect(diagnosticsContain(parsed.diagnostics, "bake.specular.mips"),
         "wrong mip diagnostic should name bake.specular.mips");
}

void testSh9PayloadRejectsEightCoefficients() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed =
      parser.parseSh9IrradiancePayload(ResourceUri("memory://diffuse-sh9"), R"(
schema: lxe.sh9.v1
space: world
basis: real-sh
order: 2
layout: rgb-interleaved
coefficients:
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
)");

  expect(!parsed.payload.has_value(),
         "SH9 payload with eight coefficients should reject");
  expect(diagnosticsContain(parsed.diagnostics, "coefficients must contain 9"),
         "SH9 coefficient diagnostic should explain required count");
}

void testSh9PayloadRejectsWrongLayout() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed =
      parser.parseSh9IrradiancePayload(ResourceUri("memory://diffuse-sh9"), R"(
schema: lxe.sh9.v1
space: world
basis: real-sh
order: 2
layout: bgr-interleaved
coefficients:
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
)");

  expect(!parsed.payload.has_value(),
         "SH9 payload with wrong layout should reject");
  expect(diagnosticsContain(parsed.diagnostics, "layout"),
         "SH9 wrong layout diagnostic should name layout");
}

void testSh9PayloadRejectsNonNumericCoefficient() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed =
      parser.parseSh9IrradiancePayload(ResourceUri("memory://diffuse-sh9"), R"(
schema: lxe.sh9.v1
space: world
basis: real-sh
order: 2
layout: rgb-interleaved
coefficients:
  - [not-a-number, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
  - [0.0, 0.0, 0.0]
)");

  expect(!parsed.payload.has_value(),
         "SH9 payload with non-numeric coefficient should reject");
  expect(diagnosticsContain(parsed.diagnostics, "coefficients[0]"),
         "SH9 non-numeric coefficient diagnostic should name coefficient");
}

void testMaterialManifestRejectsWrongBrdfSize() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed = parser.parseMaterialManifest(
      ResourceUri("memory://material-manifest"), R"(
schema: lxe.material-ibl-bake.v1
material:
  uri: memory://materials/standard-pbr.material
  type: standard-pbr
  hash: sha256:material
bake:
  brdf:
    model: ggx-smith
    format: RG16Float
    size: 128
outputs:
  brdf:
    file: brdf_lut.ktx2
)");

  expect(!parsed.manifest.has_value(),
         "wrong material BRDF size should reject manifest");
  expect(diagnosticsContain(parsed.diagnostics, "brdf.size"),
         "wrong BRDF size diagnostic should name brdf.size");
}

void testEnvironmentManifestRejectsWrongSourceHashAgainstExpectedKey() {
  EnvironmentIblBakeManifest manifest;
  manifest.sourceUri = ResourceUri("memory://env/source.hdr");
  manifest.sourceHash = "sha256:old";
  manifest.diffuseFile = "diffuse_sh9.yaml";
  manifest.specularFile = "specular_prefilter.ktx2";

  const auto result = validateIblBakeManifestSource(
      manifest, ResourceUri("memory://env/source.hdr"), "sha256:new");

  expect(!result.ok, "environment manifest wrong source hash should reject");
  expect(diagnosticsContain(result.diagnostics, "source.hash"),
         "wrong environment source hash diagnostic should name source.hash");
}

void testEnvironmentManifestRejectsMissingPayloadFiles() {
  LX_infra::IblBakeManifestParser parser;
  const auto parsed =
      parser.parseEnvironmentManifest(ResourceUri("memory://env-manifest"), R"(
schema: lxe.environment-ibl-bake.v1
source:
  uri: memory://env/source.hdr
  hash: sha256:env
bake:
  diffuse:
    basis: sh9
  specular:
    format: RGBA16Float
    resolution: 256
    mips: 9
    roughness: alpha-squared
    layout: cubemap
    faces: 6
outputs:
  diffuse:
    file: missing_diffuse_sh9.yaml
  specular:
    file: missing_specular_prefilter.ktx2
)");
  expect(parsed.manifest.has_value(),
         "manifest with referenced payload files should parse before payload "
         "existence validation");

  const auto result = parser.validateEnvironmentPayloadFiles(
      repoRootForTest() / ".tmp_env_manifest.yaml", *parsed.manifest);

  expect(!result.ok,
         "environment manifest with missing payload files should reject");
  expect(diagnosticsContain(result.diagnostics, "outputs.diffuse.file"),
         "missing diffuse payload diagnostic should name output field");
}

void testAtomicEnvironmentManifestCommitKeepsOldFileOnInvalidManifest() {
  const std::filesystem::path path =
      repoRootForTest() / ".tmp_invalid_ibl_manifest.yaml";
  writeTextFile(path, "old-manifest");

  LX_infra::IblBakeManifestParser parser;
  EnvironmentIblBakeManifest manifest;
  manifest.sourceUri = ResourceUri("memory://env/source.hdr");
  manifest.sourceHash = "sha256:env";
  manifest.specularResolution = 256;
  manifest.specularMips = 8;
  manifest.diffuseFile = "diffuse_sh9.yaml";
  manifest.specularFile = "specular_prefilter.ktx2";

  const auto result = parser.writeEnvironmentManifestAtomically(path, manifest);

  expect(!result.ok, "invalid environment manifest should not be committed");
  expect(readTextFile(path) == "old-manifest",
         "invalid atomic manifest commit should keep old file");
  std::filesystem::remove(path);
}

void testAtomicEnvironmentManifestCommitWritesValidManifest() {
  const std::filesystem::path path =
      repoRootForTest() / ".tmp_valid_ibl_manifest.yaml";
  const std::filesystem::path tempPath =
      path.parent_path() / (path.filename().generic_string() + ".tmp");
  std::filesystem::remove(path);
  std::filesystem::remove(tempPath);

  LX_infra::IblBakeManifestParser parser;
  EnvironmentIblBakeManifest manifest;
  manifest.sourceUri = ResourceUri("memory://env/source.hdr");
  manifest.sourceHash = "sha256:env";
  manifest.specularResolution = 256;
  manifest.specularMips = deriveIblBakeMipCount(manifest.specularResolution);
  manifest.diffuseFile = "diffuse_sh9.yaml";
  manifest.specularFile = "specular_prefilter.ktx2";

  const auto result = parser.writeEnvironmentManifestAtomically(path, manifest);

  expect(result.ok, "valid environment manifest should be committed");
  expect(contains(readTextFile(path), "lxe.environment-ibl-bake.v1"),
         "valid atomic commit should write manifest contents");
  expect(!std::filesystem::exists(tempPath),
         "successful atomic manifest commit should not leave temp file");
  std::filesystem::remove(path);
}

void testAtomicManifestCommitCleansTempOnRenameFailure() {
  const std::filesystem::path path =
      repoRootForTest() / ".tmp_ibl_manifest_target_dir";
  const std::filesystem::path tempPath =
      path.parent_path() / (path.filename().generic_string() + ".tmp");
  std::filesystem::remove_all(path);
  std::filesystem::remove(tempPath);
  std::filesystem::create_directory(path);

  LX_infra::IblBakeManifestParser parser;
  const auto result = parser.writeAtomically(path, "manifest");

  expect(!result.ok, "atomic commit to existing directory should fail");
  expect(!std::filesystem::exists(tempPath),
         "failed atomic manifest rename should remove temp file");
  std::filesystem::remove_all(path);
}

} // namespace

int main() {
  testEventSequenceIsMonotonic();
  testRunningJobGuardRejectsForce();
  testBakeCommandsUseJobService();
  testIblBakeServiceCacheHitSkipsGpuBakeAndActivates();
  testIblBakeServiceInvalidCacheRebakesAndWritesCache();
  testIblBakeServiceActivationRequiresDispatcher();
  testIblBakeServiceFailedActivationPreservesActiveSceneIblGeneration();
  testIblBakeServiceForceIgnoresValidCacheWhenIdle();
  testIblBakeServiceRestartsAfterCompletedWorker();
  testIblBakeServiceRejectsDuplicateRunningJob();
  testIblBakeServiceCancelStopsBeforeGpuBake();
  testIblBakeServiceExecutorFailureCanRetry();
  testSceneReferencedAssetsWithoutBakeMarkersProduceNoItems();
  testStandardPbrMaterialBakeItemsAreDeduplicated();
  testReleasedObjectMarkerDoesNotLeakToReusedSlot();
  testSceneRuntimeRegistersObjectBakeMarker();
  testDifferentEnvironmentKeysProduceDistinctItems();
  testSameEnvironmentSourceIsDeduplicatedAcrossFeatures();
  testSameEnvironmentUriWithDifferentHashProducesDistinctItems();
  testUnsupportedMaterialTypeProducesWarning();
  testIblManifestDerivesMipCount();
  testEnvironmentManifestRejectsUnknownField();
  testEnvironmentManifestRejectsWrongMipCount();
  testSh9PayloadRejectsEightCoefficients();
  testSh9PayloadRejectsWrongLayout();
  testSh9PayloadRejectsNonNumericCoefficient();
  testMaterialManifestRejectsWrongBrdfSize();
  testEnvironmentManifestRejectsWrongSourceHashAgainstExpectedKey();
  testEnvironmentManifestRejectsMissingPayloadFiles();
  testAtomicEnvironmentManifestCommitKeepsOldFileOnInvalidManifest();
  testAtomicEnvironmentManifestCommitWritesValidManifest();
  testAtomicManifestCommitCleansTempOnRenameFailure();
  return 0;
}
