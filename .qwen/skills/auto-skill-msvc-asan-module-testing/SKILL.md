---
name: msvc-asan-module-testing
description: MSVC ASAN + C++20 modules 构建配置——所有 target 必须同时启用 ASAN，修改编译选项后需清理 BMI 缓存
source: auto-skill
extracted_at: '2026-06-18T08:30:00.000Z'
---

# MSVC AddressSanitizer 与 C++20 模块测试

## 核心规则

ASAN 必须同时加到**所有被链接的 target**（库 + 可执行文件）上，否则 STL 的 `annotate_string` / `annotate_vector` 元数据不匹配会导致链接失败。

## 正确的 CMakeLists.txt 配置

### 问题：仅测试 target 启用 ASAN → LNK2038

```cmake
# ❌ 错误 — 只给 test exe 加 ASAN，库没加
target_compile_options(lxe_test_new_modules PRIVATE /fsanitize=address)
target_link_options(lxe_test_new_modules PRIVATE /fsanitize=address)
```

**症状**：
```
LX_New_Common.lib(time.cppm.obj) : error LNK2038: 检测到"annotate_string"的不匹配项:
  值"0"不匹配值"1"(main.cpp.obj 中)
LX_New_Common.lib(raw_buffer.cppm.obj) : error LNK2038: 检测到"annotate_vector"的不匹配项:
  值"0"不匹配值"1"(main.cpp.obj 中)
src\test\new\lxe_test_new_modules.exe : fatal error LNK1319: 检测到 N 个不匹配项
```

**根因**：MSVC 的 STL 在 ASAN 启用时会在容器类型上添加额外标注（`annotate_string=1`, `annotate_vector=1`）。如果 exe 编译时 ASAN=ON（标注=1）而库编译时 ASAN=OFF（标注=0），跨边界的 STL 类型定义不一致。

### 解决：所有 target 同时启用

```cmake
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN AND MSVC)
  # 对测试 exe 和所有依赖库同时启用 ASAN
  foreach(_t lxe_test_new_modules ${LX_NEW_CORE_LIB} ${LX_NEW_COMMON_LIB})
    if(TARGET ${_t})
      target_compile_options(${_t} PRIVATE /fsanitize=address)
    endif()
  endforeach()
  # ASAN 需要关闭增量链接
  target_link_options(lxe_test_new_modules PRIVATE /INCREMENTAL:NO)
elseif(ENABLE_ASAN)
  # GCC/Clang: 全局 add_compile_options 更简单
  target_compile_options(lxe_test_new_modules PRIVATE
    -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lxe_test_new_modules PRIVATE
    -fsanitize=address -fno-omit-frame-pointer)
endif()
```

**注意**：`/fsanitize=address` 是编译器选项，不是链接器选项。不要通过 `target_link_options` 传递 `/fsanitize=address`（link.exe 不识别它，会报 LNK4044 warning）。只需 `target_compile_options` 即可——编译器会在 `.obj` 中嵌入 ASAN 元数据，链接器据此自动链接 ASAN 运行时。

## 修改编译选项后必须清理 BMI 缓存

### 问题：ASAN 启用/关闭后 C3474

当编译选项改变（如添加或移除 `/fsanitize=address`），MSVC 的 `.modmap` 和 `.ifc` 缓存文件会过期，导致：

```
error C3474: 无法打开输出文件"src\...\LX_New_Common.Platform-Types.ifc"
```

### 解决方案

```powershell
# 删除所有模块中间产物，然后重新 configure + build
del /S /Q src\new_common\CMakeFiles\LX_New_Common.dir\*.modmap src\new_common\CMakeFiles\LX_New_Common.dir\*.ifc src\new_common\CMakeFiles\LX_New_Common.dir\*.obj src\new_common\CMakeFiles\LX_New_Common.dir\CXX.dd 2>nul
del /S /Q src\new_core\CMakeFiles\LX_New_Core.dir\*.modmap src\new_core\CMakeFiles\LX_New_Core.dir\*.ifc src\new_core\CMakeFiles\LX_New_Core.dir\*.obj src\new_core\CMakeFiles\LX_New_Core.dir\CXX.dd 2>nul
del /S /Q src\test\new\CMakeFiles\lxe_test_new_modules.dir\*.modmap src\test\new\CMakeFiles\lxe_test_new_modules.dir\*.ifc src\test\new\CMakeFiles\lxe_test_new_modules.dir\*.obj src\test\new\CMakeFiles\lxe_test_new_modules.dir\CXX.dd 2>nul

cmake .. -DENABLE_ASAN=ON -G Ninja
cmake --build . --target lxe_test_new_modules --config Debug
```

**关键**：删除 `.modmap` 文件是必须的——它们记录了模块依赖关系，编译选项变化后内容会过期。

## 运行 ASAN 测试：DLL 路径

MSVC 的 ASAN 默认使用动态链接（`/MDd` + dynamic runtime）。运行时需要找到 ASAN DLL：

```powershell
# 方法 1：将 MSVC bin 目录加入 PATH
set "PATH=C:\Program Files\Microsoft Visual Studio\2022\<Edition>\VC\Tools\MSVC\<version>\bin\Hostx64\x64;%PATH%"
.\build\src\test\new\lxe_test_new_modules.exe

# 方法 2：复制 DLL 到 exe 旁边
copy "...\bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" .\build\src\test\new\
```

**症状**（未配置 DLL 路径）：退出码 `-1073741515` (`0xC0000135` = `STATUS_DLL_NOT_FOUND`)，无输出。

## 完整工作流

```powershell
# 1. 配置
cd build
cmake .. -DENABLE_ASAN=ON -G Ninja

# 2. （首次或编译选项变更后）清理模块缓存
# 见上方"解决方案"中的 del 命令

# 3. 重新 configure（会重新生成 .modmap）
cmake .. -DENABLE_ASAN=ON -G Ninja

# 4. 构建
cmake --build . --target lxe_test_new_modules --config Debug

# 5. 运行（确保 ASAN DLL 在 PATH 中）
set "PATH=<MSVC_BIN_PATH>;%PATH%"
.\build\src\test\new\lxe_test_new_modules.exe
```

如果 ASAN 检测到泄漏或溢出，程序会输出详细的 `ERROR: AddressSanitizer:` 报告并以非零退出码终止。退出码 0 = 无问题。
