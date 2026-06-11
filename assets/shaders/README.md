# Shader Path Conventions (Vulkan)

## Source GLSL files
GLSL sources live under technique-specific directories:

- `assets/shaders/glsl/techniques/Forward/*.vert`
- `assets/shaders/glsl/techniques/Forward/*.frag`

Example:
- `assets/shaders/glsl/techniques/Forward/pbr.vert`
- `assets/shaders/glsl/techniques/Forward/pbr.frag`

## Generated SPIR-V files
The Vulkan backend loads shaders from the runtime asset tree:

- `assets/shaders/glsl/techniques/Forward/<shaderName>.vert.spv`
- `assets/shaders/glsl/techniques/Forward/<shaderName>.frag.spv`

So for `shaderName = pbr`, the loader expects:

- `assets/shaders/glsl/techniques/Forward/pbr.vert.spv`
- `assets/shaders/glsl/techniques/Forward/pbr.frag.spv`

## Build target
The `CompileShaders` CMake target uses `glslc` to compile the GLSL sources and
generate the `.spv` outputs under:

- `<build>/assets/shaders/glsl/`
