# 05 · 完整 main.cpp

> 把 shader / material / mesh / scene / renderer 串成一个可运行的程序。这里必须以当前真实存在的 `src/test/test_render_triangle.cpp` 和 `EngineLoop` 为准，不再沿用旧文档那种手写 while-loop + 过时 push constant 字段的写法。

## 目标

- 打开一个 800×600 窗口
- 一只立方体从相机正前方出现，上光照
- 每帧绕 Y 轴旋转
- 关窗 / ESC 退出

## 文件位置

```text
建议新增：
src/test/test_pbr_cube.cpp
```

放到 `src/test/` 而不是 `src/` 是因为它属于"示例程序 / 集成入口"，和 `test_render_triangle.cpp` 一个层次。下一章会单独加一个 target。

## 骨架 (逐段展开)

### 1. 头与 include

```cpp
// src/test/test_pbr_cube.cpp
#include "core/gpu/engine_loop.hpp"
#include "core/rhi/renderer.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/asset/vertex_buffer.hpp"
#include "core/asset/mesh.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/filesystem_tools.hpp"

#include "backend/vulkan/vulkan_renderer.hpp"
#include "infra/window/window.hpp"
#include "infra/material_loader/pbr_material_loader.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <vector>

using namespace LX_core;
using namespace LX_core::gpu;
```

### 2. 立方体几何 (来自上一章)

```cpp
namespace {

VertexPosNormalUvBone v(float x, float y, float z,
                        float nx, float ny, float nz) {
    return VertexPosNormalUvBone(
        {x, y, z}, {nx, ny, nz}, {0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f}, {0, 0, 0, 0},
        {0.0f, 0.0f, 0.0f, 0.0f});
}

MeshPtr makeCubeMesh() {
    std::vector<VertexPosNormalUvBone> vertices = {
        // 复制 04 章的 24 顶点数组
        v( 0.5f, -0.5f, -0.5f,  1, 0, 0),
        v( 0.5f,  0.5f, -0.5f,  1, 0, 0),
        v( 0.5f,  0.5f,  0.5f,  1, 0, 0),
        v( 0.5f, -0.5f,  0.5f,  1, 0, 0),
        v(-0.5f, -0.5f,  0.5f, -1, 0, 0),
        v(-0.5f,  0.5f,  0.5f, -1, 0, 0),
        v(-0.5f,  0.5f, -0.5f, -1, 0, 0),
        v(-0.5f, -0.5f, -0.5f, -1, 0, 0),
        v(-0.5f,  0.5f, -0.5f,  0, 1, 0),
        v(-0.5f,  0.5f,  0.5f,  0, 1, 0),
        v( 0.5f,  0.5f,  0.5f,  0, 1, 0),
        v( 0.5f,  0.5f, -0.5f,  0, 1, 0),
        v(-0.5f, -0.5f,  0.5f,  0, -1, 0),
        v(-0.5f, -0.5f, -0.5f,  0, -1, 0),
        v( 0.5f, -0.5f, -0.5f,  0, -1, 0),
        v( 0.5f, -0.5f,  0.5f,  0, -1, 0),
        v(-0.5f, -0.5f,  0.5f,  0, 0, 1),
        v( 0.5f, -0.5f,  0.5f,  0, 0, 1),
        v( 0.5f,  0.5f,  0.5f,  0, 0, 1),
        v(-0.5f,  0.5f,  0.5f,  0, 0, 1),
        v( 0.5f, -0.5f, -0.5f,  0, 0, -1),
        v(-0.5f, -0.5f, -0.5f,  0, 0, -1),
        v(-0.5f,  0.5f, -0.5f,  0, 0, -1),
        v( 0.5f,  0.5f, -0.5f,  0, 0, -1),
    };

    std::vector<uint32_t> indices;
    indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    auto vb = VertexBuffer<VertexPosNormalUvBone>::create(std::move(vertices));
    auto ib = IndexBuffer::create(std::move(indices));
    return Mesh::create(vb, ib);
}

} // anonymous namespace
```

### 3. main

当前推荐写法就是直接照着 `test_render_triangle.cpp` 的结构走：

