# REQ-DIAG-MIN: minimal Vulkan demo target. Bypasses all engine layers
# (resource manager, descriptor manager, command buffer manager, frame graph,
# imgui) to isolate the resize-triggered black-screen / flicker bug.

add_executable(demo_minimal_resize
  main.cpp
)

target_include_directories(demo_minimal_resize PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(demo_minimal_resize PRIVATE
  ${CORE_LIB}
  ${INFRA_LIB}
  ${GRAPHICS_LIB}
)

# Ensure shaders are compiled before the demo runs so the runtime can load
# minimal.vert.spv / minimal.frag.spv from getRuntimeShaderBinaryDir().
add_dependencies(demo_minimal_resize CompileShaders)
