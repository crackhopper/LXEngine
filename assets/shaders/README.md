# Shader Path Conventions (Vulkan)

## Source GLSL files
GLSL sources live under:

- `assets/shaders/glsl/*.vert`
- `assets/shaders/glsl/*.frag`

Example:
- `assets/shaders/glsl/blinnphong_0.vert`
- `assets/shaders/glsl/blinnphong_0.frag`

## Generated SPIR-V files
The Vulkan backend loads shaders from the runtime asset tree:

- `assets/shaders/glsl/<shaderName>.vert.spv`
- `assets/shaders/glsl/<shaderName>.frag.spv`

So for `shaderName = blinnphong_0`, the loader expects:

- `assets/shaders/glsl/blinnphong_0.vert.spv`
- `assets/shaders/glsl/blinnphong_0.frag.spv`

## Build target
The `CompileShaders` CMake target uses `glslc` to compile the GLSL sources and
generate the `.spv` outputs under:

- `<build>/assets/shaders/glsl/`
