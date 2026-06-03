# Output Profiles Realtime Render Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement REQ-068-a by splitting scene output profiles from offline render settings, adding realtime profile generation and local verification, and making realtime/offline EXR comparison testable by Codex.

**Architecture:** Split the work into three layers. First, update the scene/profile data model and CLI so both offline and realtime paths use the same `OutputProfile`. Second, refactor shared output/tone-mapping utilities and command registration so realtime profile commands have a small home. Third, implement realtime profile generation, local CLI access, and EXR comparison on top of existing Vulkan frame-graph dump/readback behavior.

**Tech Stack:** C++20, CMake/Ninja, Vulkan, yaml-cpp, tinyexr, stb_image_write, GLSL/glslc, LXEngine command bus, LXEngine scene document / offline compiler.

---

## File Structure

### Core / Infra Profile Model

- Modify `src/core/offline/offline_render_profile.hpp`: replace `OfflineRenderProfile` with `OutputProfile`, `OfflineRenderSettings`, and resolver result types.
- Modify `src/core/offline/offline_render_profile.cpp`: implement defaults, output profile resolution, and offline settings overrides.
- Modify `src/core/offline/offline_scene.hpp`: change `OfflineRenderJob.profile` to output + offline settings.
- Modify `src/infra/scene_io/scene_document.hpp`: expose `outputProfiles()` and `offlineRenderSettings()`.
- Modify `src/infra/scene_io/scene_document.cpp`: parse/save new YAML schema; fail fast on old `scene.offlineRender.profiles`.

### CLI / Tools

- Modify `src/tools/lxe_offline_render/offline_render_cli.hpp`.
- Modify `src/tools/lxe_offline_render/offline_render_cli.cpp`.
- Modify `src/tools/lxe_offline_render/main.cpp`.
- Create `src/tools/lxe_realtime_render/CMakeLists.txt`.
- Create `src/tools/lxe_realtime_render/main.cpp`.
- Create `src/tools/lxe_compare_exr/CMakeLists.txt`.
- Create `src/tools/lxe_compare_exr/main.cpp`.
- Modify `src/tools/CMakeLists.txt` to add both tools.

### Shared Image / Tone Mapping

- Create `src/core/image/tone_mapping.hpp`.
- Create `src/core/image/tone_mapping.cpp`.
- Create `src/infra/image/rgba_image_io.hpp`.
- Create `src/infra/image/rgba_image_io.cpp`.
- Modify `src/infra/offline/offline_image_writer.hpp`.
- Modify `src/infra/offline/offline_image_writer.cpp` to call shared image utilities instead of owning tone mapping/write logic.
- Create `assets/shaders/glsl/common/tone_mapping.glsl`.

### Realtime Render Generation

- Modify `src/backend/vulkan/vulkan_realtime_renderer.hpp`.
- Modify `src/backend/vulkan/vulkan_realtime_renderer.cpp`.
- Modify `src/backend/vulkan/vulkan_renderer.hpp`.
- Modify `src/demos/lxe_editor/editor_session.hpp`.
- Modify `src/demos/lxe_editor/editor_session.cpp`.
- Modify `src/demos/lxe_editor/main.cpp`.
- Create `src/demos/lxe_editor/realtime_render_profile.hpp`.
- Create `src/demos/lxe_editor/realtime_render_profile.cpp`.

### Command Registration Split

- Create `src/demos/lxe_editor/commands/lxe_editor_command_helpers.hpp`.
- Create `src/demos/lxe_editor/commands/lxe_editor_command_helpers.cpp`.
- Create `src/demos/lxe_editor/commands/register_lxe_editor_commands.hpp`.
- Create `src/demos/lxe_editor/commands/register_lxe_editor_commands.cpp`.
- Create `src/demos/lxe_editor/commands/project_commands.cpp`.
- Create `src/demos/lxe_editor/commands/scene_project_commands.cpp`.
- Create `src/demos/lxe_editor/commands/recording_commands.cpp`.
- Create `src/demos/lxe_editor/commands/display_commands.cpp`.
- Create `src/demos/lxe_editor/commands/render_debug_commands.cpp`.
- Create `src/demos/lxe_editor/commands/realtime_render_commands.cpp`.
- Modify `src/demos/lxe_editor/lxe_editor_commands.hpp` to delegate to the new folder.
- Modify `src/demos/lxe_editor/lxe_editor_commands.cpp` to retain only compatibility includes or a thin forwarding implementation.

### Tests / Assets / Docs

- Modify `src/test/integration/test_scene_document.cpp`.
- Modify `src/test/integration/test_offline_render_cli.cpp`.
- Modify `src/test/integration/test_offline_image_writer.cpp`.
- Create `src/test/integration/test_output_profile_resolution.cpp`.
- Create `src/test/integration/test_tone_mapping.cpp`.
- Create `src/test/integration/test_realtime_render_profile_commands.cpp`.
- Create `src/test/integration/test_exr_compare.cpp`.
- Modify `src/test/CMakeLists.txt`.
- Modify `assets/scenes/ibl_metal_sphere.scene.yaml`.
- Create `assets/scenes/realtime_offline_compare.scene.yaml`.
- Modify `notes/requirements/068-a-output-profiles-and-realtime-render-generation.md` implementation status after completion.

---

## Task 1: Add New Profile Types and Resolver Tests

**Files:**
- Modify: `src/core/offline/offline_render_profile.hpp`
- Modify: `src/core/offline/offline_render_profile.cpp`
- Create: `src/test/integration/test_output_profile_resolution.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Write the failing resolver test**

Create `src/test/integration/test_output_profile_resolution.cpp`:

```cpp
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

