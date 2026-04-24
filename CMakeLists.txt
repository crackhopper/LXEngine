cmake_minimum_required(VERSION 3.16)

# CMake 4.x no longer tolerates some old third-party policy baselines.
# Keep compatibility handling in-project instead of relying on CLI flags.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

set(LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV ON CACHE BOOL
    "Enable detailed Windows MSVC bootstrap logging")

# Windows: allow plain terminals to bootstrap the MSVC toolchain
# before CMake compiler detection runs in project().
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows_msvc_env.cmake")

# 项目名称
project(Renderer LANGUAGES C CXX)

# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

# 设定 C++ 20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LX_ENABLE_SANITIZERS "Enable ASan+UBSan for supported toolchains" OFF)
if(LX_ENABLE_SANITIZERS)
  if(MSVC)
    message(FATAL_ERROR "LX_ENABLE_SANITIZERS currently supports GCC/Clang only")
  endif()

  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR
      "LX_ENABLE_SANITIZERS requires GCC or Clang compatible toolchains")
  endif()

  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  message(STATUS "LX_ENABLE_SANITIZERS=ON: enabling AddressSanitizer and UndefinedBehaviorSanitizer")
endif()

# 设置编译器编码选项
if(MSVC)
    # MSVC: 明确指定使用 UTF-8
    add_compile_options(/utf-8)
else()
    # GCC/Clang: 设置源码字符集
    add_compile_options(-finput-charset=UTF-8)
    add_compile_options(-fexec-charset=UTF-8)
endif()


# 定义一个预编译宏，方便我们自己使用
add_definitions(-D_UTF8_SOURCE)



# -------------------------------
# 渲染器内部模块
# -------------------------------
add_subdirectory(src/core)
add_subdirectory(src/infra)
add_subdirectory(src/backend)
add_subdirectory(src/test)

# REQ-019: demo executables (default ON). Not registered with CTest.
option(LX_BUILD_DEMOS "Build demo executables" ON)
if(LX_BUILD_DEMOS)
  add_subdirectory(src/demos)
endif()




# -------------------------------
# 构建期 runtime-root 同步：symlink 优先，copy 兜底
# -------------------------------
function(lx_sync_runtime_dir dir_name)
  if(NOT EXISTS "${CMAKE_BINARY_DIR}/${dir_name}")
    file(CREATE_LINK
      "${CMAKE_SOURCE_DIR}/${dir_name}"
      "${CMAKE_BINARY_DIR}/${dir_name}"
      SYMBOLIC
      RESULT _lx_link_result
    )
    if(NOT _lx_link_result EQUAL 0)
      message(STATUS "Symlink failed for ${dir_name} (${_lx_link_result}), copying to build dir")
      file(COPY "${CMAKE_SOURCE_DIR}/${dir_name}" DESTINATION "${CMAKE_BINARY_DIR}")
    endif()
  endif()
endfunction()

lx_sync_runtime_dir("assets")
lx_sync_runtime_dir("materials")

# 引入shader编译的target
add_subdirectory(shaders)

# -------------------------------
# Bootstrap / env-probe executable
# Not the primary demo entrypoint. The user-facing integration demo lives at
# src/demos/scene_viewer/ as demo_scene_viewer.
# -------------------------------
add_executable(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(${PROJECT_NAME} 
  PRIVATE 
  ${CORE_LIB}
  ${INFRA_LIB}
  ${GRAPHICS_LIB}
)
add_dependencies(${PROJECT_NAME} CompileShaders)
if (WIN32)
# -------------------------------
# Windows 特殊：定义宏
# 关闭MSVC的安全警告，例如，用 strcpy_s 替代 strcpy，用 scanf_s 替代 scanf
# 提高可移植性
# -------------------------------
  target_compile_definitions(${PROJECT_NAME} PRIVATE "_CRT_SECURE_NO_WARNINGS")
endif()
