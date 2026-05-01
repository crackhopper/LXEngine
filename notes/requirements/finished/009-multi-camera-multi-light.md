# REQ-009: 多 Camera / 多 Light 与 RenderTarget 驱动的资源过滤

## 背景

REQ-008 打通了 `FrameGraph → RenderQueue → RenderingItem` 的数据流，但它保留了 `Scene` 单 camera / 单 light 的假设，`Scene::getSceneLevelResources()` 是无参版本：所有 scene 级资源一视同仁地合并到每个 `RenderingItem`。这个假设在真实渲染里很快会崩塌：

1. **多 camera 是必然需求** —— 主相机画 swapchain、shadow 相机画 shadow map、反射相机画 cubemap、UI overlay 相机画另一个 attachment。每个 camera 本质上对应一个 `RenderTarget`：画到哪里取决于它绑了哪个 target
2. **多 light 是必然需求** —— 一个场景里可以有多个 directional light / point light / spot light，它们并不都参与所有 pass。例如只有参与 shadow pass 的 light 才需要在 shadow map 绘制时写入 depth；参与 forward pass 的 light 列表和参与 deferred pass 的不一样
3. **`getSceneLevelResources()` 无参版本是妥协** —— 目前 `src/core/scene/scene.hpp` 里的 `camera` / `directionalLight` 是**单个**字段（见 REQ-008 R3 的实现）。REQ-008 把这两个字段的 UBO 塞到每个 item 上；但如果场景里有多个 camera，每个 pass 应该只用**它自己目标 target 上那个 camera** 的 UBO；如果有多个 light，每个 pass 应该只用**pass mask 匹配**的 light 的 UBO
4. **`Camera` 和 `RenderTarget` 的关系在代码里缺失** —— `src/core/scene/camera.hpp` 现在只有投影矩阵和视图矩阵，它画到哪个 target 完全由 `VulkanRenderer` 的硬编码决定。`FramePass.target` 字段在 `src/core/scene/frame_graph.hpp` 里存在但没人读它去匹配 camera
5. **`LightBase` 是空壳** —— `src/core/scene/light.hpp:6` 的 `class LightBase {};` 没有任何虚方法；`DirectionalLight` 在 `scene.hpp` 里作为一个独立字段存在，不走 `LightBase` 抽象。无法通过 `LightBase` 做多态容器

正确的模型应该是：

- **Camera 持有一个 `RenderTarget`**（或 `nullopt` 表示"默认 swapchain"，由 `VulkanRenderer::initScene` 在创建 framegraph 时填真值）
- **Light 持有 pass mask**（`ResourcePassFlag`），`LightBase` 成为抽象接口暴露 `getPassMask()` / `getUBO()` / `supportsPass(pass)`
- **Scene 持有 `vector<CameraPtr>` / `vector<LightBasePtr>`**
- **`Scene::getSceneLevelResources(pass, target)`** 按两个轴过滤：camera 必须 target 匹配、light 必须 pass 匹配
- **`RenderQueue::buildFromScene(scene, pass, target)`** 新增 target 参数（REQ-008 是无 target 版本），`FrameGraph::buildFromScene` 从 `pass.target` 取
- **`VulkanRenderer::initScene`** 在建 `FramePass` 时，把从 device 派生的 swapchain `RenderTarget` 填进所有 `m_target == nullopt` 的 camera

## 目标

1. `LightBase` 成为抽象接口，`DirectionalLight` 实现它并持有 `m_passMask`
2. `Camera` 持有 `std::optional<RenderTarget> m_target`，默认 `nullopt` 表示"用 swapchain"
3. `Scene` 改为持有 `vector<CameraPtr>` / `vector<LightBasePtr>`，保留 `vector<IRenderablePtr>` 不变
4. `Scene::getSceneLevelResources(pass, target)` 按 pass + target 过滤 camera / light UBO
5. `RenderQueue::buildFromScene(scene, pass, target)` 新增 target 参数
6. `FrameGraph::buildFromScene(scene)` 从每个 `FramePass.target` 取 target 传入 queue
7. `VulkanRenderer::initScene` 从 device 派生 swapchain `RenderTarget`，填到所有 nullopt camera
8. 新增多 camera / 多 light / pass mask 过滤的集成测试

本需求的**前置强依赖是 REQ-008** —— REQ-008 必须先落地把数据流打通，本需求才在它的路径上加过滤维度。

## 需求

### R1: `LightBase` 抽象接口

`src/core/scene/light.hpp` 的 `LightBase` 当前是空类。改为：

