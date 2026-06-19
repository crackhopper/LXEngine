#include "infra/resource_parsers/ibl_bake_manifest_parser.hpp"

#include "core/scene/ibl_bake_keys.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <system_error>
#include <unordered_set>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace LX_infra {
namespace {

using EnvironmentManifest = LX_core::EnvironmentIblBakeManifest;
using MaterialManifest = LX_core::MaterialIblBakeManifest;

constexpr std::array<u8, 12> kKtx2Identifier = {
    0xABu, 0x4Bu, 0x54u, 0x58u, 0x20u, 0x32u,
    0x30u, 0xBBu, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
constexpr u32 kVkFormatR16G16Sfloat = 83u;
constexpr u32 kVkFormatR16G16B16A16Sfloat = 97u;

struct Ktx2Header final {
  u32 vkFormat = 0;
  u32 typeSize = 0;
  u32 pixelWidth = 0;
  u32 pixelHeight = 0;
  u32 pixelDepth = 0;
  u32 layerCount = 0;
  u32 faceCount = 0;
  u32 levelCount = 0;
  u32 supercompressionScheme = 0;
};

struct Ktx2LevelIndex final {
  u64 byteOffset = 0;
  u64 byteLength = 0;
  u64 uncompressedByteLength = 0;
};

void addDiagnostic(std::vector<std::string> &diagnostics,
                   const LX_core::ResourceUri &uri, const std::string &field,
                   const std::string &message) {
  diagnostics.push_back(uri.string() + ": " + field + ": " + message);
}

void appendDiagnostics(LX_core::IblBakeValidationResult &result,
                       const std::vector<std::string> &diagnostics) {
  if (diagnostics.empty()) {
    return;
  }
  result.ok = false;
  result.diagnostics.insert(result.diagnostics.end(), diagnostics.begin(),
                            diagnostics.end());
}

[[nodiscard]] std::string readTextFile(const std::filesystem::path &path,
                                       std::vector<std::string> &diagnostics,
                                       const std::string &field) {
  std::ifstream in(path);
  if (!in) {
    diagnostics.push_back(field + " failed to open payload file: " +
                          path.generic_string());
    return {};
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] std::vector<u8>
readBinaryFile(const std::filesystem::path &path,
               std::vector<std::string> &diagnostics,
               const std::string &field) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    diagnostics.push_back(field + " failed to open payload file: " +
                          path.generic_string());
    return {};
  }
  const std::streamsize size = in.tellg();
  if (size <= 0) {
    diagnostics.push_back(field + " payload file is empty: " +
                          path.generic_string());
    return {};
  }
  std::vector<u8> bytes(static_cast<usize>(size));
  in.seekg(0, std::ios::beg);
  if (!in.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()))) {
    diagnostics.push_back(field + " failed to read payload file: " +
                          path.generic_string());
    return {};
  }
  return bytes;
}

