# REQ-019: default integration demo. Loads DamagedHelmet, drives EngineLoop,
# wires Orbit/FreeFly cameras, and renders a minimal ImGui overlay on top of
# the existing VulkanRenderer.

add_executable(lxe_editor
  api_token_state.cpp
  editor_session.cpp
  main.cpp
  camera_rig.cpp
  lxe_editor_api_protocol.cpp
  lxe_editor_api_server.cpp
  lxe_editor_api_service.cpp
  editor_config_state.cpp
  editor_data_state.cpp
  editor_camera_state.cpp
  lxe_editor_mcp_handler.cpp
  runtime_state.cpp
  scene_catalog.cpp
  scene_builder.cpp
  scene_document.cpp
  scene_interaction_controller.cpp
  scene_input_routing.cpp
  scene_view_rect.cpp
  lxe_editor_commands.cpp
  scene_runtime.cpp
  scene_session.cpp
  ui_overlay.cpp
)

target_include_directories(lxe_editor PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/infra/external/yaml-cpp/include
)

target_link_libraries(lxe_editor PRIVATE
  ${CORE_LIB}
  ${INFRA_LIB}
  ${GRAPHICS_LIB}
  imgui
  yaml-cpp::yaml-cpp
)

if(WIN32)
  target_link_libraries(lxe_editor PRIVATE ws2_32)
endif()

# Ensure shaders are compiled before the demo runs so the runtime pipeline
# cache sees up-to-date SPIR-V.
add_dependencies(lxe_editor CompileShaders)
