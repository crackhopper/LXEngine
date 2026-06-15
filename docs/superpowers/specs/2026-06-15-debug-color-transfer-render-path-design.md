# Debug Color Transfer Render Path Design

Date: 2026-06-15

## Stage

This spec defines a focused render-debug tool for color-transfer and final
attachment diagnosis. It is not a rendering fix by itself.

The immediate problem is that the live editor can show the helmet scene as
nearly black while HDR dumps and CPU tone-mapped smoke outputs are visible. A
temporary shader experiment that forced manual gamma made the editor visible,
but that only proves a color-transfer symptom. It does not prove the correct
root cause.

The tool must make the color pipeline observable before any final fix is
chosen.

## Current Facts

The intended display pipeline is:

```text
Forward / DeferredLighting
  -> linear HDR color, usually RGBA16Float, values may exceed 1
PostProcess tone mapping
  -> display-linear LDR, clamped or compressed to 0..1
Final transfer
  -> either Vulkan sRGB attachment encoding or shader-side manual encoding
```

Vulkan's framebuffer rules make the final transfer format-dependent:

- If the color attachment numeric format is sRGB, Vulkan converts final RGB
  values into nonlinear sRGB before writing the attachment.
- If the color attachment numeric format is not sRGB, Vulkan writes the final
  RGB values unchanged apart from normal format conversion and quantization.
- `VkSurfaceFormatKHR::format` and `VkSurfaceFormatKHR::colorSpace` are related
  but separate facts. The attachment write conversion is controlled by the
  `VkFormat` sRGB family, not by `VkColorSpaceKHR` alone.

The current code intends this pairing:

| Final target format | Shader final output | Reason |
|---|---|---|
| `RGBA8Srgb` / `BGRA8Srgb` | linear LDR | Vulkan performs linear-to-sRGB on attachment write |
| `RGBA8` / `BGRA8` | sRGB-encoded LDR | UNORM attachment stores the shader value unchanged |

The current editor command surface already exposes:

- `render debug dump <target> [camera-path] [path]`
- `render debug stats <target>`
- `render debug live-stats`

Those commands are useful but insufficient for the current failure because they
do not prove the complete final transfer contract:

- selected surface `VkFormat`;
- selected surface `VkColorSpaceKHR`;
- image view format;
- dynamic rendering pipeline color format;
- pass target format;
- attachment contract format;
- actual post-process output encoding mode;
- known-value ramp behavior.

Existing `assets/render_paths/*.render-path.yaml` files are still the graph
truth source. Debugging must extend that system, not introduce a second public
render graph model.

## Goal

Add a dedicated debug render path and editor-accessible export workflow that
captures the important color-transfer stages in one run.

The export must answer these questions without further shader experiments:

1. Is HDR lighting nonblack and carrying high dynamic range values?
2. Does tone mapping produce a meaningful linear `0..1` image?
3. Does an sRGB color attachment encode linear shader output correctly?
4. Does a UNORM attachment with shader-side manual sRGB encoding match the sRGB
   attachment result?
5. Do known constant/ramp outputs prove attachment conversion independently of
   the helmet material and lighting path?
6. Do live swapchain format facts match the format assumptions used by
   PostProcess?

## Non-Goals

This slice does not:

- change the production display policy;
- choose SRGB swapchain versus UNORM fallback as a fix;
- alter PBR lighting, material parsing, or tone mapping curves;
- add a second RenderPathGraph schema;
- move render orchestration into `src/editor`;
- replace RenderDoc or implement a general frame capture system;
- require an environment, IBL, skybox, or room scene.

## Architecture

The implementation should add one debug graph asset:

```text
assets/render_paths/debug_color_transfer.render-path.yaml
```

The graph uses the existing `schema: lxe.render-path-graph.v1` contract. It
declares normal pass sources, targets, render state, and attachment formats.

The editor exposes the workflow as a command:

```text
render debug export-path color-transfer [camera-path] [out-dir]
```

The editor command layer must only parse command arguments and return the
structured result. It must call a renderer/backend hook for the actual export.
The export logic belongs in backend/core-facing render code so the same path can
later be reused by headless realtime smoke and offline comparisons.

