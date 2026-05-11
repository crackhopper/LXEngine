# REQ-019: default integration demo. Loads DamagedHelmet, drives EngineLoop,
# wires Orbit/FreeFly cameras, and renders a minimal ImGui overlay on top of
# the existing VulkanRenderer.

add_executable(demo_scene_viewer
  main.cpp
  camera_rig.cpp
  editor_camera_state.cpp
  scene_builder.cpp
  scene_document.cpp
  scene_runtime.cpp
  ui_overlay.cpp
)

target_include_directories(demo_scene_viewer PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/infra/external/yaml-cpp/include
)

target_link_libraries(demo_scene_viewer PRIVATE
  ${CORE_LIB}
  ${INFRA_LIB}
  ${GRAPHICS_LIB}
  imgui
  yaml-cpp::yaml-cpp
)

# Ensure shaders are compiled before the demo runs so the runtime pipeline
# cache sees up-to-date SPIR-V.
add_dependencies(demo_scene_viewer CompileShaders)
