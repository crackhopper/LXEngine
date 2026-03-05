option(USE_SDL "Use SDL2 as the window backend" ON)  # 默认用 SDL
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
# 查找SDL3库
find_package(SDL3 REQUIRED)
if(NOT SDL3_FOUND)
    message(FATAL_ERROR "SDL3 not found")
endif()
# -------------------------------
# imgui
# -------------------------------
add_subdirectory(external/cmake/imgui)

set(INFRA_LIB LX_Infra CACHE INTERNAL "Infra library name")


file(GLOB_RECURSE INFRA_SOURCES
 "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
 "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)





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
