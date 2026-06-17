# LX_New_Common: 底层基础库（math / memory / platform / container / log ...）
# 采用 C++20 named modules。FILE_SET CXX_MODULES 需要 CMake 3.28+。
if(CMAKE_VERSION VERSION_LESS 3.28)
  message(FATAL_ERROR
    "LX_New_Common requires CMake 3.28+ for C++20 module support "
    "(FILE_SET CXX_MODULES). Current CMake: ${CMAKE_VERSION}")
endif()

set(CMAKE_CL_SHOWINCLUDES_PREFIX "")
set(LX_NEW_COMMON_LIB LX_New_Common CACHE INTERNAL "New common base library")
  
file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS
 "${CMAKE_CURRENT_SOURCE_DIR}/*.cppm")

add_library(${LX_NEW_COMMON_LIB} STATIC)

# Enable module dependency scanning so MSVC+Ninja resolves partition ordering
set_target_properties(${LX_NEW_COMMON_LIB} PROPERTIES CXX_SCAN_FOR_MODULES ON)

target_sources(${LX_NEW_COMMON_LIB}
  PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${COMMON_SOURCES}
)

target_compile_features(${LX_NEW_COMMON_LIB} PUBLIC cxx_std_20)
