# 03 · PBR Material Loader

> 当前仓库**还没有** PBR loader。这里的任务不是复述一份过时接口，而是基于现在真实存在的 `loadBlinnPhongMaterial()`，说明 PBR loader 应该怎样接进现有材质系统。

## 参考样板

先读 `src/infra/material_loader/blinn_phong_material_loader.cpp`。它是当前唯一一个完整接进引擎的材质 loader，也是 PBR 版本应该直接复用的样板。

PBR 版最少要完成四件事：

1. 定位 `shaders/glsl/pbr.vert` / `pbr.frag`
2. 调 `ShaderCompiler::compileProgram(...)` + `ShaderReflector`
3. 构造 `MaterialTemplate` / `MaterialPassDefinition` / `MaterialInstance`
4. 给 `MaterialUBO` 和纹理绑定种子默认值

## 文件位置

```text
建议新增：
src/infra/material_loader/pbr_material_loader.hpp
src/infra/material_loader/pbr_material_loader.cpp
```

## hpp

```cpp
// src/infra/material_loader/pbr_material_loader.hpp
#pragma once
#include "core/asset/material_instance.hpp"
#include "core/frame_graph/pass.hpp"

namespace LX_infra {

LX_core::MaterialInstanceSharedPtr
loadPbrMaterial(LX_core::ResourcePassFlag passFlag =
                    LX_core::ResourcePassFlag::Forward);

} // namespace LX_infra
```

## cpp

核心结构应该和 `loadBlinnPhongMaterial()` 保持一致，只替换 shader 名、默认值和需要的纹理资源：

```cpp
// src/infra/material_loader/pbr_material_loader.cpp
#include "infra/material_loader/pbr_material_loader.hpp"
#include "core/frame_graph/pass.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace LX_infra {

namespace fs = std::filesystem;

LX_core::MaterialInstanceSharedPtr
loadPbrMaterial(LX_core::ResourcePassFlag passFlag) {
    const std::string baseName = "pbr";

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
            "pbr GLSL sources not found (expected .../shaders/glsl/)");
    }

    const fs::path vert = glslDir / (baseName + ".vert");
    const fs::path frag = glslDir / (baseName + ".frag");

    auto compiled = ShaderCompiler::compileProgram(vert, frag, {});
    if (!compiled.success) {
        throw std::runtime_error("pbr compile failed: " +
                                 compiled.errorMessage);
    }

    auto bindings = ShaderReflector::reflect(compiled.stages);
    auto vertexInputs = ShaderReflector::reflectVertexInputs(compiled.stages);
    auto shader = std::make_shared<CompiledShader>(
        std::move(compiled.stages), bindings, vertexInputs, baseName);

    auto tmpl = LX_core::MaterialTemplate::create(baseName, shader);

    LX_core::ShaderProgramSet programSet;
    programSet.shaderName = baseName;
    programSet.shader = shader;

    LX_core::MaterialPassDefinition entry;
    entry.shaderSet = programSet;
    entry.renderState = LX_core::RenderState{};
    entry.buildCache();
    tmpl->setPass(LX_core::Pass_Forward, std::move(entry));
    tmpl->buildBindingCache();

    auto mat = LX_core::MaterialInstance::create(tmpl, passFlag);

    mat->setVec4(LX_core::StringID("baseColorFactor"),
                 LX_core::Vec4f{0.85f, 0.20f, 0.20f, 1.0f});
    mat->setFloat(LX_core::StringID("metallicFactor"), 0.0f);
    mat->setFloat(LX_core::StringID("roughnessFactor"), 0.35f);
    mat->setFloat(LX_core::StringID("ao"), 1.0f);
    mat->syncGpuData();

    return mat;
}

} // namespace LX_infra
```

## 为什么必须按这个流程来

每一步对应的责任：

| 阶段 | 模块 | 为什么必需 |
|------|------|-----------|
| GLSL → SPIR-V | `ShaderCompiler` | 运行期编译让我们改 shader 不用重启构建 |
| SPIR-V → ShaderResourceBinding | `ShaderReflector` | 反射驱动的材质绑定，不用手写 set/binding 表 |
| `CompiledShader` 包装 | `src/infra/shader_compiler/compiled_shader.hpp` | 把 bytecode + bindings 封成 `IShader` |
| `MaterialTemplate::create` | `core/asset/material_template.hpp` | 蓝图：一个材质模板可以实例化多次 |
| `setPass(Pass_Forward, entry)` | 同上 | 某个 pass 下用哪套 shader + render state |
| `buildBindingCache()` | 同上 | 把反射结果做成 `StringID -> ShaderResourceBinding` 查表 |
| `MaterialInstance::create` | 同上 | 真正分配 material-owned buffer slots 并持有运行时资源状态 |
| `setVec3 / setFloat` | 同上 | 对照反射 member 偏移量，按类型校验写入 |
| `syncGpuData()` | 同上 | 标记所有 dirty 的参数资源，下一帧 `syncResource` 推 GPU |

### 材质 binding 名字约定

再强调一次：

> **`MaterialUBO` 不再是硬约定。** `MaterialInstance` 现在会把所有非系统保留的 material-owned buffer binding 都纳入自己的参数接口。你可以把块名写成 `PbrParams`、`SurfaceParams`，只要 loader / 调用方按同一个 `bindingName.memberName` 去写值即可。

真正的硬约定是系统保留名字：

- `CameraUBO`
- `LightUBO`
- `Bones`

这些名字属于 engine contract，不能随意改成别的语义。

### 纹理绑定也要一起考虑

和 Blinn-Phong 不同，当前 `pbr.frag` 默认就读取：

- `albedoMap`
- `normalMap`（条件编译）
- `metallicRoughnessMap`（条件编译）

所以真正落地 loader 时要做二选一：

1. 先把 `pbr.frag` 精简成无纹理版本，只保留 `MaterialUBO`
2. 保持现状，并在 loader/调用方里通过 `setTexture(...)` 填这些 binding

---

## 加入 CMake 构建

`src/infra/CMakeLists.txt` 里现在是一个 `INFRA_SOURCES` 列表，不是旧文档里的 `target_sources(...)`。新增 PBR loader 时，应该往这个列表里追加：

```cmake
# src/infra/CMakeLists.txt
set(INFRA_SOURCES
    material_loader/blinn_phong_material_loader.cpp
    material_loader/pbr_material_loader.cpp
    # ...
)
```

具体文件位置可以通过以下方式核对：

```bash
rg -n "blinn_phong_material_loader" src/infra/CMakeLists.txt
```

---

## 下一步

材质 loader 的接线方式到这里就明确了。下一章回到几何本身，继续准备立方体网格。

→ [04-cube-geometry.md](04-cube-geometry.md)
