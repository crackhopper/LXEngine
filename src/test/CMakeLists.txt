find_package(Python3 COMPONENTS Interpreter REQUIRED)

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
  test_material_instance
  test_audio_spectrum_texture
  test_pipeline_identity
  test_pipeline_build_info
  test_frame_graph
  test_scene_node_validation
  test_scene_path_lookup
  test_pipeline_cache
  test_engine_loop
  test_debug_draw
  test_mesh_debug_geometry
  test_generic_material_loader
  test_material_variant_rules
  test_assets_layout
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
  test_scene_document
  test_project_document
  test_project_catalog
  test_project_session
  test_scene_runtime
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
  test_lxe_editor_session
  test_lxe_editor_api_service
  test_lxe_editor_api_server
  test_lxe_editor_memory_probe
  test_lxe_editor_recording
  test_gltf_loader
  test_offline_render_cli
  test_offline_image_writer
  test_offline_scene_compiler
  test_offline_gpu_scene

  # test_render_sphere
  # test_render_model
)

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
  lx_register_ctest(${TEST_EXE})

  # set_target_properties(${TEST_EXE} PROPERTIES
  #   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test/"
  # )
endforeach()

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
  target_sources(test_command_bus PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_interaction_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/recording_controller.cpp
  )
  target_include_directories(test_command_bus PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
  )
  target_link_libraries(test_command_bus PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

# debug_ui smoke test needs access to the imgui header
if(TARGET test_debug_ui_smoke)
  target_link_libraries(test_debug_ui_smoke PRIVATE imgui)
endif()

if(TARGET test_lxe_editor_log_file)
  target_sources(test_lxe_editor_log_file PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/editor_log_file.cpp
  )
  target_include_directories(test_lxe_editor_log_file PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
  )
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
  target_sources(test_lxe_editor_layout PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/builtin_asset_catalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/ui_overlay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/camera_rig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/editor_config_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/editor_data_state.cpp
  )
  target_include_directories(test_lxe_editor_layout PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
  )
  target_link_libraries(test_lxe_editor_layout PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

if(TARGET test_lxe_editor_display_config)
  target_sources(test_lxe_editor_display_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/display_launch_options.cpp
  )
endif()

if(TARGET test_lxe_editor_interaction)
  target_sources(test_lxe_editor_interaction PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_interaction_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_input_routing.cpp
  )
  target_include_directories(test_lxe_editor_interaction PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
  )
  target_link_libraries(test_lxe_editor_interaction PRIVATE yaml-cpp::yaml-cpp)
endif()

if(TARGET test_lxe_editor_session)
  target_sources(test_lxe_editor_session PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/builtin_asset_catalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/editor_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/camera_rig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/editor_config_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/editor_data_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/editor_scene_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_catalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_document.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/recording_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_interaction_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_input_routing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/ui_overlay.cpp
  )
  target_include_directories(test_lxe_editor_session PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/external/yaml-cpp/include
  )
  target_link_libraries(test_lxe_editor_session PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

if(TARGET test_lxe_editor_api_service)
  target_sources(test_lxe_editor_api_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_api_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_api_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/api_token_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/recording_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_interaction_controller.cpp
  )
  target_include_directories(test_lxe_editor_api_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/external/yaml-cpp/include
  )
  target_link_libraries(test_lxe_editor_api_service PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

if(TARGET test_lxe_editor_api_server)
  target_sources(test_lxe_editor_api_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_api_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_api_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_api_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/api_token_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/recording_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/runtime_state.cpp
  )
  target_include_directories(test_lxe_editor_api_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/external/yaml-cpp/include
  )
  target_link_libraries(test_lxe_editor_api_server PRIVATE yaml-cpp::yaml-cpp)
  if(WIN32)
    target_link_libraries(test_lxe_editor_api_server PRIVATE ws2_32)
  endif()
endif()

if(TARGET test_lxe_editor_memory_probe)
  target_sources(test_lxe_editor_memory_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/builtin_asset_catalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/editor_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/camera_rig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/editor_config_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/editor_data_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/editor_scene_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/lxe_editor_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_catalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_document.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/recording_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_interaction_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_input_routing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/ui_overlay.cpp
  )
  target_include_directories(test_lxe_editor_memory_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/external/yaml-cpp/include
  )
  target_link_libraries(test_lxe_editor_memory_probe PRIVATE imgui yaml-cpp::yaml-cpp)
endif()

if(TARGET test_lxe_editor_recording)
  target_sources(test_lxe_editor_recording PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/recording_controller.cpp
  )
  target_include_directories(test_lxe_editor_recording PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
  )
endif()

if(TARGET test_offline_render_cli)
  target_link_libraries(test_offline_render_cli PRIVATE lxe_offline_render_cli)
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
  target_sources(test_project_document PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_document.cpp
  )
  target_include_directories(test_project_document PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/external/yaml-cpp/include
  )
  target_link_libraries(test_project_document PRIVATE yaml-cpp::yaml-cpp)
endif()

if(TARGET test_project_catalog)
  target_sources(test_project_catalog PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_catalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_document.cpp
  )
  target_include_directories(test_project_catalog PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/external/yaml-cpp/include
  )
  target_link_libraries(test_project_catalog PRIVATE yaml-cpp::yaml-cpp)
endif()

if(TARGET test_project_session)
  target_sources(test_project_session PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_catalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_document.cpp
  )
  target_include_directories(test_project_session PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/external/yaml-cpp/include
  )
  target_link_libraries(test_project_session PRIVATE yaml-cpp::yaml-cpp)
endif()

if(TARGET test_scene_runtime)
  target_sources(test_scene_runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/builtin_asset_catalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/project_document.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/lxe_editor/scene_runtime.cpp
  )
  target_include_directories(test_scene_runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/external/yaml-cpp/include
  )
  target_link_libraries(test_scene_runtime PRIVATE yaml-cpp::yaml-cpp)
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

lx_register_python_ctest(test_lxe_editor_api_blackbox)

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
