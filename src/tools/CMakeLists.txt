find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_subdirectory(lxe_compare_exr)
add_subdirectory(lxe_offline_render)
add_subdirectory(lxe_realtime_render)

add_custom_target(RunRealtimeOfflineDiagnostic
  COMMAND $<TARGET_FILE:lxe_offline_render>
    --scene assets/scenes/realtime_offline_compare_diagnostic.scene.yaml
    --profile preview
    --out artifacts/compare/diagnostic/offline/render
  COMMAND $<TARGET_FILE:lxe_compare_exr>
    --reference artifacts/compare/diagnostic/offline/render.exr
    --candidate "artifacts/compare/diagnostic/realtime/Realtime%20Offline%20Compare%20Diagnostic/preview/render-linear.exr"
    --diagnostic-radius 2
  DEPENDS lxe_offline_render lxe_compare_exr
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)

add_custom_target(RunRealtimeOfflineHelmetPbrCompare
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/src/tools/lxe_realtime_render/lxe_realtime_render.py
    --scene assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml
    --profile preview
    --editor $<TARGET_FILE:lxe_editor>
    --xvfb
    --timeout-sec 120
  COMMAND $<TARGET_FILE:lxe_offline_render>
    --scene assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml
    --profile preview
    --out artifacts/compare/helmet_pbr/offline/render
  COMMAND $<TARGET_FILE:lxe_compare_exr>
    --reference artifacts/compare/helmet_pbr/offline/render.exr
    --candidate "artifacts/compare/helmet_pbr/realtime/Realtime%20Offline%20Compare%20Helmet%20PBR/preview/render-linear.exr"
    --diagnostic-radius 2
  DEPENDS lxe_realtime_render lxe_offline_render lxe_compare_exr
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)