```cpp
namespace LX_core {

class LightBase {
public:
    virtual ~LightBase() = default;

    /// 该 light 参与哪些 pass。默认参与 Forward + Deferred，不参与 Shadow。
    /// 子类可 override 或在构造时通过 setPassMask() 配置。
    virtual ResourcePassFlag getPassMask() const = 0;

    /// 返回该 light 的 UBO（shader binding name 由资源自身的 getBindingName() 决定）。
    /// 若该 light 不需要 UBO（例如仅存在于 shader 常量里），返回 nullptr。
    virtual IRenderResourcePtr getUBO() const = 0;

    /// 该 light 是否参与指定 pass。默认实现基于 getPassMask()。
    virtual bool supportsPass(StringID pass) const {
        const auto flag = passFlagFromStringID(pass);
        return (static_cast<uint32_t>(getPassMask()) &
                static_cast<uint32_t>(flag)) != 0;
    }
};

using LightBasePtr = std::shared_ptr<LightBase>;

} // namespace LX_core
```

**Include 要求**: `light.hpp` 需要 include `core/scene/pass.hpp`（拿 `passFlagFromStringID` 和 `StringID`）以及 `core/gpu/render_resource.hpp`（拿 `IRenderResourcePtr` 和 `ResourcePassFlag`）。

### R2: `DirectionalLight` 实现 `LightBase`

`src/core/scene/directional_light.hpp`（或现在 light 所在文件）里的 `DirectionalLight`:

```cpp
class DirectionalLight : public LightBase {
public:
    DirectionalLight(/* 现有构造参数 */,
                     ResourcePassFlag passMask =
                         ResourcePassFlag::Forward | ResourcePassFlag::Deferred);

    // 现有的 setDirection / setColor / setIntensity 保持

    ResourcePassFlag getPassMask() const override { return m_passMask; }
    IRenderResourcePtr getUBO() const override;

    void setPassMask(ResourcePassFlag mask) { m_passMask = mask; }

private:
    ResourcePassFlag m_passMask;
    // 现有 UBO 成员（若存在）或通过其他途径构造 UBO
};
```

**默认 pass mask = `Forward | Deferred`**：保守默认值，不包含 `Shadow`，因为 directional light 本身参与 shadow pass 的方式是作为 *shadow caster light*，通常需要显式配置。

**现有 `Scene::directionalLight` 字段** (`src/core/scene/scene.hpp:35` 左右) 会在 R4 被 `std::vector<LightBasePtr> m_lights;` 替换。

### R3: `Camera` 持有 `std::optional<RenderTarget>`

`src/core/scene/camera.hpp:45` 左右的 `Camera` 类新增：

```cpp
#include "core/scene/render_target.hpp"  // 若尚未 include
#include <optional>

class Camera {
public:
    // ... 现有的 view / projection / UBO 成员保持 ...

    /// 该 camera 绘制到的目标。nullopt 表示"用 swapchain 默认 target"，
    /// 由 VulkanRenderer::initScene 在建 FrameGraph 时填入真值。
    const std::optional<RenderTarget>& getTarget() const { return m_target; }
    void setTarget(RenderTarget target) { m_target = std::move(target); }
    void clearTarget() { m_target.reset(); }

    /// 该 camera 是否绘制到指定 target。
    /// - m_target 有值 → 按字段比较（或用 RenderTarget::operator==）
    /// - m_target 为 nullopt → 只匹配"默认 target"（由调用方判断相等性）
    bool matchesTarget(const RenderTarget &target) const;

private:
    std::optional<RenderTarget> m_target;
    // 现有字段保持
};
```

**`RenderTarget::operator==`** 本 REQ 需要 `RenderTarget` 支持相等比较。如果 `src/core/scene/render_target.hpp` 里还没有 `operator==`，本 REQ 顺便加上（按字段比较 `colorFormat` / `depthFormat` / `sampleCount`；若 `RenderTarget` 结构更复杂则按语义相等比较）。

**`Camera::matchesTarget` 实现**:

```cpp
bool Camera::matchesTarget(const RenderTarget &target) const {
    if (!m_target.has_value()) return false;
    return *m_target == target;
}
```

注意: `matchesTarget` 仅在 `m_target` 有值时返回 true；**nullopt 的 camera 必须先被 `VulkanRenderer::initScene` 填真值才能被过滤命中**，这是 R7 要做的事。

### R4: `Scene` 改为多 camera / 多 light 容器

`src/core/scene/scene.hpp:30` 的 `Scene` 类:

