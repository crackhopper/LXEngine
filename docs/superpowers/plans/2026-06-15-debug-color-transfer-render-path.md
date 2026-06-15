# Debug Color Transfer Render Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a dedicated color-transfer debug render path and editor export workflow that proves where HDR, tone mapping, sRGB attachment conversion, UNORM manual encoding, or readback diverges before changing the production renderer.

**Architecture:** Keep `RenderPathGraph` as the graph truth source and keep render execution in backend/core-facing Vulkan code. `src/editor` only exposes the command and JSON result. The export bundle records both images and the format facts needed to decide the next root-cause fix.

**Tech Stack:** C++20, GLSL fullscreen passes, Vulkan dynamic/offscreen attachments, existing `lxe_editor` command bus/API, TinyEXR/STB image IO, Python unittest smoke under Xvfb.

---

## File Structure

- Create `assets/render_paths/debug_color_transfer.render-path.yaml`: the diagnostic graph asset.
- Create `assets/shaders/glsl/debug_color_transfer_tonemap.vert` and `.frag`: tone map HDR to linear LDR.
- Create `assets/shaders/glsl/debug_color_transfer_copy.vert` and `.frag`: copy linear or manual-sRGB encoded LDR.
- Create `assets/shaders/glsl/debug_color_transfer_ramp.vert` and `.frag`: output known ramp/probe values.
- Modify `src/core/frame_graph/pass.hpp`: add pass ids for debug color-transfer fullscreen passes.
- Modify `src/test/integration/test_render_resource_parsers.cpp`: parse and validate the debug graph asset.
- Modify `src/infra/image/rgba_image_io.hpp/.cpp`: add raw RGBA8 PNG write support and a no-extra-gamma LDR export helper.
- Modify `src/backend/vulkan/vulkan_renderer_types.hpp`: add debug export request/result, exported target records, format facts, and ramp probe records.
- Modify `src/backend/vulkan/vulkan_realtime_renderer.hpp/.cpp`: add the one-shot debug export implementation.
- Modify `src/backend/vulkan/vulkan_renderer.hpp/.cpp`: forward the new export API.
- Create `src/editor/project/debug_render_export.hpp/.cpp`: editor-side JSON helper for export results.
- Modify `src/editor/CMakeLists.txt`: compile `project/debug_render_export.cpp`.
- Modify `src/editor/app/editor_session.hpp/.cpp`: add an export hook and `render debug export-path color-transfer` command branch.
- Modify `src/editor/main.cpp`: wire the editor hook to the Vulkan renderer.
- Modify `src/test/integration/test_realtime_render_profile_commands.cpp`: add static API shape and JSON helper tests.
- Modify `src/test/integration/test_helmet_standard_pbr_realtime_smoke.py`: add a color-transfer export smoke.
- Modify `src/tools/lxe_realtime_render/lxe_realtime_render.py`: add an optional debug export run and validation helpers.
- Update `notes/subsystems/vulkan-backend.md`: document the debug color-transfer workflow and the rule that this tool diagnoses before fixing.

---

### Task 1: Add Debug Graph Asset And Shader Parser Coverage

**Files:**
- Create: `assets/render_paths/debug_color_transfer.render-path.yaml`
- Create: `assets/shaders/glsl/debug_color_transfer_tonemap.vert`
- Create: `assets/shaders/glsl/debug_color_transfer_tonemap.frag`
- Create: `assets/shaders/glsl/debug_color_transfer_copy.vert`
- Create: `assets/shaders/glsl/debug_color_transfer_copy.frag`
- Create: `assets/shaders/glsl/debug_color_transfer_ramp.vert`
- Create: `assets/shaders/glsl/debug_color_transfer_ramp.frag`
- Modify: `src/core/frame_graph/pass.hpp`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`

- [ ] **Step 1: Add failing parser test for the new graph asset**

In `src/test/integration/test_render_resource_parsers.cpp`, add this function after `testDefaultRenderPathGraphAssetParses()`:

```cpp
void testDebugColorTransferRenderPathGraphAssetParses() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse(
      "assets/render_paths/debug_color_transfer.render-path.yaml",
      readTextFile("assets/render_paths/debug_color_transfer.render-path.yaml"));

  EXPECT(parsed.renderPathGraph.has_value(),
         "debug color transfer graph asset should parse");
  EXPECT(parsed.diagnostics.empty(),
         "debug color transfer graph should not emit diagnostics");
  if (!parsed.renderPathGraph.has_value()) {
    return;
  }

  const auto &graph = *parsed.renderPathGraph;
  EXPECT(graph.name == "DebugColorTransfer",
         "debug graph should retain name");
  EXPECT(graph.renderPath == LX_core::RenderPath::Forward,
         "debug graph should use Forward render path");
  EXPECT(graph.passes.size() == 6,
         "debug graph should declare Forward plus five diagnostic passes");

  bool sawSrgbAttachment = false;
  bool sawUnormAttachment = false;
  bool sawLinearLdr = false;
  for (const auto &pass : graph.passes) {
    for (const auto &attachment : pass.attachments) {
      if (attachment.target == LX_core::StringID("debug.final.srgb") ||
          attachment.target == LX_core::StringID("debug.ramp.srgb")) {
        sawSrgbAttachment =
            sawSrgbAttachment ||
            attachment.format == LX_core::ImageFormat::RGBA8Srgb;
      }
      if (attachment.target ==
              LX_core::StringID("debug.final.unorm_manual_srgb") ||
          attachment.target ==
              LX_core::StringID("debug.ramp.unorm_manual_srgb")) {
        sawUnormAttachment =
            sawUnormAttachment ||
            attachment.format == LX_core::ImageFormat::RGBA8;
      }
      if (attachment.target == LX_core::StringID("debug.ldr.linear")) {
        sawLinearLdr =
            sawLinearLdr ||
            attachment.format == LX_core::ImageFormat::RGBA16Float;
      }
    }
  }
  EXPECT(sawSrgbAttachment, "debug graph should contain SRGB attachments");
  EXPECT(sawUnormAttachment, "debug graph should contain UNORM attachments");
  EXPECT(sawLinearLdr, "debug graph should contain linear LDR float target");

  const LX_core::FrameGraph frameGraph =
      LX_core::buildFrameGraphFromRenderPathGraph(
          graph, LX_core::GraphResourceRegistry::makeDefault());
  const auto compiled =
      frameGraph.compile(LX_core::GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(),
         "debug graph asset should compile into a FrameGraph plan");
}
```

Call it from `main()`:

```cpp
  testDebugColorTransferRenderPathGraphAssetParses();
