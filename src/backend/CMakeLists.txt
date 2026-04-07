
option(USE_GEOMETRY_SHADER "创建设备的时候开启几何着色器特性" ON)

# -------------------------------
# Vulkan
# -------------------------------
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found")
endif()

set(GRAPHICS_LIB LX_Backend CACHE INTERNAL "Graphics library name")

file(GLOB_RECURSE GRAPHIC_SOURCES
 "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
 "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_library(${GRAPHICS_LIB} ${GRAPHIC_SOURCES})

# 其他模块依赖
target_link_libraries(${GRAPHICS_LIB} PRIVATE ${CORE_LIB} ${INFRA_LIB})

# Vulkan 相关依赖
target_include_directories(${GRAPHICS_LIB} PUBLIC ${Vulkan_INCLUDE_DIRS})
target_link_libraries(${GRAPHICS_LIB} PUBLIC Vulkan::Vulkan)

target_include_directories(${GRAPHICS_LIB} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)


if(USE_GEOMETRY_SHADER)
    target_compile_definitions(${GRAPHICS_LIB} PRIVATE USE_GEOMETRY_SHADER=1)
endif()


# message(STATUS "GRAPHICS_LIB sources: ${GRAPHIC_SOURCES}")

# get_target_property(GRAPHICS_INCLUDE_INTERFACE ${GRAPHICS_LIB} INTERFACE_INCLUDE_DIRECTORIES)
# message(STATUS "GRAPHICS_LIB INTERFACE include dirs: ${GRAPHICS_INCLUDE_INTERFACE}")

# get_target_property(GRAPHICS_INCLUDE ${GRAPHICS_LIB} INCLUDE_DIRECTORIES)
# message(STATUS "GRAPHICS_LIB include dirs: ${GRAPHICS_INCLUDE}")
