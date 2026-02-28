
set(GRAPHIC_LIB LX_Graphics CACHE INTERNAL "Graphics library name")

file(GLOB_RECURSE GRAPHIC_SOURCES
 "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")


add_library(${GRAPHIC_LIB} ${GRAPHIC_SOURCES})

target_include_directories(${GRAPHIC_LIB} PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(${GRAPHIC_LIB} PRIVATE Vulkan::Vulkan)
target_link_libraries(${GRAPHIC_LIB} PRIVATE ${FOUNDATION_LIB})


target_include_directories(${GRAPHIC_LIB} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)


# message(STATUS "GRAPHIC_LIB : ${GRAPHIC_LIB}")
# message(STATUS "FOUNDATION_LIB : ${FOUNDATION_LIB}")

# get_target_property(FOUNDATION_INCLUDE ${FOUNDATION_LIB} INTERFACE_INCLUDE_DIRECTORIES)
# message(STATUS "FOUNDATION_LIB INTERFACE include dirs: ${FOUNDATION_INCLUDE}")