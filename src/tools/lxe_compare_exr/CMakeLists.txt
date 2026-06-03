add_library(lxe_compare_exr_core STATIC exr_compare_metrics.cpp)
target_link_libraries(lxe_compare_exr_core
  PRIVATE
  ${CORE_LIB}
)
target_include_directories(lxe_compare_exr_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_executable(lxe_compare_exr main.cpp)
target_link_libraries(lxe_compare_exr
  PRIVATE
  lxe_compare_exr_core
  ${INFRA_LIB}
)