```

- [ ] **Step 2: Run the failing parser test**

Run:

```bash
ninja test_render_resource_parsers
./build/src/test/test_render_resource_parsers
```

Expected: build or test fails because `assets/render_paths/debug_color_transfer.render-path.yaml` does not exist yet.

- [ ] **Step 3: Add debug pass ids**

In `src/core/frame_graph/pass.hpp`, after `Pass_PostProcess`, add:

```cpp
inline const StringID Pass_DebugToneMapLinear =
    StringID("DebugToneMapLinear");
inline const StringID Pass_DebugSrgbAttachment =
    StringID("DebugSrgbAttachment");
inline const StringID Pass_DebugUnormManualSrgb =
    StringID("DebugUnormManualSrgb");
inline const StringID Pass_DebugRampSrgb = StringID("DebugRampSrgb");
inline const StringID Pass_DebugRampUnormManualSrgb =
    StringID("DebugRampUnormManualSrgb");
```

- [ ] **Step 4: Add the debug graph asset**

Create `assets/render_paths/debug_color_transfer.render-path.yaml`:

```yaml
schema: lxe.render-path-graph.v1
name: DebugColorTransfer
renderPath: Forward

features:
  toneMapping:
    uri: effects/tone_mapping.render-feature.yaml

passes:
  - id: Forward
    stage: raster
    dispatch: draw
    shader: render_paths/Forward/pbr
    input:
      kind: scene-renderables
      material:
        type: [matte, uber, metal, substrate, standard-pbr]
        required: true
      geometry:
        vertex: position-only
        topology: triangle-list
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
          samples: 1
          layers: 1
        - target: depth.main
          format: D32Float
          samples: 1
          layers: 1
          depth: true
    sources:
      - geometry.vertex
      - geometry.index
      - material.bsdf
      - scene.camera
      - scene.lights
    targets: [hdr.color, depth.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
      blendEnable: false

  - id: DebugToneMapLinear
    stage: raster
    dispatch: fullscreen
    shader: debug_color_transfer_tonemap
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: debug.ldr.linear
          format: RGBA16Float
          samples: 1
          layers: 1
    sources: [hdr.color, feature.toneMapping]
    targets: [debug.ldr.linear]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
      blendEnable: false

  - id: DebugSrgbAttachment
    stage: raster
    dispatch: fullscreen
    shader: debug_color_transfer_copy
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: debug.final.srgb
          format: RGBA8Srgb
          samples: 1
          layers: 1
    sources: [debug.ldr.linear]
    targets: [debug.final.srgb]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
      blendEnable: false

  - id: DebugUnormManualSrgb
    stage: raster
    dispatch: fullscreen
    shader: debug_color_transfer_copy
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: debug.final.unorm_manual_srgb
          format: RGBA8
          samples: 1
          layers: 1
    sources: [debug.ldr.linear]
    targets: [debug.final.unorm_manual_srgb]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
      blendEnable: false

  - id: DebugRampSrgb
    stage: raster
    dispatch: fullscreen
    shader: debug_color_transfer_ramp
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: debug.ramp.srgb
          format: RGBA8Srgb
          samples: 1
          layers: 1
    sources: [feature.toneMapping]
    targets: [debug.ramp.srgb]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
      blendEnable: false

  - id: DebugRampUnormManualSrgb
    stage: raster
    dispatch: fullscreen
    shader: debug_color_transfer_ramp
    input:
      kind: fullscreen-triangle
    rendering:
      mode: dynamic
      attachments:
        - target: debug.ramp.unorm_manual_srgb
          format: RGBA8
          samples: 1
          layers: 1
    sources: [feature.toneMapping]
    targets: [debug.ramp.unorm_manual_srgb]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
      blendEnable: false
```

- [ ] **Step 5: Add fullscreen vertex shaders**

Create each `.vert` file with the same content:

```glsl
#version 450

layout(location = 0) out vec2 vUV;

void main() {
    vec2 pos;
    if (gl_VertexIndex == 0) {
        pos = vec2(-1.0, -1.0);
    } else if (gl_VertexIndex == 1) {
        pos = vec2(3.0, -1.0);
    } else {
        pos = vec2(-1.0, 3.0);
    }

    vUV = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
```

Files:

```text
assets/shaders/glsl/debug_color_transfer_tonemap.vert
assets/shaders/glsl/debug_color_transfer_copy.vert
assets/shaders/glsl/debug_color_transfer_ramp.vert
```

- [ ] **Step 6: Add tone-map-only fragment shader**

Create `assets/shaders/glsl/debug_color_transfer_tonemap.frag`:

```glsl
#version 450

#include "common/tone_mapping.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;

layout(set = 0, binding = 1) uniform DebugColorTransferUBO {
    float exposure;
    int toneMappingMode;
    int outputEncodingMode;
    float gamma;
} debugTransfer;

void main() {
    vec3 hdr = texture(SceneColor, vUV).rgb;
    vec3 mapped = debugTransfer.toneMappingMode == 1
                      ? lxToneMapReinhard(hdr, debugTransfer.exposure)
                      : lxToneMapAces(hdr, debugTransfer.exposure);
    outColor = vec4(mapped, 1.0);
}
```

- [ ] **Step 7: Add copy/manual-encoding fragment shader**

Create `assets/shaders/glsl/debug_color_transfer_copy.frag`:

```glsl
#version 450

#include "common/tone_mapping.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;

layout(set = 0, binding = 1) uniform DebugColorTransferUBO {
    float exposure;
    int toneMappingMode;
    int outputEncodingMode;
    float gamma;
} debugTransfer;

void main() {
    vec3 linearLdr = texture(SceneColor, vUV).rgb;
    vec3 encoded = debugTransfer.outputEncodingMode == 1
                       ? lxLinearToSrgbGamma(linearLdr, debugTransfer.gamma)
                       : clamp(linearLdr, vec3(0.0), vec3(1.0));
    outColor = vec4(encoded, 1.0);
}
```

- [ ] **Step 8: Add ramp fragment shader**

Create `assets/shaders/glsl/debug_color_transfer_ramp.frag`:

```glsl
#version 450

#include "common/tone_mapping.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform DebugColorTransferUBO {
    float exposure;
    int toneMappingMode;
    int outputEncodingMode;
    float gamma;
} debugTransfer;

float fixedProbe(float x) {
    if (x < 0.2) {
        return 0.0;
    }
    if (x < 0.4) {
        return 0.18;
    }
    if (x < 0.6) {
        return 0.5;
    }
    if (x < 0.8) {
        return 1.0;
    }
    return clamp((x - 0.8) / 0.2, 0.0, 1.0);
}

void main() {
    float linearValue = fixedProbe(vUV.x);
    vec3 linearColor = vec3(linearValue);
    vec3 encoded = debugTransfer.outputEncodingMode == 1
                       ? lxLinearToSrgbGamma(linearColor, debugTransfer.gamma)
                       : linearColor;
    outColor = vec4(encoded, 1.0);
}
```

- [ ] **Step 9: Run parser and shader checks**

Run:

```bash
ninja test_render_resource_parsers test_shader_compiler
./build/src/test/test_render_resource_parsers
./build/src/test/test_shader_compiler
```

Expected: both tests pass. If `test_shader_compiler` fails because the new UBO is not reflected in an expected audit, add explicit layout assertions for `DebugColorTransferUBO` beside the `PostProcessUBO` assertions.

- [ ] **Step 10: Commit**

Run:

```bash
git add assets/render_paths/debug_color_transfer.render-path.yaml \
  assets/shaders/glsl/debug_color_transfer_tonemap.vert \
  assets/shaders/glsl/debug_color_transfer_tonemap.frag \
  assets/shaders/glsl/debug_color_transfer_copy.vert \
  assets/shaders/glsl/debug_color_transfer_copy.frag \
  assets/shaders/glsl/debug_color_transfer_ramp.vert \
  assets/shaders/glsl/debug_color_transfer_ramp.frag \
  src/core/frame_graph/pass.hpp \
  src/test/integration/test_render_resource_parsers.cpp
git commit -m "test: add debug color transfer render path asset"
```

---

### Task 2: Add Export Types And Raw Image IO

**Files:**
- Modify: `src/backend/vulkan/vulkan_renderer_types.hpp`
- Modify: `src/infra/image/rgba_image_io.hpp`
- Modify: `src/infra/image/rgba_image_io.cpp`
- Create: `src/editor/project/debug_render_export.hpp`
- Create: `src/editor/project/debug_render_export.cpp`
- Modify: `src/editor/CMakeLists.txt`
- Modify: `src/test/integration/test_realtime_render_profile_commands.cpp`

- [ ] **Step 1: Add failing API shape test**

In `src/test/integration/test_realtime_render_profile_commands.cpp`, add:

```cpp
void testDebugColorTransferExportApiShape() {
  using LX_core::backend::VulkanDebugColorTransferExportRequest;
  using LX_core::backend::VulkanDebugColorTransferExportResult;
  using LX_core::backend::VulkanDebugColorTransferTargetRecord;
  using LX_core::backend::VulkanRenderer;
  using LX_core::backend::VulkanRealtimeRenderer;

  static_assert(std::is_same_v<
                decltype(std::declval<VulkanRenderer &>()
                             .exportDebugColorTransfer(
                                 std::declval<
                                     const VulkanDebugColorTransferExportRequest &>())),
                VulkanDebugColorTransferExportResult>);
  static_assert(std::is_same_v<
                decltype(std::declval<VulkanRealtimeRenderer &>()
                             .exportDebugColorTransfer(
                                 std::declval<
                                     const VulkanDebugColorTransferExportRequest &>())),
                VulkanDebugColorTransferExportResult>);

  VulkanDebugColorTransferExportRequest request{
      .cameraPath = "/editor_cam",
      .outputDirectory = "artifacts/debug/color-transfer",
  };
  EXPECT(request.cameraPath == "/editor_cam",
         "debug export request should expose camera path");
  EXPECT(request.outputDirectory.filename() == "color-transfer",
         "debug export request should expose output directory");

  VulkanDebugColorTransferExportResult result;
  result.manifestPath = "manifest.json";
  result.targets.push_back(VulkanDebugColorTransferTargetRecord{
      .name = "debug.ramp.srgb",
      .path = "ramp_srgb_attachment.png",
      .format = "R8G8B8A8_SRGB",
      .width = 64,
      .height = 64,
  });
  EXPECT(result.manifestPath.filename() == "manifest.json",
         "debug export result should expose manifest path");
  EXPECT(result.targets.size() == 1,
         "debug export result should expose target records");
}
```

Call it from `main()`:

```cpp
  testDebugColorTransferExportApiShape();
```

- [ ] **Step 2: Run the failing API shape test**

Run:

```bash
ninja test_realtime_render_profile_commands
./build/src/test/test_realtime_render_profile_commands
```

Expected: compile fails because the request/result types and methods do not exist.

- [ ] **Step 3: Add backend request/result types**

In `src/backend/vulkan/vulkan_renderer_types.hpp`, add after `VulkanRealtimeProfileOutputResult`:

```cpp
struct VulkanDebugColorTransferTargetRecord final {
  std::string name;
  std::filesystem::path path;
  std::string format;
  u32 width = 0;
  u32 height = 0;
  double minValue = 0.0;
  double maxValue = 0.0;
  double meanValue = 0.0;
  double nonZeroRatio = 0.0;
};

struct VulkanDebugColorTransferProbeRecord final {
  std::string target;
  std::string label;
  u32 x = 0;
  u32 y = 0;
  u32 red = 0;
  u32 green = 0;
  u32 blue = 0;
  u32 expected = 0;
};

struct VulkanDebugColorTransferFormatFacts final {
  std::string surfaceFormat;
  std::string surfaceColorSpace;
  std::string swapchainImageViewFormat;
  std::string swapchainTargetFormat;
};

struct VulkanDebugColorTransferExportRequest final {
  std::optional<std::string> cameraPath;
  std::filesystem::path outputDirectory;
  u32 width = 0;
  u32 height = 0;
};

struct VulkanDebugColorTransferExportResult final {
  std::filesystem::path manifestPath;
  std::filesystem::path outputDirectory;
  VulkanDebugColorTransferFormatFacts formatFacts;
  std::vector<VulkanDebugColorTransferTargetRecord> targets;
  std::vector<VulkanDebugColorTransferProbeRecord> probes;
};
```

- [ ] **Step 4: Add raw RGBA8 PNG writer**

In `src/infra/image/rgba_image_io.hpp`, add:

```cpp
void writeRawRgba8Png(const std::filesystem::path &path, u32 width, u32 height,
                      const std::vector<unsigned char> &rgba);
```

In `src/infra/image/rgba_image_io.cpp`, add:

```cpp
void writeRawRgba8Png(const std::filesystem::path &path, u32 width, u32 height,
                      const std::vector<unsigned char> &rgba) {
  const usize expectedSize = static_cast<usize>(width) *
                             static_cast<usize>(height) * 4u;
  if (width == 0 || height == 0 || rgba.size() != expectedSize) {
    throw std::runtime_error("invalid raw RGBA8 PNG payload for " +
                             path.string());
  }
  const int ok =
      stbi_write_png(path.string().c_str(), static_cast<int>(width),
                     static_cast<int>(height), 4, rgba.data(),
                     static_cast<int>(width * 4u));
  if (ok == 0) {
    throw std::runtime_error("failed to write raw RGBA8 PNG " + path.string());
  }
}
```

- [ ] **Step 5: Add editor JSON helper**

Create `src/editor/project/debug_render_export.hpp`:

```cpp
#pragma once

#include "backend/vulkan/vulkan_renderer_types.hpp"

#include <string>
#include <string_view>

namespace LX_demo::lxe_editor {

[[nodiscard]] std::string debugColorTransferExportResultJson(
    const LX_core::backend::VulkanDebugColorTransferExportResult &result);

} // namespace LX_demo::lxe_editor
```

Create `src/editor/project/debug_render_export.cpp`:

```cpp
#include "editor/project/debug_render_export.hpp"

#include <sstream>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    switch (ch) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out.push_back(ch);
      break;
    }
  }
  return out;
}