template <typename T>
[[nodiscard]] T readLe(std::span<const u8> bytes, usize offset,
                       std::vector<std::string> &diagnostics,
                       const LX_core::ResourceUri &uri,
                       const std::string &field) {
  if (offset + sizeof(T) > bytes.size()) {
    addDiagnostic(diagnostics, uri, field, "truncated KTX2 payload");
    return {};
  }
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

[[nodiscard]] std::optional<LX_core::TextureFormat>
textureFormatForVkFormat(u32 vkFormat) {
  if (vkFormat == kVkFormatR16G16Sfloat) {
    return LX_core::TextureFormat::RG16Float;
  }
  if (vkFormat == kVkFormatR16G16B16A16Sfloat) {
    return LX_core::TextureFormat::RGBA16Float;
  }
  return std::nullopt;
}

[[nodiscard]] Ktx2Header parseKtx2Header(
    std::vector<std::string> &diagnostics, const LX_core::ResourceUri &uri,
    std::span<const u8> bytes) {
  Ktx2Header header;
  if (bytes.size() < 80u) {
    addDiagnostic(diagnostics, uri, "$", "KTX2 file is too small");
    return header;
  }
  if (!std::equal(kKtx2Identifier.begin(), kKtx2Identifier.end(),
                  bytes.begin())) {
    addDiagnostic(diagnostics, uri, "$", "not a KTX2 file");
    return header;
  }

  header.vkFormat = readLe<u32>(bytes, 12u, diagnostics, uri, "vkFormat");
  header.typeSize = readLe<u32>(bytes, 16u, diagnostics, uri, "typeSize");
  header.pixelWidth =
      readLe<u32>(bytes, 20u, diagnostics, uri, "pixelWidth");
  header.pixelHeight =
      readLe<u32>(bytes, 24u, diagnostics, uri, "pixelHeight");
  header.pixelDepth =
      readLe<u32>(bytes, 28u, diagnostics, uri, "pixelDepth");
  header.layerCount =
      readLe<u32>(bytes, 32u, diagnostics, uri, "layerCount");
  header.faceCount = readLe<u32>(bytes, 36u, diagnostics, uri, "faceCount");
  header.levelCount =
      readLe<u32>(bytes, 40u, diagnostics, uri, "levelCount");
  header.supercompressionScheme =
      readLe<u32>(bytes, 44u, diagnostics, uri, "supercompressionScheme");
  return header;
}

[[nodiscard]] std::vector<Ktx2LevelIndex> parseKtx2LevelIndex(
    std::vector<std::string> &diagnostics, const LX_core::ResourceUri &uri,
    std::span<const u8> bytes, u32 levelCount) {
  constexpr usize kLevelIndexOffset = 80u;
  constexpr usize kLevelIndexSize = 24u;
  const usize byteCount = static_cast<usize>(levelCount) * kLevelIndexSize;
  std::vector<Ktx2LevelIndex> levels;
  if (kLevelIndexOffset + byteCount > bytes.size()) {
    addDiagnostic(diagnostics, uri, "levels", "KTX2 level index is truncated");
    return levels;
  }

  levels.reserve(levelCount);
  for (u32 level = 0; level < levelCount; ++level) {
    const usize offset = kLevelIndexOffset +
                         static_cast<usize>(level) * kLevelIndexSize;
    levels.push_back(Ktx2LevelIndex{
        .byteOffset =
            readLe<u64>(bytes, offset + 0u, diagnostics, uri, "levels"),
        .byteLength =
            readLe<u64>(bytes, offset + 8u, diagnostics, uri, "levels"),
        .uncompressedByteLength =
            readLe<u64>(bytes, offset + 16u, diagnostics, uri, "levels"),
    });
  }
  return levels;
}

void validateKtx2LevelPayloads(std::vector<std::string> &diagnostics,
                               const LX_core::ResourceUri &uri,
                               std::span<const u8> bytes,
                               const LX_core::TextureDesc &desc) {
  const std::vector<Ktx2LevelIndex> levels =
      parseKtx2LevelIndex(diagnostics, uri, bytes, desc.mipLevels);
  if (levels.size() != desc.mipLevels) {
    return;
  }

  u32 mipWidth = desc.width;
  u32 mipHeight = desc.height;
  const usize bytesPerPixel = LX_core::textureBytesPerPixel(desc.format);
  for (u32 mip = 0; mip < desc.mipLevels; ++mip) {
    const usize expectedBytes =
        static_cast<usize>(std::max(mipWidth, 1u)) *
        static_cast<usize>(std::max(mipHeight, 1u)) * bytesPerPixel *
        static_cast<usize>(desc.arrayLayers);
    const Ktx2LevelIndex &level = levels[mip];
    if (level.byteLength != expectedBytes ||
        level.uncompressedByteLength != expectedBytes) {
      addDiagnostic(diagnostics, uri, "levels",
                    "KTX2 level byte length does not match texture contract");
    }
    if (level.byteLength == 0u) {
      addDiagnostic(diagnostics, uri, "levels",
                    "KTX2 level payload must be non-empty");
    }
    if (level.byteOffset > bytes.size() ||
        level.byteLength > bytes.size() - static_cast<usize>(level.byteOffset)) {
      addDiagnostic(diagnostics, uri, "levels",
                    "KTX2 level payload is out of bounds");
    }
    mipWidth = std::max(mipWidth / 2u, 1u);
    mipHeight = std::max(mipHeight / 2u, 1u);
  }
}

YAML::Node loadYaml(std::vector<std::string> &diagnostics,
                    const LX_core::ResourceUri &uri,
                    std::string_view yamlText) {
  try {
    YAML::Node root = YAML::Load(std::string(yamlText));
    if (!root || !root.IsMap()) {
      addDiagnostic(diagnostics, uri, "$", "root must be a map");
    }
    return root;
  } catch (const YAML::Exception &e) {
    addDiagnostic(diagnostics, uri, "$", e.what());
    return {};
  }
}

bool rejectUnknownFields(std::vector<std::string> &diagnostics,
                         const LX_core::ResourceUri &uri,
                         const YAML::Node &node, const std::string &path,
                         std::initializer_list<const char *> allowed) {
  if (!node || !node.IsMap()) {
    addDiagnostic(diagnostics, uri, path, "must be a map");
    return false;
  }
  const std::unordered_set<std::string> allowedSet(allowed.begin(),
                                                   allowed.end());
  bool ok = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(diagnostics, uri, path,
                    "field names must be scalar strings");
      ok = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (allowedSet.find(key) == allowedSet.end()) {
      addDiagnostic(diagnostics, uri,
                    path == "$" ? key : path + "." + key,
                    "unknown field");
      ok = false;
    }
  }
  return ok;
}

