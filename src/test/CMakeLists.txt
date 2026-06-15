find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(LXE_EDITOR_TESTS
  test_command_bus
  test_command_bus_v2
  test_scene_tree_panel
  test_editor_multi_select
  test_inspector_panel
  test_viewport_overlay
  test_gizmo_adapter
  test_lxe_editor_display_config
  test_lxe_editor_log_file
  test_lxe_editor_layout
  test_lxe_editor_interaction
  test_realtime_render_profile_commands
  test_lxe_editor_api_service
  test_lxe_editor_api_server
  test_lxe_editor_memory_probe
  test_lxe_editor_recording
  test_lxe_editor_render_view
  test_project_document
  test_project_catalog
  test_project_session
  test_scene_runtime
)

set(LXE_HAS_EDITOR_TARGET OFF)
if(DEFINED LXE_EDITOR_LIB)
  if(TARGET ${LXE_EDITOR_LIB})
    set(LXE_HAS_EDITOR_TARGET ON)
  endif()
endif()

set(TEST_INTEGRATION_EXE_LIST
  test_vulkan_viewport_convention
  test_vulkan_buffer
  test_vulkan_command_buffer
  test_vulkan_swapchain
  test_vulkan_framebuffer
  test_vulkan_pipeline
  test_vulkan_frame_graph
  test_vulkan_texture
  test_vulkan_shader
  test_vulkan_resource_manager
  test_vulkan_ibl_bake
  test_vulkan_offline_renderer
  test_vulkan_device_selection
  test_build_info
  test_string_table
  test_tone_mapping
  test_material_instance
  test_material_source_contract
  test_material_v2_parser
  test_material_v2_resource_dependencies
  test_material_v2_converter_defaults
  test_default_material_asset_audit
  test_render_path_graph_pass_contract
  test_render_resource_parsers
  test_default_forward_render_path_graph_source
  test_default_deferred_render_path_graph_source
  test_render_work_compiler
  test_frame_graph_registry
  test_scene_resource_abstraction
  test_scene_resource_upload_view_v2
  test_gpu_resource_table_contract
  test_upload_task_graph
  test_bindless_indirect_contract
  test_material_source_variant_pipeline
  test_073d_render_path_hard_cut
  test_bindless_validation_contract
  test_071g_legacy_boundary_removal
  test_scene_package_round_trip
  test_render_validation_profile
  test_diagnostics_compare
  test_audio_spectrum_texture
  test_pipeline_identity
  test_pipeline_build_info
  test_frame_graph
  test_scene_resource_table
  test_scene_path_lookup
  test_pipeline_cache
  test_engine_loop
  test_input_state
  test_clock
  test_math
  test_picking
  test_sdl_input
  test_orbit_camera_controller
  test_freefly_camera_controller
  test_imgui_overlay
  test_vulkan_renderer_memory_probe
  test_vulkan_offscreen_submit_memory_probe
  test_command_bus
  test_command_bus_v2
  test_project_document
  test_scene_events
  test_debug_ui_smoke
  test_scene_tree_panel
  test_editor_multi_select
  test_inspector_panel
  test_viewport_overlay
  test_gizmo_adapter
  test_lxe_editor_display_config
  test_lxe_editor_log_file
  test_lxe_editor_layout
  test_lxe_editor_interaction
  test_realtime_render_profile_commands
  test_lxe_editor_api_service
  test_lxe_editor_api_server
  test_lxe_editor_memory_probe
  test_lxe_editor_recording
  test_lxe_editor_render_view
  test_lxe_editor_source_boundary
  test_gltf_scene_asset_loader
  test_output_profile_resolution
  test_lxe_compare_exr_metrics
  test_offline_integrator_selection
  test_offline_gpu_scene

  # test_render_sphere
  # test_render_model
)

if(NOT LXE_HAS_EDITOR_TARGET)
  list(REMOVE_ITEM TEST_INTEGRATION_EXE_LIST ${LXE_EDITOR_TESTS})
  list(REMOVE_ITEM TEST_INTEGRATION_EXE_LIST test_lxe_editor_source_boundary)
endif()

set(TEST_VIDEO_DEVICE_EXE_LIST
  test_vulkan_buffer
  test_vulkan_command_buffer
  test_vulkan_swapchain
  test_vulkan_framebuffer
  test_vulkan_pipeline
  test_vulkan_frame_graph
  test_vulkan_texture
  test_vulkan_shader
  test_vulkan_resource_manager
  test_vulkan_ibl_bake
  test_pipeline_cache
  test_imgui_overlay
  test_vulkan_renderer_memory_probe
  test_vulkan_offscreen_submit_memory_probe
  test_command_bus
  test_lxe_editor_memory_probe
)