[[nodiscard]] std::string pathJson(const std::filesystem::path &path) {
  return jsonEscape(path.generic_string());
}

} // namespace

std::string debugColorTransferExportResultJson(
    const LX_core::backend::VulkanDebugColorTransferExportResult &result) {
  std::ostringstream oss;
  oss << "{\"manifestPath\":\"" << pathJson(result.manifestPath)
      << "\",\"outputDirectory\":\"" << pathJson(result.outputDirectory)
      << "\",\"targets\":[";
  for (usize i = 0; i < result.targets.size(); ++i) {
    const auto &target = result.targets[i];
    if (i != 0) {
      oss << ',';
    }
    oss << "{\"name\":\"" << jsonEscape(target.name) << "\",\"path\":\""
        << pathJson(target.path) << "\",\"format\":\""
        << jsonEscape(target.format) << "\",\"width\":" << target.width
        << ",\"height\":" << target.height << "}";
  }
  oss << "]}";
  return oss.str();
}

} // namespace LX_demo::lxe_editor
```

Add `project/debug_render_export.cpp` to `LXE_EDITOR_SOURCES` in `src/editor/CMakeLists.txt`.

- [ ] **Step 6: Add renderer method declarations**

In both `src/backend/vulkan/vulkan_realtime_renderer.hpp` and `src/backend/vulkan/vulkan_renderer.hpp`, add:

```cpp
  VulkanDebugColorTransferExportResult exportDebugColorTransfer(
      const VulkanDebugColorTransferExportRequest &request);