```cpp
int main() {
    // 注意：cdToWhereShadersExist 目前是按编译产物 *.spv 找文件。
    // 如果你把 PBR shader 命名为 pbr.vert/pbr.frag，这里传 "pbr"。
    if (!cdToWhereShadersExist("pbr")) {
        std::cerr << "Failed to locate shader assets for pbr\n";
        return 1;
    }

    LX_infra::Window::Initialize();
    WindowPtr window =
        std::make_shared<LX_infra::Window>("PBR Cube", 800, 600);

    RendererPtr renderer =
        std::make_shared<backend::VulkanRenderer>(
            backend::VulkanRenderer::Token{});
    renderer->initialize(window, "PbrCubeApp");

    auto mesh = makeCubeMesh();
    auto material = LX_infra::loadPbrMaterial();

    material->setFloat(StringID("roughnessFactor"), 0.25f);
    material->setFloat(StringID("metallicFactor"), 0.0f);
    material->setVec4(StringID("baseColorFactor"),
                      Vec4f{0.80f, 0.15f, 0.15f, 1.0f});
    material->updateUBO();

    auto skeleton = Skeleton::create({});
    auto renderable =
        std::make_shared<RenderableSubMesh>(mesh, material, skeleton);

    auto scene = Scene::create(renderable);

    auto camera = scene->getCameras().front();
    auto dirLight = std::dynamic_pointer_cast<DirectionalLight>(
        scene->getLights().front());

    if (dirLight && dirLight->ubo) {
        dirLight->ubo->param.dir = Vec4f{-0.4f, -1.0f, -0.3f, 0.0f};
        dirLight->ubo->param.color = Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
        dirLight->ubo->setDirty();
    }

    EngineLoop loop;
    loop.initialize(window, renderer);
    loop.startScene(scene);

    loop.setUpdateHook([&](Scene &, const Clock &clock) {
        camera->position = {0.0f, 0.8f, 2.5f};
        camera->target = {0.0f, 0.0f, 0.0f};
        camera->up = Vec3f(0.0f, 1.0f, 0.0f);
        camera->aspect = 800.0f / 600.0f;
        camera->fovY = 45.0f;
        camera->updateMatrices();

        PC_Draw pc{};
        pc.model = Mat4f::rotationY(static_cast<float>(clock.totalTime()) * 0.8f);
        renderable->objectPC->update(pc);
    });

    loop.run();
    renderer->shutdown();
    return 0;
}
```

这就是当前代码库里真正推荐的入口形状：

- `startScene(scene)` 做一次性场景初始化
- update hook 让业务层每帧修改 CPU 真值
- `EngineLoop` 内部负责 `clock.tick()`、`uploadData()`、`draw()`

## 数据流小结

把“场景开始时”和“每帧运行时”分开看会更准确。

### 场景开始时

```
EngineLoop::startScene(scene)
  ├─ 计算 swapchain target
  ├─ 回填 camera target
  ├─ 调 renderer->initScene(scene)
  ├─ FrameGraph::buildFromScene(scene)
  └─ preloadPipelines(...)
```

### 每帧运行时

```
[EngineLoop / main loop]
  clock.tick()
  camera->updateMatrices()     → CameraUBO::setDirty()
  renderable->objectPC->update → ObjectPC.data 内联更新 (PushConstant 不走 dirty)
    │
renderer->uploadData()
  ├─ 扫描 FrameGraph 的所有 pass/item
  ├─ 对 dirty 的 IRenderResource 做 staging copy 到 GPU
  └─ collectGarbage()
    │
renderer->draw()
  ├─ acquireNextImage
  ├─ begin render pass
  ├─ for each item in pass.queue:
  │     pipeline = resourceManager.getOrCreateRenderPipeline(item)  // cache 命中
  │     cmd->bindPipeline / bindResources / drawItem
  │       drawItem 会把 item.objectInfo.data 写为 push constant
  └─ submit / present
```

---

## Model 矩阵的旋转

`Mat4f::rotationY(radians)` 已经被当前示例风格默认接受。并且现在 `PC_Draw` 只保留 `model`，不再有旧文档里那些 `enableLighting` / `enableSkinning` 字段。

```cpp
Mat4f rotY(float rad) {
    float c = std::cos(rad), s = std::sin(rad);
    Mat4f m = Mat4f::identity();
    m(0,0) =  c; m(0,2) = s;
    m(2,0) = -s; m(2,2) = c;
    return m;
}
```

> 具体 API 以 `core/math/mat.hpp` 为准 —— 如果项目里是 `Mat4f::rotationAxis(axis, rad)` 就用那个。这个细节不影响教程逻辑。

---

## 下一步

代码都写完了。最后一章：CMake 注册、构建、运行、排错清单。

→ [06-build-and-run.md](06-build-and-run.md)
