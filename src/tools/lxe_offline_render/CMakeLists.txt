add_library(lxe_offline_render_cli STATIC offline_render_cli.cpp)
target_link_libraries(lxe_offline_render_cli
  PUBLIC
  ${CORE_LIB}
  ${INFRA_LIB}
)
target_include_directories(lxe_offline_render_cli PUBLIC ${CMAKE_SOURCE_DIR}/src)

execute_process(
  COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse HEAD
  OUTPUT_VARIABLE LXE_OFFLINE_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND git -C "${CMAKE_SOURCE_DIR}" status --porcelain
  OUTPUT_VARIABLE LXE_OFFLINE_GIT_STATUS
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if("${LXE_OFFLINE_GIT_COMMIT}" STREQUAL "")
  set(LXE_OFFLINE_GIT_COMMIT "unknown")
endif()
if("${LXE_OFFLINE_GIT_STATUS}" STREQUAL "")
  set(LXE_OFFLINE_GIT_DIRTY 0)
else()
  set(LXE_OFFLINE_GIT_DIRTY 1)
endif()

add_executable(lxe_offline_render main.cpp)
target_link_libraries(lxe_offline_render
  PRIVATE
  ${CORE_LIB}
  ${INFRA_LIB}
  ${GRAPHICS_LIB}
  lxe_offline_render_cli
)
add_dependencies(lxe_offline_render CompileShaders)
target_compile_definitions(lxe_offline_render PRIVATE
  "LXE_OFFLINE_GIT_COMMIT=\"${LXE_OFFLINE_GIT_COMMIT}\""
  "LXE_OFFLINE_GIT_DIRTY=${LXE_OFFLINE_GIT_DIRTY}"
)
