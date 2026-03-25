
set(TEST_INTEGRATION_EXE_LIST
  test_vulkan_buffer
  test_vulkan_command_buffer
  test_vulkan_swapchain
  test_vulkan_framebuffer
  test_vulkan_pipeline
  test_vulkan_texture
  test_vulkan_shader
  test_vulkan_resource_manager

  # test_render_sphere
  # test_render_model
)

add_custom_target(BuildTest)
foreach(TEST_EXE ${TEST_INTEGRATION_EXE_LIST})
  add_executable(${TEST_EXE} integration/${TEST_EXE}.cpp)
  target_link_libraries(${TEST_EXE}
    PRIVATE
    ${CORE_LIB}
    ${INFRA_LIB}
    ${GRAPHICS_LIB}
  )

  add_dependencies(BuildTest ${TEST_EXE})

  # set_target_properties(${TEST_EXE} PROPERTIES 
  #   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test/" 
  # )  
endforeach()


set(TEST_APP_EXE_LIST
  test_render_triangle
)
foreach(TEST_EXE ${TEST_APP_EXE_LIST})
  add_executable(${TEST_EXE} ${TEST_EXE}.cpp)
  target_link_libraries(${TEST_EXE}
    PRIVATE
    ${CORE_LIB}
    ${INFRA_LIB}
    ${GRAPHICS_LIB}
  )
  add_dependencies(BuildTest ${TEST_EXE})
endforeach()

# Ensure shader SPIR-V is built before any shader-dependent tests run.
add_dependencies(BuildTest CompileShaders)