```cpp
class Scene {
public:
    // 现有 renderable 接口保持:
    void addRenderable(IRenderablePtr r);
    const std::vector<IRenderablePtr>& getRenderables() const;

    // 新: camera 容器
    void addCamera(CameraPtr camera);
    const std::vector<CameraPtr>& getCameras() const;

    // 新: light 容器
    void addLight(LightBasePtr light);
    const std::vector<LightBasePtr>& getLights() const;

    // REQ-008 引入的无参版本被 REQ-009 替换为带过滤的版本:
    std::vector<IRenderResourcePtr>
    getSceneLevelResources(StringID pass, const RenderTarget &target) const;

private:
    std::vector<IRenderablePtr> m_renderables;
    std::vector<CameraPtr>      m_cameras;
    std::vector<LightBasePtr>   m_lights;
};
```

**删除**：

- 原 `CameraPtr camera;` 字段（`scene.hpp:32` 左右）
- 原 `DirectionalLightPtr directionalLight;` 字段（`scene.hpp:33` 左右）
- REQ-008 引入的无参 `getSceneLevelResources()` —— 被带参版本替换

**迁移**：REQ-008 的测试 setup 里调用的 `scene->camera = ...` / `scene->directionalLight = ...` 必须改为 `scene->addCamera(...)` / `scene->addLight(...)`（见 R8）。

### R5: `Scene::getSceneLevelResources(pass, target)` 按 pass + target 过滤

`src/core/scene/scene.cpp` 里的实现:

```cpp
std::vector<IRenderResourcePtr>
Scene::getSceneLevelResources(StringID pass, const RenderTarget &target) const {
    std::vector<IRenderResourcePtr> out;

    // Camera：仅画到此 target 的那些
    for (const auto &cam : m_cameras) {
        if (!cam) continue;
        if (!cam->matchesTarget(target)) continue;
        if (auto ubo = cam->getUBO()) {
            out.push_back(std::dynamic_pointer_cast<IRenderResource>(ubo));
        }
    }

    // Light：参与此 pass 的那些（与 target 无关）
    for (const auto &light : m_lights) {
        if (!light) continue;
        if (!light->supportsPass(pass)) continue;
        if (auto ubo = light->getUBO()) {
            out.push_back(ubo);
        }
    }

    return out;
}
```

**设计决定 1：camera 按 target 过滤，不按 pass 过滤**
一个 camera 画到一个 target，这个 target 可以被多个 pass 共享（例如 forward pass 和 overlay pass 共画 swapchain）。camera 的身份由 target 决定，不由 pass 决定。

**设计决定 2：light 按 pass 过滤，不按 target 过滤**
同一个 light 可以影响多个 target 上的 shading。target 只决定"画到哪里"，light 决定"shading 参与哪些阶段"。

**设计决定 3：顺序仍然是 camera 先、light 后**
保持和 REQ-008 无参版本的合并顺序一致，不破坏 descriptor set 的 binding 绑定路径。

**空列表是合法结果**: 某个 pass × target 组合可能过滤掉所有 camera 和 light（例如 shadow pass × shadow atlas target 上没有任何 directional light 被标记参与 shadow）。返回空 `vector` 是合法的，上层（`RenderQueue::buildFromScene`）不应该假设非空。

### R6: `RenderQueue::buildFromScene(scene, pass, target)` 新增 target 参数

`src/core/scene/render_queue.hpp`:

```cpp
class RenderQueue {
public:
    // REQ-008 的签名 buildFromScene(scene, pass) 被替换为:
    void buildFromScene(const Scene &scene,
                        StringID pass,
                        const RenderTarget &target);
};
```

实现改动（相对 REQ-008 的版本）:

```cpp
void RenderQueue::buildFromScene(const Scene &scene,
                                 StringID pass,
                                 const RenderTarget &target) {
    clearItems();
    auto sceneResources = scene.getSceneLevelResources(pass, target);
    for (const auto &renderable : scene.getRenderables()) {
        if (!renderable) continue;
        if (!renderable->supportsPass(pass)) continue;
        RenderingItem item = makeItemFromRenderable(renderable, pass);
        item.descriptorResources.insert(item.descriptorResources.end(),
                                        sceneResources.begin(),
                                        sceneResources.end());
        m_items.push_back(std::move(item));
    }
    sort();
}
```

**唯一差别**: `getSceneLevelResources()` 变成 `getSceneLevelResources(pass, target)`。renderable 过滤路径保持不变（`supportsPass(pass)`，和 target 无关 —— renderable 参与哪些 pass 和它画到哪个 target 是两个独立维度）。

**`FrameGraph::buildFromScene(scene)` 的适配** (`src/core/scene/frame_graph.cpp`):