function(lx_register_ctest test_name)
  add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>)

  set(test_labels "auto")
  if(${test_name} IN_LIST TEST_VIDEO_DEVICE_EXE_LIST)
    list(APPEND test_labels "requires_video_device")
  else()
    list(APPEND test_labels "headless_ok")
  endif()

  set_tests_properties(${test_name} PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    LABELS "${test_labels}"
  )
endfunction()

function(lx_register_python_ctest test_name)
  add_test(
    NAME ${test_name}
    COMMAND
      ${CMAKE_COMMAND} -E env
      PYTHONPATH=${CMAKE_SOURCE_DIR}
      ${Python3_EXECUTABLE}
      -m unittest discover
      -s ${CMAKE_SOURCE_DIR}/tests/lxe_editor
      -p test_*.py
  )
  set_tests_properties(${test_name} PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    LABELS "auto;requires_video_device"
  )
endfunction()

add_test(
  NAME test_lxe_pbrt_scene_convert
  COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_lxe_pbrt_scene_convert.py
    --source-dir ${CMAKE_SOURCE_DIR}
)
set_tests_properties(test_lxe_pbrt_scene_convert PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  LABELS "auto;headless_ok"
)

add_test(
  NAME test_lxe_gltf_material_convert
  COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_lxe_gltf_material_convert.py
    --source-dir ${CMAKE_SOURCE_DIR}
)
set_tests_properties(test_lxe_gltf_material_convert PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  LABELS "auto;headless_ok"
)

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

