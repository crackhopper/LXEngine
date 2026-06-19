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

set(LX_PROJECT_VERSION "0.2.0-pre" CACHE STRING
    "LXEngine project version embedded into build metadata")

# 导出 compile_commands.json，供 clangd / cpptools 智能提示使用。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

option(LX_BUILD_EDITOR "Build the lxe_editor application" ON)
option(LX_BUILD_NEW_TRACK "Build C++20 module rewrite libraries and tests" OFF)
if(LX_BUILD_EDITOR)
  add_subdirectory(src/editor)
endif()

if(TARGET imgui)
  target_link_libraries(${CORE_LIB} PRIVATE imgui)
endif()


# -------------------------------
# 构建期 runtime-root 同步：build/assets 作为统一资产根。
# 非 shader 资产做 symlink/copy；shader source+spv 由 assets/shaders 维护。
# -------------------------------
if(IS_SYMLINK "${CMAKE_BINARY_DIR}/assets")
  file(REMOVE "${CMAKE_BINARY_DIR}/assets")
endif()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/assets")

function(lx_sync_asset_subdir subdir_name)
  set(_src "${CMAKE_SOURCE_DIR}/assets/${subdir_name}")
  set(_dst "${CMAKE_BINARY_DIR}/assets/${subdir_name}")
  if(NOT EXISTS "${_src}")
    return()
  endif()
  if(NOT EXISTS "${_dst}")
    file(CREATE_LINK
      "${_src}"
      "${_dst}"
      SYMBOLIC
      RESULT _lx_link_result
    )
    if(NOT _lx_link_result EQUAL 0)
      message(STATUS "Symlink failed for assets/${subdir_name} (${_lx_link_result}), copying to build dir")
      file(COPY "${_src}" DESTINATION "${CMAKE_BINARY_DIR}/assets")
    endif()
  endif()
endfunction()

lx_sync_asset_subdir("env")
lx_sync_asset_subdir("models")
lx_sync_asset_subdir("textures")
lx_sync_asset_subdir("materials")
lx_sync_asset_subdir("scenes")
lx_sync_asset_subdir("render_paths")

file(GLOB LX_CONFIGURE_TIME_TMP_PATHS
  LIST_DIRECTORIES true
  "${CMAKE_SOURCE_DIR}/tmp_*"
)
file(GLOB_RECURSE LX_CONFIGURE_TIME_SOURCE_SPV
  "${CMAKE_SOURCE_DIR}/assets/shaders/*.spv"
)
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
  "${CMAKE_SOURCE_DIR}/artifacts"
  "${CMAKE_SOURCE_DIR}/.pytest_cache"
  ${LX_CONFIGURE_TIME_TMP_PATHS}
  ${LX_CONFIGURE_TIME_SOURCE_SPV}
)

add_custom_target(lxe_clean_workspace
  COMMAND ${CMAKE_COMMAND}
    -D "LXE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_SOURCE_DIR}/cmake/lxe_clean_workspace.cmake"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Remove LXEngine workspace temporary files"
  VERBATIM
)

# 引入 shader 编译 target
add_subdirectory(assets/shaders)
add_subdirectory(src/tools)

# -------------------------------
# 新分层（C++20 modules）：new_common 基础库 -> new_core
# 与旧 core/infra/backend 完全隔离。
# -------------------------------
if(LX_BUILD_NEW_TRACK)
  add_subdirectory(src/new_common)
  add_subdirectory(src/new_core)
endif()

add_subdirectory(src/test)