std::string requiredString(std::vector<std::string> &diagnostics,
                           const LX_core::ResourceUri &uri,
                           const YAML::Node &node, const std::string &path) {
  if (!node || !node.IsScalar()) {
    addDiagnostic(diagnostics, uri, path, "missing scalar string");
    return {};
  }
  return node.as<std::string>();
}

[[nodiscard]] std::optional<LX_core::EnvironmentIblBakeSourceKind>
parseEnvironmentSourceKind(std::vector<std::string> &diagnostics,
                           const LX_core::ResourceUri &uri,
                           const YAML::Node &node,
                           const std::string &field) {
  const std::string value = requiredString(diagnostics, uri, node, field);
  if (value.empty()) {
    return std::nullopt;
  }
  if (value == "equirect2D") {
    return LX_core::EnvironmentIblBakeSourceKind::Equirect2D;
  }
  if (value == "textureCube") {
    return LX_core::EnvironmentIblBakeSourceKind::TextureCube;
  }
  addDiagnostic(diagnostics, uri, field,
                "expected equirect2D or textureCube");
  return std::nullopt;
}

u32 requiredU32(std::vector<std::string> &diagnostics,
                const LX_core::ResourceUri &uri, const YAML::Node &node,
                const std::string &path) {
  if (!node || !node.IsScalar()) {
    addDiagnostic(diagnostics, uri, path, "missing unsigned integer");
    return 0;
  }
  try {
    return node.as<u32>();
  } catch (const YAML::Exception &e) {
    addDiagnostic(diagnostics, uri, path, e.what());
    return 0;
  }
}

float requiredFloat(std::vector<std::string> &diagnostics,
                    const LX_core::ResourceUri &uri, const YAML::Node &node,
                    const std::string &path) {
  if (!node || !node.IsScalar()) {
    addDiagnostic(diagnostics, uri, path, "missing number");
    return 0.0F;
  }
  try {
    return node.as<float>();
  } catch (const YAML::Exception &e) {
    addDiagnostic(diagnostics, uri, path, e.what());
    return 0.0F;
  }
}

void appendValidationDiagnostics(std::vector<std::string> &diagnostics,
                                 const LX_core::ResourceUri &uri,
                                 const LX_core::IblBakeValidationResult &result) {
  for (const std::string &diagnostic : result.diagnostics) {
    addDiagnostic(diagnostics, uri, "$", diagnostic);
  }
}

template <typename T>
std::string pathString(const T &path) {
  return std::filesystem::path(path).generic_string();
}

[[nodiscard]] std::filesystem::path
resolvePayloadPath(const std::filesystem::path &manifestPath,
                   const std::filesystem::path &relativePath) {
  return relativePath.is_absolute() ? relativePath
                                    : manifestPath.parent_path() / relativePath;
}

void validateSh9PayloadFile(const IblBakeManifestParser &parser,
                            LX_core::IblBakeValidationResult &result,
                            const std::filesystem::path &manifestPath,
                            const std::filesystem::path &relativePath,
                            const std::string &field) {
  const std::filesystem::path path =
      resolvePayloadPath(manifestPath, relativePath);
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    result.ok = false;
    result.diagnostics.push_back(field + " missing payload file: " +
                                 path.generic_string());
    return;
  }
  std::vector<std::string> diagnostics;
  const std::string text = readTextFile(path, diagnostics, field);
  if (!diagnostics.empty()) {
    appendDiagnostics(result, diagnostics);
    return;
  }
  const auto parsed = parser.parseSh9IrradiancePayload(
      LX_core::ResourceUri(path.generic_string()), text);
  if (!parsed.payload.has_value()) {
    appendDiagnostics(result, parsed.diagnostics);
  }
}

void validateKtx2PayloadFile(const IblBakeManifestParser &parser,
                             LX_core::IblBakeValidationResult &result,
                             const std::filesystem::path &manifestPath,
                             const std::filesystem::path &relativePath,
                             const std::string &field,
                             LX_core::TextureFormat expectedFormat,
                             LX_core::TextureDimension expectedDimension,
                             u32 expectedWidth, u32 expectedHeight,
                             u32 expectedMips, u32 expectedFaces) {
  const std::filesystem::path path =
      resolvePayloadPath(manifestPath, relativePath);
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    result.ok = false;
    result.diagnostics.push_back(field + " missing payload file: " +
                                 path.generic_string());
    return;
  }
  std::vector<std::string> diagnostics;
  const std::vector<u8> bytes = readBinaryFile(path, diagnostics, field);
  if (!diagnostics.empty()) {
    appendDiagnostics(result, diagnostics);
    return;
  }
  const auto parsed = parser.parseKtx2Payload(
      LX_core::ResourceUri(path.generic_string()), bytes);
  if (!parsed.info.has_value()) {
    appendDiagnostics(result, parsed.diagnostics);
    return;
  }
  const ParsedIblBakeKtx2Payload::Info &info = *parsed.info;
  if (info.format != expectedFormat) {
    result.ok = false;
    result.diagnostics.push_back(field + " KTX2 format does not match");
  }
  if (info.dimension != expectedDimension) {
    result.ok = false;
    result.diagnostics.push_back(field + " KTX2 dimension does not match");
  }
  if (info.width != expectedWidth || info.height != expectedHeight) {
    result.ok = false;
    result.diagnostics.push_back(field + " KTX2 size does not match");
  }
  if (info.mipLevels != expectedMips) {
    result.ok = false;
    result.diagnostics.push_back(field + " KTX2 mip count does not match");
  }
  if (info.faceCount != expectedFaces) {
    result.ok = false;
    result.diagnostics.push_back(field + " KTX2 face count does not match");
  }
}

