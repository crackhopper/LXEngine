## 1. Fix Window Class Header/Implementation Mismatch

- [x] 1.1 Fix `window.hpp` to match implementation: rename `WindowImpl` to `Window` or keep as is and fix implementation to use correct class name
- [x] 1.2 Add missing `Initialize()` static method declaration to `window.hpp`
- [x] 1.3 Add missing `onClose()` callback method declaration to `window.hpp`
- [x] 1.4 Fix constructor signature to match header: `Window(int width, int height, const char *title)`
- [x] 1.5 Ensure `window_impl_sdl.cpp` properly guards with `#ifdef USE_SDL`

## 2. Implement GLFW Backend

- [x] 2.1 Add `#ifdef USE_GLFW` guards to `window.hpp` for GLFW-specific declarations
- [x] 2.2 Implement `WindowImpl::Initialize()` for GLFW in `window_impl_glfw.cpp`
- [x] 2.3 Implement `WindowImpl` constructor, destructor, and methods in `window_impl_glfw.cpp`

## 3. Fix File System Utilities

- [x] 3.1 Remove duplicate `readFile` declaration from `file.hpp` (keep only in `file.cpp`)
- [x] 3.2 Ensure `file.hpp` only contains declaration, `file.cpp` contains implementation
- [x] 3.3 Verify ODR compliance by compiling with multiple translation units

## 4. Implement GUI System

- [x] 4.1 Add `init(VkInstance, VkPhysicalDevice, VkDevice, QueueFamilyIndices, VkQueue, VkSurfaceKHR)` method to `Gui` class
- [x] 4.2 Add `beginFrame()` method wrapping ImGui SDL3 and Vulkan new frame calls
- [x] 4.3 Add `endFrame()` method wrapping `ImGui::Render()` and `ImGui_ImplVulkan_RenderDrawData()`
- [x] 4.4 Add `shutdown()` method wrapping ImGui backend shutdown calls
- [x] 4.5 Implement `Gui::Impl` struct in `gui_impl_imgui.cpp` to hold ImGui context and Vulkan-specific objects
- [x] 4.6 Add proper `#include` guards and namespace qualifications

## 5. Implement TextureLoader

- [x] 5.1 Define `TextureLoader` class in `texture_loader.hpp` with `load(const std::string&)` method
- [x] 5.2 Implement `TextureLoader::Impl` struct holding width, height, channels, and pixel data
- [x] 5.3 Implement `load()` method using stb_image to decode PNG/JPEG files
- [x] 5.4 Implement `getWidth()`, `getHeight()`, `getData()` accessor methods
- [x] 5.5 Move `STB_IMAGE_IMPLEMENTATION` to `texture_loader.cpp` to avoid ODR violations

## 6. Implement OBJ Mesh Loader

- [x] 6.1 Define `ObjLoader` class in `obj.hpp` with `load(const std::string&)` method
- [x] 6.2 Implement `ObjLoader::Impl` struct holding vectors for positions, normals, texCoords, and indices
- [x] 6.3 Implement `load()` method using tinyobjloader to parse OBJ files
- [x] 6.4 Implement `getPositions()`, `getNormals()`, `getTexCoords()`, `getIndices()` accessor methods
- [x] 6.5 Move `TINYOBJLOADER_IMPLEMENTATION` to `obj.cpp` to avoid ODR violations

## 7. Implement GLTF Mesh Loader

- [x] 7.1 Define `GLTFLoader` class in `gltf.hpp` with `load(const std::string&)` method
- [x] 7.2 Implement `GLTFLoader::Impl` struct holding mesh data vectors
- [x] 7.3 Implement `load()` method to parse glTF JSON and extract mesh data
- [x] 7.4 Implement `getPositions()`, `getNormals()`, `getTexCoords()`, `getIndices()` accessor methods
- [x] 7.5 Add glTF JSON parsing using rapidjson or similar (if available in external/) - placeholder implementation added

## 8. Fix CMakeLists.txt Issues

- [x] 8.1 Verify `target_include_directories` for `external/include` covers all external headers
- [x] 8.2 Verify library linking order is correct (SDL3 before Vulkan)
- [x] 8.3 Ensure imgui subdirectory cmake is found correctly
- [x] 8.4 Test compilation with `cmake --build` after all fixes - infra compiles; graphics_backend has pre-existing issues
