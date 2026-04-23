option(USE_SDL "Use SDL2 as the window backend" ON)
option(USE_GLFW "Use GLFW as the window backend" OFF)

set(LX_INFRA_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

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
# Windows/Linux: prefer vendored lightweight source checked into
# src/infra/external/SPIRV-Cross. Falls back to SPIRV_CROSS_DIR or system libs.
# Runtime downloading is intentionally disabled.
option(SPIRV_CROSS_DIR "Path to SPIRV-Cross installation (contains include/ and lib/)" "")

set(SPIRV_CROSS_FOUND FALSE)
set(LX_SPIRV_CROSS_LINK_TARGETS FALSE CACHE INTERNAL
    "Link SPIRV-Cross via vendored source targets" FORCE)
set(_SPIRV_CORE_PATH "")
set(_SPIRV_GLSL_PATH "")
set(_SPIRV_CROSS_VENDORED_DIR "${LX_INFRA_EXTERNAL_DIR}/SPIRV-Cross")

if(EXISTS "${_SPIRV_CROSS_VENDORED_DIR}/src/spirv_cross.cpp"
   AND EXISTS "${_SPIRV_CROSS_VENDORED_DIR}/src/spirv_glsl.cpp")
  add_library(spirv-cross-core STATIC
    "${_SPIRV_CROSS_VENDORED_DIR}/src/spirv_cross.cpp"
    "${_SPIRV_CROSS_VENDORED_DIR}/src/spirv_parser.cpp"
    "${_SPIRV_CROSS_VENDORED_DIR}/src/spirv_cfg.cpp"
    "${_SPIRV_CROSS_VENDORED_DIR}/src/spirv_cross_parsed_ir.cpp")
  target_include_directories(spirv-cross-core
    PUBLIC
      "${_SPIRV_CROSS_VENDORED_DIR}/src"
      "${_SPIRV_CROSS_VENDORED_DIR}/include")

  add_library(spirv-cross-glsl STATIC
    "${_SPIRV_CROSS_VENDORED_DIR}/src/spirv_glsl.cpp")
  target_link_libraries(spirv-cross-glsl PUBLIC spirv-cross-core)
  target_include_directories(spirv-cross-glsl
    PUBLIC
      "${_SPIRV_CROSS_VENDORED_DIR}/src"
      "${_SPIRV_CROSS_VENDORED_DIR}/include")

  set(SPIRV_CROSS_FOUND TRUE)
  set(LX_SPIRV_CROSS_LINK_TARGETS TRUE CACHE INTERNAL
      "Link SPIRV-Cross via vendored source targets" FORCE)
  set(SPIRV_CROSS_INCLUDE_DIR "${_SPIRV_CROSS_VENDORED_DIR}/src")
  unset(SPIRV_CROSS_CORE_LIB CACHE)
  unset(SPIRV_CROSS_GLSL_LIB CACHE)
elseif(SPIRV_CROSS_DIR)
  find_library(_SPIRV_CORE_PATH NAMES spirv-cross-core
    PATHS "${SPIRV_CROSS_DIR}/lib" NO_DEFAULT_PATH)
  if(_SPIRV_CORE_PATH)
    find_library(_SPIRV_GLSL_PATH NAMES spirv-cross-glsl
      PATHS "${SPIRV_CROSS_DIR}/lib" NO_DEFAULT_PATH)
    set(SPIRV_CROSS_INCLUDE_DIR "${SPIRV_CROSS_DIR}/include")
    set(SPIRV_CROSS_FOUND TRUE)
  endif()
elseif(EXISTS "${_SPIRV_CROSS_VENDORED_DIR}")
  set(_SPIRV_CROSS_LIB_SEARCH_DIRS
    "${_SPIRV_CROSS_VENDORED_DIR}/lib"
    "${_SPIRV_CROSS_VENDORED_DIR}/lib/Debug"
    "${_SPIRV_CROSS_VENDORED_DIR}/lib/Release")

  foreach(_dir IN LISTS _SPIRV_CROSS_LIB_SEARCH_DIRS)
    if(NOT _SPIRV_CORE_PATH)
      if(WIN32 AND EXISTS "${_dir}/spirv-cross-core.lib")
        set(_SPIRV_CORE_PATH "${_dir}/spirv-cross-core.lib")
      elseif(NOT WIN32 AND EXISTS "${_dir}/libspirv-cross-core.a")
        set(_SPIRV_CORE_PATH "${_dir}/libspirv-cross-core.a")
      endif()
    endif()
    if(NOT _SPIRV_GLSL_PATH)
      if(WIN32 AND EXISTS "${_dir}/spirv-cross-glsl.lib")
        set(_SPIRV_GLSL_PATH "${_dir}/spirv-cross-glsl.lib")
      elseif(NOT WIN32 AND EXISTS "${_dir}/libspirv-cross-glsl.a")
        set(_SPIRV_GLSL_PATH "${_dir}/libspirv-cross-glsl.a")
      endif()
    endif()
  endforeach()

  if(_SPIRV_CORE_PATH)
    set(SPIRV_CROSS_INCLUDE_DIR "${_SPIRV_CROSS_VENDORED_DIR}/include")
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
  message(FATAL_ERROR
    "SPIRV-Cross not found.\n"
    "Offline builds are required, so CMake will not download it.\n"
    "Provide a lightweight prebuilt package under "
    "${_SPIRV_CROSS_VENDORED_DIR} (include/ + lib/) "
    "or set SPIRV_CROSS_DIR.")
else()
  if(LX_SPIRV_CROSS_LINK_TARGETS)
    message(STATUS "SPIRV-Cross: using vendored source build")
  else()
    set(SPIRV_CROSS_CORE_LIB "${_SPIRV_CORE_PATH}" CACHE INTERNAL
        "SPIRV-Cross core library path" FORCE)
    if(_SPIRV_GLSL_PATH)
      set(SPIRV_CROSS_GLSL_LIB "${_SPIRV_GLSL_PATH}" CACHE INTERNAL
          "SPIRV-Cross glsl library path" FORCE)
    endif()
    message(STATUS "SPIRV-Cross core: ${SPIRV_CROSS_CORE_LIB}")
    message(STATUS "SPIRV-Cross glsl: ${SPIRV_CROSS_GLSL_LIB}")
  endif()
endif()

# -------------------------------
# yaml-cpp (material asset files)
# -------------------------------
# Prefer vendored lightweight source checked into src/infra/external/yaml-cpp.
# Runtime downloading is intentionally disabled.
option(YAML_CPP_DIR "Path to yaml-cpp installation (contains include/ and lib/)" "")

set(_YAML_CPP_VENDORED_DIR "${LX_INFRA_EXTERNAL_DIR}/yaml-cpp")
set(_YAML_CPP_INCLUDE_DIR "")
set(_YAML_CPP_LIB_DIR "")
set(_YAML_CPP_LIB "")

if(EXISTS "${_YAML_CPP_VENDORED_DIR}/src/node.cpp"
   AND EXISTS "${_YAML_CPP_VENDORED_DIR}/include/yaml-cpp/yaml.h")
  file(GLOB YAML_CPP_SOURCES CONFIGURE_DEPENDS
    "${_YAML_CPP_VENDORED_DIR}/src/*.cpp")
  add_library(yaml-cpp STATIC ${YAML_CPP_SOURCES})
  target_include_directories(yaml-cpp
    PUBLIC "${_YAML_CPP_VENDORED_DIR}/include"
    PRIVATE "${_YAML_CPP_VENDORED_DIR}/src")
  target_compile_definitions(yaml-cpp PUBLIC YAML_CPP_STATIC_DEFINE)
  if(NOT TARGET yaml-cpp::yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
  endif()
elseif(YAML_CPP_DIR)
  set(_YAML_CPP_INCLUDE_DIR "${YAML_CPP_DIR}/include")
  set(_YAML_CPP_LIB_DIR "${YAML_CPP_DIR}/lib")
elseif(EXISTS "${_YAML_CPP_VENDORED_DIR}")
  set(_YAML_CPP_INCLUDE_DIR "${_YAML_CPP_VENDORED_DIR}/include")
  set(_YAML_CPP_LIB_DIR "${_YAML_CPP_VENDORED_DIR}/lib")
endif()

if(_YAML_CPP_LIB_DIR)
  foreach(_dir
      "${_YAML_CPP_LIB_DIR}"
      "${_YAML_CPP_LIB_DIR}/Debug"
      "${_YAML_CPP_LIB_DIR}/Release")
    if(NOT _YAML_CPP_LIB)
      if(WIN32 AND EXISTS "${_dir}/yaml-cpp.lib")
        set(_YAML_CPP_LIB "${_dir}/yaml-cpp.lib")
      elseif(WIN32 AND EXISTS "${_dir}/yaml-cppd.lib")
        set(_YAML_CPP_LIB "${_dir}/yaml-cppd.lib")
      elseif(NOT WIN32 AND EXISTS "${_dir}/libyaml-cpp.a")
        set(_YAML_CPP_LIB "${_dir}/libyaml-cpp.a")
      endif()
    endif()
  endforeach()
endif()

if(NOT TARGET yaml-cpp AND
   (NOT EXISTS "${_YAML_CPP_INCLUDE_DIR}/yaml-cpp/yaml.h" OR NOT _YAML_CPP_LIB))
  message(FATAL_ERROR
    "yaml-cpp not found.\n"
    "Offline builds are required, so CMake will not download it.\n"
    "Provide a lightweight prebuilt package under "
    "${_YAML_CPP_VENDORED_DIR} (include/ + lib/) or set YAML_CPP_DIR.")
endif()

if(NOT TARGET yaml-cpp AND _YAML_CPP_LIB)
  add_library(yaml-cpp UNKNOWN IMPORTED)
  set_target_properties(yaml-cpp PROPERTIES
    IMPORTED_LOCATION "${_YAML_CPP_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_YAML_CPP_INCLUDE_DIR}")
endif()
if(NOT TARGET yaml-cpp::yaml-cpp)
  add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
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
    set(SDL3_DIR "${LX_INFRA_EXTERNAL_DIR}/SDL3/cmake")
    find_package(SDL3 REQUIRED CONFIG)
    if(NOT EXISTS "${LX_INFRA_EXTERNAL_DIR}/SDL3/lib/x64/SDL3.lib"
       OR NOT EXISTS "${LX_INFRA_EXTERNAL_DIR}/SDL3/lib/x64/SDL3.dll")
        message(FATAL_ERROR
            "Vendored SDL3 for Windows is incomplete.\n"
            "Expected ${LX_INFRA_EXTERNAL_DIR}/SDL3/lib/x64/SDL3.lib and SDL3.dll.")
    endif()
    if(NOT SDL3_FOUND)
        message(FATAL_ERROR "SDL3 not found")
    endif()
else()
    find_package(SDL3 QUIET)
    if(NOT SDL3_FOUND AND EXISTS "${LX_INFRA_EXTERNAL_DIR}/SDL3/lib/libSDL3.so")
        if(NOT TARGET SDL3::Headers)
            add_library(SDL3::Headers INTERFACE IMPORTED GLOBAL)
            set_target_properties(SDL3::Headers PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LX_INFRA_EXTERNAL_DIR}/SDL3/include")
        endif()
        if(NOT TARGET SDL3::SDL3)
            add_library(SDL3::SDL3 SHARED IMPORTED GLOBAL)
            set_target_properties(SDL3::SDL3 PROPERTIES
                IMPORTED_LOCATION "${LX_INFRA_EXTERNAL_DIR}/SDL3/lib/libSDL3.so"
                INTERFACE_LINK_LIBRARIES "SDL3::Headers")
        endif()
        set(SDL3_FOUND TRUE)
    elseif(NOT SDL3_FOUND)
        message(FATAL_ERROR
            "SDL3 not found.\n"
            "Offline builds are required, so CMake will not download it.\n"
            "Install SDL3 locally or vendor a lightweight package under "
            "${LX_INFRA_EXTERNAL_DIR}/SDL3.")
    endif()
endif()

if(NOT TARGET LX_SDL3)
    add_library(LX_SDL3 INTERFACE)
    target_link_libraries(LX_SDL3 INTERFACE SDL3::SDL3)
endif()

# -------------------------------
# GLFW
# -------------------------------
if(USE_GLFW)
    find_package(glfw3 QUIET CONFIG)

    if(TARGET GLFW::GLFW)
        set(LX_GLFW_TARGET GLFW::GLFW)
    elseif(TARGET glfw)
        set(LX_GLFW_TARGET glfw)
    elseif(TARGET glfw3::glfw)
        set(LX_GLFW_TARGET glfw3::glfw)
    else()
        find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h)
        find_library(GLFW_LIBRARY NAMES glfw3 glfw)
        if(GLFW_INCLUDE_DIR AND GLFW_LIBRARY)
            add_library(LX_GLFW_IMPORTED UNKNOWN IMPORTED GLOBAL)
            set_target_properties(LX_GLFW_IMPORTED PROPERTIES
                IMPORTED_LOCATION "${GLFW_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_DIR}")
            set(LX_GLFW_TARGET LX_GLFW_IMPORTED)
        else()
            message(FATAL_ERROR
                "GLFW not found.\n"
                "Install a GLFW development package or provide a CMake package "
                "that defines GLFW::GLFW / glfw / glfw3::glfw.")
        endif()
    endif()

    if(NOT TARGET LX_GLFW)
        add_library(LX_GLFW INTERFACE)
        target_link_libraries(LX_GLFW INTERFACE ${LX_GLFW_TARGET})
    endif()
endif()

# -------------------------------
# imgui - only needed backends
# -------------------------------
add_subdirectory(external/cmake/imgui)

set(INFRA_LIB LX_Infra CACHE INTERNAL "Infra library name")

# Explicitly list source files to avoid picking up example files and unwanted backends
set(INFRA_SOURCES
    material_loader/generic_material_loader.cpp
    gui/imgui_gui.cpp
    gui/debug_ui.cpp
    texture_loader/texture_loader.cpp
    texture_loader/placeholder_textures.cpp
    mesh_loader/obj_mesh_loader.cpp
    mesh_loader/gltf_mesh_loader.cpp
    mesh_loader/cgltf_impl.cpp
    shader_compiler/shader_compiler.cpp
    shader_compiler/shader_reflector.cpp
    shader_compiler/compiled_shader.cpp
)

# Add window implementation based on backend
if(USE_SDL)
    list(APPEND INFRA_SOURCES window/sdl_window.cpp window/sdl3_input_state.cpp)
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
  yaml-cpp::yaml-cpp
)

# SPIRV-Cross is resolved via vendored source or vendored/system library paths.
if(LX_SPIRV_CROSS_LINK_TARGETS)
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
    target_link_libraries(${INFRA_LIB} PRIVATE LX_SDL3)
elseif(USE_GLFW)
    target_compile_definitions(${INFRA_LIB} PRIVATE USE_GLFW=1)
    target_link_libraries(${INFRA_LIB} PRIVATE LX_GLFW)
endif()
