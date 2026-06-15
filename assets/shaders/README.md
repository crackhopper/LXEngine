# Shader Path Conventions (Vulkan)

## Source GLSL files
Realtime RenderPath GLSL sources live under render-path-specific directories:

- `assets/shaders/glsl/render_paths/Forward/*.vert`
- `assets/shaders/glsl/render_paths/Forward/*.frag`
- `assets/shaders/glsl/render_paths/Deferred/*.vert`
- `assets/shaders/glsl/render_paths/Deferred/*.frag`
- `assets/shaders/glsl/render_paths/Post/*.vert`
- `assets/shaders/glsl/render_paths/Post/*.frag`
- `assets/shaders/glsl/render_paths/Debug/*.vert`
- `assets/shaders/glsl/render_paths/Debug/*.frag`

Example:
- `assets/shaders/glsl/render_paths/Forward/pbr.vert`
- `assets/shaders/glsl/render_paths/Forward/pbr.frag`
- `assets/shaders/glsl/render_paths/Deferred/pbr_gbuffer.vert`
- `assets/shaders/glsl/render_paths/Deferred/pbr_gbuffer.frag`
- `assets/shaders/glsl/render_paths/Post/post_process.vert`
- `assets/shaders/glsl/render_paths/Post/post_process.frag`
- `assets/shaders/glsl/render_paths/Debug/debug_overlay.vert`
- `assets/shaders/glsl/render_paths/Debug/debug_overlay.frag`

`assets/shaders/glsl/techniques/OfflineRT` remains under `techniques/` until
the OfflineRT migration owned by `REQ-073-g/h`.

RenderPathGraph pass shader URIs are strict: graph-authored pass shaders must
use `render_paths/...` names, such as `render_paths/Post/post_process`. Root
short names like `post_process` and direct source paths like
`assets/shaders/glsl/render_paths/Post/post_process.frag` are rejected by the
resolver.

## Generated SPIR-V files
The Vulkan backend loads shaders from the runtime asset tree:

- `assets/shaders/glsl/render_paths/Forward/<shaderName>.vert.spv`
- `assets/shaders/glsl/render_paths/Forward/<shaderName>.frag.spv`
- `assets/shaders/glsl/render_paths/Deferred/<shaderName>.vert.spv`
- `assets/shaders/glsl/render_paths/Deferred/<shaderName>.frag.spv`
- `assets/shaders/glsl/render_paths/Post/<shaderName>.vert.spv`
- `assets/shaders/glsl/render_paths/Post/<shaderName>.frag.spv`
- `assets/shaders/glsl/render_paths/Debug/<shaderName>.vert.spv`
- `assets/shaders/glsl/render_paths/Debug/<shaderName>.frag.spv`

So for `shaderName = pbr`, the loader expects:

- `assets/shaders/glsl/render_paths/Forward/pbr.vert.spv`
- `assets/shaders/glsl/render_paths/Forward/pbr.frag.spv`

For deferred `shaderName = pbr_gbuffer`, the loader expects:

- `assets/shaders/glsl/render_paths/Deferred/pbr_gbuffer.vert.spv`
- `assets/shaders/glsl/render_paths/Deferred/pbr_gbuffer.frag.spv`

## Build target
The `CompileShaders` CMake target uses `glslc` to compile the GLSL sources and
generate the `.spv` outputs under:

- `<build>/assets/shaders/glsl/`