```

In `src/backend/vulkan/vulkan_renderer.cpp`, add forwarding:

```cpp
VulkanDebugColorTransferExportResult VulkanRenderer::exportDebugColorTransfer(
    const VulkanDebugColorTransferExportRequest &request) {
  return p_realtime->exportDebugColorTransfer(request);
}
```

In `src/backend/vulkan/vulkan_realtime_renderer.cpp`, add the public wrapper:

```cpp
VulkanDebugColorTransferExportResult
VulkanRealtimeRenderer::exportDebugColorTransfer(
    const VulkanDebugColorTransferExportRequest &request) {
  return p_impl->exportDebugColorTransfer(request);
}
```

At this task, the private impl can throw:

```cpp
VulkanDebugColorTransferExportResult exportDebugColorTransfer(
    const VulkanDebugColorTransferExportRequest &) {
  throw std::runtime_error("debug color transfer export is not implemented");
}
```

- [ ] **Step 7: Run API shape tests**

Run:

```bash
ninja test_realtime_render_profile_commands
./build/src/test/test_realtime_render_profile_commands
```

Expected: pass.

- [ ] **Step 8: Commit**

Run:

```bash
git add src/backend/vulkan/vulkan_renderer_types.hpp \
  src/backend/vulkan/vulkan_realtime_renderer.hpp \
  src/backend/vulkan/vulkan_realtime_renderer.cpp \
  src/backend/vulkan/vulkan_renderer.hpp \
  src/backend/vulkan/vulkan_renderer.cpp \
  src/infra/image/rgba_image_io.hpp \
  src/infra/image/rgba_image_io.cpp \
  src/editor/project/debug_render_export.hpp \
  src/editor/project/debug_render_export.cpp \
  src/editor/CMakeLists.txt \
  src/test/integration/test_realtime_render_profile_commands.cpp