```cpp
void FrameGraph::buildFromScene(const Scene &scene) {
    for (auto &pass : m_passes) {
        pass.queue.buildFromScene(scene, pass.name, pass.target);
    }
}
```

每个 `FramePass` 已经持有自己的 `target` 字段，直接传进去即可。

### R7: `VulkanRenderer::initScene` 填默认 swapchain target

`src/backend/vulkan/vk_renderer.cpp` 的 `Impl::initScene` 在 `m_frameGraph.addPass(...)` 之后、`m_frameGraph.buildFromScene(*scene)` 之前，新增：

```cpp
void initScene(ScenePtr _scene) override {
    scene = _scene;

    // 1. 从 device 派生 swapchain 对应的默认 target
    RenderTarget swapchainTarget = makeSwapchainTarget();

    // 2. 把所有 nullopt camera 的 target 设为 swapchain target
    for (const auto &cam : scene->getCameras()) {
        if (!cam) continue;
        if (!cam->getTarget().has_value()) {
            cam->setTarget(swapchainTarget);
        }
    }

    // 3. 配置 pass（target 使用 swapchain）
    m_frameGraph.addPass(FramePass{
        Pass_Forward,
        swapchainTarget,
        {}
    });

    // 4. 从 scene 填 queue (REQ-008 的行为保持)
    m_frameGraph.buildFromScene(*scene);

    // 5. 初始上传 + pipeline 预构建 (REQ-008 的行为保持)
    // ...
}
```

**`makeSwapchainTarget()` 辅助函数**（替换 REQ-008 的 `defaultForwardTarget()`）：

```cpp
RenderTarget makeSwapchainTarget() const {
    RenderTarget t{};
    t.colorFormat = toImageFormat(device->getSurfaceFormat().format);
    t.depthFormat = toImageFormat(device->getDepthFormat());
    t.sampleCount = 1;
    return t;
}
```

**`toImageFormat` (VkFormat → ImageFormat) 若尚不存在**：本 REQ 需要它真正返回正确的 format，不能像 REQ-008 那样返回默认 `RenderTarget{}` 占位，因为 R5 的过滤依赖 `RenderTarget::operator==` 能准确匹配 camera 设置的 target 和 framepass target。如果 `toImageFormat` 尚未实现，本 REQ 顺便实现（只需要覆盖 swapchain 会用到的几种 VkFormat，例如 `VK_FORMAT_B8G8R8A8_SRGB`、`VK_FORMAT_D32_SFLOAT`）。

**顺序重要**: camera 的 `setTarget` 必须在 `m_frameGraph.buildFromScene(*scene)` 之前完成，否则 `getSceneLevelResources(pass, target)` 过滤时 nullopt camera 仍然匹配不上 swapchain target，被静默丢弃。

### R8: 新增多 camera / 多 light / pass mask 过滤测试

**8.1 `src/test/integration/scene_test_helpers.hpp` 更新**

REQ-008 引入的 `firstItemFromScene(scene, pass)` 需要升级为带 target 参数的版本。策略：保留原签名，内部用 `RenderTarget{}` 默认值；同时新增带 target 的重载:

```cpp
inline LX_core::RenderingItem
firstItemFromScene(LX_core::Scene &scene,
                   LX_core::StringID pass,
                   const LX_core::RenderTarget &target = {}) {
    LX_core::RenderQueue q;
    q.buildFromScene(scene, pass, target);
    assert(!q.getItems().empty() && "scene produced no items for pass/target");
    return q.getItems().front();
}
```

REQ-008 迁移后的 5 个测试文件原本传入默认 `RenderTarget{}`，它们在 R8 里需要给每个 camera 显式 `setTarget(RenderTarget{})`，否则过滤会让 queue 为空（因为 nullopt camera 不匹配任何 target）。每个被迁移的测试 setup 加一行：

```cpp
scene->addCamera(makeDefaultCamera());  // 原来是 scene->camera = makeDefaultCamera()
scene->getCameras().front()->setTarget(RenderTarget{});
```

更好的方案：在测试 helper 里提供 `makeDefaultCameraWithTarget()`:

```cpp
inline LX_core::CameraPtr makeDefaultCameraWithTarget() {
    auto cam = /* 构造默认 camera */;
    cam->setTarget(LX_core::RenderTarget{});
    return cam;
}
```

**8.2 `test_frame_graph.cpp` 新增 multi-camera scenario**

