#define STB_IMAGE_IMPLEMENTATION
#include "texture_loader.hpp"
#include "infra/image/rgba_image_io.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <stb/stb_image.h>

namespace infra {

struct TextureLoader::Impl {
  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char *data = nullptr;

  ~Impl() {
    if (data) {
      stbi_image_free(data);
    }
  }
};

TextureLoader::TextureLoader() : pImpl(std::make_unique<Impl>()) {}

TextureLoader::~TextureLoader() = default;

void TextureLoader::load(const std::string &filename) {
  int width, height, channels;
  // glTF and the renderer sample LDR texture data with v=0 at the first image
  // row. Flipping here misaligns all material maps before shader sampling.
  stbi_set_flip_vertically_on_load(false);
  unsigned char *imageData =
      stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);

  if (!imageData) {
    throw std::runtime_error("Failed to load texture: " + filename);
  }

  pImpl->width = width;
  pImpl->height = height;
  pImpl->channels = channels;
  pImpl->data = imageData;
}

int TextureLoader::getWidth() const { return pImpl->width; }

int TextureLoader::getHeight() const { return pImpl->height; }

int TextureLoader::getChannels() const { return pImpl->channels; }

const unsigned char *TextureLoader::getData() const { return pImpl->data; }

namespace {

constexpr std::array<u8, 12> kKtx2Identifier = {
    0xABu, 0x4Bu, 0x54u, 0x58u, 0x20u, 0x32u,
    0x30u, 0xBBu, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
constexpr u32 kVkFormatR16G16B16A16Sfloat = 97u;

template <typename T> [[nodiscard]] T readLe(const std::vector<u8> &bytes,
                                             usize offset) {
  if (offset + sizeof(T) > bytes.size()) {
    throw std::runtime_error("truncated KTX2 header");
  }
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

struct Ktx2Header {
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

struct Ktx2LevelIndex {
  u64 byteOffset = 0;
  u64 byteLength = 0;
  u64 uncompressedByteLength = 0;
};

[[nodiscard]] std::vector<u8> readBinaryFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    throw std::runtime_error("Failed to open texture: " + path.string());
  }
  const std::streamsize size = in.tellg();
  if (size < 0) {
    throw std::runtime_error("Failed to size texture: " + path.string());
  }
  std::vector<u8> bytes(static_cast<usize>(size));
  in.seekg(0, std::ios::beg);
  if (!bytes.empty() &&
      !in.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()))) {
    throw std::runtime_error("Failed to read texture: " + path.string());
  }
  return bytes;
}

[[nodiscard]] Ktx2Header parseKtx2Header(const std::vector<u8> &bytes) {
  if (bytes.size() < 80u) {
    throw std::runtime_error("KTX2 file is too small");
  }
  if (!std::equal(kKtx2Identifier.begin(), kKtx2Identifier.end(),
                  bytes.begin())) {
    throw std::runtime_error("Texture is not a KTX2 file");
  }

  Ktx2Header header;
  header.vkFormat = readLe<u32>(bytes, 12u);
  header.typeSize = readLe<u32>(bytes, 16u);
  header.pixelWidth = readLe<u32>(bytes, 20u);
  header.pixelHeight = readLe<u32>(bytes, 24u);
  header.pixelDepth = readLe<u32>(bytes, 28u);
  header.layerCount = readLe<u32>(bytes, 32u);
  header.faceCount = readLe<u32>(bytes, 36u);
  header.levelCount = readLe<u32>(bytes, 40u);
  header.supercompressionScheme = readLe<u32>(bytes, 44u);
  return header;
}

[[nodiscard]] std::vector<Ktx2LevelIndex>
parseKtx2LevelIndex(const std::vector<u8> &bytes, u32 levelCount) {
  constexpr usize kLevelIndexOffset = 80u;
  constexpr usize kLevelIndexSize = 24u;
  const usize byteCount = static_cast<usize>(levelCount) * kLevelIndexSize;
  if (kLevelIndexOffset + byteCount > bytes.size()) {
    throw std::runtime_error("KTX2 level index is truncated");
  }

  std::vector<Ktx2LevelIndex> levels;
  levels.reserve(levelCount);
  for (u32 level = 0; level < levelCount; ++level) {
    const usize offset = kLevelIndexOffset +
                         static_cast<usize>(level) * kLevelIndexSize;
    levels.push_back(Ktx2LevelIndex{
        .byteOffset = readLe<u64>(bytes, offset + 0u),
        .byteLength = readLe<u64>(bytes, offset + 8u),
        .uncompressedByteLength = readLe<u64>(bytes, offset + 16u),
    });
  }
  return levels;
}

