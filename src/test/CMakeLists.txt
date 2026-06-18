find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(TEST_INTEGRATION_EXE_LIST
  test_bindless_indirect_contract
  test_bindless_validation_contract
  test_gltf_scene_asset_loader
  test_frame_graph_executor
  test_lxe_editor_render_debug_dump
  test_lxe_editor_source_boundary
  test_material_source_variant_pipeline
  test_render_path_graph_pass_contract
  test_render_resource_parsers
  test_render_work_compiler
  test_scene_bake_cache
  test_vulkan_post_process_builder
  test_scene_resource_upload_view_v2
  test_vulkan_ibl_bake
  test_vulkan_offline_renderer
  test_vulkan_texture_format_mapping
)

function(lx_register_ctest test_name)
  add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>)
  set_tests_properties(${test_name} PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    LABELS "auto;headless_ok"
  )
endfunction()

add_custom_target(BuildTest)
add_dependencies(BuildTest
  lxe_compare_exr
  lxe_image_probe
  lxe_offline_render
)

foreach(TEST_EXE ${TEST_INTEGRATION_EXE_LIST})
  add_executable(${TEST_EXE} integration/${TEST_EXE}.cpp)
  target_link_libraries(${TEST_EXE}
    PRIVATE
    ${CORE_LIB}
    ${INFRA_LIB}
    ${GRAPHICS_LIB}
  )

  add_dependencies(BuildTest ${TEST_EXE})
  if(TARGET SyncShaderRuntimeSources)
    add_dependencies(${TEST_EXE} SyncShaderRuntimeSources)
  endif()
  lx_register_ctest(${TEST_EXE})
endforeach()

if(TARGET ${LXE_EDITOR_LIB})
  target_link_libraries(test_lxe_editor_render_debug_dump PRIVATE ${LXE_EDITOR_LIB})
  target_link_libraries(test_scene_bake_cache PRIVATE ${LXE_EDITOR_LIB})
  target_link_libraries(test_vulkan_ibl_bake PRIVATE ${LXE_EDITOR_LIB})
endif()

foreach(TEST_EXE
    test_lxe_editor_source_boundary
    test_material_source_variant_pipeline
    test_render_resource_parsers
    test_render_work_compiler)
  target_compile_definitions(${TEST_EXE}
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endforeach()

if(TARGET lxe_editor)
  add_test(
    NAME test_helmet_standard_pbr_realtime_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_helmet_standard_pbr_realtime_smoke.py
      --source-dir ${CMAKE_SOURCE_DIR}
      --editor $<TARGET_FILE:lxe_editor>
  )
  set_tests_properties(test_helmet_standard_pbr_realtime_smoke PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    LABELS "auto;requires_video_device"
  )
endif()

add_test(
  NAME test_generate_finite_skybox_room
  COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_generate_finite_skybox_room.py
    --source-dir ${CMAKE_SOURCE_DIR}
    --binary-dir ${CMAKE_BINARY_DIR}
)
set_tests_properties(test_generate_finite_skybox_room PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  LABELS "auto;headless_ok"
)

# Shader compiler coverage stays standalone so it can validate shaderc and
# SPIRV-Cross without linking the full infra/backend libraries.
add_executable(test_shader_compiler
  integration/test_shader_compiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/utils/env.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../infra/shader_compiler/shader_compiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../infra/shader_compiler/shader_reflector.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../infra/shader_compiler/compiled_shader.cpp
)
target_include_directories(test_shader_compiler PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/..
)
if(SPIRV_CROSS_INCLUDE_DIR AND NOT "${SPIRV_CROSS_INCLUDE_DIR}" STREQUAL "")
  target_include_directories(test_shader_compiler PRIVATE ${SPIRV_CROSS_INCLUDE_DIR})
endif()
if(SHADERC_INCLUDE_DIR AND NOT "${SHADERC_INCLUDE_DIR}" STREQUAL "")
  target_include_directories(test_shader_compiler PRIVATE ${SHADERC_INCLUDE_DIR})
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
add_test(NAME test_shader_compiler
  COMMAND $<TARGET_FILE:test_shader_compiler> "${CMAKE_SOURCE_DIR}/assets/shaders/glsl")
set_tests_properties(test_shader_compiler PROPERTIES
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  LABELS "auto;headless_ok"
)

add_dependencies(BuildTest CompileShaders)

# 新分层（C++20 modules）的 smoke exe，自包含子目录。
add_subdirectory(new)
