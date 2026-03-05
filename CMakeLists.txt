cmake_minimum_required(VERSION 3.16)

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
add_subdirectory(src/graphics_backend)
add_subdirectory(src/test)




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
  ${APPLICATION_LIB}
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






