#include "core/package/scene_package_manifest.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace LX_core {
namespace {

constexpr std::string_view kManifestMagic = "LXPKG_SCENE_MANIFEST";
constexpr std::array<char, 8> kPackageMagic = {'L', 'X', 'P', 'K',
                                               'G', '0', '0', '1'};

[[nodiscard]] std::string toString(SceneResourceType type) {
  switch (type) {
  case SceneResourceType::Mesh:
    return "Mesh";
  case SceneResourceType::Texture:
    return "Texture";
  case SceneResourceType::Material:
    return "Material";
  case SceneResourceType::Camera:
    return "Camera";
  case SceneResourceType::Light:
    return "Light";
  case SceneResourceType::RenderEffect:
    return "RenderEffect";
  }
  return "Material";
}

[[nodiscard]] SceneResourceType parseResourceType(std::string_view value) {
  if (value == "Mesh") {
    return SceneResourceType::Mesh;
  }
  if (value == "Texture") {
    return SceneResourceType::Texture;
  }
  if (value == "Material") {
    return SceneResourceType::Material;
  }
  if (value == "Camera") {
    return SceneResourceType::Camera;
  }
  if (value == "Light") {
    return SceneResourceType::Light;
  }
  if (value == "RenderEffect") {
    return SceneResourceType::RenderEffect;
  }
  throw std::runtime_error("Unknown scene package resource type: " +
                           std::string(value));
}

[[nodiscard]] std::string toString(ResourceMetadataState state) {
  switch (state) {
  case ResourceMetadataState::Empty:
    return "Empty";
  case ResourceMetadataState::Alive:
    return "Alive";
  case ResourceMetadataState::Error:
    return "Error";
  }
  return "Alive";
}

[[nodiscard]] ResourceMetadataState parseMetadataState(std::string_view value) {
  if (value == "Empty") {
    return ResourceMetadataState::Empty;
  }
  if (value == "Alive") {
    return ResourceMetadataState::Alive;
  }
  if (value == "Error") {
    return ResourceMetadataState::Error;
  }
  throw std::runtime_error("Unknown scene package metadata state: " +
                           std::string(value));
}

[[nodiscard]] std::string toHex(std::string_view value) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string output;
  output.reserve(value.size() * 2u);
  for (const unsigned char c : value) {
    output.push_back(digits[(c >> 4u) & 0x0fu]);
    output.push_back(digits[c & 0x0fu]);
  }
  return output;
}

[[nodiscard]] u8 parseHexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<u8>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<u8>(c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<u8>(c - 'A' + 10);
  }
  throw std::runtime_error("Invalid hex digit in scene package manifest");
}

[[nodiscard]] std::string fromHex(std::string_view value) {
  if ((value.size() % 2u) != 0u) {
    throw std::runtime_error("Odd-length hex string in scene package manifest");
  }
  std::string output;
  output.reserve(value.size() / 2u);
  for (usize i = 0; i < value.size(); i += 2u) {
    const u8 high = parseHexDigit(value[i]);
    const u8 low = parseHexDigit(value[i + 1u]);
    output.push_back(static_cast<char>((high << 4u) | low));
  }
  return output;
}

[[nodiscard]] bool lessUri(const ResourceUri &lhs, const ResourceUri &rhs) {
  return lhs.string() < rhs.string();
}

void sortMetadata(ResourceMetadata &metadata) {
  std::sort(metadata.dependencies.begin(), metadata.dependencies.end(),
            lessUri);
  std::sort(metadata.diagnostics.begin(), metadata.diagnostics.end(),
            [](const ResourceDiagnostic &lhs,
               const ResourceDiagnostic &rhs) {
              return std::tie(lhs.ownerUri.string(), lhs.resourceUri.string(),
                              lhs.parserName, lhs.message) <
                     std::tie(rhs.ownerUri.string(), rhs.resourceUri.string(),
                              rhs.parserName, rhs.message);
            });
}

