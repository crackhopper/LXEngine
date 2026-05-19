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

} // namespace

int main() {
  testFakeSpectrumCreatesShadertoyTextureShape();
  testTextureUpdateRejectsWrongByteCount();

  if (failures != 0) {
    std::cerr << "test_audio_spectrum_texture failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_audio_spectrum_texture passed\n";
  return 0;
}
