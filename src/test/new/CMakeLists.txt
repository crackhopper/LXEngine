# 针对 LX_new 模块分层的 smoke exe。
file(GLOB_RECURSE TEST_MODULE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cppm")

add_executable(lxe_test_new_modules main.cpp)
target_sources(lxe_test_new_modules
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES
    FILES ${TEST_MODULE_SOURCES}
)

# 纯消费方 exe：显式开启模块扫描，确保 import 的 BMI 被注入编译命令。
set_target_properties(lxe_test_new_modules PROPERTIES CXX_SCAN_FOR_MODULES ON)

target_link_libraries(lxe_test_new_modules
  PRIVATE
    ${LX_NEW_CORE_LIB}
    ${LX_NEW_COMMON_LIB}
)

add_test(NAME lxe_test_new_modules
  COMMAND $<TARGET_FILE:lxe_test_new_modules>)
set_tests_properties(lxe_test_new_modules PROPERTIES
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  LABELS "auto;headless_ok"
)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN AND MSVC)
  # ASAN 必须同时加到被链接的库上，否则 STL 的 annotate_* 元数据不匹配 (LNK2038)
  foreach(_t lxe_test_new_modules ${LX_NEW_CORE_LIB} ${LX_NEW_COMMON_LIB})
    if(TARGET ${_t})
      target_compile_options(${_t} PRIVATE /fsanitize=address)
    endif()
  endforeach()
  target_link_options(lxe_test_new_modules PRIVATE /INCREMENTAL:NO)
elseif(ENABLE_ASAN)
  target_compile_options(lxe_test_new_modules PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lxe_test_new_modules PRIVATE -fsanitize=address -fno-omit-frame-pointer)
endif()