git commit -m "feat: add debug color transfer export api"
```

---

### Task 3: Add Editor Command Hook

**Files:**
- Modify: `src/editor/app/editor_session.hpp`
- Modify: `src/editor/app/editor_session.cpp`
- Modify: `src/editor/main.cpp`
- Modify: `src/test/integration/test_realtime_render_profile_commands.cpp`

- [ ] **Step 1: Add failing JSON helper and command-shape tests**

In `src/test/integration/test_realtime_render_profile_commands.cpp`, include:

```cpp
#include "editor/project/debug_render_export.hpp"
```

Add:

```cpp
void testDebugColorTransferExportResultJson() {
  LX_core::backend::VulkanDebugColorTransferExportResult result;
  result.manifestPath = "artifacts/debug/color-transfer/manifest.json";
  result.outputDirectory = "artifacts/debug/color-transfer";
  result.targets.push_back(LX_core::backend::VulkanDebugColorTransferTargetRecord{
      .name = "debug.final.srgb",
      .path = "artifacts/debug/color-transfer/srgb_attachment.png",
      .format = "R8G8B8A8_SRGB",
      .width = 64,
      .height = 32,
  });

  const std::string json =
      LX_demo::lxe_editor::debugColorTransferExportResultJson(result);
  EXPECT(json.find("\"manifestPath\"") != std::string::npos,
         "debug export JSON should expose manifest path");
  EXPECT(json.find("srgb_attachment.png") != std::string::npos,
         "debug export JSON should expose target path");
  EXPECT(json.find("\"width\":64") != std::string::npos,
         "debug export JSON should expose width");
}
```

Call it from `main()`.

- [ ] **Step 2: Add the render debug hook field**

In `LxeEditorSession::RenderDebugCommandHooks`, add:

```cpp
    std::function<LX_core::backend::VulkanDebugColorTransferExportResult(
        const LX_core::backend::VulkanDebugColorTransferExportRequest &)>
        exportColorTransferPath;
```

Add the necessary include in `editor_session.hpp`:

```cpp
#include "backend/vulkan/vulkan_renderer_types.hpp"
```

- [ ] **Step 3: Add command branch before the existing dump branch**

In `LxeEditorSession::initialize(...)`, inside the `"render"` handler and before the `dump` usage check, add:

```cpp
        if (args.size() >= 3 && args[0] == "debug" &&
            args[1] == "export-path" && args[2] == "color-transfer") {
          if (!m_renderDebugCommandHooks.exportColorTransferPath) {
            return makeCommandError(
                "render debug export-path color-transfer unavailable");
          }
          if (args.size() > 5) {
            return makeCommandError(
                "usage: render debug export-path color-transfer "
                "[camera-path] [out-dir]");
          }
          LX_core::backend::VulkanDebugColorTransferExportRequest request;
          if (args.size() >= 4) {
            request.cameraPath = args[3];
          }
          if (args.size() == 5) {
            request.outputDirectory = std::filesystem::path(args[4]);
          }
          try {
            const auto result =
                m_renderDebugCommandHooks.exportColorTransferPath(request);
            return makeCommandOk(
                "debug color transfer exported: " +
                    result.manifestPath.generic_string(),
                debugColorTransferExportResultJson(result));
          } catch (const std::exception &e) {
            return makeCommandError(e.what());
          }
        }
```

Add include in `editor_session.cpp`:

```cpp
#include "editor/project/debug_render_export.hpp"
```

Update the command usage string to include:

```text
render debug export-path color-transfer [camera-path] [out-dir]
```

- [ ] **Step 4: Wire editor main hook**

In `src/editor/main.cpp`, add to `renderDebugCommandHooks`:

```cpp
        .exportColorTransferPath =
            [vulkanRenderer](
                const LX_core::backend::VulkanDebugColorTransferExportRequest
                    &request) {
              return vulkanRenderer->exportDebugColorTransfer(request);
            },
```

- [ ] **Step 5: Run command/API shape tests**

Run:

```bash
ninja test_realtime_render_profile_commands test_lxe_editor_api_service
./build/src/test/test_realtime_render_profile_commands
./build/src/test/test_lxe_editor_api_service
```

Expected: pass.

- [ ] **Step 6: Commit**

Run:

```bash
git add src/editor/app/editor_session.hpp \
  src/editor/app/editor_session.cpp \
  src/editor/main.cpp \
  src/test/integration/test_realtime_render_profile_commands.cpp
git commit -m "feat: expose debug color transfer export command"
```

---

### Task 4: Implement Backend Export Bundle

**Files:**
- Modify: `src/backend/vulkan/vulkan_post_process_builder.hpp`
- Modify: `src/backend/vulkan/vulkan_post_process_builder.cpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/backend/vulkan/vulkan_renderer_types.hpp`
- Modify: `src/infra/image/rgba_image_io.hpp`
- Modify: `src/infra/image/rgba_image_io.cpp`

- [ ] **Step 1: Add debug fullscreen material builders**

In `VulkanPostProcessBuilder`, add:

```cpp
  [[nodiscard]] LX_core::MaterialInstance::UniquePtr
  createDebugColorTransferMaterial(LX_core::StringID pass,
                                   const char *shaderName,
                                   VulkanPostProcessOutputEncoding outputEncoding) const;
```

Implement it like `createStandardPostProcessMaterial`, but use:

```cpp
auto shader = std::make_shared<StaticFullscreenShader>(
    shaderName, loadGraphicsShaderStages(shaderName),
    debugColorTransferBindings(shaderName));
```

Add a binding helper in `vulkan_post_process_builder.cpp`:

```cpp
ShaderBindingLayout debugColorTransferBindings(const char *shaderName) {
  ShaderBindingLayout layout;
  if (std::string_view(shaderName) != "debug_color_transfer_ramp") {
    layout.bindings.push_back(ShaderBindingInfo{
        "SceneColor", 0, ShaderBindingKind::CombinedImageSampler,
        ShaderStageMask::Fragment});
  }
  layout.bindings.push_back(ShaderBindingInfo{
      "DebugColorTransferUBO", 1, ShaderBindingKind::UniformBuffer,
      ShaderStageMask::Fragment});
  layout.uniformBlocks.push_back(ShaderUniformBlockInfo{
      "DebugColorTransferUBO",
      16,
      {
          LX_core::StructMemberInfo{"exposure", LX_core::ShaderValueType::Float,
                                    0, 4, 1},
          LX_core::StructMemberInfo{"toneMappingMode",
                                    LX_core::ShaderValueType::Int, 4, 4, 1},
          LX_core::StructMemberInfo{"outputEncodingMode",
                                    LX_core::ShaderValueType::Int, 8, 4, 1},
          LX_core::StructMemberInfo{"gamma", LX_core::ShaderValueType::Float,
                                    12, 4, 1},
      }});
  return layout;
}
```

When creating the material, write:

```cpp
  material->writeShaderBindingParameter(LX_core::StringID("DebugColorTransferUBO"),
                                        LX_core::StringID("exposure"), 1.0f);
  material->writeShaderBindingParameter(LX_core::StringID("DebugColorTransferUBO"),
                                        LX_core::StringID("toneMappingMode"), 0);
  material->writeShaderBindingParameter(
      LX_core::StringID("DebugColorTransferUBO"),
      LX_core::StringID("outputEncodingMode"),
      static_cast<int>(outputEncoding));
  material->writeShaderBindingParameter(LX_core::StringID("DebugColorTransferUBO"),
                                        LX_core::StringID("gamma"), 2.2f);
