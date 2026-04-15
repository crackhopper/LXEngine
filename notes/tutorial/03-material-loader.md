# 03 · PBR Material Loader

> 写 `loadPbrCubeMaterial()` — 把 `pbr_cube.*` 变成一个可以挂到 `RenderableSubMesh` 的 `MaterialInstance::Ptr`。

## 参考样板

直接读一遍 `src/infra/material_loader/blinn_phong_material_loader.cpp`，这是唯一一个现存 loader。我们的 PBR loader 的结构是**一对一复制** + 改名字 + 改默认值。

```
复制 → blinnphong_material_loader.{cpp,hpp}
重命名 → pbr_cube_material_loader.{cpp,hpp}
改 baseName → "pbr_cube"
改默认 seed → metallic / roughness / ao / baseColor
```

## 文件位置

```
src/infra/loaders/pbr_cube_material_loader.hpp
src/infra/loaders/pbr_cube_material_loader.cpp
```

## hpp

```cpp
// src/infra/loaders/pbr_cube_material_loader.hpp
#pragma once
#include "core/asset/material.hpp"
#include "core/frame_graph/pass.hpp"

namespace LX_infra {

/// 构造一个 Cook–Torrance PBR 材质实例。
/// shader: shaders/glsl/pbr_cube.{vert,frag}
/// 需要的 binding 名:
///   - set 0: "CameraUBO"
///   - set 1: "MaterialUBO"
///   - set 2: "LightUBO"
/// 默认 seed:
///   baseColor  = (0.85, 0.20, 0.20)   稍微偏亚麻红
///   metallic   = 0.0                  电介质
///   roughness  = 0.35                 有一点高光，但不是镜面
///   ao         = 1.0
LX_core::MaterialInstance::Ptr
loadPbrCubeMaterial(LX_core::ResourcePassFlag passFlag =
                        LX_core::ResourcePassFlag::Forward);

} // namespace LX_infra
```

## cpp

对照 `blinnphong_material_loader.cpp` 的每一段：

```cpp
// src/infra/loaders/pbr_cube_material_loader.cpp
#include "infra/loaders/pbr_cube_material_loader.hpp"
#include "core/frame_graph/pass.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace LX_infra {

namespace fs = std::filesystem;

LX_core::MaterialInstance::Ptr
loadPbrCubeMaterial(LX_core::ResourcePassFlag passFlag) {
    const std::string baseName = "pbr_cube";

    // ── 1. 定位 GLSL 源文件 ─────────────────────────────
    // 和 blinnphong loader 一样向上最多回溯 4 层找 shaders/glsl/。
    // 运行期 app 启动时会先 cdToWhereShadersExist() 来校准 cwd。
    fs::path cwd = fs::current_path();
    fs::path glslDir;
    for (int i = 0; i < 4; ++i) {
        fs::path candidate = cwd / "shaders" / "glsl";
        if (fs::exists(candidate / (baseName + ".vert")) &&
            fs::exists(candidate / (baseName + ".frag"))) {
            glslDir = std::move(candidate);
            break;
        }
        fs::path parent = cwd.parent_path();
        if (parent == cwd) break;
        cwd = parent;
    }
    if (glslDir.empty()) {
        throw std::runtime_error(
            "pbr_cube GLSL sources not found (expected .../shaders/glsl/)");
    }

    const fs::path vert = glslDir / (baseName + ".vert");
    const fs::path frag = glslDir / (baseName + ".frag");

    // ── 2. 编译 + 反射 ─────────────────────────────────
    auto compiled = ShaderCompiler::compileProgram(vert, frag, {});
    if (!compiled.success) {
        throw std::runtime_error("pbr_cube compile failed: " +
                                 compiled.errorMessage);
    }

    auto bindings = ShaderReflector::reflect(compiled.stages);
    auto shader   = std::make_shared<CompiledShader>(
        std::move(compiled.stages), bindings, baseName);

    // ── 3. MaterialTemplate ────────────────────────────
    auto tmpl = LX_core::MaterialTemplate::create(baseName, shader);

    LX_core::ShaderProgramSet programSet;
    programSet.shaderName = baseName;

    LX_core::RenderPassEntry entry;
    entry.shaderSet   = programSet;
    entry.renderState = LX_core::RenderState{}; // 默认：深度测试开、back-face cull
    entry.buildCache();
    tmpl->setPass(LX_core::Pass_Forward, std::move(entry));
    tmpl->buildBindingCache();

    // ── 4. MaterialInstance + 默认 seed ────────────────
    auto mat = LX_core::MaterialInstance::create(tmpl, passFlag);

    mat->setVec3 (LX_core::StringID("baseColor"), LX_core::Vec3f{0.85f, 0.20f, 0.20f});
    mat->setFloat(LX_core::StringID("metallic"),  0.0f);
    mat->setFloat(LX_core::StringID("roughness"), 0.35f);
    mat->setFloat(LX_core::StringID("ao"),        1.0f);
    mat->updateUBO();

    return mat;
}

} // namespace LX_infra
```

## 为什么是这个流程

每一步对应的责任：

| 阶段 | 模块 | 为什么必需 |
|------|------|-----------|
| GLSL → SPIR-V | `ShaderCompiler` | 运行期编译让我们改 shader 不用重启构建 |
| SPIR-V → ShaderResourceBinding | `ShaderReflector` | 反射驱动的材质绑定，不用手写 set/binding 表 |
| `CompiledShader` 包装 | `src/infra/shader_compiler/compiled_shader.hpp` | 把 bytecode + bindings 封成 `IShader` |
| `MaterialTemplate::create` | `core/asset/material.hpp:175` | 蓝图：一个材质模板可以实例化多次 |
| `setPass(Pass_Forward, entry)` | 同上 | 某个 pass 下用哪套 shader + render state |
| `buildBindingCache()` | 同上 | 把反射结果做成 `StringID → ShaderResourceBinding` 查表 |
| `MaterialInstance::create` | 同上:272 | 真正分配 std140 字节 buffer 的 owner |
| `setVec3 / setFloat` | 同上 | 对照反射 member 偏移量，按类型校验写入 |
| `updateUBO()` | 同上 | 标 `m_uboResource` dirty，下一帧 `syncResource` 推 GPU |

### MaterialUBO 名字约定

再强调一次：

> **`MaterialUBO` 必须叫这个名字**。`MaterialInstance` 构造时硬编码查找 `binding.name == "MaterialUBO"` 来定位自己的 UBO。shader 里命名成别的（比如 `PbrParams`）会导致 `m_uboBinding == nullptr`，后面所有 setter 都会 assert fail。

PBR shader 里写 `layout(...) uniform MaterialUBO { ... } material;` 就是为了这个。`material` 是 GLSL 的实例名，可以随便改；`MaterialUBO` 是块名，不能改。

---

## 加入 CMake 构建

`src/infra/CMakeLists.txt` 里原本加的是 `loaders/blinnphong_material_loader.cpp`。把新 loader 追加进去：

```cmake
# src/infra/CMakeLists.txt  (节选)
target_sources(${INFRA_LIB} PRIVATE
    loaders/blinnphong_material_loader.cpp
    loaders/pbr_cube_material_loader.cpp   # ← 新增
    # ... 其他 sources
)
```

具体文件位置可以通过以下方式核对：

```bash
grep -n blinnphong_material_loader src/infra/CMakeLists.txt
```

在那一行下面加一行 `loaders/pbr_cube_material_loader.cpp` 即可。

---

## 下一步

材质就绪。下一章构造立方体几何。

→ [04-cube-geometry.md](04-cube-geometry.md)