The live editor scene must remain loaded after the export. The debug graph may
be used for a one-shot diagnostic frame, but it must not permanently replace the
scene's active production render path unless a later explicit command requests
that.

## Debug Render Path

The debug render path should produce these logical targets:

| Target | Format | Purpose |
|---|---|---|
| `hdr.color` | `RGBA16Float` | Raw linear HDR lighting output |
| `debug.ldr.linear` | `RGBA16Float` | Tone-mapped linear LDR result, no gamma |
| `debug.final.srgb` | `RGBA8Srgb` or `BGRA8Srgb` | Linear shader output written through an sRGB attachment |
| `debug.final.unorm_manual_srgb` | `RGBA8` or `BGRA8` | Shader-side sRGB encoding written through UNORM |
| `debug.ramp.srgb` | `RGBA8Srgb` or `BGRA8Srgb` | Known constants/ramp, linear shader output |
| `debug.ramp.unorm_manual_srgb` | `RGBA8` or `BGRA8` | Known constants/ramp, shader-side sRGB encoding |

The graph should keep the first pass equivalent to the normal direct helmet
path where practical:

```text
Forward
  -> hdr.color, depth.main
```

Then add diagnostic fullscreen passes:

```text
DebugToneMapLinear
  hdr.color -> debug.ldr.linear
  tone mapping only, no final sRGB encoding

DebugSrgbAttachment
  debug.ldr.linear -> debug.final.srgb
  shader outputs linear LDR

DebugUnormManualSrgb
  debug.ldr.linear -> debug.final.unorm_manual_srgb
  shader outputs sRGB-encoded LDR

DebugRampSrgb
  constants/ramp -> debug.ramp.srgb
  shader outputs known linear values

DebugRampUnormManualSrgb
  constants/ramp -> debug.ramp.unorm_manual_srgb
  shader outputs known sRGB-encoded values
```

The ramp pass must contain known probe values. At minimum:

- black: linear `0.0`;
- mid gray: linear `0.18`;
- half: linear `0.5`;
- white: linear `1.0`;
- a horizontal linear gradient from `0.0` to `1.0`.

Expected approximate 8-bit sRGB bytes for the fixed probes are:

| Linear input | sRGB encoded byte, approximate |
|---|---:|
| `0.0` | `0` |
| `0.18` | `118` |
| `0.5` | `188` |
| `1.0` | `255` |

The implementation should use tolerance rather than exact equality because the
precise sRGB transfer function and readback path may differ by a few LSBs.

## Export Bundle

The command should create a directory containing:

```text
manifest.json
hdr_color.exr
hdr_color_preview.png
tone_mapped_linear.exr
tone_mapped_linear_preview.png
srgb_attachment.png
unorm_manual_srgb.png
ramp_srgb_attachment.png
ramp_unorm_manual_srgb.png
```

`hdr_color.exr` and `tone_mapped_linear.exr` preserve linear values. Their PNG
previews are for human inspection only and must state the preview transform in
the manifest.

The final `*.png` outputs are byte-level previews of the final LDR targets. The
export must not accidentally apply an extra gamma pass when writing
`srgb_attachment.png` or `unorm_manual_srgb.png`; otherwise the diagnostic would
hide the actual attachment result.

## Manifest

`manifest.json` must include:

- scene name and camera path;
- render path graph URI;
- selected `VkSurfaceFormatKHR.format`;
- selected `VkSurfaceFormatKHR.colorSpace`;
- swapchain image view format;
- swapchain target `ImageFormat`;
- each debug pass id;
- each pass logical target;
- each pass attachment contract `ImageFormat`;
- each pass actual Vulkan attachment `VkFormat`;
- each pass dynamic rendering pipeline color format;
- each pass shader output encoding mode: `linear` or `srgb`;
- tone mapping mode, exposure, gamma, bloom intensity;
- stats for every exported target: width, height, format, min, max, mean,
  nonzero ratio;
- fixed probe samples for ramp targets, including raw byte values and expected
  approximate values.

The manifest is the main evidence file. Visual PNGs are supporting evidence.

## Diagnostic Interpretation

The tool should make these outcomes distinguishable:

| Observation | Likely boundary to inspect |
|---|---|
| `hdr_color.exr` is black | Forward/material/light/camera path |
| `hdr_color.exr` is high range but `tone_mapped_linear.exr` is black or still high range | Tone mapping shader or UBO |
| `tone_mapped_linear.exr` is good but `srgb_attachment.png` is dark | sRGB attachment conversion, attachment format, pipeline format, or readback |
| `srgb_attachment.png` and `unorm_manual_srgb.png` differ strongly | Final transfer path mismatch |
| `ramp_srgb_attachment.png` is dark while manual ramp is correct | sRGB attachment conversion path is not actually active |
| ramp outputs are correct but helmet output is wrong | PBR/tone mapping/input data issue, not attachment conversion |
| manifest says output mode is linear but attachment is UNORM | format propagation or UBO selection bug |
| manifest says output mode is sRGB but attachment is SRGB | double-encoding bug |

This is the intended decision point. The next fix must target the first boundary
that contradicts the manifest and exported images.

## API Boundary

Add a backend-facing request/response shape similar to:

```text
DebugRenderPathExportRequest
  graphUri
  cameraPath
  outputDirectory
  exportColorTransferBundle

DebugRenderPathExportResult
  manifestPath
  exportedFiles
  perTargetStats
```

Exact names can follow existing renderer naming, but the boundary must stay out
of editor UI code. `src/editor` should only expose the command and display the
structured result.

If a renderer implementation cannot export a requested target, it must return a
clear diagnostic. It must not silently skip an output and still report success.

## Tests And Audits

Required parser/asset tests:

- `debug_color_transfer.render-path.yaml` parses through the existing
  RenderPathGraph parser.
- `RGBA8Srgb` / `BGRA8Srgb` attachment formats remain accepted and retained in
  attachment contracts.
- Unknown or misspelled debug graph fields fail through the existing strict
  parser behavior.

Required command tests:

- `render debug export-path color-transfer` reports a clear error when the
  renderer hook is unavailable.
- The command returns structured JSON with at least `manifestPath` and the
  exported file list when the hook succeeds.

Required Vulkan/headless smoke:

- Run the debug color-transfer export under `xvfb-run`.
- Assert the export bundle exists.
- Assert `hdr_color.exr` has nonzero HDR data for the helmet scene.
- Assert `tone_mapped_linear.exr` has values in or near the expected LDR range.
- Assert ramp probe bytes in `ramp_srgb_attachment.png` are close to the
  expected sRGB-encoded bytes.
- Assert `ramp_srgb_attachment.png` and `ramp_unorm_manual_srgb.png` agree
  within a small tolerance at the fixed probes.

Required source audit:

```text
rg -n "debug_color_transfer|export-path color-transfer|outputEncodingMode|BGRA8Srgb|RGBA8Srgb" \
  assets/render_paths src/backend src/core src/editor src/test
```

The audit should show that the debug path uses the existing render graph,
renderer, and command surfaces rather than a parallel editor-owned renderer.

## Verification

The implementation is not complete until these commands, or their current build
equivalents, pass:

```bash
ninja test_render_resource_parsers
ninja test_shader_compiler
ninja BuildTest
ctest --output-on-failure -L auto -LE requires_video_device
xvfb-run -a ctest --output-on-failure -L requires_video_device
```

For the current helmet issue, the implementer must also open and visually
compare:

- `hdr_color_preview.png`;
- `tone_mapped_linear_preview.png`;
- `srgb_attachment.png`;
- `unorm_manual_srgb.png`;
- the current live editor view or screenshot.

The completion report must explain which boundary is wrong before proposing or
applying the final render fix.

## Risks

The largest risk is accidentally building a debug path that applies extra CPU
gamma during PNG export. That would make a broken attachment path look correct.
The manifest must state whether an output is raw LDR bytes, CPU preview, or EXR
linear data.

Another risk is testing only offscreen debug attachments while the live
swapchain path remains broken. The export must record live swapchain format
facts even when the controlled sRGB target is offscreen.

The architecture risk is creating editor-specific rendering logic. The editor
command is only a frontend to backend/core debug export. The render path remains
a normal RenderPathGraph asset.

The process risk is treating this tool as the fix. It is evidence collection.
Any later code change must name the broken boundary proven by the export.