```

- [ ] **Step 2: Add reusable byte conversion helpers**

In `vulkan_realtime_renderer.cpp`, factor these helpers near existing dump helpers:

```cpp
std::vector<unsigned char> makeRgba8PixelsFromDump(VkFormat format, u32 width,
                                                   u32 height,
                                                   const void *mapped);
LX_core::offline::OfflineReadbackImage makeRgba32fImageFromDump(
    VkFormat format, u32 width, u32 height, const void *mapped);
```

`makeRgba8PixelsFromDump` must preserve RGB byte values and reorder BGRA to RGBA:

```cpp
if (format == VK_FORMAT_B8G8R8A8_UNORM ||
    format == VK_FORMAT_B8G8R8A8_SRGB) {
  out[i + 0] = src[i + 2];
  out[i + 1] = src[i + 1];
  out[i + 2] = src[i + 0];
  out[i + 3] = src[i + 3];
}
```

Do not call tone mapping or gamma from this helper.

- [ ] **Step 3: Add manifest writer**

In `vulkan_realtime_renderer.cpp`, add:

```cpp
void writeDebugColorTransferManifest(
    const std::filesystem::path &path,
    const VulkanDebugColorTransferExportResult &result) {
  std::ofstream out(path);
  if (!out.is_open()) {
    throw std::runtime_error("failed to write debug color transfer manifest " +
                             path.string());
  }
  out << "{\n";
  out << "  \"surfaceFormat\": \""
      << jsonEscape(result.formatFacts.surfaceFormat) << "\",\n";
  out << "  \"surfaceColorSpace\": \""
      << jsonEscape(result.formatFacts.surfaceColorSpace) << "\",\n";
  out << "  \"swapchainImageViewFormat\": \""
      << jsonEscape(result.formatFacts.swapchainImageViewFormat) << "\",\n";
  out << "  \"targets\": [\n";
  for (usize i = 0; i < result.targets.size(); ++i) {
    const auto &target = result.targets[i];
    out << "    {\"name\":\"" << jsonEscape(target.name) << "\",\"path\":\""
        << jsonEscape(target.path.generic_string()) << "\",\"format\":\""
        << jsonEscape(target.format) << "\",\"width\":" << target.width
        << ",\"height\":" << target.height << ",\"min\":" << target.minValue
        << ",\"max\":" << target.maxValue << ",\"mean\":"
        << target.meanValue << ",\"nonZeroRatio\":"
        << target.nonZeroRatio << "}";
    out << (i + 1 == result.targets.size() ? "\n" : ",\n");
  }
  out << "  ]\n";
  out << "}\n";
}
```

Reuse the existing `jsonEscape` helper if it is already in scope; otherwise add a local one beside the metadata writer.

- [ ] **Step 4: Implement one-shot graph setup**

In `VulkanRealtimeRenderer::Impl::exportDebugColorTransfer`, implement this sequence:

```text
validate renderer initialized
choose camera from request.cameraPath or active debug camera
choose extent from request.width/height or swapchain extent
create output directory
load assets/render_paths/debug_color_transfer.render-path.yaml
validate expected pass set
resolve material source variants
build a temporary FrameGraph from the debug graph
temporarily install fullscreen facts for:
  DebugToneMapLinear -> debug_color_transfer_tonemap, Linear
  DebugSrgbAttachment -> debug_color_transfer_copy, Linear
  DebugUnormManualSrgb -> debug_color_transfer_copy, Srgb
  DebugRampSrgb -> debug_color_transfer_ramp, Linear
  DebugRampUnormManualSrgb -> debug_color_transfer_ramp, Srgb