void testMissingProfileThrows() {
  LX_core::offline::RenderProfileDocument document;
  document.defaultOutputProfile = "missing";
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
  testMissingProfileThrows();
  if (failures != 0) {
    std::cerr << "test_output_profile_resolution failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_output_profile_resolution passed\n";
  return 0;
}
```

- [ ] **Step 2: Register the failing test target**

In `src/test/CMakeLists.txt`, add `test_output_profile_resolution` to `TEST_INTEGRATION_EXE_LIST` near `test_offline_render_cli`:

```cmake
  test_output_profile_resolution
  test_offline_render_cli
```

- [ ] **Step 3: Run the test to verify it fails**

Run:

```bash
cmake --build build --target test_output_profile_resolution
```

Expected: compile failure because `RenderProfileDocument`, `OutputProfile`, and `resolveRenderProfileDocument` do not exist yet.

- [ ] **Step 4: Replace profile model declarations**

In `src/core/offline/offline_render_profile.hpp`, replace the old profile structs with:

```cpp
struct OutputCameraOverrides final {
  std::optional<float> fovY;
  std::optional<float> aspect;
  std::optional<float> nearPlane;
  std::optional<float> farPlane;
  std::optional<float> focusDistance;
  std::optional<float> orthographicHeight;
  std::optional<u32> cullingMask;
};

struct OutputProfile final {
  std::string cameraPath = "/game_cam";
  u32 width = 512;
  u32 height = 512;
  std::string outputFormat = "exr-png";
  std::filesystem::path outDir = "artifacts";
  OutputCameraOverrides cameraOverrides;
  std::map<std::string, std::string> extensionYamlByField;
};

struct OfflineRenderSettings final {
  std::string integrator = "primary-ray";
  u32 samples = 1;
  u32 maxBounce = 1;
  u32 seed = 1;
  std::string profileName = "preview";
  bool shadows = true;
  std::map<std::string, std::string> extensionYamlByField;
};

struct RenderProfileDocument final {
  std::string defaultOutputProfile = "preview";
  std::unordered_map<std::string, OutputProfile> outputProfiles;
  OfflineRenderSettings offline;

  [[nodiscard]] bool empty() const { return outputProfiles.empty(); }
};

struct RenderProfileCliOverrides final {
  std::optional<std::string> profileName;
  std::optional<u32> width;
  std::optional<u32> height;
  std::optional<u32> samples;
  std::optional<u32> maxBounce;
  std::optional<u32> seed;
  std::optional<std::filesystem::path> outputPath;
};

struct ResolvedRenderProfile final {
  std::string profileName;
  OutputProfile output;
  OfflineRenderSettings offline;
  std::optional<std::filesystem::path> outputPath;
};

[[nodiscard]] OutputProfile makeDefaultOutputProfile();
[[nodiscard]] OfflineRenderSettings makeDefaultOfflineRenderSettings();
[[nodiscard]] RenderProfileDocument makeDefaultRenderProfileDocument();

[[nodiscard]] ResolvedRenderProfile resolveRenderProfileDocument(
    const RenderProfileDocument &document,
    const RenderProfileCliOverrides &overrides);
```

Add `#include <filesystem>` at the top of that header.

- [ ] **Step 5: Implement the resolver**

In `src/core/offline/offline_render_profile.cpp`, implement:

```cpp
OutputProfile makeDefaultOutputProfile() { return OutputProfile{}; }

OfflineRenderSettings makeDefaultOfflineRenderSettings() {
  return OfflineRenderSettings{};
}

RenderProfileDocument makeDefaultRenderProfileDocument() {
  RenderProfileDocument document;
  document.defaultOutputProfile = "preview";
  document.outputProfiles.emplace("preview", makeDefaultOutputProfile());
  document.offline = makeDefaultOfflineRenderSettings();
  return document;
}

ResolvedRenderProfile resolveRenderProfileDocument(
    const RenderProfileDocument &document,
    const RenderProfileCliOverrides &overrides) {
  const RenderProfileDocument effective =
      document.empty() ? makeDefaultRenderProfileDocument() : document;

  OfflineRenderSettings offline = effective.offline;
  if (overrides.profileName.has_value()) {
    offline.profileName = *overrides.profileName;
  } else if (offline.profileName.empty()) {
    offline.profileName = effective.defaultOutputProfile;
  }

  const auto profileIt = effective.outputProfiles.find(offline.profileName);
  if (profileIt == effective.outputProfiles.end()) {
    throw std::runtime_error("output profile not found: " + offline.profileName);
  }

  OutputProfile output = profileIt->second;
  if (overrides.width.has_value()) {
    output.width = *overrides.width;
  }
  if (overrides.height.has_value()) {
    output.height = *overrides.height;
  }
  if (overrides.samples.has_value()) {
    offline.samples = *overrides.samples;
  }
  if (overrides.maxBounce.has_value()) {
    offline.maxBounce = *overrides.maxBounce;
  }
  if (overrides.seed.has_value()) {
    offline.seed = *overrides.seed;
  }

  if (output.width == 0 || output.height == 0) {
    throw std::runtime_error("output profile " + offline.profileName +
                             " width/height must be positive");
  }
  if (offline.samples == 0 || offline.maxBounce == 0) {
    throw std::runtime_error("offlineRender samples/maxBounce must be positive");
  }

  return ResolvedRenderProfile{
      .profileName = offline.profileName,
      .output = std::move(output),
      .offline = std::move(offline),
      .outputPath = overrides.outputPath,
  };
}
```

- [ ] **Step 6: Run the resolver test**

Run:

```bash
cmake --build build --target test_output_profile_resolution
./build/src/test/test_output_profile_resolution
```

Expected: `test_output_profile_resolution passed`.

- [ ] **Step 7: Commit**

```bash
git add src/core/offline/offline_render_profile.hpp \
  src/core/offline/offline_render_profile.cpp \
  src/test/integration/test_output_profile_resolution.cpp \
  src/test/CMakeLists.txt
git commit -m "add output profile resolver model"
```

---

## Task 2: Migrate Scene YAML Parsing and Scene Assets

**Files:**
- Modify: `src/infra/scene_io/scene_document.hpp`
- Modify: `src/infra/scene_io/scene_document.cpp`
- Modify: `src/test/integration/test_scene_document.cpp`
- Modify: `assets/scenes/ibl_metal_sphere.scene.yaml`
- Modify: other `assets/scenes/*.scene.yaml` files that contain `offlineRender.profiles`

- [ ] **Step 1: Add failing scene-document tests**

In `src/test/integration/test_scene_document.cpp`, add tests:

```cpp
void testOutputProfilesRoundTrip() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_output_profiles.yaml");
  std::ofstream out(path);
  out << "scene:\n"
         "  name: output profile test\n"
         "  gameplayCameraPath: /game_cam\n"
         "  defaultOutputProfile: preview\n"
         "  outputProfiles:\n"
         "    preview:\n"
         "      camera: /game_cam\n"
         "      width: 64\n"
         "      height: 36\n"
         "      outputFormat: exr-png\n"
         "      outDir: artifacts/compare\n"
         "      cameraOverrides:\n"
         "        fovY: 42.0\n"
         "        nearPlane: 0.1\n"
         "        farPlane: 80.0\n"
         "        focusDistance: 5.0\n"
         "  offlineRender:\n"
         "    integrator: primary-ray\n"
         "    samples: 1\n"
         "    maxBounce: 1\n"
         "    seed: 7\n"
         "    profile: preview\n"
         "    shadows: false\n"
         "root:\n"
         "  nodeName: scene_root\n"
         "  name: ''\n"
         "  transform:\n"
         "    translation: [0.0, 0.0, 0.0]\n"
         "    rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "    scale: [1.0, 1.0, 1.0]\n"
         "  visibilityMask: 4294967295\n";
  out.close();

  const auto doc = LX_infra::scene_io::loadSceneDocument(path);
  EXPECT(doc.hasRenderProfileDocument(), "render profile document should load");
  const auto &profiles = doc.renderProfileDocument();
  EXPECT(profiles.defaultOutputProfile == "preview", "default profile");
  EXPECT(profiles.outputProfiles.at("preview").width == 64u, "width");
  EXPECT(profiles.outputProfiles.at("preview").cameraOverrides.fovY == 42.0f,
         "camera override fov");
  EXPECT(profiles.offline.maxBounce == 1u, "maxBounce");
  EXPECT(profiles.offline.shadows == false, "shadow flag");

  const std::filesystem::path saved =
      makeTempPath("lx_scene_output_profiles_saved.yaml");
  LX_infra::scene_io::saveSceneDocument(saved, doc);
  const std::string savedText = readFile(saved);
  EXPECT(savedText.find("outputProfiles:") != std::string::npos,
         "saved new outputProfiles");
  EXPECT(savedText.find("maxBounce: 1") != std::string::npos,
         "saved maxBounce");
}

void testOldOfflineRenderProfilesRejected() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_old_offline_profiles.yaml");
  std::ofstream out(path);
  out << "scene:\n"
         "  name: old profile test\n"
         "  offlineRender:\n"
         "    defaultProfile: preview\n"
         "    profiles:\n"
         "      preview:\n"
         "        width: 64\n"
         "        height: 36\n"
         "root:\n"
         "  nodeName: scene_root\n"
         "  name: ''\n"
         "  transform:\n"
         "    translation: [0.0, 0.0, 0.0]\n"
         "    rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "    scale: [1.0, 1.0, 1.0]\n"
         "  visibilityMask: 4294967295\n";
  out.close();

  bool rejected = false;
  try {
    (void)LX_infra::scene_io::loadSceneDocument(path);
  } catch (const std::exception &e) {
    rejected = std::string(e.what()).find("scene.offlineRender.profiles") !=
               std::string::npos;
  }
  EXPECT(rejected, "old offlineRender.profiles should be rejected");
}
```

Add both function calls in `main()`.

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target test_scene_document
./build/src/test/test_scene_document
```

Expected: compile failure for missing `hasRenderProfileDocument()` or runtime failure from unsupported schema.

- [ ] **Step 3: Update `SceneDocument` API**

In `src/infra/scene_io/scene_document.hpp`, replace offline profile accessors:

```cpp
bool hasRenderProfileDocument() const;
const LX_core::offline::RenderProfileDocument &renderProfileDocument() const;
void setRenderProfileDocument(
    LX_core::offline::RenderProfileDocument profiles);
```

Remove or stop using:

```cpp
bool hasOfflineRenderProfiles() const;
const LX_core::offline::OfflineRenderProfiles &offlineRenderProfiles() const;
void setOfflineRenderProfiles(...);
```

- [ ] **Step 4: Parse new YAML fields**

In `src/infra/scene_io/scene_document.cpp`, add helpers named:

```cpp
[[nodiscard]] LX_core::offline::OutputCameraOverrides
loadOutputCameraOverrides(const YAML::Node &node,
                          const std::string &profileName);

[[nodiscard]] LX_core::offline::OutputProfile
loadOutputProfile(const YAML::Node &node, const std::string &name);

[[nodiscard]] LX_core::offline::OfflineRenderSettings
loadOfflineRenderSettings(const YAML::Node &node);

[[nodiscard]] LX_core::offline::RenderProfileDocument
loadRenderProfileDocument(const YAML::Node &sceneNode);
```

The fail-fast old-schema check must be:

```cpp
if (const YAML::Node offlineRenderNode = sceneNode["offlineRender"];
    offlineRenderNode && offlineRenderNode["profiles"]) {
  throw std::runtime_error(
      "scene.offlineRender.profiles is no longer supported; use "
      "scene.outputProfiles plus scene.offlineRender");
}
```

- [ ] **Step 5: Save new YAML fields**

Replace `saveOfflineRenderProfiles()` with:

```cpp
void saveRenderProfileDocument(
    YAML::Emitter &out,
    const LX_core::offline::RenderProfileDocument &document);
```

It must emit:

```yaml
defaultOutputProfile: preview
outputProfiles:
  preview: ...
offlineRender:
  integrator: primary-ray
  samples: 1
  maxBounce: 1
  seed: 1
  profile: preview
  shadows: true
```

- [ ] **Step 6: Migrate scene assets**

Update `assets/scenes/ibl_metal_sphere.scene.yaml` from:

```yaml
offlineRender:
  defaultProfile: preview
  profiles:
    preview:
      backend: vulkan-compute
      integrator: primary-ray
      width: 512
```

to:

```yaml
defaultOutputProfile: preview
outputProfiles:
  preview:
    camera: /game_cam
    width: 512
    height: 512
    outputFormat: exr-png
    outDir: artifacts/offline/preview
  mvp:
    camera: /game_cam
    width: 1024
    height: 576
    outputFormat: exr-png
    outDir: artifacts/offline/mvp
  reference:
    camera: /game_cam
    width: 1920
    height: 1080
    outputFormat: exr-png
    outDir: artifacts/offline/reference
offlineRender:
  integrator: primary-ray
  samples: 1
  maxBounce: 1
  seed: 1
  profile: preview
  shadows: true
```

Apply equivalent migration to every scene found by:

```bash
rg -n "offlineRender:|profiles:|maxDepth|backend:" assets/scenes
```

- [ ] **Step 7: Run schema tests**

Run:

```bash
cmake --build build --target test_scene_document test_output_profile_resolution
./build/src/test/test_scene_document
./build/src/test/test_output_profile_resolution
```

Expected: both tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/infra/scene_io/scene_document.hpp \
  src/infra/scene_io/scene_document.cpp \
  src/test/integration/test_scene_document.cpp \
  assets/scenes
git commit -m "migrate scenes to output profiles"
```

---

## Task 3: Update Offline CLI and Offline Render Job

**Files:**
- Modify: `src/core/offline/offline_scene.hpp`
- Modify: `src/tools/lxe_offline_render/offline_render_cli.hpp`
- Modify: `src/tools/lxe_offline_render/offline_render_cli.cpp`
- Modify: `src/tools/lxe_offline_render/main.cpp`
- Modify: `src/test/integration/test_offline_render_cli.cpp`
- Modify: `src/backend/vulkan/offline/vulkan_offline_renderer.cpp`
- Modify: `assets/shaders/glsl/offline_primary_ray.comp`

- [ ] **Step 1: Update failing CLI test**

In `src/test/integration/test_offline_render_cli.cpp`, change the argument vector to use `--max-bounce`:

```cpp
const std::vector<std::string> args{
    "--scene",      "assets/scenes/ibl_metal_sphere.scene.yaml",
    "--profile",    "mvp",
    "--width",      "64",
    "--height",     "36",
    "--samples",    "2",
    "--max-bounce", "3",
    "--seed",       "11",
    "--out",        "artifacts/offline/test"};
```

Add assertions:

```cpp
EXPECT(options.overrides.maxBounce == 3u, "maxBounce should parse");
EXPECT(!options.cameraPath.has_value(), "camera comes from output profile");
```

Add rejection test:

```cpp
void testCliRejectsMaxDepth() {
  bool rejected = false;
  try {
    (void)LX_tools::offline_render::parseOfflineRenderCliArguments(
        {"--scene", "assets/scenes/ibl_metal_sphere.scene.yaml",
         "--max-depth", "4"});
  } catch (const std::exception &e) {
    rejected = std::string(e.what()).find("--max-depth") != std::string::npos;
  }
  EXPECT(rejected, "--max-depth should be rejected");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target test_offline_render_cli
./build/src/test/test_offline_render_cli
```

Expected: compile failure or test failure because `maxBounce` is not parsed.

- [ ] **Step 3: Update CLI option structure**

In `src/tools/lxe_offline_render/offline_render_cli.hpp`, make the options:

```cpp
struct OfflineRenderCliOptions final {
  std::string scenePath;
  LX_core::offline::RenderProfileCliOverrides overrides;
};
```

Do not keep `cameraPath`; camera comes from selected `OutputProfile`.

- [ ] **Step 4: Parse `--max-bounce` and reject `--max-depth`**

In `src/tools/lxe_offline_render/offline_render_cli.cpp`, update parsing:

```cpp
} else if (arg == "--max-bounce") {
  options.overrides.maxBounce =
      parsePositiveU32(requireValue(args, i, arg), arg);
} else if (arg == "--max-depth") {
  throw std::runtime_error(
      "--max-depth was removed; use --max-bounce instead");
}
```

Remove `--camera` parsing from offline CLI. Do not add a replacement camera override option in this requirement; camera selection is owned by `--profile`.

- [ ] **Step 5: Change `OfflineRenderJob` fields**

In `src/core/offline/offline_scene.hpp`, replace:

```cpp
OfflineRenderProfile profile;
std::string cameraPath;
```

with:

```cpp
OutputProfile output;
OfflineRenderSettings offline;
std::string profileName;
```

- [ ] **Step 6: Update `lxe_offline_render/main.cpp`**

Use the scene document resolver:

```cpp
const auto document = LX_infra::scene_io::loadSceneDocument(args.scenePath);
const auto renderProfiles =
    document.hasRenderProfileDocument()
        ? document.renderProfileDocument()
        : LX_core::offline::makeDefaultRenderProfileDocument();
const auto resolved =
    LX_core::offline::resolveRenderProfileDocument(renderProfiles,
                                                   args.overrides);

LX_infra::offline::OfflineAssetResolver resolver(args.scenePath);
LX_infra::offline::OfflineSceneCompiler compiler(resolver);
auto scene = compiler.compile(document, resolved.output.cameraPath);

LX_core::offline::OfflineRenderJob job;
job.scene = std::move(scene);
job.output = resolved.output;
job.offline = resolved.offline;
job.profileName = resolved.profileName;
job.outputPath = resolved.outputPath.value_or("");
```

- [ ] **Step 7: Update offline renderer and shader names**

In `src/backend/vulkan/offline/vulkan_offline_renderer.cpp`, replace reads:

```cpp
job.profile.width
job.profile.height
job.profile.samples
```

with:

```cpp
job.output.width
job.output.height
job.offline.samples
```

Rename C++ `OfflineSceneParams::maxDepth` if present to `maxBounce`. In `assets/shaders/glsl/offline_primary_ray.comp`, rename corresponding uniform/SSBO field to `maxBounce`.

Add shadow toggle to params:

```cpp
u32 shadowsEnabled = job.offline.shadows ? 1u : 0u;
```

In shader shadow code, gate shadow tracing:

```glsl
float shadow = params.shadowsEnabled != 0u
    ? traceShadow(hitPoint + normal * 0.001, lightDir)
    : 1.0;
```

- [ ] **Step 8: Update offline image writer call sites**

In `src/tools/lxe_offline_render/main.cpp`, use:

```cpp
outputRequest.profileName = resolved.profileName;
```

and set output request job fields from `job.output` / `job.offline`.

- [ ] **Step 9: Run offline tests and CLI smoke**

Run:

```bash
cmake --build build --target CompileShaders test_offline_render_cli test_offline_scene_compiler test_offline_gpu_scene test_vulkan_offline_renderer lxe_offline_render
./build/src/test/test_offline_render_cli
./build/src/test/test_offline_scene_compiler
./build/src/test/test_offline_gpu_scene
./build/src/test/test_vulkan_offline_renderer
./build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/scenes/ibl_metal_sphere.scene.yaml \
  --profile mvp \
  --width 32 \
  --height 32 \
  --samples 1 \
  --max-bounce 1 \
  --seed 17 \
  --out /tmp/lxengine_068_offline.png
```

Expected: all tests pass and CLI prints `lxe_offline_render completed`.

- [ ] **Step 10: Commit**

```bash
git add src/core/offline/offline_scene.hpp \
  src/core/offline/offline_render_profile.* \
  src/tools/lxe_offline_render \
  src/backend/vulkan/offline/vulkan_offline_renderer.cpp \
  assets/shaders/glsl/offline_primary_ray.comp \
  assets/shaders/glsl/offline_primary_ray.comp.spv \
  src/test/integration/test_offline_render_cli.cpp
git commit -m "migrate offline cli to output profiles"
```

---

## Task 4: Extract Shared Tone Mapping and Image I/O

**Files:**
- Create: `src/core/image/tone_mapping.hpp`
- Create: `src/core/image/tone_mapping.cpp`
- Create: `src/infra/image/rgba_image_io.hpp`
- Create: `src/infra/image/rgba_image_io.cpp`
- Modify: `src/infra/offline/offline_image_writer.hpp`
- Modify: `src/infra/offline/offline_image_writer.cpp`
- Create: `src/test/integration/test_tone_mapping.cpp`
- Modify: `src/test/integration/test_offline_image_writer.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Write tone mapping tests**

Create `src/test/integration/test_tone_mapping.cpp`:

```cpp
#include "core/image/tone_mapping.hpp"

#include <cmath>
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

void testAcesSamples() {
  LX_core::image::ToneMappingSettings settings;
  settings.mode = LX_core::image::ToneMappingMode::Aces;
  settings.exposure = 1.0f;
  settings.gamma = 2.2f;

  EXPECT(LX_core::image::toneMapLinearToSrgb8(0.0f, settings) == 0,
         "black stays black");
  const auto mid = LX_core::image::toneMapLinearToSrgb8(0.18f, settings);
  EXPECT(mid > 110 && mid < 140, "middle gray maps to stable preview range");
  const auto white = LX_core::image::toneMapLinearToSrgb8(1.0f, settings);
  EXPECT(white > 220 && white < 240, "white sample matches previous ACES");
}

void testNonFiniteBecomesBlack() {
  LX_core::image::ToneMappingSettings settings;
  const auto value =
      LX_core::image::toneMapLinearToSrgb8(
          std::numeric_limits<float>::quiet_NaN(), settings);
  EXPECT(value == 0, "NaN is clamped to black");
}
} // namespace

