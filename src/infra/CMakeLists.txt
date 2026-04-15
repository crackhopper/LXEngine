option(USE_SDL "Use SDL2 as the window backend" ON)
option(USE_GLFW "Use GLFW as the window backend" OFF)

# -------------------------------
# shaderc (runtime GLSL -> SPIR-V)
# -------------------------------
# Windows: set SHADERC_DIR to the directory containing include/ and lib/
# Linux: auto-detected via find_library, or set SHADERC_DIR to override
option(SHADERC_DIR "Path to shaderc installation (contains include/ and lib/)" "")

if(SHADERC_DIR)
  find_library(SHADERC_LIB NAMES shaderc_shared shaderc shaderc_combined
    PATHS "${SHADERC_DIR}/lib" NO_DEFAULT_PATH)
  set(SHADERC_INCLUDE_DIR "${SHADERC_DIR}/include")
else()
  find_library(SHADERC_LIB NAMES shaderc_shared shaderc shaderc_combined)
  # Try Vulkan SDK path
  if(NOT SHADERC_LIB AND DEFINED ENV{VULKAN_SDK})
    find_library(SHADERC_LIB NAMES shaderc_shared shaderc shaderc_combined
      PATHS "$ENV{VULKAN_SDK}/lib" "$ENV{VULKAN_SDK}/Lib")
    set(SHADERC_INCLUDE_DIR "$ENV{VULKAN_SDK}/include")
  endif()
endif()

if(NOT SHADERC_LIB)
  message(FATAL_ERROR "shaderc not found. Set SHADERC_DIR or install libshaderc-dev (Linux) / Vulkan SDK (Windows)")
endif()
message(STATUS "shaderc library: ${SHADERC_LIB}")

# shaderc_combined (static) requires SPIRV-Tools and SPIRV-Tools-opt
if(SHADERC_LIB MATCHES "shaderc_combined")
  if(SHADERC_DIR)
    find_library(SPIRV_TOOLS_LIB NAMES SPIRV-Tools PATHS "${SHADERC_DIR}/lib" NO_DEFAULT_PATH)
    find_library(SPIRV_TOOLS_OPT_LIB NAMES SPIRV-Tools-opt PATHS "${SHADERC_DIR}/lib" NO_DEFAULT_PATH)
  else()
    find_library(SPIRV_TOOLS_LIB NAMES SPIRV-Tools)
    find_library(SPIRV_TOOLS_OPT_LIB NAMES SPIRV-Tools-opt)
  endif()
  if(SPIRV_TOOLS_LIB)
    set(SHADERC_DEPS ${SPIRV_TOOLS_LIB} CACHE INTERNAL "shaderc transitive deps")
    if(SPIRV_TOOLS_OPT_LIB)
      set(SHADERC_DEPS ${SPIRV_TOOLS_LIB} ${SPIRV_TOOLS_OPT_LIB} CACHE INTERNAL "shaderc transitive deps")
    endif()
    message(STATUS "SPIRV-Tools: ${SPIRV_TOOLS_LIB}")
  endif()
else()
  set(SHADERC_DEPS "" CACHE INTERNAL "shaderc transitive deps")
endif()

# -------------------------------
# SPIRV-Cross (reflection)
# -------------------------------
# Windows: set SPIRV_CROSS_DIR to the directory containing include/ and lib/
# Linux: auto-detected via find_library, or set SPIRV_CROSS_DIR to override
# If not found, falls back to FetchContent to build from source.
option(SPIRV_CROSS_DIR "Path to SPIRV-Cross installation (contains include/ and lib/)" "")

set(SPIRV_CROSS_FOUND FALSE)
set(_SPIRV_CORE_PATH "")
set(_SPIRV_GLSL_PATH "")

if(SPIRV_CROSS_DIR)
  find_library(_SPIRV_CORE_PATH NAMES spirv-cross-core
    PATHS "${SPIRV_CROSS_DIR}/lib" NO_DEFAULT_PATH)
  if(_SPIRV_CORE_PATH)
    find_library(_SPIRV_GLSL_PATH NAMES spirv-cross-glsl
      PATHS "${SPIRV_CROSS_DIR}/lib" NO_DEFAULT_PATH)
    set(SPIRV_CROSS_INCLUDE_DIR "${SPIRV_CROSS_DIR}/include")
    set(SPIRV_CROSS_FOUND TRUE)
  endif()
else()
  find_library(_SPIRV_CORE_PATH NAMES spirv-cross-core)
  if(_SPIRV_CORE_PATH)
    find_library(_SPIRV_GLSL_PATH NAMES spirv-cross-glsl)
    set(SPIRV_CROSS_FOUND TRUE)
  elseif(DEFINED ENV{VULKAN_SDK})
    find_library(_SPIRV_CORE_PATH NAMES spirv-cross-core
      PATHS "$ENV{VULKAN_SDK}/lib" "$ENV{VULKAN_SDK}/Lib")
    if(_SPIRV_CORE_PATH)
      find_library(_SPIRV_GLSL_PATH NAMES spirv-cross-glsl
        PATHS "$ENV{VULKAN_SDK}/lib" "$ENV{VULKAN_SDK}/Lib")
      set(SPIRV_CROSS_INCLUDE_DIR "$ENV{VULKAN_SDK}/include")
      set(SPIRV_CROSS_FOUND TRUE)
    endif()
  endif()
endif()