```cpp
TEST_SCENARIO("multi-camera: each camera scoped to its target") {
    RenderTarget targetA{/* color A */, /* depth A */, 1};
    RenderTarget targetB{/* color B */, /* depth B */, 1};

    auto camA = makeMockCameraWithTarget(targetA);
    auto camB = makeMockCameraWithTarget(targetB);

    auto scene = Scene::create(/* renderable */);
    scene->addCamera(camA);
    scene->addCamera(camB);

    // Scene level resources for targetA should only include camA's UBO
    auto resA = scene->getSceneLevelResources(Pass_Forward, targetA);
    ASSERT_EQ(resA.size(), 1);
    ASSERT_EQ(resA[0], camA->getUBO());

    auto resB = scene->getSceneLevelResources(Pass_Forward, targetB);
    ASSERT_EQ(resB.size(), 1);
    ASSERT_EQ(resB[0], camB->getUBO());
}
```

**8.3 `test_frame_graph.cpp` 新增 multi-light + pass mask scenario**

```cpp
TEST_SCENARIO("multi-light: pass mask filters lights per pass") {
    auto lightForward = makeMockLight(ResourcePassFlag::Forward);
    auto lightShadow  = makeMockLight(ResourcePassFlag::Shadow);
    auto lightBoth    = makeMockLight(ResourcePassFlag::Forward |
                                      ResourcePassFlag::Shadow);

    auto scene = Scene::create(/* renderable */);
    scene->addCamera(makeMockCameraWithTarget(RenderTarget{}));
    scene->addLight(lightForward);
    scene->addLight(lightShadow);
    scene->addLight(lightBoth);

    auto resForward = scene->getSceneLevelResources(Pass_Forward, RenderTarget{});
    // 1 camera + 2 lights (lightForward, lightBoth) = 3
    ASSERT_EQ(resForward.size(), 3);

    auto resShadow = scene->getSceneLevelResources(Pass_Shadow, RenderTarget{});
    // 1 camera + 2 lights (lightShadow, lightBoth) = 3
    ASSERT_EQ(resShadow.size(), 3);
}
```

**8.4 `test_frame_graph.cpp` 新增 nullopt camera scenario**

```cpp
TEST_SCENARIO("camera with nullopt target does not match any target until filled") {
    auto cam = makeMockCameraNoTarget();  // m_target == nullopt
    auto scene = Scene::create(/* renderable */);
    scene->addCamera(cam);

    // 未填 target 之前，过滤应得空 camera UBO（assert 0 camera UBO）
    auto resBefore = scene->getSceneLevelResources(Pass_Forward, RenderTarget{});
    ASSERT_EQ(resBefore.size(), 0);  // 0 camera + 0 light

    // 填入 target
    cam->setTarget(RenderTarget{});

    auto resAfter = scene->getSceneLevelResources(Pass_Forward, RenderTarget{});
    ASSERT_EQ(resAfter.size(), 1);  // 1 camera + 0 light
}
```

## 测试

- **`src/test/integration/test_frame_graph.cpp`** — 新增 3 个 scenario：multi-camera target 过滤、multi-light pass mask 过滤、nullopt camera 填充行为 (R8)
- **`src/test/integration/scene_test_helpers.hpp`** — 升级 `firstItemFromScene` 带 target 参数，新增 `makeDefaultCameraWithTarget` helper (R8.1)
- **`src/test/integration/test_vulkan_command_buffer.cpp`** / **`test_vulkan_resource_manager.cpp`** / **`test_vulkan_pipeline.cpp`** / **`test_pipeline_cache.cpp`** / **`test_pipeline_build_info.cpp`** — REQ-008 迁移后的这些测试需要把 `scene->camera = ...` / `scene->directionalLight = ...` 改成 `scene->addCamera(...)` / `scene->addLight(...)`，并为 camera 显式 `setTarget`
- **`src/test/test_render_triangle.cpp`** — 验证 `VulkanRenderer::initScene` 正确把 swapchain target 填到 nullopt camera 之后，三角形照常绘制

## 修改范围

