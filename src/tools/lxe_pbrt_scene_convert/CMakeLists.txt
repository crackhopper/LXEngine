find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(LXE_PBRT_SCENE_CONVERT
  ${CMAKE_CURRENT_SOURCE_DIR}/lxe_pbrt_scene_convert.py
)

set(LXE_PBRT_BMW_M6_SOURCE "" CACHE FILEPATH
  "Path to the external PBRT v3 bmw-m6.pbrt file for RunPbrtBmwM6Convert"
)

add_custom_target(lxe_pbrt_scene_convert
  COMMAND ${Python3_EXECUTABLE} ${LXE_PBRT_SCENE_CONVERT} --help
  DEPENDS ${LXE_PBRT_SCENE_CONVERT}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)

if(LXE_PBRT_BMW_M6_SOURCE)
add_custom_target(RunPbrtBmwM6Convert
  COMMAND ${Python3_EXECUTABLE} ${LXE_PBRT_SCENE_CONVERT}
    --input ${LXE_PBRT_BMW_M6_SOURCE}
    --out assets/models/bmw-m6
    --scene assets/models/bmw-m6/pbrt_bmw_m6.scene.yaml
  DEPENDS ${LXE_PBRT_SCENE_CONVERT}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)
else()
add_custom_target(RunPbrtBmwM6Convert
  COMMAND ${CMAKE_COMMAND} -E echo
    "Set -DLXE_PBRT_BMW_M6_SOURCE=/path/to/bmw-m6.pbrt before running RunPbrtBmwM6Convert"
  COMMAND ${CMAKE_COMMAND} -E false
  DEPENDS ${LXE_PBRT_SCENE_CONVERT}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)
endif()

add_custom_target(RunPbrtBmwM6OfflineSmoke
  COMMAND ${CMAKE_COMMAND} -E make_directory artifacts/pbrt/bmw-m6/offline-smoke
  COMMAND $<TARGET_FILE:lxe_offline_render>
    --scene assets/models/bmw-m6/pbrt_bmw_m6.scene.yaml
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