void sortManifestResources(ScenePackageManifest &manifest) {
  for (auto &record : manifest.resources) {
    sortMetadata(record.metadata);
  }
  std::sort(manifest.resources.begin(), manifest.resources.end(),
            [](const ScenePackageResourceRecord &lhs,
               const ScenePackageResourceRecord &rhs) {
              return std::tie(lhs.metadata.uri.string(), lhs.metadata.type,
                              lhs.metadata.contentHash,
                              lhs.sourceHandle.index,
                              lhs.sourceHandle.generation) <
                     std::tie(rhs.metadata.uri.string(), rhs.metadata.type,
                              rhs.metadata.contentHash,
                              rhs.sourceHandle.index,
                              rhs.sourceHandle.generation);
            });
}

class StableHashBuilder final {
public:
  void append(std::string_view value) {
    appendBytes(value);
    appendByte(0xffu);
  }

  void append(u64 value) {
    for (int i = 0; i < 8; ++i) {
      appendByte(static_cast<u8>((value >> (i * 8)) & 0xffu));
    }
  }

  [[nodiscard]] std::string hex() const {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << m_hash;
    return out.str();
  }

private:
  void appendBytes(std::string_view value) {
    append(static_cast<u64>(value.size()));
    for (const unsigned char c : value) {
      appendByte(c);
    }
  }

  void appendByte(u8 value) {
    m_hash ^= value;
    m_hash *= 1099511628211ull;
  }

  u64 m_hash = 14695981039346656037ull;
};

void appendHashResource(StableHashBuilder &hash,
                        const ScenePackageResourceRecord &record) {
  const auto &metadata = record.metadata;
  hash.append("resource");
  hash.append(toString(metadata.type));
  hash.append(toString(metadata.state));
  hash.append(metadata.uri.string());
  hash.append(metadata.contentHash);

  hash.append(static_cast<u64>(metadata.dependencies.size()));
  for (const auto &dependency : metadata.dependencies) {
    hash.append(dependency.string());
  }

  hash.append(static_cast<u64>(metadata.diagnostics.size()));
  for (const auto &diagnostic : metadata.diagnostics) {
    hash.append(diagnostic.ownerUri.string());
    hash.append(diagnostic.resourceUri.string());
    hash.append(diagnostic.parserName);
    hash.append(diagnostic.message);
  }
}

[[nodiscard]] std::string_view payloadAfter(std::string_view line,
                                            std::string_view key) {
  if (!line.starts_with(key)) {
    throw std::runtime_error("Expected scene package manifest key: " +
                             std::string(key));
  }
  if (line.size() == key.size()) {
    return {};
  }
  if (line[key.size()] != ' ') {
    throw std::runtime_error("Malformed scene package manifest line: " +
                             std::string(line));
  }
  return line.substr(key.size() + 1u);
}

[[nodiscard]] u64 parseUnsigned(std::string_view value) {
  u64 parsed = 0;
  const auto *begin = value.data();
  const auto *end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    throw std::runtime_error("Invalid unsigned integer in scene package manifest");
  }
  return parsed;
}

[[nodiscard]] std::vector<std::string> splitLines(std::string_view text) {
  std::vector<std::string> lines;
  usize begin = 0;
  while (begin <= text.size()) {
    const usize end = text.find('\n', begin);
    if (end == std::string_view::npos) {
      if (begin < text.size()) {
        lines.emplace_back(text.substr(begin));
      }
      break;
    }
    lines.emplace_back(text.substr(begin, end - begin));
    begin = end + 1u;
  }
  return lines;
}

class ManifestLineReader final {
public:
  explicit ManifestLineReader(std::string_view text) : m_lines(splitLines(text)) {}

  [[nodiscard]] std::string_view next() {
    if (m_index >= m_lines.size()) {
      throw std::runtime_error("Unexpected end of scene package manifest");
    }
    return m_lines[m_index++];
  }

private:
  std::vector<std::string> m_lines;
  usize m_index = 0;
};

void appendU32(std::vector<std::byte> &bytes, u32 value) {
  for (int i = 0; i < 4; ++i) {
    bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xffu));
  }
}

