## Why

The `src/infra` module has multiple compilation errors and missing implementations that prevent the project from building. The issues include header/source mismatches, empty stub classes, and undefined functions that must be resolved before the renderer can compile successfully.

## What Changes

- Fix `Window` class header/implementation mismatch between `window.hpp` and `window_impl_sdl.cpp`
- Implement missing `Gui` class methods in `gui.hpp` / `gui_impl_imgui.cpp`
- Implement `TextureLoader` class in `texture_loader.hpp`
- Implement `ObjLoader` class in `obj.hpp` / `obj.cpp`
- Implement `GLTFLoader` class in `gltf.hpp` / `gltf.cpp`
- Fix `readFile` ODR violation between `file.hpp` and `file.cpp`
- Add missing `Window::Initialize()` static method
- Add missing `Window::onClose()` callback method
- Ensure GLFW backend compiles when `USE_GLFW` is enabled
- Fix CMakeLists.txt to properly link all dependencies

## Capabilities

### New Capabilities
- `window-system`: Window creation and event handling abstraction for SDL3 and GLFW
- `gui-system`: Dear ImGui integration for debugging UI overlays
- `texture-loading`: STB Image-based texture loading from various formats
- `mesh-loading`: OBJ and GLTF mesh format support via tinyobjloader

### Modified Capabilities
- None - this is a bug fix change

## Impact

- **Affected code**: `src/infra/` directory (env, fs, gui, mesh_loader, texture_loader, window)
- **Dependencies**: SDL3, GLFW, imgui, stb_image, tinyobjloader
- **Build system**: CMakeLists.txt in src/infra requires corrections to include paths and linked libraries
