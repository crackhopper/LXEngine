add_subdirectory(lxe_compare_exr)
add_subdirectory(lxe_offline_render)
add_subdirectory(lxe_realtime_render)

if(UNIX AND NOT APPLE)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  add_custom_target(RunRealtimeOfflineDiagnostic
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/src/tools/lxe_realtime_render/lxe_realtime_render.py
      --scene assets/scenes/realtime_offline_compare_diagnostic.scene.yaml
      --profile preview
      --editor $<TARGET_FILE:lxe_editor>
      --xvfb
      --timeout-sec 120
    COMMAND $<TARGET_FILE:lxe_offline_render>
      --scene assets/scenes/realtime_offline_compare_diagnostic.scene.yaml
      --profile preview
      --out artifacts/compare/diagnostic/offline/render
    COMMAND $<TARGET_FILE:lxe_compare_exr>
      --reference artifacts/compare/diagnostic/offline/render.exr
      --candidate "artifacts/compare/diagnostic/realtime/Realtime%20Offline%20Compare%20Diagnostic/preview/render-linear.exr"
      --diagnostic-radius 2
    DEPENDS lxe_realtime_render lxe_offline_render lxe_compare_exr
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM
  )

  add_custom_target(RunRealtimeOfflineHelmetPbrCompareHeadless
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
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/src/tools/lxe_compare_exr/lxe_compare_exr_gate.py
      --compare-tool $<TARGET_FILE:lxe_compare_exr>
      --reference artifacts/compare/helmet_pbr/offline/render.exr
      --candidate "artifacts/compare/helmet_pbr/realtime/Realtime%20Offline%20Compare%20Helmet%20PBR/preview/render-linear.exr"
      --max-mean-abs-error 0.05
      --max-rmse 0.15
      --linear-l1-threshold 0.4
      --min-linear-l1-ratio 0.90
      --min-reference-lit-pixels 8000
      --min-candidate-lit-pixels 8000
      --min-lit-pixel-ratio 0.75
      --max-lit-pixel-ratio 1.35
      --max-centroid-delta 8
    DEPENDS lxe_realtime_render lxe_offline_render lxe_compare_exr
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM
  )

  add_custom_target(RunRealtimeOfflineHelmetPbrCompare
    DEPENDS RunRealtimeOfflineHelmetPbrCompareHeadless
  )
endif()
