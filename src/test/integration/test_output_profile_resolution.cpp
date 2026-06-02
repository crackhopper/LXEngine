#include "core/offline/offline_render_profile.hpp"

#include <iostream>

namespace {
int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testDefaultProfilesResolvePreview() {
  const auto document = LX_core::offline::makeDefaultRenderProfileDocument();
  LX_core::offline::RenderProfileCliOverrides overrides;
  const auto resolved =
      LX_core::offline::resolveRenderProfileDocument(document, overrides);

  EXPECT(resolved.profileName == "preview", "default output profile name");
  EXPECT(resolved.output.width == 512u, "default width");
  EXPECT(resolved.output.height == 512u, "default height");
  EXPECT(resolved.output.cameraPath == "/game_cam", "default camera path");
  EXPECT(resolved.output.outDir == "artifacts", "default outDir");
  EXPECT(resolved.offline.samples == 1u, "default samples");
  EXPECT(resolved.offline.maxBounce == 1u, "default maxBounce");
}

void testCliOverridesOutputAndOfflineFields() {
  LX_core::offline::RenderProfileDocument document;
  document.defaultOutputProfile = "preview";
  LX_core::offline::OutputProfile preview;
  preview.cameraPath = "/game_cam";
  preview.width = 512;
  preview.height = 512;
  preview.outDir = "artifacts";
  document.outputProfiles.emplace("preview", preview);
  document.offline.profileName = "preview";
  document.offline.samples = 1;
  document.offline.maxBounce = 1;

  LX_core::offline::RenderProfileCliOverrides overrides;
  overrides.profileName = "preview";
  overrides.width = 64;
  overrides.height = 36;
  overrides.samples = 8;
  overrides.maxBounce = 3;
  overrides.seed = 99;
  overrides.outputPath = "artifacts/manual/render";

  const auto resolved =
      LX_core::offline::resolveRenderProfileDocument(document, overrides);

  EXPECT(resolved.output.width == 64u, "width override");
  EXPECT(resolved.output.height == 36u, "height override");
  EXPECT(resolved.offline.samples == 8u, "samples override");
  EXPECT(resolved.offline.maxBounce == 3u, "maxBounce override");
  EXPECT(resolved.offline.seed == 99u, "seed override");
  EXPECT(resolved.outputPath == "artifacts/manual/render", "output path");
}

void testDefaultOutputProfileSelectsNonPreviewWhenOfflineProfileEmpty() {
  LX_core::offline::RenderProfileDocument document;
  document.defaultOutputProfile = "reference";
  LX_core::offline::OutputProfile reference;
  reference.width = 1920;
  reference.height = 1080;
  document.outputProfiles.emplace("reference", reference);

  const auto resolved = LX_core::offline::resolveRenderProfileDocument(
      document, LX_core::offline::RenderProfileCliOverrides{});

  EXPECT(resolved.profileName == "reference", "default output profile name");
  EXPECT(resolved.output.width == 1920u, "reference width");
  EXPECT(resolved.output.height == 1080u, "reference height");
}

void testMissingProfileThrows() {
  LX_core::offline::RenderProfileDocument document;
  document.outputProfiles.emplace("preview",
                                  LX_core::offline::makeDefaultOutputProfile());
  document.offline.profileName = "missing";
  bool threw = false;
  try {
    (void)LX_core::offline::resolveRenderProfileDocument(
        document, LX_core::offline::RenderProfileCliOverrides{});
  } catch (const std::exception &e) {
    threw = std::string(e.what()).find("output profile not found") !=
            std::string::npos;
  }
  EXPECT(threw, "missing output profile should throw useful message");
}
} // namespace

int main() {
  testDefaultProfilesResolvePreview();
  testCliOverridesOutputAndOfflineFields();
  testDefaultOutputProfileSelectsNonPreviewWhenOfflineProfileEmpty();
  testMissingProfileThrows();
  if (failures != 0) {
    std::cerr << "test_output_profile_resolution failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_output_profile_resolution passed\n";
  return 0;
}
