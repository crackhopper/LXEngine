add_library(lxe_offline_render_cli STATIC offline_render_cli.cpp)
target_link_libraries(lxe_offline_render_cli
  PUBLIC
  ${CORE_LIB}
  ${INFRA_LIB}
)
target_include_directories(lxe_offline_render_cli PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_executable(lxe_offline_render main.cpp)
target_link_libraries(lxe_offline_render
  PRIVATE
  ${CORE_LIB}
  ${INFRA_LIB}
  ${GRAPHICS_LIB}
  lxe_offline_render_cli
)
add_dependencies(lxe_offline_render CompileShaders)
