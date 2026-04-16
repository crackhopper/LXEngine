
set(TEST_INTEGRATION_EXE_LIST
  test_vulkan_buffer
  test_vulkan_command_buffer
  test_vulkan_swapchain
  test_vulkan_framebuffer
  test_vulkan_pipeline
  test_vulkan_texture
  test_vulkan_shader
  test_vulkan_resource_manager
  test_string_table
  test_material_instance
  test_pipeline_identity
  test_pipeline_build_info
  test_frame_graph
  test_scene_node_validation
  test_pipeline_cache
  test_engine_loop
  test_generic_material_loader
  test_material_variant_rules
  test_assets_layout
  test_input_state
  test_clock
  test_sdl_input
  test_orbit_camera_controller
  test_freefly_camera_controller

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

# SDL input test needs USE_SDL define and SDL3 headers
if(USE_SDL AND TARGET test_sdl_input)
  target_compile_definitions(test_sdl_input PRIVATE USE_SDL=1)
  target_link_libraries(test_sdl_input PRIVATE LX_SDL3)
endif()


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

# --- Shader compiler test (no GPU required, uses shaderc + SPIRV-Cross) ---
# Compile shader_compiler sources directly to avoid depending on full LX_Infra/LX_Core libs
add_executable(test_shader_compiler
  integration/test_shader_compiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../infra/shader_compiler/shader_compiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../infra/shader_compiler/shader_reflector.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../infra/shader_compiler/compiled_shader.cpp
)
target_include_directories(test_shader_compiler PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..       # for infra/
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/..  # for core/
)
# SPIRV-Cross includes & link (must match infra/CMakeLists discovery)
if(SPIRV_CROSS_INCLUDE_DIR AND NOT "${SPIRV_CROSS_INCLUDE_DIR}" STREQUAL "")
  target_include_directories(test_shader_compiler PRIVATE ${SPIRV_CROSS_INCLUDE_DIR})
endif()
if(LX_SPIRV_CROSS_LINK_TARGETS)
  target_link_libraries(test_shader_compiler PRIVATE spirv-cross-core spirv-cross-glsl)
elseif(SPIRV_CROSS_CORE_LIB)
  target_link_libraries(test_shader_compiler PRIVATE ${SPIRV_CROSS_CORE_LIB})
  if(SPIRV_CROSS_GLSL_LIB)
    target_link_libraries(test_shader_compiler PRIVATE ${SPIRV_CROSS_GLSL_LIB})
  endif()
endif()
target_link_libraries(test_shader_compiler PRIVATE ${SHADERC_LIB} ${SHADERC_DEPS})
add_dependencies(BuildTest test_shader_compiler)

# Ensure shader SPIR-V is built before any shader-dependent tests run.
add_dependencies(BuildTest CompileShaders)