bool closeFileDescriptor(AtomicCommitResult &result, int fd,
                         const std::filesystem::path &path) {
#if defined(__unix__) || defined(__APPLE__)
  if (::close(fd) != 0) {
    result.diagnostics.push_back("failed to close temp file '" +
                                 path.generic_string() + "': " +
                                 std::strerror(errno));
    return false;
  }
  return true;
#else
  (void)result;
  (void)fd;
  (void)path;
  return true;
#endif
}

bool writeTempFile(AtomicCommitResult &result,
                   const std::filesystem::path &tempPath,
                   std::string_view contents) {
#if defined(__unix__) || defined(__APPLE__)
  const int fd = ::open(tempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) {
    result.diagnostics.push_back("failed to open temp file '" +
                                 tempPath.generic_string() + "': " +
                                 std::strerror(errno));
    return false;
  }

  usize totalWritten = 0;
  const char *data = contents.data();
  while (totalWritten < contents.size()) {
    const ssize_t written = ::write(fd, data + totalWritten,
                                    contents.size() - totalWritten);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      result.diagnostics.push_back("failed to write temp file '" +
                                   tempPath.generic_string() + "': " +
                                   std::strerror(errno));
      closeFileDescriptor(result, fd, tempPath);
      std::filesystem::remove(tempPath);
      return false;
    }
    if (written == 0) {
      result.diagnostics.push_back("failed to write temp file '" +
                                   tempPath.generic_string() +
                                   "': wrote zero bytes");
      closeFileDescriptor(result, fd, tempPath);
      std::filesystem::remove(tempPath);
      return false;
    }
    totalWritten += static_cast<usize>(written);
  }

  if (::fsync(fd) != 0) {
    result.diagnostics.push_back("failed to sync temp file '" +
                                 tempPath.generic_string() + "': " +
                                 std::strerror(errno));
    closeFileDescriptor(result, fd, tempPath);
    std::filesystem::remove(tempPath);
    return false;
  }
  if (!closeFileDescriptor(result, fd, tempPath)) {
    std::filesystem::remove(tempPath);
    return false;
  }
  return true;
#else
  {
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out) {
      result.diagnostics.push_back("failed to open temp file '" +
                                   tempPath.generic_string() + "': " +
                                   std::strerror(errno));
      return false;
    }
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.flush();
    if (!out) {
      result.diagnostics.push_back("failed to write temp file '" +
                                   tempPath.generic_string() + "'");
      out.close();
      std::filesystem::remove(tempPath);
      return false;
    }
    out.close();
    if (!out) {
      result.diagnostics.push_back("failed to close temp file '" +
                                   tempPath.generic_string() + "'");
      std::filesystem::remove(tempPath);
      return false;
    }
  }
  return true;
#endif
}

bool syncDirectory(AtomicCommitResult &result,
                   const std::filesystem::path &directory) {
#if defined(__unix__) || defined(__APPLE__)
  const std::filesystem::path syncPath = directory.empty() ? "." : directory;
#if defined(O_DIRECTORY)
  const int flags = O_RDONLY | O_DIRECTORY;
#else
  const int flags = O_RDONLY;
#endif
  const int fd = ::open(syncPath.c_str(), flags);
  if (fd < 0) {
    result.diagnostics.push_back("failed to open directory '" +
                                 syncPath.generic_string() + "': " +
                                 std::strerror(errno));
    return false;
  }
  if (::fsync(fd) != 0) {
    result.diagnostics.push_back("failed to sync directory '" +
                                 syncPath.generic_string() + "': " +
                                 std::strerror(errno));
    closeFileDescriptor(result, fd, syncPath);
    return false;
  }
  return closeFileDescriptor(result, fd, syncPath);
#else
  (void)result;
  (void)directory;
  return true;
#endif
}

} // namespace

