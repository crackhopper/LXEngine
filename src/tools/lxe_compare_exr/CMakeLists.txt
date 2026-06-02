add_executable(lxe_compare_exr main.cpp)
target_link_libraries(lxe_compare_exr
  PRIVATE
  ${CORE_LIB}
  ${INFRA_LIB}
)
