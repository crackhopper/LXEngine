add_library(lxe_image_probe_core STATIC image_probe.cpp)
target_link_libraries(lxe_image_probe_core
  PRIVATE
  ${CORE_LIB}
  ${INFRA_LIB}
)
target_include_directories(lxe_image_probe_core
  PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  PRIVATE
  ${CMAKE_SOURCE_DIR}/src/infra/external/include
)

add_executable(lxe_image_probe main.cpp)
target_link_libraries(lxe_image_probe
  PRIVATE
  lxe_image_probe_core
  ${INFRA_LIB}
)