ParsedIblBakeManifest<EnvironmentManifest>
IblBakeManifestParser::parseEnvironmentManifest(
    const LX_core::ResourceUri &uri, std::string_view yamlText) const {
  ParsedIblBakeManifest<EnvironmentManifest> result;
  YAML::Node root = loadYaml(result.diagnostics, uri, yamlText);
  if (!root || !result.diagnostics.empty()) {
    return result;
  }

  rejectUnknownFields(result.diagnostics, uri, root, "$",
                      {"schema", "source", "bake", "outputs"});
  const std::string schema =
      requiredString(result.diagnostics, uri, root["schema"], "schema");
  if (!schema.empty() && schema != "lxe.environment-ibl-bake.v1") {
    addDiagnostic(result.diagnostics, uri, "schema",
                  "expected lxe.environment-ibl-bake.v1");
  }

  const YAML::Node source = root["source"];
  const YAML::Node bake = root["bake"];
  const YAML::Node diffuse = bake ? bake["diffuse"] : YAML::Node{};
  const YAML::Node specular = bake ? bake["specular"] : YAML::Node{};
  const YAML::Node outputs = root["outputs"];
  const YAML::Node outputDiffuse =
      outputs ? outputs["diffuse"] : YAML::Node{};
  const YAML::Node outputSpecular =
      outputs ? outputs["specular"] : YAML::Node{};

  rejectUnknownFields(result.diagnostics, uri, source, "source",
                      {"uri", "hash", "kind"});
  rejectUnknownFields(result.diagnostics, uri, bake, "bake",
                      {"diffuse", "specular"});
  rejectUnknownFields(result.diagnostics, uri, diffuse, "bake.diffuse",
                      {"basis"});
  rejectUnknownFields(result.diagnostics, uri, specular, "bake.specular",
                      {"format", "resolution", "mips", "roughness", "layout",
                       "faces"});
  rejectUnknownFields(result.diagnostics, uri, outputs, "outputs",
                      {"diffuse", "specular"});
  rejectUnknownFields(result.diagnostics, uri, outputDiffuse,
                      "outputs.diffuse", {"file"});
  rejectUnknownFields(result.diagnostics, uri, outputSpecular,
                      "outputs.specular", {"file"});

  EnvironmentManifest manifest;
  manifest.sourceUri = LX_core::ResourceUri(requiredString(
      result.diagnostics, uri, source["uri"], "source.uri"));
  manifest.sourceHash =
      requiredString(result.diagnostics, uri, source["hash"], "source.hash");
  if (auto sourceKind = parseEnvironmentSourceKind(result.diagnostics, uri,
                                                   source["kind"],
                                                   "source.kind")) {
    manifest.sourceKind = *sourceKind;
  }
  manifest.diffuseBasis = requiredString(result.diagnostics, uri,
                                         diffuse["basis"],
                                         "bake.diffuse.basis");
  manifest.specularFormat = requiredString(result.diagnostics, uri,
                                           specular["format"],
                                           "bake.specular.format");
  manifest.specularResolution = requiredU32(result.diagnostics, uri,
                                            specular["resolution"],
                                            "bake.specular.resolution");
  manifest.specularMips = requiredU32(result.diagnostics, uri,
                                      specular["mips"],
                                      "bake.specular.mips");
  manifest.specularRoughness = requiredString(result.diagnostics, uri,
                                              specular["roughness"],
                                              "bake.specular.roughness");
  manifest.specularLayout = requiredString(result.diagnostics, uri,
                                           specular["layout"],
                                           "bake.specular.layout");
  manifest.specularFaces = requiredU32(result.diagnostics, uri,
                                       specular["faces"],
                                       "bake.specular.faces");
  manifest.diffuseFile = requiredString(result.diagnostics, uri,
                                        outputDiffuse["file"],
                                        "outputs.diffuse.file");
  manifest.specularFile = requiredString(result.diagnostics, uri,
                                         outputSpecular["file"],
                                         "outputs.specular.file");

  appendValidationDiagnostics(result.diagnostics, uri,
                              LX_core::validateIblBakeManifest(manifest));
  if (result.diagnostics.empty()) {
    result.manifest = std::move(manifest);
  }
  return result;
}

