add_custom_target(lxe_realtime_render
  COMMAND ${CMAKE_COMMAND} -E echo
          "Run ${CMAKE_SOURCE_DIR}/src/tools/lxe_realtime_render/lxe_realtime_render.py"
)

if(TARGET lxe_editor)
  add_dependencies(lxe_realtime_render lxe_editor)
endif()
