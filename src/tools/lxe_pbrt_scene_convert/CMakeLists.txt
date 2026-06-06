find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(LXE_PBRT_SCENE_CONVERT
  ${CMAKE_CURRENT_SOURCE_DIR}/lxe_pbrt_scene_convert.py
)

add_custom_target(lxe_pbrt_scene_convert
  COMMAND ${Python3_EXECUTABLE} ${LXE_PBRT_SCENE_CONVERT} --help
  DEPENDS ${LXE_PBRT_SCENE_CONVERT}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)

add_custom_target(RunPbrtBmwM6Convert
  COMMAND ${Python3_EXECUTABLE} ${LXE_PBRT_SCENE_CONVERT}
    --input data/pbrt-v3-scenes/pbrt-v3-scenes/bmw-m6/bmw-m6.pbrt
    --out data/scenes/bmw-m6
    --scene data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml
  DEPENDS ${LXE_PBRT_SCENE_CONVERT}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)

add_custom_target(RunPbrtBmwM6OfflineSmoke
  COMMAND ${CMAKE_COMMAND} -E make_directory artifacts/pbrt/bmw-m6/offline-smoke
  COMMAND $<TARGET_FILE:lxe_offline_render>
    --scene data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml
    --profile offline-pbrt-reference
    --width 320
    --height 228
    --samples 1
    --max-bounce 1
    --out artifacts/pbrt/bmw-m6/offline-smoke/render
  DEPENDS lxe_offline_render
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)
