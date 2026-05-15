# REQ-019: default integration demo. Loads DamagedHelmet, drives EngineLoop,
# wires Orbit/FreeFly cameras, and renders a minimal ImGui overlay on top of
# the existing VulkanRenderer.

execute_process(
  COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse HEAD
  OUTPUT_VARIABLE LXE_EDITOR_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse --short=12 HEAD
  OUTPUT_VARIABLE LXE_EDITOR_GIT_COMMIT_SHORT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND git -C "${CMAKE_SOURCE_DIR}" status --porcelain
  OUTPUT_VARIABLE LXE_EDITOR_GIT_STATUS
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if("${LXE_EDITOR_GIT_COMMIT}" STREQUAL "")
  set(LXE_EDITOR_GIT_COMMIT "unknown")
endif()
if("${LXE_EDITOR_GIT_COMMIT_SHORT}" STREQUAL "")
  set(LXE_EDITOR_GIT_COMMIT_SHORT "unknown")
endif()
if("${LXE_EDITOR_GIT_STATUS}" STREQUAL "")
  set(LXE_EDITOR_GIT_DIRTY 0)
else()
  set(LXE_EDITOR_GIT_DIRTY 1)
endif()
if(CMAKE_CONFIGURATION_TYPES)
  set(LXE_EDITOR_BUILD_TYPE "$<CONFIG>")
elseif(CMAKE_BUILD_TYPE)
  set(LXE_EDITOR_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
  set(LXE_EDITOR_BUILD_TYPE "unknown")
endif()
string(TIMESTAMP LXE_EDITOR_BUILT_AT "%Y-%m-%dT%H:%M:%SZ" UTC)

add_executable(lxe_editor
  api_token_state.cpp
  builtin_asset_catalog.cpp
  editor_session.cpp
  main.cpp
  camera_rig.cpp
  display_launch_options.cpp
  lxe_editor_api_protocol.cpp
  lxe_editor_api_server.cpp
  lxe_editor_api_service.cpp
  lxe_editor_build_info.cpp
  project_catalog.cpp
  project_document.cpp
  project_session.cpp
  recording_controller.cpp
  editor_config_state.cpp
  editor_data_state.cpp
  editor_log_file.cpp
  editor_camera_state.cpp
  editor_scene_state.cpp
  runtime_state.cpp
  scene_builder.cpp
  scene_document.cpp
  scene_interaction_controller.cpp
  scene_input_routing.cpp
  scene_view_rect.cpp
  lxe_editor_commands.cpp
  scene_runtime.cpp
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

target_compile_definitions(lxe_editor PRIVATE
  "LXE_EDITOR_GIT_COMMIT=\"${LXE_EDITOR_GIT_COMMIT}\""
  "LXE_EDITOR_GIT_COMMIT_SHORT=\"${LXE_EDITOR_GIT_COMMIT_SHORT}\""
  "LXE_EDITOR_GIT_DIRTY=${LXE_EDITOR_GIT_DIRTY}"
  "LXE_EDITOR_BUILD_TYPE=\"${LXE_EDITOR_BUILD_TYPE}\""
  "LXE_EDITOR_BUILT_AT=\"${LXE_EDITOR_BUILT_AT}\""
)

if(WIN32)
  target_link_libraries(lxe_editor PRIVATE ws2_32)
endif()

# Ensure shaders are compiled before the demo runs so the runtime pipeline
# cache sees up-to-date SPIR-V.
add_dependencies(lxe_editor CompileShaders)