if(NOT SPIRV_CROSS_FOUND)
  message(STATUS "SPIRV-Cross not found locally, fetching from GitHub...")
  include(FetchContent)
  set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_ENABLE_GLSL ON CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
  set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
  fetchcontent_declare(
    spirv-cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG        vulkan-sdk-1.3.290.0
    GIT_SHALLOW    TRUE
  )
  fetchcontent_makeavailable(spirv-cross)
  set(LX_SPIRV_CROSS_USE_FETCH_TARGETS TRUE CACHE INTERNAL
      "Link SPIRV-Cross via FetchContent targets" FORCE)
  set(SPIRV_CROSS_CORE_LIB spirv-cross-core CACHE INTERNAL
      "SPIRV-Cross core (target name when using FetchContent)" FORCE)
  set(SPIRV_CROSS_GLSL_LIB spirv-cross-glsl CACHE INTERNAL
      "SPIRV-Cross glsl (target name when using FetchContent)" FORCE)
  set(SPIRV_CROSS_INCLUDE_DIR "${spirv-cross_SOURCE_DIR}" CACHE INTERNAL
      "SPIRV-Cross include dir" FORCE)
else()
  set(LX_SPIRV_CROSS_USE_FETCH_TARGETS FALSE CACHE INTERNAL
      "Link SPIRV-Cross via FetchContent targets" FORCE)
  set(SPIRV_CROSS_CORE_LIB "${_SPIRV_CORE_PATH}" CACHE INTERNAL
      "SPIRV-Cross core library path" FORCE)
  if(_SPIRV_GLSL_PATH)
    set(SPIRV_CROSS_GLSL_LIB "${_SPIRV_GLSL_PATH}" CACHE INTERNAL
        "SPIRV-Cross glsl library path" FORCE)
  endif()
  message(STATUS "SPIRV-Cross core: ${SPIRV_CROSS_CORE_LIB}")
  message(STATUS "SPIRV-Cross glsl: ${SPIRV_CROSS_GLSL_LIB}")
endif()

# -------------------------------
# Vulkan
# -------------------------------
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found")
endif()

# -------------------------------
# SDL3
# -------------------------------
if(WIN32)
    set(SDL3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/SDL3-3.3.6/cmake)
    find_package(SDL3 REQUIRED)
    if(NOT SDL3_FOUND)
        message(FATAL_ERROR "SDL3 not found")
    endif()
else()
    find_package(SDL3 QUIET)
    if(NOT SDL3_FOUND)
        include(FetchContent)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        fetchcontent_declare(
            SDL3
            URL https://github.com/libsdl-org/SDL/releases/download/prerelease-3.3.6/SDL3-3.3.6.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        fetchcontent_makeavailable(SDL3)
    endif()
endif()

# -------------------------------
# imgui - only needed backends
# -------------------------------
add_subdirectory(external/cmake/imgui)

set(INFRA_LIB LX_Infra CACHE INTERNAL "Infra library name")

# Explicitly list source files to avoid picking up example files and unwanted backends
set(INFRA_SOURCES
    material_loader/blinn_phong_material_loader.cpp
    gui/imgui_gui.cpp
    texture_loader/texture_loader.cpp
    mesh_loader/obj_mesh_loader.cpp
    mesh_loader/gltf_mesh_loader.cpp
    shader_compiler/shader_compiler.cpp
    shader_compiler/shader_reflector.cpp
    shader_compiler/compiled_shader.cpp
)

# Add window implementation based on backend
if(USE_SDL)
    list(APPEND INFRA_SOURCES window/sdl_window.cpp)
elseif(USE_GLFW)
    list(APPEND INFRA_SOURCES window/glfw_window.cpp)
endif()

add_library(${INFRA_LIB} ${INFRA_SOURCES})

target_link_libraries(${INFRA_LIB}
  PRIVATE
  ${CORE_LIB}
  Vulkan::Vulkan
  imgui
  ${SHADERC_LIB}
  ${SHADERC_DEPS}
)

# SPIRV-Cross: FetchContent → CMake targets (PUBLIC so final exes resolve symbols).
# System install → full library paths from find_library.
if(LX_SPIRV_CROSS_USE_FETCH_TARGETS)
  target_link_libraries(${INFRA_LIB} PUBLIC spirv-cross-core spirv-cross-glsl)
elseif(SPIRV_CROSS_CORE_LIB)
  target_link_libraries(${INFRA_LIB} PRIVATE ${SPIRV_CROSS_CORE_LIB})
  if(SPIRV_CROSS_GLSL_LIB)
    target_link_libraries(${INFRA_LIB} PRIVATE ${SPIRV_CROSS_GLSL_LIB})
  endif()
endif()

target_include_directories(${INFRA_LIB} PRIVATE external/include)
if(SHADERC_INCLUDE_DIR AND NOT "${SHADERC_INCLUDE_DIR}" STREQUAL "")
  target_include_directories(${INFRA_LIB} PRIVATE ${SHADERC_INCLUDE_DIR})
endif()
if(SPIRV_CROSS_INCLUDE_DIR AND NOT "${SPIRV_CROSS_INCLUDE_DIR}" STREQUAL "")
  target_include_directories(${INFRA_LIB} PRIVATE ${SPIRV_CROSS_INCLUDE_DIR})
endif()
target_include_directories(${INFRA_LIB} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(USE_SDL)
    target_compile_definitions(${INFRA_LIB} PRIVATE USE_SDL=1)
    target_link_libraries(${INFRA_LIB} PRIVATE SDL3::SDL3)
elseif(USE_GLFW)
    target_compile_definitions(${INFRA_LIB} PRIVATE USE_GLFW=1)
    target_link_libraries(${INFRA_LIB} PRIVATE GLFW::GLFW)
endif()