| 文件 | 改动 |
|------|------|
| `src/core/scene/light.hpp` | `LightBase` 改为抽象接口：`getPassMask()` / `getUBO()` / `supportsPass()` 虚方法 |
| `src/core/scene/directional_light.hpp` / `.cpp`（或现有 light 实现文件） | `DirectionalLight` 继承 `LightBase`，新增 `m_passMask`、`setPassMask`、override 虚方法 |
| `src/core/scene/camera.hpp` / `.cpp` | 新增 `std::optional<RenderTarget> m_target`、`getTarget` / `setTarget` / `clearTarget` / `matchesTarget` |
| `src/core/scene/render_target.hpp` | 新增 `RenderTarget::operator==`（若尚无） |
| `src/core/scene/scene.hpp` / `.cpp` | `camera` / `directionalLight` 单字段 → `vector<CameraPtr> m_cameras` / `vector<LightBasePtr> m_lights`；`getSceneLevelResources` 签名改为 `(pass, target)` |
| `src/core/scene/render_queue.hpp` / `.cpp` | `buildFromScene` 签名改为 `(scene, pass, target)` |
| `src/core/scene/frame_graph.cpp` | 调用改为 `pass.queue.buildFromScene(scene, pass.name, pass.target)` |
| `src/backend/vulkan/vk_renderer.cpp` | `initScene`: 派生 swapchain `RenderTarget` → 填 nullopt camera → 传入 `m_frameGraph.addPass`。引入 `makeSwapchainTarget()` helper |
| `src/backend/vulkan/details/image_format.hpp` / `.cpp` | `toImageFormat(VkFormat)` helper（若尚未实现） |
| `src/test/integration/scene_test_helpers.hpp` | `firstItemFromScene` 签名升级；新增 `makeDefaultCameraWithTarget` helper |
| `src/test/integration/test_frame_graph.cpp` | 新增 3 个 scenario |
| `src/test/integration/test_vulkan_*.cpp` / `test_pipeline_*.cpp` | REQ-008 迁移后的 setup 改为 `addCamera` / `addLight` + 显式 `setTarget` |
| `notes/subsystems/scene.md` / `camera-and-light.md`（若存在） | 多 camera / 多 light 架构图 |
| `notes/subsystems/frame-graph.md` | `FramePass.target` 真正被消费的说明 |

## 边界与约束

- **不**实现多个 `FramePass` 的并行绘制调度 —— 本 REQ 扩展架构使其**能够**支持多 target，但 `VulkanRenderer::initScene` 仍然只 `addPass(Pass_Forward)`；真正的 shadow prepass / deferred lighting / overlay pass 的添加是下游工作
- **不**实现 `PointLight` / `SpotLight` / `AreaLight` —— 本 REQ 只把 `DirectionalLight` 对齐到 `LightBase` 接口。其他 light 类型作为下游
- **不**支持动态 scene 变更（运行期 add/remove camera/light 后自动 rebuild queue）—— 本 REQ 继承 REQ-008 的"initScene 时构建一次"假设
- **不**实现 shadow map 的 `RenderTarget` 派生（从 shadow atlas image view 构造 RenderTarget）—— 留给后续 shadow pass 需求
- **不**让 Light 持有 target —— light 的过滤维度只有 pass mask，不按 target 过滤。这是 R5 的设计决定
- **不**让 Renderable 按 target 过滤 —— renderable 的过滤维度只有 `supportsPass(pass)`。target 与 renderable 无关
- **`RenderTarget::operator==` 的相等语义**: 按 `colorFormat` / `depthFormat` / `sampleCount` 三字段比较。若未来 `RenderTarget` 扩展为持有具体 attachment handle（例如 image view ptr），相等语义要同步更新 —— 这是下游风险点，本 REQ 不预设
- **nullopt camera 过滤行为**: `Camera::matchesTarget` 在 nullopt 时**永远返回 false**。`VulkanRenderer::initScene` 负责在构建 framegraph 之前把 nullopt camera 填上真值。依赖这个填充顺序的单元测试必须显式验证 "`initScene` 之前 nullopt → `initScene` 之后匹配上" 的行为 (R8.4 覆盖)
- **`toImageFormat` 只需要覆盖 swapchain 实际用到的几种 VkFormat**: 本 REQ 不要求做完整的 VkFormat ↔ ImageFormat 映射表。覆盖 `VK_FORMAT_B8G8R8A8_SRGB` / `VK_FORMAT_B8G8R8A8_UNORM` / `VK_FORMAT_D32_SFLOAT` / `VK_FORMAT_D24_UNORM_S8_UINT` 即可，其他 fallback 到默认或抛异常
- **与 REQ-008 的兼容性**: 本 REQ 会**破坏** REQ-008 的 `getSceneLevelResources()` 无参签名和 `buildFromScene(scene, pass)` 无 target 签名，强制所有调用方升级。REQ-008 的 `finished` 归档顶部需要 banner 说明（见冲突扫描）

## 冲突扫描

