## Context

The `src/infra` module is a platform abstraction layer providing window management, GUI rendering, file system utilities, and asset loading. It is built on top of SDL3 (primary) or GLFW (optional) for windowing, and integrates Dear ImGui for debug UI. External dependencies are located in `src/infra/external/`.

Current compilation barriers:

1. **`window.hpp` vs `window_impl_sdl.cpp` mismatch**: The header declares `LX_infra::WindowImpl` inheriting from `LX_core::Window`, but the implementation defines `struct Window::Impl` (not `WindowImpl`) with different constructors and members. The header lacks `Initialize()`, `onClose()`, and proper constructors.

2. **ODR violation in `readFile`**: `file.hpp` declares `std::vector<char> readFile(const std::string &filename)` but `file.cpp` implements it with a different signature using `const char*` or without proper namespace qualification, causing linking failures.

3. **`Gui` class is empty stub**: `gui.hpp` defines a bare `class Gui` with only an `Impl` pointer and no public methods, yet `gui_impl_imgui.cpp` includes ImGui headers but does not implement any `Gui` methods.

4. **`TextureLoader` is header-only stub**: `texture_loader.hpp` defines `STB_IMAGE_IMPLEMENTATION` but has no `TextureLoader` class definition.

5. **Empty mesh loaders**: `obj.hpp` and `gltf.hpp` are empty files, while `obj.cpp` includes `TINYOBJLOADER_IMPLEMENTATION` but has no implementation.

6. **`WindowImpl_glfw.cpp` exists but `window.hpp` lacks GLFW conditional declarations**: The GLFW backend will not compile due to missing `#ifdef USE_GLFW` guards and missing declarations.

## Goals / Non-Goals

**Goals:**
- Fix all compilation errors in `src/infra` so the module builds successfully
- Implement all stub classes with functional, minimal implementations
- Maintain compatibility with both SDL3 and GLFW backends
- Preserve the PImpl idiom used for SDL/GLFW abstraction

**Non-Goals:**
- This is not a feature enhancement — no new rendering capabilities
- No changes to the Vulkan backend or renderer core
- No changes to external dependencies (SDL3, GLFW, imgui, stb, tinyobjloader)

## Decisions

1. **Fix `Window` class hierarchy**: Rename implementation struct to match header, add missing `Initialize()` and `onClose()`, fix constructor signature. Keep PImpl pattern.

2. **Fix `readFile` signature**: Ensure declaration in `file.hpp` matches implementation in `file.cpp` exactly. Move implementation to header as inline or ensure single definition.

3. **Implement `Gui` class minimally**: Provide `init()`, `beginFrame()`, `endFrame()`, `shutdown()` methods that wrap ImGui backend initialization and rendering. Use PImpl to hide ImGui context.

4. **Implement `TextureLoader` class**: Create `TextureLoader` with `load(const std::string&)` method using stb_image. Store texture data (width, height, channels, pixel data).

5. **Implement `ObjLoader` and `GLTFLoader`**: Provide minimal loader classes with `load(const std::string&)` methods that parse OBJ/GLTF files and return loaded mesh data structures (positions, normals, indices).

6. **CMake corrections**: Ensure proper include paths for external headers (`external/include`), correct library linking order, and proper conditional compilation for SDL/GLFW.

## Risks / Trade-offs

- **Risk**: PImpl with SDL vs GLFW backends could diverge at runtime vs compile time. **Mitigation**: Both backends share identical public API; differences are hidden in `Impl` struct.
- **Risk**: stb_image and tinyobjloader are header-only libraries. **Mitigation**: Ensure `STB_IMAGE_IMPLEMENTATION` and `TINYOBJLOADER_IMPLEMENTATION` are defined in exactly one translation unit to avoid ODR violations.
- **Risk**: ImGui requires Vulkan or OpenGL integration. **Mitigation**: Use Vulkan implementation since renderer is Vulkan-based; initialize ImGui with Vulkan-specific functions.