[[nodiscard]] u32 readU32(std::span<const std::byte> bytes, usize offset) {
  if (bytes.size() < offset + 4u) {
    throw std::runtime_error("Truncated scene package byte header");
  }
  u32 value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<u32>(std::to_integer<u8>(bytes[offset + i]))
             << (i * 8);
  }
  return value;
}

} // namespace

ScenePackageManifest
buildScenePackageManifest(const SceneResourceGraphExport &graph) {
  if (graph.handles.size() != graph.resources.size()) {
    throw std::runtime_error(
        "Scene resource graph handles/resources size mismatch");
  }

  ScenePackageManifest manifest;
  manifest.resources.reserve(graph.resources.size());
  for (usize i = 0; i < graph.resources.size(); ++i) {
    manifest.resources.push_back(ScenePackageResourceRecord{
        .sourceHandle = graph.handles[i],
        .metadata = graph.resources[i],
    });
  }

  sortManifestResources(manifest);
  manifest.rootHash = computeScenePackageRootHash(manifest);
  return manifest;
}

std::string computeScenePackageRootHash(const ScenePackageManifest &manifest) {
  ScenePackageManifest sorted = manifest;
  sortManifestResources(sorted);

  StableHashBuilder hash;
  hash.append("LXPKG_SCENE_RESOURCE_ROOT");
  hash.append(static_cast<u64>(sorted.schemaVersion));
  hash.append(static_cast<u64>(sorted.resources.size()));
  for (const auto &record : sorted.resources) {
    appendHashResource(hash, record);
  }
  return hash.hex();
}

std::string writeScenePackageManifest(const ScenePackageManifest &manifest) {
  ScenePackageManifest sorted = manifest;
  sortManifestResources(sorted);
  sorted.rootHash = computeScenePackageRootHash(sorted);

  std::ostringstream out;
  out << kManifestMagic << ' ' << sorted.schemaVersion << '\n';
  out << "root " << sorted.rootHash << '\n';
  out << "resources " << sorted.resources.size() << '\n';
  for (const auto &record : sorted.resources) {
    const auto &metadata = record.metadata;
    out << "resource\n";
    out << "handle " << record.sourceHandle.index << ' '
        << record.sourceHandle.generation << '\n';
    out << "type " << toString(metadata.type) << '\n';
    out << "state " << toString(metadata.state) << '\n';
    out << "uri " << toHex(metadata.uri.string()) << '\n';
    out << "content " << toHex(metadata.contentHash) << '\n';
    out << "dependencies " << metadata.dependencies.size() << '\n';
    for (const auto &dependency : metadata.dependencies) {
      out << "dep " << toHex(dependency.string()) << '\n';
    }
    out << "diagnostics " << metadata.diagnostics.size() << '\n';
    for (const auto &diagnostic : metadata.diagnostics) {
      out << "diag-owner " << toHex(diagnostic.ownerUri.string()) << '\n';
      out << "diag-resource " << toHex(diagnostic.resourceUri.string()) << '\n';
      out << "diag-parser " << toHex(diagnostic.parserName) << '\n';
      out << "diag-message " << toHex(diagnostic.message) << '\n';
    }
    out << "end-resource\n";
  }
  return out.str();
}

