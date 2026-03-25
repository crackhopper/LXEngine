option(USE_SDL "Use SDL2 as the window backend" ON)
option(USE_GLFW "Use GLFW as the window backend" OFF)

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
set(SDL3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/SDL3-3.3.6/cmake)
find_package(SDL3 REQUIRED)
if(NOT SDL3_FOUND)
    message(FATAL_ERROR "SDL3 not found")
endif()

# -------------------------------
# imgui - only needed backends
# -------------------------------
add_subdirectory(external/cmake/imgui)

set(INFRA_LIB LX_Infra CACHE INTERNAL "Infra library name")

# Explicitly list source files to avoid picking up example files and unwanted backends
set(INFRA_SOURCES
    gui/gui_impl_imgui.cpp
    texture_loader/texture_loader.cpp
    mesh_loader/obj/obj.cpp
    mesh_loader/gltf/gltf.cpp
)

# Add window implementation based on backend
if(USE_SDL)
    list(APPEND INFRA_SOURCES window/window_impl_sdl.cpp)
elseif(USE_GLFW)
    list(APPEND INFRA_SOURCES window/window_impl_glfw.cpp)
endif()

add_library(${INFRA_LIB} ${INFRA_SOURCES})

target_link_libraries(${INFRA_LIB}
  PRIVATE
  ${CORE_LIB}
  Vulkan::Vulkan
  imgui
)
target_include_directories(${INFRA_LIB} PRIVATE external/include)
target_include_directories(${INFRA_LIB} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(USE_SDL)
    target_compile_definitions(${INFRA_LIB} PRIVATE USE_SDL=1)
    target_link_libraries(${INFRA_LIB} PRIVATE SDL3::SDL3)
elseif(USE_GLFW)
    target_compile_definitions(${INFRA_LIB} PRIVATE USE_GLFW=1)
    target_link_libraries(${INFRA_LIB} PRIVATE GLFW::GLFW)
endif()
