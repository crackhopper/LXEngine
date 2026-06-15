set(LXE_EDITOR_LIB LX_Editor CACHE INTERNAL "LXE editor application library")

set(LXE_EDITOR_SOURCES
  api/api_token_state.cpp
  api/lxe_editor_api_protocol.cpp
  api/lxe_editor_api_server.cpp
  api/lxe_editor_api_service.cpp
  app/display_launch_options.cpp
  app/editor_config.cpp
  app/editor_config_state.cpp
  app/editor_data_state.cpp
  app/editor_log_file.cpp
  app/editor_scene_state.cpp
  app/editor_session.cpp
  app/editor_state.cpp
  app/runtime_state.cpp
  commands/builtin_commands.cpp
  commands/command_bus.cpp
  commands/display_commands.cpp
  commands/lxe_editor_command_helpers.cpp
  commands/lxe_editor_commands.cpp
  commands/project_commands.cpp
  commands/realtime_render_commands.cpp
  commands/recording_commands.cpp
  commands/render_debug_commands.cpp
  commands/scene_project_commands.cpp
  panels/console_input_controller.cpp
  panels/console_panel.cpp
  panels/inspector_panel.cpp
  panels/scene_tree_panel.cpp
  panels/viewport_overlay.cpp
  project/builtin_asset_catalog.cpp
  project/debug_render_export.cpp
  project/project_catalog.cpp
  project/project_document.cpp
  project/project_session.cpp
  project/realtime_render_profile.cpp
  project/scene_builder.cpp
  runtime/camera_rig.cpp
  runtime/recording_controller.cpp
  runtime/scene_input_routing.cpp
  runtime/scene_interaction_controller.cpp
  runtime/scene_runtime.cpp
  runtime/scene_view_rect.cpp
  render/editor_render_view.cpp
  ui/gizmo_adapter.cpp
  ui/ui_overlay.cpp
)

add_library(${LXE_EDITOR_LIB} ${LXE_EDITOR_SOURCES})

target_include_directories(${LXE_EDITOR_LIB}
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/infra/external/yaml-cpp/include
)

target_link_libraries(${LXE_EDITOR_LIB}
  PUBLIC
    ${CORE_LIB}
    ${INFRA_LIB}
    ${GRAPHICS_LIB}
    imgui
    yaml-cpp::yaml-cpp
)

if(WIN32)
  target_link_libraries(${LXE_EDITOR_LIB} PUBLIC ws2_32)
endif()

add_executable(lxe_editor main.cpp)
target_link_libraries(lxe_editor PRIVATE ${LXE_EDITOR_LIB})
add_dependencies(lxe_editor CompileShaders)
