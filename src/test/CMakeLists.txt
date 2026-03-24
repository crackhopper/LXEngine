
set(TEST_EXE_LIST 
  test_vulkan_device
  test_vulkan_buffer
  test_vulkan_texture
  test_vulkan_shader
  test_vulkan_renderpass
  test_vulkan_framebuffer
  test_vulkan_depth
  test_vulkan_command_buffer
  test_vulkan_pipeline
  test_vulkan_resource_manager
  test_vulkan_renderer
  test_render_triangle
  # test_render_sphere
  # test_render_model
)

add_custom_target(BuildTest)
foreach(TEST_EXE ${TEST_EXE_LIST})
  add_executable(${TEST_EXE} ${TEST_EXE}.cpp)
  target_link_libraries(${TEST_EXE} 
    PRIVATE 
    ${CORE_LIB}
    ${INFRA_LIB}
    ${GRAPHICS_LIB}
  )
  target_include_directories(${TEST_EXE} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  add_dependencies(BuildTest ${TEST_EXE})

  # set_target_properties(${TEST_EXE} PROPERTIES 
  #   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test/" 
  # )  
endforeach()


# Ensure shader SPIR-V is built before any shader-dependent tests run.
add_dependencies(BuildTest CompileShaders)