add_custom_target(BuildTest)
add_dependencies(BuildTest
  lxe_compare_exr
  lxe_offline_render
  lxe_realtime_render
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

  # set_target_properties(${TEST_EXE} PROPERTIES
  #   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test/"
  # )
endforeach()

if(TARGET test_lxe_compare_exr_metrics)
  target_link_libraries(test_lxe_compare_exr_metrics PRIVATE lxe_compare_exr_core)
endif()

if(TARGET test_diagnostics_compare)
  target_link_libraries(test_diagnostics_compare PRIVATE lxe_compare_exr_core)
endif()

if(TARGET test_default_material_asset_audit)
  target_link_libraries(test_default_material_asset_audit PRIVATE yaml-cpp::yaml-cpp)
endif()

if(TARGET test_material_source_contract)
  target_compile_definitions(test_material_source_contract
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()

if(TARGET test_material_source_variant_pipeline)
  target_compile_definitions(test_material_source_variant_pipeline
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()

if(TARGET test_073d_render_path_hard_cut)
  target_compile_definitions(test_073d_render_path_hard_cut
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()

if(TARGET test_071g_legacy_boundary_removal)
  target_compile_definitions(test_071g_legacy_boundary_removal
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()

if(TARGET test_render_resource_parsers)
  target_compile_definitions(test_render_resource_parsers
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()

if(TARGET test_lxe_editor_source_boundary)
  target_compile_definitions(test_lxe_editor_source_boundary
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()

if(TARGET test_render_work_compiler)
  target_compile_definitions(test_render_work_compiler
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()

if(TARGET test_scene_resource_abstraction)
  target_compile_definitions(test_scene_resource_abstraction
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()

if(TARGET test_default_forward_render_path_graph_source)
  target_compile_definitions(test_default_forward_render_path_graph_source
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()

if(TARGET test_default_deferred_render_path_graph_source)
  target_compile_definitions(test_default_deferred_render_path_graph_source
    PRIVATE
    LXE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
endif()

# SDL input test needs USE_SDL define and SDL3 headers
if(USE_SDL AND TARGET test_sdl_input)
  target_compile_definitions(test_sdl_input PRIVATE USE_SDL=1)
  target_link_libraries(test_sdl_input PRIVATE LX_SDL3)
endif()

# ImGui overlay test needs USE_SDL and access to the imgui header
if(USE_SDL AND TARGET test_imgui_overlay)
  target_compile_definitions(test_imgui_overlay PRIVATE USE_SDL=1)
  target_link_libraries(test_imgui_overlay PRIVATE imgui)
endif()

if(USE_SDL AND TARGET test_vulkan_renderer_memory_probe)
  target_compile_definitions(test_vulkan_renderer_memory_probe PRIVATE USE_SDL=1)
  target_link_libraries(test_vulkan_renderer_memory_probe PRIVATE imgui)
endif()

if(USE_SDL AND TARGET test_vulkan_frame_graph)
  target_compile_definitions(test_vulkan_frame_graph PRIVATE USE_SDL=1)
  target_link_libraries(test_vulkan_frame_graph PRIVATE imgui)
endif()

if(TARGET test_command_bus)
  target_link_libraries(test_command_bus PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

if(TARGET test_realtime_render_profile_commands)
  target_link_libraries(test_realtime_render_profile_commands PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

# debug_ui smoke test needs access to the imgui header
if(TARGET test_debug_ui_smoke)
  target_link_libraries(test_debug_ui_smoke PRIVATE imgui)
endif()

if(TARGET test_lxe_editor_log_file)
  target_link_libraries(test_lxe_editor_log_file PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

if(TARGET test_scene_tree_panel)
  target_link_libraries(test_scene_tree_panel PRIVATE imgui)
endif()

if(TARGET test_editor_multi_select)
  target_link_libraries(test_editor_multi_select PRIVATE imgui)
endif()

if(TARGET test_inspector_panel)
  target_link_libraries(test_inspector_panel PRIVATE imgui)
endif()

if(TARGET test_viewport_overlay)
  target_link_libraries(test_viewport_overlay PRIVATE imgui)
endif()

if(TARGET test_gizmo_adapter)
  target_link_libraries(test_gizmo_adapter PRIVATE imgui)
endif()

if(TARGET test_lxe_editor_layout)
  target_link_libraries(test_lxe_editor_layout PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

if(TARGET test_lxe_editor_display_config)
  target_link_libraries(test_lxe_editor_display_config PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

if(TARGET test_lxe_editor_interaction)
  target_link_libraries(test_lxe_editor_interaction PRIVATE yaml-cpp::yaml-cpp)
endif()

if(TARGET test_lxe_editor_session)
  target_link_libraries(test_lxe_editor_session PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

if(TARGET test_lxe_editor_api_service)
  target_link_libraries(test_lxe_editor_api_service PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

if(TARGET test_lxe_editor_api_server)
  target_link_libraries(test_lxe_editor_api_server PRIVATE imgui yaml-cpp::yaml-cpp)
  if(WIN32)
    target_link_libraries(test_lxe_editor_api_server PRIVATE ws2_32)
  endif()
endif()

if(TARGET test_lxe_editor_memory_probe)
  target_link_libraries(test_lxe_editor_memory_probe PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

if(TARGET test_offline_render_cli)
  target_link_libraries(test_offline_render_cli PRIVATE lxe_offline_render_cli)
endif()

if(TARGET test_output_profile_resolution)
  set_property(TARGET test_output_profile_resolution PROPERTY LINK_LIBRARIES "")
  target_sources(test_output_profile_resolution PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/offline/offline_render_profile.cpp
  )
  target_include_directories(test_output_profile_resolution PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/..
  )
endif()

if(TARGET test_scene_document)
  target_sources(test_scene_document PRIVATE
  )
  target_include_directories(test_scene_document PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/external/yaml-cpp/include
  )
  target_link_libraries(test_scene_document PRIVATE yaml-cpp::yaml-cpp)
endif()

if(TARGET test_project_document)
  target_link_libraries(test_project_document PRIVATE yaml-cpp::yaml-cpp)
endif()

if(TARGET test_project_catalog)
  target_link_libraries(test_project_catalog PRIVATE yaml-cpp::yaml-cpp)
endif()

if(TARGET test_project_session)
  target_link_libraries(test_project_session PRIVATE yaml-cpp::yaml-cpp)
endif()

if(TARGET test_scene_runtime)
  target_link_libraries(test_scene_runtime PRIVATE yaml-cpp::yaml-cpp)
endif()

foreach(LXE_EDITOR_TEST ${LXE_EDITOR_TESTS})
  if(LXE_HAS_EDITOR_TARGET AND TARGET ${LXE_EDITOR_TEST})
    target_link_libraries(${LXE_EDITOR_TEST} PRIVATE ${LXE_EDITOR_LIB})
    target_include_directories(${LXE_EDITOR_TEST} PRIVATE
      ${CMAKE_SOURCE_DIR}/src
      ${CMAKE_SOURCE_DIR}/src/infra/external/yaml-cpp/include
    )
  endif()
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

if(TARGET lxe_editor)
  lx_register_python_ctest(test_lxe_editor_api_blackbox)
endif()

# --- Shader compiler test (no GPU required, uses shaderc + SPIRV-Cross) ---
# Compile shader_compiler sources directly to avoid depending on full LX_Infra/LX_Core libs
add_executable(test_shader_compiler
  integration/test_shader_compiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/utils/env.cpp
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
add_test(NAME test_shader_compiler
  COMMAND $<TARGET_FILE:test_shader_compiler> "${CMAKE_SOURCE_DIR}/assets/shaders/glsl")
set_tests_properties(test_shader_compiler PROPERTIES
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  LABELS "auto;headless_ok"
)

# Ensure shader SPIR-V is built before any shader-dependent tests run.
add_dependencies(BuildTest CompileShaders)
