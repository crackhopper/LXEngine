#include "infra/resource_parsers/texture_resource_parser.hpp"

#include "core/asset/texture.hpp"
#include "infra/texture_loader/texture_loader.hpp"

#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace LX_infra {
namespace {

[[nodiscard]] bool isMemoryUri(const LX_core::ResourceUri &uri) {
  return uri.string().rfind("memory://", 0) == 0;
}

[[nodiscard]] std::filesystem::path pathFromUri(const LX_core::ResourceUri &uri) {
  const std::string &text = uri.string();
  constexpr std::string_view assetsPrefix = "assets://";
  constexpr std::string_view filePrefix = "file://";
  if (text.rfind(assetsPrefix, 0) == 0) {
    return std::filesystem::path("assets") /
           text.substr(assetsPrefix.size());
  }
  if (text.rfind(filePrefix, 0) == 0) {
    return std::filesystem::path(text.substr(filePrefix.size()));
  }
  return std::filesystem::path(text);
}

[[nodiscard]] LX_core::CombinedTextureSamplerUniquePtr makeWhiteSampler() {
  return std::make_unique<LX_core::CombinedTextureSampler>(
      LX_core::createWhiteTexture(1, 1));
}

[[nodiscard]] LX_core::CombinedTextureSamplerUniquePtr
loadTextureSampler(const LX_core::ResourceUri &uri,
                   LX_core::TextureContent content) {
  infra::TextureLoader loader;
  loader.load(pathFromUri(uri).string());

  LX_core::TextureDesc desc;
  desc.width = static_cast<u32>(loader.getWidth());
  desc.height = static_cast<u32>(loader.getHeight());
  desc.content = content;
  desc.format = (content == LX_core::TextureContent::Color ||
                 content == LX_core::TextureContent::Emissive)
                    ? LX_core::TextureFormat::RGBA8Srgb
                    : LX_core::TextureFormat::RGBA8;

  const usize byteCount = LX_core::expectedTextureByteCount(desc);
  std::vector<u8> pixels(byteCount);
  std::memcpy(pixels.data(), loader.getData(), byteCount);
  return std::make_unique<LX_core::CombinedTextureSampler>(
      std::make_shared<LX_core::Texture>(desc, std::move(pixels)));
}

[[nodiscard]] ParsedSceneResource makeFailedTextureParse(
    LX_core::SceneResourceTable &table, const LX_core::ResourceUri &uri,
    const LX_core::ResourceUri &ownerUri, std::string message) {
  ParsedSceneResource parsed;
  parsed.metadata.type = LX_core::SceneResourceType::Texture;
  parsed.metadata.uri = uri;
  parsed.metadata.state = LX_core::ResourceState::Failed;
  parsed.metadata.diagnostics.push_back(LX_core::ResourceDiagnostic{
      .ownerUri = ownerUri,
      .resourceUri = uri,
      .parserName = "TextureResourceParser",
      .message = message,
  });
  parsed.diagnostics.push_back(std::move(message));
  parsed.identity = table.internResourceMetadata(parsed.metadata);
  return parsed;
}

} // namespace

ParsedSceneResource TextureResourceParser::parse(
    LX_core::SceneResourceTable &table, const LX_core::ResourceUri &uri,
    const SceneResourceParseContext &context) const {
  const LX_core::ResourceUri canonicalUri =
      table.resolveUri(context.ownerUri, uri);

  if (const auto existing = table.findTexture(canonicalUri)) {
    ParsedSceneResource parsed;
    parsed.metadata.type = LX_core::SceneResourceType::Texture;
    parsed.metadata.uri = canonicalUri;
    parsed.identity = table.loadOrGetResource(
        LX_core::SceneResourceType::Texture, canonicalUri);
    if (const auto *metadata = table.findResourceMetadata(parsed.identity)) {
      parsed.metadata = *metadata;
    }
    return parsed;
  }

  LX_core::CombinedTextureSamplerUniquePtr sampler;
  if (isMemoryUri(canonicalUri)) {
    sampler = makeWhiteSampler();
  } else {
    try {
      sampler = loadTextureSampler(canonicalUri, context.textureContent);
    } catch (const std::exception &error) {
      return makeFailedTextureParse(table, canonicalUri, context.ownerUri,
                                    error.what());
    }
  }

  const LX_core::TextureHandle handle =
      table.registerTexture(canonicalUri, std::move(sampler));
  ParsedSceneResource parsed;
  parsed.metadata.type = LX_core::SceneResourceType::Texture;
  parsed.metadata.uri = canonicalUri;
  if (!handle.isValid()) {
    return makeFailedTextureParse(table, canonicalUri, context.ownerUri,
                                  "texture payload registration failed");
  }
  parsed.identity = table.loadOrGetResource(LX_core::SceneResourceType::Texture,
                                            canonicalUri);
  if (const auto *metadata = table.findResourceMetadata(parsed.identity)) {
    parsed.metadata = *metadata;
  }
  return parsed;
}

} // namespace LX_infra
