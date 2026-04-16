cmake_minimum_required(VERSION 3.16)

# CMake 4.x no longer tolerates some old third-party policy baselines.
# Keep compatibility handling in-project instead of relying on CLI flags.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

# 项目名称
project(Renderer LANGUAGES C CXX)

# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设定 C++ 20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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




# -------------------------------
# 构建期资产同步：symlink 优先，copy 兜底
# -------------------------------
if(NOT EXISTS "${CMAKE_BINARY_DIR}/assets")
  file(CREATE_LINK
    "${CMAKE_SOURCE_DIR}/assets"
    "${CMAKE_BINARY_DIR}/assets"
    SYMBOLIC
    RESULT _assets_link_result
  )
  if(NOT _assets_link_result EQUAL 0)
    message(STATUS "Symlink failed (${_assets_link_result}), copying assets to build dir")
    file(COPY "${CMAKE_SOURCE_DIR}/assets" DESTINATION "${CMAKE_BINARY_DIR}")
  endif()
endif()

# 引入shader编译的target
add_subdirectory(shaders)

# -------------------------------
# 主项目
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