ParsedIblBakeManifest<MaterialManifest>
IblBakeManifestParser::parseMaterialManifest(
    const LX_core::ResourceUri &uri, std::string_view yamlText) const {
  ParsedIblBakeManifest<MaterialManifest> result;
  YAML::Node root = loadYaml(result.diagnostics, uri, yamlText);
  if (!root || !result.diagnostics.empty()) {
    return result;
  }
  rejectUnknownFields(result.diagnostics, uri, root, "$",
                      {"schema", "material", "bake", "outputs"});
  const std::string schema =
      requiredString(result.diagnostics, uri, root["schema"], "schema");
  if (!schema.empty() && schema != "lxe.material-ibl-bake.v1") {
    addDiagnostic(result.diagnostics, uri, "schema",
                  "expected lxe.material-ibl-bake.v1");
  }

  const YAML::Node material = root["material"];
  const YAML::Node bake = root["bake"];
  const YAML::Node brdf = bake ? bake["brdf"] : YAML::Node{};
  const YAML::Node outputs = root["outputs"];
  const YAML::Node outputBrdf = outputs ? outputs["brdf"] : YAML::Node{};

  rejectUnknownFields(result.diagnostics, uri, material, "material",
                      {"uri", "type", "hash"});
  rejectUnknownFields(result.diagnostics, uri, bake, "bake", {"brdf"});
  rejectUnknownFields(result.diagnostics, uri, brdf, "bake.brdf",
                      {"model", "format", "size"});
  rejectUnknownFields(result.diagnostics, uri, outputs, "outputs", {"brdf"});
  rejectUnknownFields(result.diagnostics, uri, outputBrdf, "outputs.brdf",
                      {"file"});

  MaterialManifest manifest;
  manifest.materialSourceUri = LX_core::ResourceUri(requiredString(
      result.diagnostics, uri, material["uri"], "material.uri"));
  manifest.materialType =
      requiredString(result.diagnostics, uri, material["type"],
                     "material.type");
  manifest.materialSourceHash =
      requiredString(result.diagnostics, uri, material["hash"],
                     "material.hash");
  manifest.brdfModel = requiredString(result.diagnostics, uri, brdf["model"],
                                      "bake.brdf.model");
  manifest.brdfFormat = requiredString(result.diagnostics, uri,
                                       brdf["format"], "bake.brdf.format");
  manifest.brdfSize =
      requiredU32(result.diagnostics, uri, brdf["size"], "bake.brdf.size");
  manifest.brdfFile =
      requiredString(result.diagnostics, uri, outputBrdf["file"],
                     "outputs.brdf.file");

  appendValidationDiagnostics(result.diagnostics, uri,
                              LX_core::validateIblBakeManifest(manifest));
  if (result.diagnostics.empty()) {
    result.manifest = std::move(manifest);
  }
  return result;
}

ParsedSh9IrradiancePayload IblBakeManifestParser::parseSh9IrradiancePayload(
    const LX_core::ResourceUri &uri, std::string_view yamlText) const {
  ParsedSh9IrradiancePayload result;
  YAML::Node root = loadYaml(result.diagnostics, uri, yamlText);
  if (!root || !result.diagnostics.empty()) {
    return result;
  }
  rejectUnknownFields(result.diagnostics, uri, root, "$",
                      {"schema", "space", "basis", "order", "layout",
                       "coefficients"});
  const std::string schema =
      requiredString(result.diagnostics, uri, root["schema"], "schema");
  if (!schema.empty() && schema != "lxe.sh9.v1") {
    addDiagnostic(result.diagnostics, uri, "schema", "expected lxe.sh9.v1");
  }

  LX_core::Sh9IrradiancePayload payload;
  payload.space = requiredString(result.diagnostics, uri, root["space"],
                                 "space");
  payload.basis = requiredString(result.diagnostics, uri, root["basis"],
                                 "basis");
  payload.order = requiredU32(result.diagnostics, uri, root["order"],
                              "order");
  payload.layout = requiredString(result.diagnostics, uri, root["layout"],
                                  "layout");
  const YAML::Node coefficients = root["coefficients"];
  if (!coefficients || !coefficients.IsSequence()) {
    addDiagnostic(result.diagnostics, uri, "coefficients",
                  "must be a sequence");
  } else if (coefficients.size() != payload.coefficients.size()) {
    addDiagnostic(result.diagnostics, uri, "coefficients",
                  "coefficients must contain 9 RGB triples");
  } else {
    for (usize i = 0; i < coefficients.size(); ++i) {
      const YAML::Node coefficient = coefficients[i];
      if (!coefficient.IsSequence() || coefficient.size() != 3) {
        addDiagnostic(result.diagnostics, uri,
                      "coefficients[" + std::to_string(i) + "]",
                      "must be an RGB triple");
        continue;
      }
      payload.coefficients[i] =
          LX_core::Vec3f{requiredFloat(result.diagnostics, uri, coefficient[0],
                                       "coefficients[" + std::to_string(i) +
                                           "][0]"),
                         requiredFloat(result.diagnostics, uri, coefficient[1],
                                       "coefficients[" + std::to_string(i) +
                                           "][1]"),
                         requiredFloat(result.diagnostics, uri, coefficient[2],
                                       "coefficients[" + std::to_string(i) +
                                           "][2]")};
    }
  }

  appendValidationDiagnostics(result.diagnostics, uri,
                              LX_core::validateIblBakePayload(payload));
  if (result.diagnostics.empty()) {
    result.payload = payload;
  }
  return result;
}

