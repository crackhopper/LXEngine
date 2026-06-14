# Shader Path Conventions (Vulkan)

## Source GLSL files
Realtime RenderPath GLSL sources live under render-path-specific directories:

- `assets/shaders/glsl/render_paths/Forward/*.vert`
- `assets/shaders/glsl/render_paths/Forward/*.frag`
- `assets/shaders/glsl/render_paths/Deferred/*.vert`
- `assets/shaders/glsl/render_paths/Deferred/*.frag`

Example:
- `assets/shaders/glsl/render_paths/Forward/pbr.vert`
- `assets/shaders/glsl/render_paths/Forward/pbr.frag`
- `assets/shaders/glsl/render_paths/Deferred/pbr_gbuffer.vert`
- `assets/shaders/glsl/render_paths/Deferred/pbr_gbuffer.frag`

`assets/shaders/glsl/techniques/OfflineRT` remains under `techniques/` until
the OfflineRT migration owned by `REQ-073-g/h`.

## Generated SPIR-V files
The Vulkan backend loads shaders from the runtime asset tree:

- `assets/shaders/glsl/render_paths/Forward/<shaderName>.vert.spv`
- `assets/shaders/glsl/render_paths/Forward/<shaderName>.frag.spv`
- `assets/shaders/glsl/render_paths/Deferred/<shaderName>.vert.spv`
- `assets/shaders/glsl/render_paths/Deferred/<shaderName>.frag.spv`

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
