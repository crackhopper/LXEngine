
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

# --- Shader compiler test (no GPU required, uses shaderc + SPIRV-Cross) ---
# Compile shader_compiler sources directly to avoid depending on full LX_Infra/LX_Core libs
add_executable(test_shader_compiler
  integration/test_shader_compiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../infra/shader_compiler/shader_compiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../infra/shader_compiler/shader_reflector.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../infra/shader_compiler/shader_impl.cpp
)
target_include_directories(test_shader_compiler PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..       # for infra/
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/..  # for core/
)
# SPIRV-Cross includes & link
if(TARGET spirv-cross-core)
  # FetchContent: use cached SPIRV_CROSS_INCLUDE_DIR (set by infra CMakeLists)
  if(SPIRV_CROSS_INCLUDE_DIR AND NOT "${SPIRV_CROSS_INCLUDE_DIR}" STREQUAL "")
    target_include_directories(test_shader_compiler PRIVATE ${SPIRV_CROSS_INCLUDE_DIR})
  endif()
  target_link_libraries(test_shader_compiler PRIVATE spirv-cross-core)
  if(TARGET spirv-cross-glsl)
    target_link_libraries(test_shader_compiler PRIVATE spirv-cross-glsl)
  endif()
else()
  target_link_libraries(test_shader_compiler PRIVATE ${SPIRV_CROSS_CORE_LIB})
  if(SPIRV_CROSS_INCLUDE_DIR AND NOT "${SPIRV_CROSS_INCLUDE_DIR}" STREQUAL "")
    target_include_directories(test_shader_compiler PRIVATE ${SPIRV_CROSS_INCLUDE_DIR})
  endif()
  if(SPIRV_CROSS_GLSL_LIB)
    target_link_libraries(test_shader_compiler PRIVATE ${SPIRV_CROSS_GLSL_LIB})
  endif()
endif()
target_link_libraries(test_shader_compiler PRIVATE ${SHADERC_LIB} ${SHADERC_DEPS})
add_dependencies(BuildTest test_shader_compiler)

# Ensure shader SPIR-V is built before any shader-dependent tests run.
add_dependencies(BuildTest CompileShaders)