ParsedIblBakeKtx2Payload IblBakeManifestParser::parseKtx2Payload(
    const LX_core::ResourceUri &uri, std::span<const u8> bytes) const {
  ParsedIblBakeKtx2Payload result;
  const Ktx2Header header = parseKtx2Header(result.diagnostics, uri, bytes);
  if (!result.diagnostics.empty()) {
    return result;
  }

  const std::optional<LX_core::TextureFormat> format =
      textureFormatForVkFormat(header.vkFormat);
  if (!format.has_value()) {
    addDiagnostic(result.diagnostics, uri, "vkFormat",
                  "unsupported KTX2 format for IBL bake payload");
  }
  if (header.typeSize != 2u) {
    addDiagnostic(result.diagnostics, uri, "typeSize",
                  "KTX2 type size must be 2");
  }
  if (header.pixelWidth == 0u || header.pixelHeight == 0u) {
    addDiagnostic(result.diagnostics, uri, "pixelWidth",
                  "KTX2 dimensions must be non-zero");
  }
  if (header.pixelDepth != 0u) {
    addDiagnostic(result.diagnostics, uri, "pixelDepth",
                  "3D KTX2 payloads are not supported for IBL bake");
  }
  if (header.layerCount != 0u) {
    addDiagnostic(result.diagnostics, uri, "layerCount",
                  "KTX2 array layers are not supported for IBL bake");
  }
  if (header.faceCount != 1u && header.faceCount != 6u) {
    addDiagnostic(result.diagnostics, uri, "faceCount",
                  "KTX2 payload must be 2D or cubemap");
  }
  if (header.levelCount == 0u) {
    addDiagnostic(result.diagnostics, uri, "levelCount",
                  "KTX2 payload must contain at least one mip level");
  }
  if (header.supercompressionScheme != 0u) {
    addDiagnostic(result.diagnostics, uri, "supercompressionScheme",
                  "supercompressed KTX2 payloads are not supported");
  }
  if (!result.diagnostics.empty() || !format.has_value()) {
    return result;
  }

  LX_core::TextureDesc desc;
  desc.width = header.pixelWidth;
  desc.height = header.pixelHeight;
  desc.format = *format;
  desc.dimension = header.faceCount == 6u
                       ? LX_core::TextureDimension::TextureCube
                       : LX_core::TextureDimension::Texture2D;
  desc.mipLevels = header.levelCount;
  desc.arrayLayers = header.faceCount == 6u ? 6u : 1u;
  try {
    validateKtx2LevelPayloads(result.diagnostics, uri, bytes, desc);
  } catch (const std::exception &error) {
    addDiagnostic(result.diagnostics, uri, "$", error.what());
  }

  if (result.diagnostics.empty()) {
    result.info = ParsedIblBakeKtx2Payload::Info{
        .format = desc.format,
        .width = desc.width,
        .height = desc.height,
        .mipLevels = desc.mipLevels,
        .dimension = desc.dimension,
        .faceCount = header.faceCount,
    };
  }
  return result;
}

std::string IblBakeManifestParser::writeEnvironmentManifest(
    const EnvironmentManifest &manifest) const {
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "schema" << YAML::Value
      << "lxe.environment-ibl-bake.v1";
  out << YAML::Key << "source" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "uri" << YAML::Value << manifest.sourceUri.string();
  out << YAML::Key << "hash" << YAML::Value << manifest.sourceHash;
  out << YAML::Key << "kind" << YAML::Value
      << std::string(
             LX_core::environmentIblBakeSourceKindName(manifest.sourceKind));
  out << YAML::EndMap;
  out << YAML::Key << "bake" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "diffuse" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "basis" << YAML::Value << manifest.diffuseBasis;
  out << YAML::EndMap;
  out << YAML::Key << "specular" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "format" << YAML::Value << manifest.specularFormat;
  out << YAML::Key << "resolution" << YAML::Value
      << manifest.specularResolution;
  out << YAML::Key << "mips" << YAML::Value << manifest.specularMips;
  out << YAML::Key << "roughness" << YAML::Value
      << manifest.specularRoughness;
  out << YAML::Key << "layout" << YAML::Value << manifest.specularLayout;
  out << YAML::Key << "faces" << YAML::Value << manifest.specularFaces;
  out << YAML::EndMap;
  out << YAML::EndMap;
  out << YAML::Key << "outputs" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "diffuse" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "file" << YAML::Value
      << pathString(manifest.diffuseFile);
  out << YAML::EndMap;
  out << YAML::Key << "specular" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "file" << YAML::Value
      << pathString(manifest.specularFile);
  out << YAML::EndMap;
  out << YAML::EndMap;
  out << YAML::EndMap;
  return out.c_str();
}

