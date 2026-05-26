#include "core/asset/audio_spectrum_texture.hpp"

#include <iostream>

using namespace LX_core;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " "       \
                << msg << " (" #cond ")\n";                                  \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testFakeSpectrumCreatesShadertoyTextureShape() {
  AudioSpectrumTexture audio(StringID("iChannel0"), 512);
  const auto sampler = audio.sampler();

  EXPECT(sampler != nullptr, "audio spectrum should expose sampler");
  EXPECT(sampler->getBindingName() == StringID("iChannel0"),
         "sampler binding name should be iChannel0");
  EXPECT(sampler->texture()->desc().width == 512,
         "audio spectrum width should match constructor");
  EXPECT(sampler->texture()->desc().height == 2,
         "audio spectrum should have spectrum and waveform rows");
  EXPECT(sampler->texture()->desc().format == TextureFormat::RGBA8,
         "audio spectrum should use RGBA8 for broad compatibility");
  EXPECT(sampler->getByteSize() == 512u * 2u * 4u,
         "audio spectrum byte size should match RGBA8 rows");

  audio.updateFake(1.25f);
  EXPECT(sampler->isDirty(), "fake audio update should mark sampler dirty");
}

void testTextureUpdateRejectsWrongByteCount() {
  auto texture = std::make_shared<Texture>(
      TextureDesc{4, 2, TextureFormat::RGBA8}, std::vector<u8>(4 * 2 * 4));
  auto sampler = std::make_shared<CombinedTextureSampler>(texture);

  bool rejected = false;
  try {
    sampler->update(std::vector<u8>(3));
  } catch (const std::runtime_error &error) {
    rejected = std::string(error.what()).find("byte count") !=
               std::string::npos;
  }
  EXPECT(rejected, "texture update should reject wrong byte count");
  EXPECT(!sampler->isDirty(),
         "rejected texture update should not mark sampler dirty");
}

void testTextureDescCountsHdrMipsAndCubeLayers() {
  TextureDesc hdrDesc{4, 2, TextureFormat::RGBA16Float};
  EXPECT(expectedTextureByteCount(hdrDesc) == 4u * 2u * 8u,
         "RGBA16Float byte count should be 8 bytes per pixel");

  TextureDesc cubeDesc;
  cubeDesc.width = 4;
  cubeDesc.height = 4;
  cubeDesc.format = TextureFormat::RGBA32Float;
  cubeDesc.dimension = TextureDimension::TextureCube;
  cubeDesc.mipLevels = 3;
  cubeDesc.arrayLayers = 6;
  EXPECT(expectedTextureByteCount(cubeDesc) ==
             ((4u * 4u) + (2u * 2u) + (1u * 1u)) * 16u * 6u,
         "cubemap byte count should include every mip and face");

  bool rejected = false;
  cubeDesc.arrayLayers = 1;
  try {
    (void)expectedTextureByteCount(cubeDesc);
  } catch (const std::runtime_error &error) {
    rejected = std::string(error.what()).find("6 array layers") !=
               std::string::npos;
  }
  EXPECT(rejected, "cubemap desc should reject non-six-layer shapes");

  rejected = false;
  cubeDesc.arrayLayers = 6;
  cubeDesc.height = 2;
  try {
    (void)expectedTextureByteCount(cubeDesc);
  } catch (const std::runtime_error &error) {
    rejected = std::string(error.what()).find("square") != std::string::npos;
  }
  EXPECT(rejected, "cubemap desc should reject non-square shapes");
}

void testTextureDescRejectsInvalidShapes() {
  TextureDesc zeroWidth{0, 4, TextureFormat::RGBA8};
  bool rejected = false;
  try {
    (void)expectedTextureByteCount(zeroWidth);
  } catch (const std::runtime_error &error) {
    rejected = std::string(error.what()).find("non-zero") !=
               std::string::npos;
  }
  EXPECT(rejected, "texture desc should reject zero dimensions");

  TextureDesc tooManyMips{4, 4, TextureFormat::RGBA8};
  tooManyMips.mipLevels = 4;
  rejected = false;
  try {
    (void)expectedTextureByteCount(tooManyMips);
  } catch (const std::runtime_error &error) {
    rejected = std::string(error.what()).find("exceeds") != std::string::npos;
  }
  EXPECT(rejected, "texture desc should reject mip counts beyond the extent");

  TextureDesc unsupportedArray{4, 4, TextureFormat::RGBA8};
  unsupportedArray.arrayLayers = 2;
  rejected = false;
  try {
    (void)expectedTextureByteCount(unsupportedArray);
  } catch (const std::runtime_error &error) {
    rejected = std::string(error.what()).find("2D array") !=
               std::string::npos;
  }
  EXPECT(rejected, "texture desc should reject unsupported 2D array textures");
}

} // namespace

int main() {
  testFakeSpectrumCreatesShadertoyTextureShape();
  testTextureUpdateRejectsWrongByteCount();
  testTextureDescCountsHdrMipsAndCubeLayers();
  testTextureDescRejectsInvalidShapes();

  if (failures != 0) {
    std::cerr << "test_audio_spectrum_texture failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_audio_spectrum_texture passed\n";
  return 0;
}
