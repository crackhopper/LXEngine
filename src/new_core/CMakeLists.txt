# LX_New_Core: 新的 core 层，依赖 LX_New_Common。
# 采用 C++20 named modules。FILE_SET CXX_MODULES 需要 CMake 3.28+。
if(CMAKE_VERSION VERSION_LESS 3.28)
  message(FATAL_ERROR
    "LX_New_Core requires CMake 3.28+ for C++20 module support "
    "(FILE_SET CXX_MODULES). Current CMake: ${CMAKE_VERSION}")
endif()

set(LX_NEW_CORE_LIB LX_New_Core CACHE INTERNAL "New core library")

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
 "${CMAKE_CURRENT_SOURCE_DIR}/*.cppm")
add_library(${LX_NEW_CORE_LIB} STATIC)

target_sources(${LX_NEW_CORE_LIB}
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${CORE_SOURCES}
)

set_target_properties(${LX_NEW_CORE_LIB} PROPERTIES CXX_SCAN_FOR_MODULES ON)

target_link_libraries(${LX_NEW_CORE_LIB} PUBLIC ${LX_NEW_COMMON_LIB})

target_compile_features(${LX_NEW_CORE_LIB} PUBLIC cxx_std_20)