std::string IblBakeManifestParser::writeMaterialManifest(
    const MaterialManifest &manifest) const {
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "schema" << YAML::Value
      << "lxe.material-ibl-bake.v1";
  out << YAML::Key << "material" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "uri" << YAML::Value
      << manifest.materialSourceUri.string();
  out << YAML::Key << "type" << YAML::Value << manifest.materialType;
  out << YAML::Key << "hash" << YAML::Value << manifest.materialSourceHash;
  out << YAML::EndMap;
  out << YAML::Key << "bake" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "brdf" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "model" << YAML::Value << manifest.brdfModel;
  out << YAML::Key << "format" << YAML::Value << manifest.brdfFormat;
  out << YAML::Key << "size" << YAML::Value << manifest.brdfSize;
  out << YAML::EndMap;
  out << YAML::EndMap;
  out << YAML::Key << "outputs" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "brdf" << YAML::Value << YAML::BeginMap;
  out << YAML::Key << "file" << YAML::Value << pathString(manifest.brdfFile);
  out << YAML::EndMap;
  out << YAML::EndMap;
  out << YAML::EndMap;
  return out.c_str();
}

std::string IblBakeManifestParser::writeSh9IrradiancePayload(
    const LX_core::Sh9IrradiancePayload &payload) const {
  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "schema" << YAML::Value << "lxe.sh9.v1";
  out << YAML::Key << "space" << YAML::Value << payload.space;
  out << YAML::Key << "basis" << YAML::Value << payload.basis;
  out << YAML::Key << "order" << YAML::Value << payload.order;
  out << YAML::Key << "layout" << YAML::Value << payload.layout;
  out << YAML::Key << "coefficients" << YAML::Value << YAML::BeginSeq;
  for (const LX_core::Vec3f &coefficient : payload.coefficients) {
    out << YAML::Flow << YAML::BeginSeq << coefficient.x << coefficient.y
        << coefficient.z << YAML::EndSeq;
  }
  out << YAML::EndSeq;
  out << YAML::EndMap;
  return out.c_str();
}

LX_core::IblBakeValidationResult
IblBakeManifestParser::validateEnvironmentPayloadFiles(
    const std::filesystem::path &manifestPath,
    const EnvironmentManifest &manifest) const {
  LX_core::IblBakeValidationResult result;
  validateSh9PayloadFile(*this, result, manifestPath, manifest.diffuseFile,
                         "outputs.diffuse.file");
  validateKtx2PayloadFile(*this, result, manifestPath, manifest.specularFile,
                          "outputs.specular.file",
                          LX_core::TextureFormat::RGBA16Float,
                          LX_core::TextureDimension::TextureCube,
                          manifest.specularResolution,
                          manifest.specularResolution, manifest.specularMips,
                          manifest.specularFaces);
  return result;
}

LX_core::IblBakeValidationResult
IblBakeManifestParser::validateMaterialPayloadFiles(
    const std::filesystem::path &manifestPath,
    const MaterialManifest &manifest) const {
  LX_core::IblBakeValidationResult result;
  validateKtx2PayloadFile(*this, result, manifestPath, manifest.brdfFile,
                          "outputs.brdf.file",
                          LX_core::TextureFormat::RG16Float,
                          LX_core::TextureDimension::Texture2D,
                          manifest.brdfSize, manifest.brdfSize, 1u, 1u);
  return result;
}

AtomicCommitResult IblBakeManifestParser::writeAtomically(
    const std::filesystem::path &finalPath, std::string_view contents) const {
  AtomicCommitResult result;
  const std::filesystem::path parent = finalPath.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      result.diagnostics.push_back("failed to create directory '" +
                                   parent.generic_string() + "': " +
                                   ec.message());
      return result;
    }
  }

  const std::filesystem::path tempPath =
      finalPath.parent_path() /
      (finalPath.filename().generic_string() + ".tmp");
  if (!writeTempFile(result, tempPath, contents)) {
    return result;
  }

  std::error_code ec;
  std::filesystem::rename(tempPath, finalPath, ec);
  if (ec) {
    std::filesystem::remove(tempPath);
    result.diagnostics.push_back("failed to rename temp manifest to '" +
                                 finalPath.generic_string() + "': " +
                                 ec.message());
    return result;
  }
  if (!syncDirectory(result, parent)) {
    return result;
  }
  result.ok = true;
  return result;
}

AtomicCommitResult IblBakeManifestParser::writeEnvironmentManifestAtomically(
    const std::filesystem::path &finalPath,
    const EnvironmentManifest &manifest) const {
  AtomicCommitResult result;
  const auto validation = LX_core::validateIblBakeManifest(manifest);
  if (!validation.ok) {
    result.diagnostics = validation.diagnostics;
    return result;
  }
  return writeAtomically(finalPath, writeEnvironmentManifest(manifest));
}

} // namespace LX_infra