int main() {
  testAcesSamples();
  testNonFiniteBecomesBlack();
  if (failures != 0) {
    std::cerr << "test_tone_mapping failed with " << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "test_tone_mapping passed\n";
  return 0;
}
```

- [ ] **Step 2: Register test**

Add `test_tone_mapping` to `TEST_INTEGRATION_EXE_LIST` in `src/test/CMakeLists.txt`.

- [ ] **Step 3: Run test to verify it fails**

Run:

```bash
cmake --build build --target test_tone_mapping
```

Expected: compile failure because `core/image/tone_mapping.hpp` does not exist.

- [ ] **Step 4: Add shared tone mapping helper**

Create `src/core/image/tone_mapping.hpp`:

```cpp
#pragma once

#include "core/platform/types.hpp"

namespace LX_core::image {

enum class ToneMappingMode { Aces, Reinhard };

struct ToneMappingSettings final {
  float exposure = 1.0f;
  float gamma = 2.2f;
  ToneMappingMode mode = ToneMappingMode::Aces;
};

[[nodiscard]] float toneMapLinear(float value,
                                  const ToneMappingSettings &settings);
[[nodiscard]] u8 toneMapLinearToSrgb8(float value,
                                      const ToneMappingSettings &settings);
[[nodiscard]] const char *toneMappingModeName(ToneMappingMode mode);

} // namespace LX_core::image
```

Create `src/core/image/tone_mapping.cpp` using the current ACES/Reinhard math from `offline_image_writer.cpp`.

- [ ] **Step 5: Add reusable image I/O**

Create `src/infra/image/rgba_image_io.hpp`:

```cpp
#pragma once

#include "core/image/tone_mapping.hpp"
#include "core/offline/offline_scene.hpp"

#include <filesystem>

namespace LX_infra::image {

void writeRgba32fExr(const std::filesystem::path &path,
                     const LX_core::offline::OfflineReadbackImage &image);
void writeToneMappedPng(const std::filesystem::path &path,
                        const LX_core::offline::OfflineReadbackImage &image,
                        const LX_core::image::ToneMappingSettings &settings);
void writeRawRgba32f(const std::filesystem::path &path,
                     const LX_core::offline::OfflineReadbackImage &image);

} // namespace LX_infra::image
```

Create `src/infra/image/rgba_image_io.cpp` by moving `writeExr`, `writePng`, and `writeRaw` logic out of `offline_image_writer.cpp`.

- [ ] **Step 6: Update offline writer**

In `src/infra/offline/offline_image_writer.hpp`, replace `OfflineToneMappingSettings` with alias:

```cpp
using OfflineToneMappingSettings = LX_core::image::ToneMappingSettings;
```

In `src/infra/offline/offline_image_writer.cpp`, replace direct writer calls with:

```cpp
LX_infra::image::writeRgba32fExr(result.exrPath, request.image);
LX_infra::image::writeToneMappedPng(result.pngPath, request.image,
                                    request.toneMapping);
LX_infra::image::writeRawRgba32f(result.rawPath, request.image);
```

Metadata must read `job.offline.maxBounce`.

- [ ] **Step 7: Run tone mapping and image writer tests**

Run:

```bash
cmake --build build --target test_tone_mapping test_offline_image_writer
./build/src/test/test_tone_mapping
./build/src/test/test_offline_image_writer
```

Expected: both tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/core/image src/infra/image src/infra/offline/offline_image_writer.* \
  src/test/integration/test_tone_mapping.cpp \
  src/test/integration/test_offline_image_writer.cpp \
  src/test/CMakeLists.txt
git commit -m "extract shared tone mapping image output"
```

---

## Task 5: Add Shader Tone Mapping Include

**Files:**
- Create: `assets/shaders/glsl/common/tone_mapping.glsl`
- Modify: realtime post-process shader files under `assets/shaders/glsl/`
- Modify: `assets/shaders/CMakeLists.txt` if includes require explicit dependency tracking

- [ ] **Step 1: Create GLSL include**

Create `assets/shaders/glsl/common/tone_mapping.glsl`:

```glsl
#ifndef LX_TONE_MAPPING_GLSL
#define LX_TONE_MAPPING_GLSL

vec3 lxToneMapAces(vec3 color, float exposure) {
  vec3 exposed = max(color * max(exposure, 0.0), vec3(0.0));
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((exposed * (a * exposed + b)) /
                   (exposed * (c * exposed + d) + e),
               vec3(0.0), vec3(1.0));
}

vec3 lxLinearToSrgbGamma(vec3 color, float gamma) {
  return pow(clamp(color, vec3(0.0), vec3(1.0)),
             vec3(1.0 / max(gamma, 0.0001)));
}

vec3 lxToneMapLinearToSrgb(vec3 color, float exposure, float gamma) {
  return lxLinearToSrgbGamma(lxToneMapAces(color, exposure), gamma);
}

#endif
```

- [ ] **Step 2: Include it in realtime post-process shader**

Find the post-process shader:

```bash
rg -n "toneMapping|ACES|Reinhard|exposure|gamma" assets/shaders/glsl
```

Replace inline math with:

```glsl
#include "common/tone_mapping.glsl"
```

and call:

```glsl
vec3 mapped = lxToneMapLinearToSrgb(hdrColor.rgb, exposure, gamma);
```

- [ ] **Step 3: Compile shaders**

Run:

```bash
cmake --build build --target CompileShaders
```

Expected: glslc compiles modified shaders and `.spv` outputs update.

- [ ] **Step 4: Commit**

```bash
git add assets/shaders/glsl/common/tone_mapping.glsl \
  assets/shaders/glsl/*.frag assets/shaders/glsl/*.comp assets/shaders/glsl/*.spv \
  assets/shaders/CMakeLists.txt
git commit -m "share shader tone mapping helper"
```

---

## Task 6: Split lxe_editor Command Registration

**Files:**
- Create: `src/demos/lxe_editor/commands/*.hpp`
- Create: `src/demos/lxe_editor/commands/*.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_commands.hpp`
- Modify: `src/demos/lxe_editor/lxe_editor_commands.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Record current command regression tests**

Run existing command tests before moving code:

```bash
cmake --build build --target test_command_bus test_lxe_editor_session test_lxe_editor_api_service
./build/src/test/test_command_bus
./build/src/test/test_lxe_editor_session
./build/src/test/test_lxe_editor_api_service
```

Expected: all pass before refactor.

- [ ] **Step 2: Create command helper header**

Create `src/demos/lxe_editor/commands/lxe_editor_command_helpers.hpp`:

```cpp
#pragma once

#include "core/editor/command_bus.hpp"

#include <string>
#include <string_view>

namespace LX_demo::lxe_editor {

[[nodiscard]] LX_core::CommandResult makeEditorCommandOk(
    std::string message, std::string structured = {});
[[nodiscard]] LX_core::CommandResult makeEditorCommandError(
    std::string message);
[[nodiscard]] std::string editorCommandJsonEscape(std::string_view text);

} // namespace LX_demo::lxe_editor
```

Create `lxe_editor_command_helpers.cpp` by moving `jsonEscape`, `makeCommandOk`, and `makeCommandError` logic from `lxe_editor_commands.cpp`.

- [ ] **Step 3: Create registration header**

Create `src/demos/lxe_editor/commands/register_lxe_editor_commands.hpp`:

```cpp
#pragma once

#include "demos/lxe_editor/lxe_editor_commands.hpp"

namespace LX_demo::lxe_editor {

void registerProjectCommands(LX_core::CommandBus &bus,
                             const LxeEditorCommandContext &context);
void registerSceneProjectCommands(LX_core::CommandBus &bus,
                                  const LxeEditorCommandContext &context);
void registerRecordingCommands(LX_core::CommandBus &bus,
                               const LxeEditorCommandContext &context);
void registerDisplayCommands(LX_core::CommandBus &bus,
                             const LxeEditorCommandContext &context);
void registerRenderDebugCommands(LX_core::CommandBus &bus,
                                 const LxeEditorCommandContext &context);
void registerRealtimeRenderCommands(LX_core::CommandBus &bus,
                                    const LxeEditorCommandContext &context);

} // namespace LX_demo::lxe_editor
```

- [ ] **Step 4: Move one command group at a time**

Move `project` command registration from `lxe_editor_commands.cpp` into `project_commands.cpp`, preserving handler logic. After each group, run:

```bash
cmake --build build --target test_lxe_editor_session
./build/src/test/test_lxe_editor_session
```

Repeat for:

```text
scene project commands -> scene_project_commands.cpp
recording commands -> recording_commands.cpp
display commands -> display_commands.cpp
render debug commands -> render_debug_commands.cpp
```

- [ ] **Step 5: Keep public entrypoint stable**

In `src/demos/lxe_editor/lxe_editor_commands.cpp`, keep:

```cpp
void registerLxeEditorCommands(LX_core::CommandBus &bus,
                               LxeEditorCommandContext context) {
  registerProjectCommands(bus, context);
  registerSceneProjectCommands(bus, context);
  registerRecordingCommands(bus, context);
  registerDisplayCommands(bus, context);
  registerRenderDebugCommands(bus, context);
  registerRealtimeRenderCommands(bus, context);
}
```

- [ ] **Step 6: Update test target sources**

In `src/test/CMakeLists.txt`, replace every test-specific source entry:

```cmake
${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_commands.cpp
```

with:

```cmake
${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_commands.cpp
${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/lxe_editor_command_helpers.cpp
${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/project_commands.cpp
${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/scene_project_commands.cpp
${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/recording_commands.cpp
${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/display_commands.cpp
${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/render_debug_commands.cpp
${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/realtime_render_commands.cpp
```

- [ ] **Step 7: Run command regression tests**

Run:

```bash
cmake --build build --target test_command_bus test_lxe_editor_session test_lxe_editor_api_service test_lxe_editor_api_server
./build/src/test/test_command_bus
./build/src/test/test_lxe_editor_session
./build/src/test/test_lxe_editor_api_service
./build/src/test/test_lxe_editor_api_server
```

Expected: all pass.

- [ ] **Step 8: Commit**

```bash
git add src/demos/lxe_editor/lxe_editor_commands.* \
  src/demos/lxe_editor/commands \
  src/test/CMakeLists.txt
git commit -m "split lxe editor command registration"
```

---

## Task 7: Add Realtime Render Command Surface

**Files:**
- Modify: `src/demos/lxe_editor/lxe_editor_commands.hpp`
- Create/Modify: `src/demos/lxe_editor/commands/realtime_render_commands.cpp`
- Modify: `src/demos/lxe_editor/editor_session.hpp`
- Modify: `src/demos/lxe_editor/editor_session.cpp`
- Create: `src/test/integration/test_realtime_render_profile_commands.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Add command test with fake hooks**

Create `src/test/integration/test_realtime_render_profile_commands.cpp`:

```cpp
#include "core/editor/command_bus.hpp"
#include "demos/lxe_editor/lxe_editor_commands.hpp"

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

void testRealtimeRenderLsAndRun() {
  LX_core::CommandBus bus;
  LX_demo::lxe_editor::LxeEditorCommandContext context;
  context.realtimeRenderListJson = []() {
    return std::string(
        "{\"profiles\":[{\"name\":\"preview\",\"camera\":\"/game_cam\","
        "\"width\":64,\"height\":36,\"outputFormat\":\"exr-png\","
        "\"outDir\":\"artifacts\"}]}");
  };
  context.realtimeRenderRun =
      [](std::string_view profile) -> LX_core::CommandResult {
    if (profile != "preview") {
      return LX_core::CommandResult{false, "bad profile", {}, {}};
    }
    return LX_core::CommandResult{
        true,
        "realtime profile generated",
        "{\"linearExr\":\"/tmp/linear.exr\","
        "\"cpuSrgbPng\":\"/tmp/cpu.png\","
        "\"pipelineSrgbPng\":\"/tmp/pipeline.png\"}",
        {}};
  };

  LX_demo::lxe_editor::registerLxeEditorCommands(bus, context);

  const auto list = bus.dispatch("realtime-render ls");
  EXPECT(list.ok, "ls should succeed");
  EXPECT(list.structured.find("\"profiles\"") != std::string::npos,
         "ls returns json");

  const auto run = bus.dispatch("realtime-render run preview");
  EXPECT(run.ok, "run should succeed");
  EXPECT(run.structured.find("linear.exr") != std::string::npos,
         "run returns output paths");
}
} // namespace

int main() {
  testRealtimeRenderLsAndRun();
  if (failures != 0) {
    std::cerr << "test_realtime_render_profile_commands failed with "
              << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "test_realtime_render_profile_commands passed\n";
  return 0;
}
```

- [ ] **Step 2: Register test target**

Add `test_realtime_render_profile_commands` to `TEST_INTEGRATION_EXE_LIST`.

Add target sources:

```cmake
if(TARGET test_realtime_render_profile_commands)
  target_sources(test_realtime_render_profile_commands PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/lxe_editor_command_helpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/project_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/scene_project_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/recording_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/display_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/render_debug_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/commands/realtime_render_commands.cpp
  )
  target_include_directories(test_realtime_render_profile_commands PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
  )
  target_link_libraries(test_realtime_render_profile_commands PRIVATE imgui yaml-cpp::yaml-cpp)
endif()
```

- [ ] **Step 3: Run test to verify it fails**

Run:

```bash
cmake --build build --target test_realtime_render_profile_commands
```

Expected: compile failure because context hooks do not exist.

- [ ] **Step 4: Add hooks to command context**

In `src/demos/lxe_editor/lxe_editor_commands.hpp`, add:

```cpp
using RealtimeRenderListJsonFn = std::function<std::string()>;
using RealtimeRenderRunFn =
    std::function<LX_core::CommandResult(std::string_view profileName)>;
```

and fields:

```cpp
RealtimeRenderListJsonFn realtimeRenderListJson;
RealtimeRenderRunFn realtimeRenderRun;
```

- [ ] **Step 5: Implement command registration**

In `src/demos/lxe_editor/commands/realtime_render_commands.cpp`:

```cpp
void registerRealtimeRenderCommands(LX_core::CommandBus &bus,
                                    const LxeEditorCommandContext &context) {
  bus.registerHandler(
      "realtime-render", "realtime-render ls | realtime-render run <profile>",
      [context](std::vector<std::string> args) {
        if (args.size() == 1 && args[0] == "ls") {
          if (!context.realtimeRenderListJson) {
            return makeEditorCommandError("realtime-render ls unavailable");
          }
          const std::string json = context.realtimeRenderListJson();
          return makeEditorCommandOk("listed realtime render profiles", json);
        }
        if (args.size() == 2 && args[0] == "run") {
          if (!context.realtimeRenderRun) {
            return makeEditorCommandError("realtime-render run unavailable");
          }
          return context.realtimeRenderRun(args[1]);
        }
        return makeEditorCommandError(
            "usage: realtime-render ls | realtime-render run <profile>");
      });
}
```

- [ ] **Step 6: Run command test**

Run:

```bash
cmake --build build --target test_realtime_render_profile_commands
./build/src/test/test_realtime_render_profile_commands
```

Expected: test passes.

- [ ] **Step 7: Commit**

```bash
git add src/demos/lxe_editor/lxe_editor_commands.hpp \
  src/demos/lxe_editor/commands/realtime_render_commands.cpp \
  src/test/integration/test_realtime_render_profile_commands.cpp \
  src/test/CMakeLists.txt
git commit -m "add realtime render profile commands"
```

---

## Task 8: Implement Realtime Profile Output Model and Writer

**Files:**
- Create: `src/demos/lxe_editor/realtime_render_profile.hpp`
- Create: `src/demos/lxe_editor/realtime_render_profile.cpp`
- Modify: `src/demos/lxe_editor/editor_session.hpp`
- Modify: `src/demos/lxe_editor/editor_session.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`

- [ ] **Step 1: Add output model**

Create `src/demos/lxe_editor/realtime_render_profile.hpp`:

```cpp
#pragma once

#include "core/offline/offline_render_profile.hpp"
#include "core/offline/offline_scene.hpp"

#include <filesystem>
#include <string>

namespace LX_demo::lxe_editor {

struct RealtimeProfileOutputRequest final {
  std::filesystem::path scenePath;
  std::string sceneName;
  std::string profileName;
  LX_core::offline::OutputProfile output;
  std::filesystem::path outputBasePath;
};

struct RealtimeProfileOutputResult final {
  std::filesystem::path linearExrPath;
  std::filesystem::path cpuSrgbPngPath;
  std::filesystem::path pipelineSrgbPngPath;
  std::filesystem::path metadataPath;
  u32 width = 0;
  u32 height = 0;
};

[[nodiscard]] std::filesystem::path makeRealtimeProfileOutputBasePath(
    const RealtimeProfileOutputRequest &request);

[[nodiscard]] std::string realtimeProfileOutputResultJson(
    const RealtimeProfileOutputResult &result);

} // namespace LX_demo::lxe_editor
```

- [ ] **Step 2: Implement path and JSON helpers**

Create `src/demos/lxe_editor/realtime_render_profile.cpp`:

```cpp
std::filesystem::path makeRealtimeProfileOutputBasePath(
    const RealtimeProfileOutputRequest &request) {
  const std::string sceneName =
      request.sceneName.empty() ? "scene" : request.sceneName;
  const std::filesystem::path outDir =
      request.output.outDir.empty() ? std::filesystem::path("artifacts")
                                    : request.output.outDir;
  return outDir / "realtime" / sceneName / request.profileName / "render";
}

std::string realtimeProfileOutputResultJson(
    const RealtimeProfileOutputResult &result) {
  std::ostringstream oss;
  oss << "{\"linearExr\":\"" << editorCommandJsonEscape(result.linearExrPath.string())
      << "\",\"cpuSrgbPng\":\"" << editorCommandJsonEscape(result.cpuSrgbPngPath.string())
      << "\",\"pipelineSrgbPng\":\""
      << editorCommandJsonEscape(result.pipelineSrgbPngPath.string())
      << "\",\"metadata\":\"" << editorCommandJsonEscape(result.metadataPath.string())
      << "\",\"width\":" << result.width
      << ",\"height\":" << result.height << "}";
  return oss.str();
}
```

- [ ] **Step 3: Add session hook result type**

In `src/demos/lxe_editor/editor_session.hpp`, add:

```cpp
struct RealtimeRenderProfileResult final {
  std::filesystem::path linearExrPath;
  std::filesystem::path cpuSrgbPngPath;
  std::filesystem::path pipelineSrgbPngPath;
  std::filesystem::path metadataPath;
  u32 width = 0;
  u32 height = 0;
};

struct RealtimeRenderProfileHooks final {
  std::function<RealtimeRenderProfileResult(
      const LX_core::Scene &scene,
      const LX_core::offline::OutputProfile &profile,
      const std::filesystem::path &basePath)>
      generate;
};
```

Add `RealtimeRenderProfileHooks` as an `initialize()` parameter and session member.

- [ ] **Step 4: Wire command hooks from session**

In `editor_session.cpp`, set `LxeEditorCommandContext` fields:

```cpp
.realtimeRenderListJson = [this]() { return realtimeRenderProfilesJson(); },
.realtimeRenderRun = [this](std::string_view profileName) {
  return runRealtimeRenderProfile(profileName);
},
```

Add private methods:

```cpp
[[nodiscard]] std::string realtimeRenderProfilesJson() const;
[[nodiscard]] LX_core::CommandResult
runRealtimeRenderProfile(std::string_view profileName);
```

`realtimeRenderProfilesJson()` must read `activeScenePath()`, load the scene document, and emit profiles.

`runRealtimeRenderProfile()` must resolve profile, build base path, call `m_realtimeRenderProfileHooks.generate`, and return structured JSON.

- [ ] **Step 5: Add fake hook test in session test**

In `src/test/integration/test_lxe_editor_session.cpp`, add a test that initializes session with fake hook returning paths and dispatches:

```cpp
const auto result = session.commandBus().dispatch("realtime-render run preview");
EXPECT(result.ok, "realtime render run should succeed with fake hook");
EXPECT(result.structured.find("linearExr") != std::string::npos,
       "structured output contains linearExr");
```

- [ ] **Step 6: Run session tests**

Run:

```bash
cmake --build build --target test_lxe_editor_session test_realtime_render_profile_commands
./build/src/test/test_lxe_editor_session
./build/src/test/test_realtime_render_profile_commands
```

Expected: both pass.

- [ ] **Step 7: Commit**

```bash
git add src/demos/lxe_editor/realtime_render_profile.* \
  src/demos/lxe_editor/editor_session.* \
  src/demos/lxe_editor/main.cpp \
  src/test/integration/test_lxe_editor_session.cpp
git commit -m "wire realtime profile output hooks"
```

---

## Task 9: Implement Vulkan Realtime Profile Generation

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.hpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/backend/vulkan/vulkan_renderer.hpp`
- Modify: `src/backend/vulkan/vulkan_renderer.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`

- [ ] **Step 1: Add renderer result type**

In `src/backend/vulkan/vulkan_renderer_types.hpp`, add:

```cpp
struct VulkanRealtimeProfileOutputResult final {
  LX_core::offline::OfflineReadbackImage linear;
  LX_core::offline::OfflineReadbackImage pipelineSrgbLinearized;
  u32 width = 0;
  u32 height = 0;
};
```

Use `OfflineReadbackImage` as a generic RGBA32F container for now.

- [ ] **Step 2: Add renderer method**

In `VulkanRealtimeRenderer` and forwarding `VulkanRenderer`, add:

```cpp
[[nodiscard]] VulkanRealtimeProfileOutputResult renderOutputProfile(
    LX_core::SceneSharedPtr scene,
    const LX_core::offline::OutputProfile &profile);
```

- [ ] **Step 3: Implement using existing frame graph dump path**

In `VulkanRealtimeRenderer::Impl`, implement `renderOutputProfile` by:

1. Finding profile camera in the scene.
2. Saving the original camera component values.
3. Applying `profile.cameraOverrides`.
4. Setting camera render target to an offscreen target with `profile.width` / `profile.height`.
5. Calling `initScene(scene)`, `uploadData()`, and `draw()`.
6. Reading `sceneHdrColor` as linear RGBA16F/RGBA32F using existing attachment dump logic.
7. Reading final post-process target as pipeline sRGB output.
8. Restoring camera component values and target.

The method must use RAII restoration:

```cpp
struct CameraRestore final {
  LX_core::CameraComponent &camera;
  LX_core::CameraComponentState state;
  ~CameraRestore() { camera.applyState(state); }
};
```

If there is no `CameraComponentState` type, create a local struct with fields copied from `CameraComponent`.

- [ ] **Step 4: Write realtime output files from main hook**

In `src/demos/lxe_editor/main.cpp`, populate `RealtimeRenderProfileHooks.generate`:

```cpp
.generate =
    [vulkanRenderer](const LX_core::Scene &scene,
                     const LX_core::offline::OutputProfile &profile,
                     const std::filesystem::path &basePath) {
      auto result = vulkanRenderer->renderOutputProfile(scene.sharedFromThis(),
                                                        profile);
      LX_demo::lxe_editor::RealtimeRenderProfileResult out;
      out.width = result.width;
      out.height = result.height;
      out.linearExrPath = basePath;
      out.linearExrPath.replace_extension(".linear.exr");
      out.cpuSrgbPngPath = basePath;
      out.cpuSrgbPngPath.replace_extension(".cpu_srgb.png");
      out.pipelineSrgbPngPath = basePath;
      out.pipelineSrgbPngPath.replace_extension(".pipeline_srgb.png");
      LX_infra::image::writeRgba32fExr(out.linearExrPath, result.linear);
      LX_infra::image::writeToneMappedPng(
          out.cpuSrgbPngPath, result.linear,
          LX_core::image::ToneMappingSettings{});
      LX_infra::image::writeToneMappedPng(
          out.pipelineSrgbPngPath, result.pipelineSrgbLinearized,
          LX_core::image::ToneMappingSettings{});
      return out;
    }
```

If `Scene::sharedFromThis()` does not exist, use the session-owned `SceneSharedPtr` directly in the hook signature instead of a reference.

- [ ] **Step 5: Run Vulkan smoke**

Run:

```bash
cmake --build build --target lxe_editor test_vulkan_frame_graph test_vulkan_offscreen_submit_memory_probe
./build/src/test/test_vulkan_frame_graph
./build/src/test/test_vulkan_offscreen_submit_memory_probe
```

Expected: tests pass. If a test requires video device, run under Xvfb:

```bash
xvfb-run -a ./build/src/test/test_vulkan_frame_graph
```

- [ ] **Step 6: Commit**

```bash
git add src/backend/vulkan/vulkan_realtime_renderer.* \
  src/backend/vulkan/vulkan_renderer.* \
  src/backend/vulkan/vulkan_renderer_types.hpp \
  src/demos/lxe_editor/main.cpp
git commit -m "generate realtime profile outputs"
```

---

## Task 10: Add Local `lxe_realtime_render` CLI

**Files:**
- Create: `src/tools/lxe_realtime_render/CMakeLists.txt`
- Create: `src/tools/lxe_realtime_render/main.cpp`
- Modify: `src/tools/CMakeLists.txt`
- Create: `src/test/integration/test_realtime_render_cli.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Add CLI parser smoke test**

Create `src/test/integration/test_realtime_render_cli.cpp`:

```cpp
#include <cstdlib>
#include <filesystem>
#include <iostream>

int main() {
  const std::filesystem::path exe =
      std::filesystem::current_path() / "src/tools/lxe_realtime_render/lxe_realtime_render";
  const std::string command =
      exe.string() + " --help > /tmp/lxe_realtime_render_help.txt";
  const int code = std::system(command.c_str());
  if (code != 0) {
    std::cerr << "lxe_realtime_render --help failed\n";
    return 1;
  }
  std::cout << "test_realtime_render_cli passed\n";
  return 0;
}
```

Register it in `src/test/CMakeLists.txt` and make it depend on `lxe_realtime_render`:

```cmake
if(TARGET test_realtime_render_cli)
  add_dependencies(test_realtime_render_cli lxe_realtime_render)
endif()
```

- [ ] **Step 2: Create tool target**

Create `src/tools/lxe_realtime_render/CMakeLists.txt`:

```cmake
add_executable(lxe_realtime_render main.cpp)
target_link_libraries(lxe_realtime_render
  PRIVATE
  ${CORE_LIB}
  ${INFRA_LIB}
  ${GRAPHICS_LIB}
)
target_include_directories(lxe_realtime_render PUBLIC ${CMAKE_SOURCE_DIR}/src)
add_dependencies(lxe_realtime_render CompileShaders)
```

Modify `src/tools/CMakeLists.txt`:

```cmake
add_subdirectory(lxe_offline_render)
add_subdirectory(lxe_realtime_render)
```

- [ ] **Step 3: Implement CLI skeleton**

Create `src/tools/lxe_realtime_render/main.cpp`:

```cpp
#include "core/utils/env.hpp"
#include "infra/build_info/build_info.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  try {
    LX_core::expSetEnvVK();
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
      std::cout << "usage: lxe_realtime_render --scene SCENE --profile NAME\n";
      return 0;
    }
    if (args.size() != 4 || args[0] != "--scene" || args[2] != "--profile") {
      throw std::runtime_error(
          "usage: lxe_realtime_render --scene SCENE --profile NAME");
    }

    const std::string scenePath = args[1];
    const std::string profileName = args[3];
    (void)scenePath;
    (void)profileName;

    throw std::runtime_error(
        "lxe_realtime_render render execution is not wired yet");
  } catch (const std::exception &e) {
    std::cerr << "lxe_realtime_render error: " << e.what() << "\n";
    return 1;
  }
}
```

- [ ] **Step 4: Run help test**

Run:

```bash
cmake --build build --target lxe_realtime_render test_realtime_render_cli
./build/src/test/test_realtime_render_cli
```

Expected: test passes.

- [ ] **Step 5: Wire real execution**

Replace the skeleton throw with code that:

1. Creates a local hidden/minimal window using the repo's existing window factory.
2. Loads the scene document.
3. Builds `SceneRuntime` or equivalent scene object from document.
4. Creates `VulkanRenderer`.
5. Calls `renderOutputProfile`.
6. Writes outputs using shared image writer.
7. Prints structured JSON:

```json
{"ok":true,"linearExr":"...","cpuSrgbPng":"...","pipelineSrgbPng":"..."}
```

If the CLI uses `lxe_editor` internally instead, wrap the editor process in RAII:

```cpp
class EditorProcess final {
public:
  ~EditorProcess() { stopIfStartedByThisProcess(); }
};
```

The wrapper must only stop the process it started.

- [ ] **Step 6: Run local CLI smoke**

Run:

```bash
cmake --build build --target lxe_realtime_render
./build/src/tools/lxe_realtime_render/lxe_realtime_render \
  --scene assets/scenes/ibl_metal_sphere.scene.yaml \
  --profile preview
```

Expected: structured JSON with `linearExr`, `cpuSrgbPng`, and `pipelineSrgbPng`; files exist.

- [ ] **Step 7: Commit**

```bash
git add src/tools/CMakeLists.txt src/tools/lxe_realtime_render \
  src/test/integration/test_realtime_render_cli.cpp src/test/CMakeLists.txt
git commit -m "add local realtime render cli"
```

---

## Task 11: Add EXR Compare Tool and Test Scene

**Files:**
- Create: `src/tools/lxe_compare_exr/CMakeLists.txt`
- Create: `src/tools/lxe_compare_exr/main.cpp`
- Modify: `src/tools/CMakeLists.txt`
- Create: `src/test/integration/test_exr_compare.cpp`
- Modify: `src/test/CMakeLists.txt`
- Create: `assets/scenes/realtime_offline_compare.scene.yaml`

- [ ] **Step 1: Create controlled comparison scene**

Create `assets/scenes/realtime_offline_compare.scene.yaml` with:

```yaml
scene:
  name: realtime_offline_compare
  gameplayCameraPath: /game_cam
  defaultOutputProfile: compare
  outputProfiles:
    compare:
      camera: /game_cam
      width: 64
      height: 64
      outputFormat: exr-png
      outDir: artifacts/compare
      cameraOverrides:
        fovY: 42.0
        nearPlane: 0.1
        farPlane: 80.0
        focusDistance: 5.0
  offlineRender:
    integrator: primary-ray
    samples: 1
    maxBounce: 1
    seed: 1
    profile: compare
    shadows: false
root:
  nodeName: scene_root
  name: ''
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  visibilityMask: 4294967295
  children:
    - nodeName: game_camera
      name: game_cam
      transform:
        translation: [0.0, 1.7, 5.0]
        rotation: [0.996038, -0.088929, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      camera:
        type: perspective
        fovY: 42.0
        aspect: 1.0
        nearPlane: 0.1
        farPlane: 80.0
        focusDistance: 5.0
        cullingMask: 4294967295
    - nodeName: compare_sphere
      name: compare_sphere
      transform:
        translation: [0.0, 0.85, 0.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      mesh:
        uri: builtin://lxe_editor/primitives/sphere
      material:
        uri: assets/materials/pbr_gold.material
      nodeMaterialOverrides:
        MaterialUBO.baseColor: [0.7, 0.5, 0.25]
        MaterialUBO.metallic: 0.0
        MaterialUBO.roughness: 0.5
    - nodeName: dir_light_node
      name: dir_light
      transform:
        translation: [0.0, 0.0, 0.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      light:
        kind: Directional
        direction: [-0.35, -1.0, -0.25]
        color: [1.0, 0.96, 0.88]
        intensity: 0.8
        shadowStrength: 0.0
```

- [ ] **Step 2: Add compare CLI**

Create `src/tools/lxe_compare_exr/CMakeLists.txt`:

```cmake
add_executable(lxe_compare_exr main.cpp)
target_link_libraries(lxe_compare_exr
  PRIVATE
  ${CORE_LIB}
  ${INFRA_LIB}
)
target_include_directories(lxe_compare_exr PUBLIC ${CMAKE_SOURCE_DIR}/src)
```

Create `src/tools/lxe_compare_exr/main.cpp` that accepts:

```bash
lxe_compare_exr --a A.exr --b B.exr --mean 0.05 --max 0.25 --rmse 0.08
```

It must print:

```json
{"ok":true,"meanAbsError":0.0,"maxAbsError":0.0,"rmse":0.0,"pixelCount":4096}
```

Use tinyexr `LoadEXR()` for both images, compare RGB only, and require equal width/height.

- [ ] **Step 3: Add compare test**

Create `src/test/integration/test_exr_compare.cpp` that:

1. Writes two identical 1x1 EXRs using `LX_infra::image::writeRgba32fExr`.
2. Executes `lxe_compare_exr --a ... --b ...`.
3. Expects exit code 0.
4. Writes a second image with RGB difference 1.0.
5. Executes with strict thresholds.
6. Expects non-zero exit code.

- [ ] **Step 4: Register tool and test**

In `src/tools/CMakeLists.txt`:

```cmake
add_subdirectory(lxe_compare_exr)
```

In `src/test/CMakeLists.txt`, add `test_exr_compare` and:

```cmake
if(TARGET test_exr_compare)
  add_dependencies(test_exr_compare lxe_compare_exr)
endif()
```

- [ ] **Step 5: Run compare test**

Run:

```bash
cmake --build build --target lxe_compare_exr test_exr_compare
./build/src/test/test_exr_compare
```

Expected: test passes.

- [ ] **Step 6: Commit**

```bash
git add src/tools/lxe_compare_exr src/tools/CMakeLists.txt \
  src/test/integration/test_exr_compare.cpp src/test/CMakeLists.txt \
  assets/scenes/realtime_offline_compare.scene.yaml
git commit -m "add realtime offline exr comparison tool"
```

---

## Task 12: Align Realtime and Offline Comparison Formula

**Files:**
- Modify: `assets/shaders/glsl/offline_primary_ray.comp`
- Modify: realtime lighting shader files under `assets/shaders/glsl/`
- Create: `assets/shaders/glsl/common/direct_lighting.glsl`
- Modify: `src/core/offline/offline_ray_scene.*` if material records need stable defaults

- [ ] **Step 1: Create shared direct lighting include**

Create `assets/shaders/glsl/common/direct_lighting.glsl`:

```glsl
#ifndef LX_DIRECT_LIGHTING_GLSL
#define LX_DIRECT_LIGHTING_GLSL

vec3 lxLambertDirect(vec3 baseColor, vec3 normal, vec3 lightDir,
                     vec3 lightColor, float lightIntensity) {
  float ndotl = max(dot(normalize(normal), normalize(lightDir)), 0.0);
  return baseColor * lightColor * lightIntensity * ndotl;
}

#endif
```

- [ ] **Step 2: Use include in offline shader**

In `assets/shaders/glsl/offline_primary_ray.comp`, include and use:

```glsl
#include "common/direct_lighting.glsl"
```

Replace primary-ray direct diffuse term with:

```glsl
vec3 direct = lxLambertDirect(material.baseColor.rgb, hitNormal, lightDir,
                              params.lightColorEnvironment.rgb,
                              params.lightDirectionIntensity.w);
```

Keep this requirement limited to the stable diffuse comparison path; do not add PBR/path-tracing expansion while implementing this plan.

- [ ] **Step 3: Use equivalent include in realtime shader**

Find realtime forward shader:

```bash
rg -n "dot\\(|baseColor|light|Directional|shadow" assets/shaders/glsl/*.frag
```

Include `common/direct_lighting.glsl` and use `lxLambertDirect()` for the compare mode path. Gate compare mode through a material/profile flag or specialization constant so normal viewport behavior is preserved.

- [ ] **Step 4: Compile shaders**

Run:

```bash
cmake --build build --target CompileShaders
```

Expected: shader compile passes.

- [ ] **Step 5: Commit**

```bash
git add assets/shaders/glsl/common/direct_lighting.glsl \
  assets/shaders/glsl/offline_primary_ray.comp \
  assets/shaders/glsl/*.frag assets/shaders/glsl/*.spv
git commit -m "align realtime offline comparison lighting"
```

---

## Task 13: End-to-End Local Verification

**Files:**
- Modify: `notes/requirements/068-a-output-profiles-and-realtime-render-generation.md`
- Modify: `notes/tutorial/offline-renderer/*.md` if old schema appears

- [ ] **Step 1: Run full build subset**

Run:

```bash
cmake --build build --target CompileShaders \
  test_output_profile_resolution \
  test_scene_document \
  test_offline_render_cli \
  test_offline_image_writer \
  test_tone_mapping \
  test_realtime_render_profile_commands \
  test_realtime_render_cli \
  test_exr_compare \
  lxe_offline_render \
  lxe_realtime_render \
  lxe_compare_exr \
  lxe_editor
```

Expected: build completes.

- [ ] **Step 2: Run headless tests**

Run:

```bash
./build/src/test/test_output_profile_resolution
./build/src/test/test_scene_document
./build/src/test/test_offline_render_cli
./build/src/test/test_offline_image_writer
./build/src/test/test_tone_mapping
./build/src/test/test_realtime_render_profile_commands
./build/src/test/test_realtime_render_cli
./build/src/test/test_exr_compare
```

Expected: each prints `passed`.

- [ ] **Step 3: Generate offline comparison output**

Run:

```bash
./build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/scenes/realtime_offline_compare.scene.yaml \
  --profile compare \
  --samples 1 \
  --max-bounce 1 \
  --seed 1 \
  --out /tmp/lxengine_compare/offline/render
```

Expected: output includes `exr=/tmp/lxengine_compare/offline/render.exr`.

- [ ] **Step 4: Generate realtime comparison output**

Run:

```bash
./build/src/tools/lxe_realtime_render/lxe_realtime_render \
  --scene assets/scenes/realtime_offline_compare.scene.yaml \
  --profile compare
```

Expected: JSON contains a `linearExr` path. If this CLI starts `lxe_editor`, verify after command exit:

```bash
pgrep -af lxe_editor || true
```

Expected: no `lxe_editor` process owned by the CLI remains.

- [ ] **Step 5: Compare EXRs**

Run:

```bash
./build/src/tools/lxe_compare_exr/lxe_compare_exr \
  --a /tmp/lxengine_compare/offline/render.exr \
  --b artifacts/compare/realtime/realtime_offline_compare/compare/render.linear.exr \
  --mean 0.08 \
  --max 0.40 \
  --rmse 0.12
```

Expected: JSON with `"ok":true`. If it fails, inspect metrics and either fix formula mismatch or update the compare scene to remove uncontrolled lighting terms.

- [ ] **Step 6: Search for removed schema and CLI**

Run:

```bash
rg -n "offlineRender\\.profiles|defaultProfile:|maxDepth|--max-depth|backend: vulkan-compute" \
  assets src notes/requirements/068-a-output-profiles-and-realtime-render-generation.md
```

Expected: no matches except explicit “removed/rejected” wording in requirements or tests.

- [ ] **Step 7: Build notes**

Run:

```bash
scripts/notes/serve_site.sh --build
```

Expected: build succeeds. Existing unrelated link warnings may remain.

- [ ] **Step 8: Update requirement implementation status**

Append to `notes/requirements/068-a-output-profiles-and-realtime-render-generation.md`:

```markdown
## 实施状态

2026-06-02 已完成：

- `.scene.yaml` 已迁移到 `outputProfiles + offlineRender`。
- `lxe_offline_render` 已使用 `--max-bounce` 和 output profile resolution。
- editor 已提供 `realtime-render ls` / `realtime-render run <profile>`。
- 本地 `lxe_realtime_render` 可生成 realtime profile outputs，并在结束后不留下它启动的 editor 进程。
- `lxe_compare_exr` 可对 realtime/offline EXR 输出做数值比较。
- 受控 compare scene 已纳入验证流程。
```

- [ ] **Step 9: Commit**

```bash
git add notes/requirements/068-a-output-profiles-and-realtime-render-generation.md \
  notes/tutorial/offline-renderer \
  assets/scenes \
  src
git commit -m "complete output profile realtime render workflow"
```

---

## Self-Review

**Spec coverage:**

- R1/R2 covered by Tasks 1-2.
- R3 covered by Task 3.
- R4/R8 covered by Tasks 6-7.
- R5/R6 covered by Tasks 8-10.
- R7/R11 covered by Tasks 4-5 and Task 12.
- R9 covered by Task 10 and Task 13.
- R10 covered by Task 11 and Task 13.

**Placeholder scan:** No task uses an unresolved “fill this in” instruction. Every test and new public interface has concrete names and code snippets.

**Type consistency:** The plan consistently uses `OutputProfile`, `OfflineRenderSettings`, `RenderProfileDocument`, `RenderProfileCliOverrides`, and `ResolvedRenderProfile`. `maxDepth` is only mentioned as rejected legacy text; implementation names use `maxBounce`.
