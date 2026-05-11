set(TEST_INTEGRATION_EXE_LIST
  test_vulkan_buffer
  test_vulkan_command_buffer
  test_vulkan_swapchain
  test_vulkan_framebuffer
  test_vulkan_pipeline
  test_vulkan_texture
  test_vulkan_shader
  test_vulkan_resource_manager
  test_vulkan_device_selection
  test_string_table
  test_material_instance
  test_pipeline_identity
  test_pipeline_build_info
  test_frame_graph
  test_scene_node_validation
  test_scene_path_lookup
  test_pipeline_cache
  test_engine_loop
  test_debug_draw
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
  test_command_bus
  test_command_bus_v2
  test_scene_document
  test_scene_runtime
  test_debug_ui_smoke
  test_scene_tree_panel
  test_editor_multi_select
  test_inspector_panel
  test_viewport_overlay
  test_gizmo_adapter
  test_scene_viewer_layout
  test_gltf_loader

  # test_render_sphere
  # test_render_model
)

set(TEST_VIDEO_DEVICE_EXE_LIST
  test_vulkan_buffer
  test_vulkan_command_buffer
  test_vulkan_swapchain
  test_vulkan_framebuffer
  test_vulkan_pipeline
  test_vulkan_texture
  test_vulkan_shader
  test_vulkan_resource_manager
  test_pipeline_cache
  test_imgui_overlay
  test_command_bus
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

if(TARGET test_command_bus)
  target_link_libraries(test_command_bus PRIVATE imgui)
endif()

# debug_ui smoke test needs access to the imgui header
if(TARGET test_debug_ui_smoke)
  target_link_libraries(test_debug_ui_smoke PRIVATE imgui)
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

if(TARGET test_scene_viewer_layout)
  target_sources(test_scene_viewer_layout PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/scene_viewer/ui_overlay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/scene_viewer/camera_rig.cpp
  )
  target_include_directories(test_scene_viewer_layout PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
  )
  target_link_libraries(test_scene_viewer_layout PRIVATE imgui)
endif()

if(TARGET test_scene_document)
  target_sources(test_scene_document PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/scene_viewer/editor_camera_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/scene_viewer/scene_document.cpp
  )
  target_include_directories(test_scene_document PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../infra/external/yaml-cpp/include
  )
  target_link_libraries(test_scene_document PRIVATE yaml-cpp::yaml-cpp)
endif()

if(TARGET test_scene_runtime)
  target_sources(test_scene_runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/scene_viewer/scene_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/scene_viewer/editor_camera_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/scene_viewer/scene_document.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../demos/scene_viewer/scene_runtime.cpp
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