execute the temporary graph into offscreen attachments
read back required attachments
write EXR/PNG/manifest bundle
restore the production frame graph/facts state
```

The expected pass set must be:

```cpp
const std::vector<LX_core::StringID> expectedPasses{
    LX_core::Pass_Forward,
    LX_core::Pass_DebugToneMapLinear,
    LX_core::Pass_DebugSrgbAttachment,
    LX_core::Pass_DebugUnormManualSrgb,
    LX_core::Pass_DebugRampSrgb,
    LX_core::Pass_DebugRampUnormManualSrgb,
};
```

Use a guard object to restore state:

```cpp
struct TemporaryFrameGraphState final {
  LX_core::FrameGraph frameGraph;
  LX_core::CompiledFrameGraph compiledFrameGraph;
  std::unordered_map<LX_core::StringID,
                     LX_core::RenderWorkBuildContext::PassPreparationFacts,
                     LX_core::StringID::Hash>
      baseFacts;
};
```

If the exact map type differs, use the existing `m_basePassPreparationFacts` type by `decltype(m_basePassPreparationFacts)`.

- [ ] **Step 5: Read back and write each target**

For each target:

```text
hdr.color -> hdr_color.exr and hdr_color_preview.png
debug.ldr.linear -> tone_mapped_linear.exr and tone_mapped_linear_preview.png
debug.final.srgb -> srgb_attachment.png
debug.final.unorm_manual_srgb -> unorm_manual_srgb.png
debug.ramp.srgb -> ramp_srgb_attachment.png
debug.ramp.unorm_manual_srgb -> ramp_unorm_manual_srgb.png
```

Rules:

- EXR outputs use `writeRgba32fExr`.
- Preview PNGs use `writeToneMappedPng`.
- Final LDR and ramp PNGs use `writeRawRgba8Png`.
- LDR PNG export must not call `writeToneMappedPng`.

- [ ] **Step 6: Add ramp probe records**

Sample the center of the four fixed vertical bands:

```cpp
struct ProbeSpec {
  const char *label;
  float x;
  u32 expected;
};
const ProbeSpec probes[] = {
    {"black", 0.1f, 0u},
    {"gray18", 0.3f, 118u},
    {"half", 0.5f, 188u},
    {"white", 0.7f, 255u},
};
```

For every ramp target, compute:

```cpp
const u32 px = std::min(width - 1u, static_cast<u32>(probe.x * width));
const u32 py = height / 2u;
const usize i = (static_cast<usize>(py) * width + px) * 4u;
```

Store `red`, `green`, `blue`, and `expected` in `result.probes`.

- [ ] **Step 7: Run build and command smoke locally**

Run:

```bash
ninja lxe_editor test_shader_compiler test_render_resource_parsers
xvfb-run -a ./src/editor/lxe_editor --api-enable --api-host 127.0.0.1 --api-port 0
```

For the manual command smoke, use the existing API tool or console to run:

```text
render debug export-path color-transfer /editor_cam artifacts/debug/color-transfer/manual
```

Expected: command returns JSON containing `manifestPath`, and the output directory contains the bundle.

- [ ] **Step 8: Commit**

Run:

```bash
git add src/backend/vulkan/vulkan_post_process_builder.hpp \
  src/backend/vulkan/vulkan_post_process_builder.cpp \
  src/backend/vulkan/vulkan_realtime_renderer.cpp \
  src/backend/vulkan/vulkan_renderer_types.hpp \
  src/infra/image/rgba_image_io.hpp \
  src/infra/image/rgba_image_io.cpp
