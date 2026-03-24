# Shader Path Conventions (Vulkan)

## Source GLSL files
GLSL sources live under:

- `shaders/glsl/*.vert` (vertex shaders)
- `shaders/glsl/*.frag` (fragment shaders)

Example:
- `shaders/glsl/blinnphong_0.vert`
- `shaders/glsl/blinnphong_0.frag`

## Generated SPIR-V files
The Vulkan backend loads shaders from this exact relative path:

- `shaders/glsl/<shaderName>.vert.spv`
- `shaders/glsl/<shaderName>.frag.spv`

So for `shaderName = blinnphong_0`, the loader expects:

- `shaders/glsl/blinnphong_0.vert.spv`
- `shaders/glsl/blinnphong_0.frag.spv`

## Build target
The `CompileShaders` CMake target uses `glslc` to compile the GLSL sources and generate the `.spv` outputs under:

- `<build>/shaders/glsl/`

Make sure the executable working directory is `<build>` (or use the provided tests which auto-`cd` to the directory containing the SPIR-V files).