void validateSupportedKtx2Cubemap(const Ktx2Header &header) {
  if (header.vkFormat != kVkFormatR16G16B16A16Sfloat) {
    throw std::runtime_error(
        "Unsupported KTX2 cubemap format: expected VK_FORMAT_R16G16B16A16_"
        "SFLOAT");
  }
  if (header.typeSize != 2u) {
    throw std::runtime_error("Unsupported KTX2 cubemap type size");
  }
  if (header.pixelWidth == 0u || header.pixelHeight == 0u) {
    throw std::runtime_error("KTX2 cubemap dimensions must be non-zero");
  }
  if (header.pixelWidth != header.pixelHeight) {
    throw std::runtime_error("KTX2 cubemap faces must be square");
  }
  if (header.pixelDepth != 0u) {
    throw std::runtime_error("3D KTX2 textures are not supported");
  }
  if (header.layerCount != 0u) {
    throw std::runtime_error("KTX2 texture arrays are not supported");
  }
  if (header.faceCount != 6u) {
    throw std::runtime_error("KTX2 cubemap must have six faces");
  }
  if (header.levelCount == 0u) {
    throw std::runtime_error("KTX2 cubemap must contain at least one mip level");
  }
  if (header.supercompressionScheme != 0u) {
    throw std::runtime_error("Supercompressed KTX2 cubemaps are not supported");
  }
}

} // namespace

LX_core::TextureSharedPtr
TextureLoader::loadHdrTexture(const std::filesystem::path &filename) {
  if (filename.extension() == ".exr" || filename.extension() == ".EXR") {
    const auto image = LX_infra::image::readRgba32fExr(filename);
    LX_core::TextureDesc desc;
    desc.width = image.width;
    desc.height = image.height;
    desc.format = LX_core::TextureFormat::RGBA32Float;
    desc.content = LX_core::TextureContent::Environment;
    const usize byteCount = LX_core::expectedTextureByteCount(desc);
    std::vector<u8> pixels(byteCount);
    std::memcpy(pixels.data(), image.rgba.data(), byteCount);
    return std::make_shared<LX_core::Texture>(desc, std::move(pixels));
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_set_flip_vertically_on_load(false);
  std::unique_ptr<float, decltype(&stbi_image_free)> imageData(
      stbi_loadf(filename.string().c_str(), &width, &height, &channels,
                 STBI_rgb_alpha),
      stbi_image_free);
  if (!imageData) {
    throw std::runtime_error("Failed to load HDR texture: " +
                             filename.string());
  }

  LX_core::TextureDesc desc;
  desc.width = static_cast<u32>(width);
  desc.height = static_cast<u32>(height);
  desc.format = LX_core::TextureFormat::RGBA32Float;
  desc.content = LX_core::TextureContent::Environment;

  const usize byteCount = LX_core::expectedTextureByteCount(desc);
  std::vector<u8> pixels(byteCount);
  std::memcpy(pixels.data(), imageData.get(), byteCount);

  return std::make_shared<LX_core::Texture>(desc, std::move(pixels));
}

LX_core::TextureSharedPtr
TextureLoader::loadKtx2Cubemap(const std::filesystem::path &filename) {
  const std::vector<u8> bytes = readBinaryFile(filename);
  const Ktx2Header header = parseKtx2Header(bytes);
  validateSupportedKtx2Cubemap(header);
  const std::vector<Ktx2LevelIndex> levels =
      parseKtx2LevelIndex(bytes, header.levelCount);

  LX_core::TextureDesc desc;
  desc.width = header.pixelWidth;
  desc.height = header.pixelHeight;
  desc.format = LX_core::TextureFormat::RGBA16Float;
  desc.content = LX_core::TextureContent::Environment;
  desc.dimension = LX_core::TextureDimension::TextureCube;
  desc.mipLevels = header.levelCount;
  desc.arrayLayers = 6u;

  std::vector<u8> pixels(LX_core::expectedTextureByteCount(desc));
  usize dstOffset = 0u;
  u32 mipWidth = desc.width;
  u32 mipHeight = desc.height;
  const usize bytesPerPixel =
      LX_core::textureBytesPerPixel(LX_core::TextureFormat::RGBA16Float);
  for (u32 mip = 0; mip < desc.mipLevels; ++mip) {
    const usize mipBytes = static_cast<usize>(std::max(mipWidth, 1u)) *
                           static_cast<usize>(std::max(mipHeight, 1u)) *
                           bytesPerPixel * static_cast<usize>(desc.arrayLayers);
    const Ktx2LevelIndex &level = levels[mip];
    if (level.uncompressedByteLength != mipBytes ||
        level.byteLength != mipBytes) {
      throw std::runtime_error(
          "Unsupported KTX2 cubemap level layout or compression");
    }
    if (level.byteOffset > bytes.size() ||
        mipBytes > bytes.size() - static_cast<usize>(level.byteOffset)) {
      throw std::runtime_error("KTX2 cubemap level data is out of bounds");
    }
    if (dstOffset > pixels.size() || mipBytes > pixels.size() - dstOffset) {
      throw std::runtime_error("KTX2 cubemap destination buffer overflow");
    }
    std::memcpy(pixels.data() + dstOffset,
                bytes.data() + static_cast<usize>(level.byteOffset), mipBytes);
    dstOffset += mipBytes;
    mipWidth = std::max(mipWidth / 2u, 1u);
    mipHeight = std::max(mipHeight / 2u, 1u);
  }

  return std::make_shared<LX_core::Texture>(desc, std::move(pixels));
}

} // namespace infra