- **REQ-008 R3**（`docs/requirements/008-frame-graph-drives-rendering.md` —— 注：本 REQ 起草时 REQ-008 尚未归档至 `finished/`，未来 REQ-008 完成时会被移到 `finished/008-*.md`）定义了 `Scene::getSceneLevelResources()` 无参版本。本 REQ R5 **替换**为带 `(pass, target)` 参数的版本。
  - **解决**: 本 REQ 落地时，在 `finished/008-frame-graph-drives-rendering.md` 顶部加 banner：
    ```markdown
    > **Partial supersede by REQ-009**：R3 `Scene::getSceneLevelResources()` 无参版本已被 REQ-009 替换为 `getSceneLevelResources(pass, target)`。R4 `RenderQueue::buildFromScene(scene, pass)` 已被替换为 `buildFromScene(scene, pass, target)`。R6 中 `FrameGraph::buildFromScene` 的实现相应调整。其他 R 继续有效。
    ```
- **REQ-008 R4** 定义了 `RenderQueue::buildFromScene(scene, pass)` 签名。本 REQ R6 改为 `(scene, pass, target)`。同样由 REQ-008 的 banner 涵盖
- **REQ-008 R6** 定义了 `VulkanRenderer::initScene` 的新流程（`defaultForwardTarget()` helper 返回占位 `RenderTarget{}`）。本 REQ R7 把占位升级为真实派生值（`makeSwapchainTarget()` + `toImageFormat`），并新增 nullopt camera 填充逻辑。这是 REQ-008 里被明确标记为 "REQ-009 会真正消费" 的下游约定，不算冲突
- **REQ-008 R7** 的 `firstItemFromScene(scene, pass)` helper 签名会被本 REQ 扩展为带 target 参数。这是向后兼容的扩展（默认参数），但所有 REQ-008 迁移过的测试 setup **必须** 显式 `addCamera` / `addLight` + `setTarget`，否则过滤后 scene 级资源为空。这一点也由 REQ-008 banner 的 "REQ-009 破坏性影响" 说明覆盖
- **REQ-005** (`docs/requirements/finished/005-unified-material-system.md`) 的 `MaterialInstance` 用 `scene.camera` 或 `scene.directionalLight` 的地方：扫描 `grep -rn "scene\.camera\|scene->camera\|scene\.directionalLight\|scene->directionalLight" src/` 的命中点需要全部改为 `scene.getCameras()[i]` / `scene.getLights()[i]` 路径。**这是范围内的迁移，不是冲突** —— 实施 R4 时作为 "场 scan + 机械替换" 完成
- **`openspec/specs/frame-graph/spec.md`** 里关于 `FramePass.target` 的定义：本 REQ **不**改 spec，只是让 `target` 字段**真正被消费**。spec 原文已经允许 `FramePass` 携带 target，所以不算 spec 变更
- **`openspec/specs/render-signature/spec.md`** 的 `Pass_*` 常量：不变
- **`notes/subsystems/scene.md`** 的数据流图：REQ-008 更新过一次（把 scene 画成被读取的对象），本 REQ 进一步扩展为多 camera / 多 light。这是 `/update-notes` 的事情，不是 spec 修改

## 依赖

- **REQ-008**（规划中，本 REQ 的**强前置**）— `FrameGraph`/`RenderQueue`/`RenderingItem` 数据流必须先通过 REQ-008 打通，本 REQ 才在这条路径上加过滤维度。**REQ-008 未完成前禁止开工**
- **REQ-007**（已完成）— `StringID`-based pass constants (`Pass_Forward` / `Pass_Shadow` / `Pass_Deferred`)
- **REQ-003b**（已完成）— `FrameGraph` / `RenderQueue` / `FramePass.target` 基础设施
- **`RenderTarget` 结构体**（`src/core/scene/render_target.hpp`，已存在）— 本 REQ 可能顺带补 `operator==`
- **`toImageFormat(VkFormat)`**（可能需要新建）— 用于从 Vulkan device surface format 派生 `RenderTarget`

## 下游

- **真正的 shadow pass** —— `VulkanRenderer::initScene` 新增 `addPass(Pass_Shadow, shadowAtlasTarget, {})`，directional light 的 pass mask 配为 `Forward | Shadow`，shadow map 作为 shader binding 传到 forward pass
- **Deferred lighting** —— 新增 `Pass_Deferred` 的 G-buffer target，同理
- **Overlay / UI pass** —— 多 camera 真正派上用场：主 camera 画 world，overlay camera 画 UI，共享 swapchain target 但在不同 pass
- **`PointLight` / `SpotLight` / `AreaLight`** —— 基于 `LightBase` 接口新增，只需要实现 `getPassMask` / `getUBO` + 构造 correspondng UBO
- **动态 scene 变更支持** —— 运行期 `scene->addCamera(...)` / `scene->addLight(...)` 后自动 rebuild `FrameGraph`
- **Reflection probe / environment map** —— 反射 camera 画到 cubemap target，material 里作为 texture binding 引用