ScenePackageManifest readScenePackageManifest(std::string_view manifestText) {
  ManifestLineReader reader(manifestText);

  const std::string_view header = reader.next();
  const std::string_view versionText = payloadAfter(header, kManifestMagic);
  ScenePackageManifest manifest;
  manifest.schemaVersion = static_cast<u32>(parseUnsigned(versionText));
  manifest.rootHash = std::string(payloadAfter(reader.next(), "root"));

  const u64 resourceCount = parseUnsigned(payloadAfter(reader.next(), "resources"));
  if (resourceCount > std::numeric_limits<u32>::max()) {
    throw std::runtime_error("Too many resources in scene package manifest");
  }
  manifest.resources.reserve(static_cast<usize>(resourceCount));

  for (u64 i = 0; i < resourceCount; ++i) {
    if (reader.next() != "resource") {
      throw std::runtime_error("Expected resource record in package manifest");
    }

    ScenePackageResourceRecord record;
    {
      const std::string_view handleText = payloadAfter(reader.next(), "handle");
      const usize split = handleText.find(' ');
      if (split == std::string_view::npos) {
        throw std::runtime_error("Malformed package resource handle");
      }
      record.sourceHandle.index =
          static_cast<u32>(parseUnsigned(handleText.substr(0, split)));
      record.sourceHandle.generation =
          static_cast<u32>(parseUnsigned(handleText.substr(split + 1u)));
    }

    record.metadata.type = parseResourceType(payloadAfter(reader.next(), "type"));
    record.metadata.state =
        parseMetadataState(payloadAfter(reader.next(), "state"));
    record.metadata.uri = ResourceUri(fromHex(payloadAfter(reader.next(), "uri")));
    record.metadata.contentHash =
        fromHex(payloadAfter(reader.next(), "content"));

    const u64 dependencyCount =
        parseUnsigned(payloadAfter(reader.next(), "dependencies"));
    record.metadata.dependencies.reserve(static_cast<usize>(dependencyCount));
    for (u64 dep = 0; dep < dependencyCount; ++dep) {
      record.metadata.dependencies.push_back(
          ResourceUri(fromHex(payloadAfter(reader.next(), "dep"))));
    }

    const u64 diagnosticCount =
        parseUnsigned(payloadAfter(reader.next(), "diagnostics"));
    record.metadata.diagnostics.reserve(static_cast<usize>(diagnosticCount));
    for (u64 diagnostic = 0; diagnostic < diagnosticCount; ++diagnostic) {
      ResourceDiagnostic entry;
      entry.ownerUri =
          ResourceUri(fromHex(payloadAfter(reader.next(), "diag-owner")));
      entry.resourceUri =
          ResourceUri(fromHex(payloadAfter(reader.next(), "diag-resource")));
      entry.parserName = fromHex(payloadAfter(reader.next(), "diag-parser"));
      entry.message = fromHex(payloadAfter(reader.next(), "diag-message"));
      record.metadata.diagnostics.push_back(std::move(entry));
    }

    if (reader.next() != "end-resource") {
      throw std::runtime_error("Expected end-resource in package manifest");
    }
    manifest.resources.push_back(std::move(record));
  }

  sortManifestResources(manifest);
  const std::string computed = computeScenePackageRootHash(manifest);
  if (manifest.rootHash != computed) {
    throw std::runtime_error("Scene package manifest root hash mismatch");
  }
  return manifest;
}

std::vector<std::byte>
writeScenePackageBytes(const ScenePackageManifest &manifest) {
  const std::string text = writeScenePackageManifest(manifest);
  if (text.size() > std::numeric_limits<u32>::max()) {
    throw std::runtime_error("Scene package manifest is too large");
  }

  std::vector<std::byte> bytes;
  bytes.reserve(kPackageMagic.size() + 4u + text.size());
  for (const char c : kPackageMagic) {
    bytes.push_back(static_cast<std::byte>(c));
  }
  appendU32(bytes, static_cast<u32>(text.size()));
  for (const unsigned char c : text) {
    bytes.push_back(static_cast<std::byte>(c));
  }
  return bytes;
}

ScenePackageManifest readScenePackageBytes(std::span<const std::byte> bytes) {
  if (bytes.size() < kPackageMagic.size() + 4u) {
    throw std::runtime_error("Truncated scene package byte stream");
  }
  for (usize i = 0; i < kPackageMagic.size(); ++i) {
    if (std::to_integer<char>(bytes[i]) != kPackageMagic[i]) {
      throw std::runtime_error("Invalid scene package byte stream magic");
    }
  }

  const u32 textSize = readU32(bytes, kPackageMagic.size());
  const usize textOffset = kPackageMagic.size() + 4u;
  if (bytes.size() != textOffset + textSize) {
    throw std::runtime_error("Scene package byte stream size mismatch");
  }

  std::string text;
  text.reserve(textSize);
  for (usize i = 0; i < textSize; ++i) {
    text.push_back(std::to_integer<char>(bytes[textOffset + i]));
  }
  return readScenePackageManifest(text);
}

} // namespace LX_core