git commit -m "feat: export debug color transfer bundle"
```

---

### Task 5: Add Python Smoke Validation

**Files:**
- Modify: `src/tools/lxe_realtime_render/lxe_realtime_render.py`
- Modify: `src/test/integration/test_helmet_standard_pbr_realtime_smoke.py`

- [ ] **Step 1: Add failing smoke test**

In `test_helmet_standard_pbr_realtime_smoke.py`, add:

```python
    def test_debug_color_transfer_export_bundle_has_ramp_proof(self) -> None:
        scene = (
            self.source_dir
            / "assets"
            / "scenes"
            / "generated"
            / "helmet_standard_pbr.scene.yaml"
        )
        command = [
            sys.executable,
            str(
                self.source_dir
                / "src"
                / "tools"
                / "lxe_realtime_render"
                / "lxe_realtime_render.py"
            ),
            "--scene",
            str(scene),
            "--profile",
            "preview",
            "--xvfb",
            "--debug-color-transfer",
            "--require-debug-color-transfer",
            "--project-name",
            "codex_test_debug_color_transfer",
        ]
        if self.editor:
            command.extend(["--editor", self.editor])

        result = subprocess.run(
            command,
            cwd=self.source_dir,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=120,
            check=False,
        )
        if result.returncode != 0:
            self.fail(
                "debug color-transfer smoke failed\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )

        payload = json.loads(result.stdout.strip())
        debug_payload = payload["debugColorTransfer"]
        self.assertTrue(debug_payload["manifestPath"])
        self.assertGreaterEqual(len(debug_payload["targets"]), 6)
        probe_labels = {
            probe["label"]
            for probe in debug_payload["manifest"].get("probes", [])
        }
        self.assertIn("gray18", probe_labels)
        self.assertIn("half", probe_labels)
```

Run it once:

```bash
python3 src/test/integration/test_helmet_standard_pbr_realtime_smoke.py \
  --source-dir . --editor build/src/editor/lxe_editor
```

Expected: fails because the tool arguments do not exist yet.

- [ ] **Step 2: Add tool arguments**

In `lxe_realtime_render.py::parse_args`, add:

```python
    parser.add_argument(
        "--debug-color-transfer",
        action="store_true",
        help="Run render debug export-path color-transfer after the scene loads.",
    )
    parser.add_argument(
        "--require-debug-color-transfer",
        action="store_true",
        help="Fail unless debug color-transfer manifest and ramp probes validate.",
    )
```

- [ ] **Step 3: Add manifest validation helper**

Add:

```python
def require_debug_color_transfer_bundle(root: Path, structured: str) -> dict[str, object]:
    payload = json.loads(structured)
    manifest_path = resolved_result_path(root, str(payload.get("manifestPath", "")))
    if not manifest_path.is_file():
        raise RuntimeError(f"debug color-transfer manifest missing: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    required_files = [
        "hdr_color.exr",
        "hdr_color_preview.png",
        "tone_mapped_linear.exr",
        "tone_mapped_linear_preview.png",
        "srgb_attachment.png",
        "unorm_manual_srgb.png",
        "ramp_srgb_attachment.png",
        "ramp_unorm_manual_srgb.png",
    ]
    output_dir = manifest_path.parent
    for name in required_files:
        path = output_dir / name
        if not path.is_file():
            raise RuntimeError(f"debug color-transfer output missing: {path}")
    probes = manifest.get("probes", [])
    if not isinstance(probes, list) or not probes:
        raise RuntimeError("debug color-transfer manifest has no probes")
    for probe in probes:
        expected = int(probe["expected"])
        for channel in ("red", "green", "blue"):
            actual = int(probe[channel])
            if abs(actual - expected) > 8:
                raise RuntimeError(
                    f"debug color-transfer probe mismatch {probe['target']} "
                    f"{probe['label']} {channel}: actual={actual} expected={expected}"
                )
    payload["manifest"] = manifest
    return payload
```

- [ ] **Step 4: Run command in main flow**

After the existing `realtime-render run` validation in `main`, add:

```python
            if args.debug_color_transfer:
                export_dir = (
                    root
                    / "artifacts"
                    / "debug"
                    / "color-transfer"
                    / args.project_name
                )
                debug_response = editor_command(
                    args.api_host,
                    api_port,
                    token,
                    "render debug export-path color-transfer "
                    f"/editor_cam {quote_command_token(str(export_dir))}",
                    args.timeout_sec,
                )
                debug_structured = debug_response.get("structuredJson", "")
                if not isinstance(debug_structured, str) or not debug_structured:
                    raise RuntimeError(
                        "debug color-transfer export did not return structured output"
                    )
                debug_payload = require_debug_color_transfer_bundle(
                    root, debug_structured
                )
                payload["debugColorTransfer"] = debug_payload
```

- [ ] **Step 5: Run smoke test**

Run:

```bash
ninja lxe_editor
python3 src/test/integration/test_helmet_standard_pbr_realtime_smoke.py \
  --source-dir . --editor build/src/editor/lxe_editor
```

Expected: pass under Xvfb when the local Vulkan environment is available.

- [ ] **Step 6: Commit**

Run:

```bash
git add src/tools/lxe_realtime_render/lxe_realtime_render.py \
  src/test/integration/test_helmet_standard_pbr_realtime_smoke.py
git commit -m "test: smoke debug color transfer export"
```

---

### Task 6: Documentation, Verification, And First Diagnostic Run

**Files:**
- Modify: `notes/subsystems/vulkan-backend.md`
- No source edits unless verification exposes an implementation bug in Tasks 1-5.

- [ ] **Step 1: Document the workflow**

In `notes/subsystems/vulkan-backend.md`, add a short subsection after the display color boundary section:

```markdown
### Debug color-transfer export

`render debug export-path color-transfer [camera-path] [out-dir]` runs a
diagnostic render path that exports HDR, tone-mapped linear, sRGB-attachment,
UNORM manual-sRGB, and fixed ramp targets into one bundle. This command is for
root-cause localization. It is not a production color-management policy.

The important rule is that raw LDR PNG outputs from this bundle are written
without an extra CPU gamma pass. If `debug.final.srgb` is dark while
`debug.final.unorm_manual_srgb` is correct, the next fix must inspect attachment
format, dynamic rendering pipeline format, image view format, or readback. If
the ramp targets agree but the helmet targets diverge, the bug is probably in
PBR/tone mapping/input data rather than Vulkan sRGB attachment conversion.
```

- [ ] **Step 2: Run focused tests**

Run:

```bash
ninja test_render_resource_parsers test_shader_compiler \
  test_realtime_render_profile_commands test_lxe_editor_api_service
./build/src/test/test_render_resource_parsers
./build/src/test/test_shader_compiler
./build/src/test/test_realtime_render_profile_commands
./build/src/test/test_lxe_editor_api_service
```

Expected: all pass.

- [ ] **Step 3: Run general headless tests**

Run:

```bash
ninja BuildTest
ctest --output-on-failure -L auto -LE requires_video_device
```

Expected: pass without new warnings in touched targets.

- [ ] **Step 4: Run video-device smoke**

Run:

```bash
xvfb-run -a ctest --output-on-failure -L requires_video_device
```

Expected: pass. If unrelated video-device tests fail due environment, record the exact failing test and stderr; do not call the implementation complete without the debug color-transfer smoke passing locally.

- [ ] **Step 5: Run the debug export manually and inspect artifacts**

Run:

```bash
python3 src/tools/lxe_realtime_render/lxe_realtime_render.py \
  --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml \
  --profile preview \
  --xvfb \
  --debug-color-transfer \
  --require-debug-color-transfer \
  --project-name codex_debug_color_transfer_manual
```

Open these images with local inspection:

```text
artifacts/debug/color-transfer/codex_debug_color_transfer_manual/hdr_color_preview.png
artifacts/debug/color-transfer/codex_debug_color_transfer_manual/tone_mapped_linear_preview.png
artifacts/debug/color-transfer/codex_debug_color_transfer_manual/srgb_attachment.png
artifacts/debug/color-transfer/codex_debug_color_transfer_manual/unorm_manual_srgb.png
artifacts/debug/color-transfer/codex_debug_color_transfer_manual/ramp_srgb_attachment.png
artifacts/debug/color-transfer/codex_debug_color_transfer_manual/ramp_unorm_manual_srgb.png
```

Write down the first contradictory boundary in the final report:

```text
hdr.color status:
tone_mapped_linear status:
srgb_attachment vs unorm_manual_srgb:
ramp_srgb_attachment vs ramp_unorm_manual_srgb:
selected surface format:
selected color space:
pipeline color format:
image view format:
next suspected boundary:
```

- [ ] **Step 6: Run source audit**

Run:

```bash
rg -n "debug_color_transfer|export-path color-transfer|outputEncodingMode|BGRA8Srgb|RGBA8Srgb" \
  assets/render_paths src/backend src/core src/editor src/test
```

Expected: hits are the debug graph/shaders, renderer export implementation,
command wiring, tests, and existing sRGB output encoding logic. There should be
no second public render graph model and no editor-owned renderer path.

- [ ] **Step 7: Commit docs and any verification-only test adjustments**

Run:

```bash
git add notes/subsystems/vulkan-backend.md
git commit -m "docs: document debug color transfer export"
```

Do not include generated artifacts under `artifacts/` in the commit.

---

## Plan Self-Review

Spec coverage:

- Dedicated debug render path: Task 1.
- Editor-accessible export workflow: Tasks 2-3.
- Backend/core ownership of render/export logic: Tasks 2 and 4.
- Export bundle and manifest facts: Task 4.
- Ramp proof and no-extra-gamma LDR output: Tasks 2, 4, and 5.
- Parser, command, Vulkan/headless smoke, source audit: Tasks 1, 3, 5, and 6.
- Documentation that this is diagnosis before fix: Task 6.

Scope check:

- This plan does not choose the production color fix.
- This plan does not alter PBR, material parsing, IBL, or the helmet scene.
- This plan does not introduce a second render graph schema.

Type consistency:

- Public request/result names are `VulkanDebugColorTransferExportRequest` and
  `VulkanDebugColorTransferExportResult`.
- Editor command is exactly `render debug export-path color-transfer`.
- Debug graph target names match the spec: `debug.ldr.linear`,
  `debug.final.srgb`, `debug.final.unorm_manual_srgb`, `debug.ramp.srgb`, and
  `debug.ramp.unorm_manual_srgb`.