## 实施状态

已完成并通过 `/finish-req` 验证（2026-04-14）。对应 openspec change `multi-camera-multi-light` 待归档，delta specs 将 sync 到 `openspec/specs/frame-graph` / `renderer-backend-vulkan`。

**R1–R8 验证结果**（`/finish-req` 阶段，grep/read 逐条对照代码）：
- R1: `LightBase` 抽象接口 (`src/core/scene/light.hpp:17`) — `getPassMask` / `getUBO` / `supportsPass` 虚方法齐备，默认实现使用 `passFlagFromStringID` ✓
- R2: `DirectionalLight : public LightBase` (`light.hpp:67`) — `m_passMask` 默认 `Forward | Deferred`、`setPassMask` override、`getUBO() -> IRenderResourcePtr` ✓
- R3: `Camera::m_target` (`camera.hpp:97`) + `getTarget` / `setTarget` / `clearTarget` / `matchesTarget`。`matchesTarget` 在 nullopt 时返回 false ✓
- R3b: `RenderTarget::operator==` / `operator!=` (`src/core/gpu/render_target.hpp:21-26`)，按 `colorFormat` / `depthFormat` / `sampleCount` 三字段比较 ✓
- R4: `Scene` 多容器 (`scene.hpp:75-76`) + `addCamera` / `addLight` / `getCameras` / `getLights`。老 `camera` / `directionalLight` 公开字段已删除 ✓
- R5: `Scene::getSceneLevelResources(pass, target)` (`scene.cpp`) — camera 按 `matchesTarget(target)` 过滤、light 按 `supportsPass(pass)` 过滤、camera-first 顺序 ✓
- R6: `RenderQueue::buildFromScene(scene, pass, target)` (`render_queue.cpp:67`) + `FrameGraph::buildFromScene` 委托 `pass.queue.buildFromScene(scene, pass.name, pass.target)` ✓
- R7: `VulkanRendererImpl::initScene` (`vk_renderer.cpp:151,166`) — `makeSwapchainTarget()` 从 `device->getSurfaceFormat()` + `device->getDepthFormat()` 经 `toImageFormat(VkFormat)` 派生；回填 nullopt camera 发生在 `m_frameGraph.buildFromScene` **之前** ✓
- R8: `test_frame_graph.cpp` 新增 `testMultiCameraTargetFilter` / `testMultiLightPassMaskFilter` / `testNullOptCameraBeforeAndAfterFill` 三个场景 — 全部通过 ✓
- REQ-008 R3/R4/R6 的 "Partial supersede by REQ-009" banner 已追加到 `finished/008-frame-graph-drives-rendering.md` 顶部 ✓

**`/finish-req` 顺带清理**:
- `src/core/scene/scene.hpp` 删除两条 stale `// 简化 RenderingItem` / `// Scene 层简化示例` 注释（这些 REQ-008 之前的"简化示例"说法已经不成立，Scene 现在是完整的多容器形式）。

**回归测试**（全部绿）:
- `test_frame_graph` — 12 scenarios，含 3 个新 REQ-009 场景
- `test_pipeline_build_info` / `test_pipeline_identity` / `test_material_instance` / `test_string_table` — non-GPU 核心层回归
- GPU 依赖测试 (`test_vulkan_*`) 在无显示环境下 SKIP cleanly，编译期验证已在 full build 阶段完成

**已知限制 / 约定**:
- Scene 构造器仍然种下一个默认 Camera（带 `setTarget(RenderTarget{})`）和一个默认 DirectionalLight（`Forward | Deferred` pass mask）。这是为了让不经过 `VulkanRenderer::initScene` 的单元测试仍然能拿到非空的 scene-level resources；真实的 production camera 由 backend 的 nullopt 回填路径处理。
- `toImageFormat(VkFormat)` 以 file-local static 形式住在 `vk_renderer.cpp` 的 anonymous namespace 中，不走 `vk_resource_manager.hpp` 的 export 路径 —— 唯一 caller 是 `makeSwapchainTarget()`，surface area 保持最小。

**下游**:
- 真正的 shadow pass —— `VulkanRenderer::initScene` 只要新增 `addPass(Pass_Shadow, shadowAtlasTarget, {})`，directional light 配 `Forward | Shadow` pass mask 即可
- `PointLight` / `SpotLight` / `AreaLight` —— 基于 `LightBase` 接口新增，不用改 scene/queue
- Overlay / UI pass —— 多 camera 共享 swapchain target，在不同 pass 各自绘制
