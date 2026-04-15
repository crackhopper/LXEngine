# 05 · 完整 main.cpp

> 把 shader / material / mesh / scene / renderer 串成一个可运行的程序。参照样板是 `src/test/test_render_triangle.cpp`。

> 更新：在 `REQ-020 EngineLoop` 之后，这一章的推荐心智模型不再是“`main.cpp` 直接手写 while-loop 驱动 renderer”，而是“`main.cpp` 负责组装 scene 与 callback，由 `EngineLoop` 驱动每帧”。下面先展示展开版，再说明它如何收敛到 `EngineLoop`。

## 目标

- 打开一个 800×600 窗口
- 一只立方体从相机正前方出现，上光照
- 每帧绕 Y 轴旋转
- 关窗 / ESC 退出

## 文件位置

```
src/test/test_pbr_cube.cpp        ← 本章产出
```

放到 `src/test/` 而不是 `src/` 是因为它属于"示例程序 / 集成入口"，和 `test_render_triangle.cpp` 一个层次。下一章会单独加一个 target。

## 骨架 (逐段展开)

### 1. 头与 include

```cpp
// src/test/test_pbr_cube.cpp
#include "core/rhi/renderer.hpp"
#include "core/asset/index_buffer.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/vertex_buffer.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/filesystem_tools.hpp"

#include "backend/vulkan/vulkan_renderer.hpp"
#include "infra/window/window.hpp"
#include "infra/loaders/pbr_cube_material_loader.hpp"

#include <chrono>
#include <cmath>
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

```cpp
int main() {
    // 让 cwd 能访问到 shaders/glsl/pbr_cube.*
    if (!cdToWhereShadersExist("pbr_cube")) {
        std::cerr << "Failed to locate shader assets for pbr_cube\n";
        return 1;
    }

    // ── 窗口 + renderer ───────────────────────────────
    LX_infra::Window::Initialize();
    WindowPtr window =
        std::make_shared<LX_infra::Window>("PBR Cube", 800, 600);

    RendererPtr renderer = std::make_shared<backend::VulkanRenderer>(
        backend::VulkanRenderer::Token{});
    renderer->initialize(window, "PbrCubeApp");

    // ── 几何 + 材质 + renderable ──────────────────────
    auto mesh     = makeCubeMesh();
    auto material = LX_infra::loadPbrCubeMaterial();

    // 改几个参数看看效果（可选）
    material->setFloat(StringID("roughness"), 0.25f);
    material->setFloat(StringID("metallic"),  0.0f);
    material->setVec3 (StringID("baseColor"), Vec3f{0.80f, 0.15f, 0.15f});
    material->updateUBO();

    auto skeleton   = Skeleton::create({}); // 空骨骼占位
    auto renderable = std::make_shared<RenderableSubMesh>(mesh, material, skeleton);

    // ── 场景 ─────────────────────────────────────────
    auto scene = Scene::create(renderable);
    renderer->initScene(scene);

    // Scene ctor 默认已经注入一个 Camera 和一个 DirectionalLight
    auto camera = scene->getCameras().front();
    auto dirLight = std::dynamic_pointer_cast<DirectionalLight>(
        scene->getLights().front());

    if (dirLight && dirLight->ubo) {
        dirLight->ubo->param.dir   = Vec4f{-0.4f, -1.0f, -0.3f, 0.0f};
        dirLight->ubo->param.color = Vec4f{ 1.0f,  1.0f,  1.0f, 1.0f};
        dirLight->ubo->setDirty();
    }

    // ── 主循环 ───────────────────────────────────────
    bool running = true;
    window->onClose([&running]() { running = false; });

    auto startTime = std::chrono::steady_clock::now();

    while (running) {
        if (window->shouldClose()) break;

        // 1. 相机
        camera->position = {0.0f, 0.8f, 2.5f};
        camera->target   = {0.0f, 0.0f, 0.0f};
        camera->up       = Vec3f(0.0f, 1.0f, 0.0f);
        camera->aspect   = 800.0f / 600.0f;
        camera->fovY     = 45.0f;
        camera->updateMatrices();

        // 2. 每帧旋转 model 矩阵 (绕 Y 轴)
        auto now = std::chrono::steady_clock::now();
        float t  = std::chrono::duration<float>(now - startTime).count();

        PC_Draw pc{};
        pc.model          = Mat4f::rotationY(t * 0.8f); // 0.8 rad/s
        pc.enableLighting = 1;
        pc.enableSkinning = 0;
        renderable->objectPC->update(pc);

        // 3. 同步资源 + 绘制
        renderer->uploadData();
        renderer->draw();
    }

    renderer->shutdown();
    return 0;
}
```

## 用 `EngineLoop` 重读这段 main

把上面的展开版压缩成引擎级工作流，其实是：

```cpp
EngineLoop loop;
loop.initialize(window, renderer);
loop.startScene(scene);
loop.setUpdateHook([&](Scene& scene, const Clock& clock) {
    camera->position = {0.0f, 0.8f, 2.5f};
    camera->target   = {0.0f, 0.0f, 0.0f};
    camera->up       = Vec3f(0.0f, 1.0f, 0.0f);
    camera->aspect   = 800.0f / 600.0f;
    camera->fovY     = 45.0f;
    camera->updateMatrices();

    PC_Draw pc{};
    pc.model = Mat4f::rotationY(clock.totalTime() * 0.8f);
    pc.enableLighting = 1;
    pc.enableSkinning = 0;
    renderable->objectPC->update(pc);
});
loop.run();
```

这才是长期推荐的接口形状：

- `startScene(scene)` 做一次性场景初始化
- update hook 让业务层每帧修改 CPU 真值
- `EngineLoop` 内部负责 `clock.tick()`、`uploadData()`、`draw()`

## 数据流小结

把“场景开始时”和“每帧运行时”分开看会更准确。

### 场景开始时

```
renderer->initScene(scene)
  ├─ 计算 swapchain target
  ├─ 回填 camera target
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

`Mat4f::rotationY(radians)` 已经在 `core/math/mat.hpp` 提供。如果没有对应的辅助函数，也可以自己展开：

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
