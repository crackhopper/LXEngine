# REQ-008: FrameGraph 驱动真正的渲染路径

> **Partial supersede by REQ-009**：R3 `Scene::getSceneLevelResources()` 无参版本、R4 `RenderQueue::buildFromScene(scene, pass)` 两参签名、R6 `defaultForwardTarget()` 占位路径已被 REQ-009 替换为 `getSceneLevelResources(pass, target)` / `buildFromScene(scene, pass, target)` / `makeSwapchainTarget()` 的真实派生版本。Scene 的单 camera/single-light 字段（`CameraPtr camera` / `DirectionalLightPtr directionalLight`）已被 `std::vector<CameraPtr>` / `std::vector<LightBasePtr>` 取代，`Camera` 持 `std::optional<RenderTarget>`，`LightBase` 升级为抽象接口。其他 R（R1/R2/R5/R7/R8）继续有效 —— `passFlagFromStringID` / `IRenderable::supportsPass` / 测试 helper `firstItemFromScene` / pass-mask 过滤 scenario 的结构没有变化。归档保留历史上下文；当前实现以 REQ-009 (`2026-04-14-multi-camera-multi-light` change) 为准。

## 背景

本项目的渲染数据流目前存在一个架构上的明显错误：**`VulkanRenderer` 完全绕过了 `FrameGraph` / `RenderQueue`，持有一个单独的 `RenderingItem` 成员变量用于 draw**。具体表现：

- `src/backend/vulkan/vk_renderer.cpp:265` — `RenderingItem renderItem{};` 作为 `Impl` 的成员
- `src/backend/vulkan/vk_renderer.cpp:119` — `initScene` 通过 `scene->buildRenderingItem(Pass_Forward)` 拿到**单个** item
- `src/backend/vulkan/vk_renderer.cpp:122-131` — Camera / Light UBO 通过 `renderItem.descriptorResources.push_back(...)` **手动 side-channel 注入**
- `src/backend/vulkan/vk_renderer.cpp:153-158` — `FrameGraph` 被建了一次，**仅用于 pipeline preload**，用完丢弃
- `src/backend/vulkan/vk_renderer.cpp:203/217/218` — draw 循环只使用 `renderItem` 一个对象

后果：

1. **场景里第二个及以后的 `IRenderable` 不会被渲染** —— `Scene::addRenderable(r)` 加进来的对象对 backend 不可见
2. **`Pass_Shadow` / `Pass_Deferred` 即使配置出来也没有 draw 路径** —— `initScene` 硬编码只 `addPass(Pass_Forward)`，且 FrameGraph 只用来 preload
3. **职责混乱** —— `Scene::buildRenderingItem(pass)` 作为"单 item 简化入口"和 `FrameGraph::buildFromScene` 调用的 `Scene::buildRenderingItemForRenderable(renderable, pass)` 形成两条平行的 item 构造路径
4. **Side-channel 重复劳动** —— 每个 backend 集成测试都必须手工 `push_back` camera / light UBO 以模拟 `initScene` 的行为（`test_vulkan_command_buffer.cpp:111` 的注释 `"Match VulkanRenderer::initScene(): inject camera/light UBO resources"` 就是气味），任何新的 scene 级资源（env map、shadow atlas）都要在每个测试里重复

正确的数据流应该是：**`FrameGraph` 驱动 → `RenderQueue` 管理 `RenderingItem` → `Scene` 作为纯数据容器被读取**。本需求把这条路径打通。

## 目标

1. **`RenderingItem` 的构造职责从 `Scene` 搬到 `RenderQueue`** —— 删除 `Scene::buildRenderingItem(pass)` 和 `Scene::buildRenderingItemForRenderable`；`RenderQueue::buildFromScene(scene, pass)` 成为唯一入口
2. **`VulkanRenderer` 持有 `FrameGraph` 成员**，生命周期与 scene 绑定；draw loop 真正遍历 `frameGraph.getPasses() × pass.queue.getItems()`
3. **Scene 级 UBO 通过 `Scene::getSceneLevelResources()` 统一暴露**，`RenderQueue::buildFromScene` 自动合并到每个 item 的 `descriptorResources`，消除 side-channel 注入
4. **`IRenderable::supportsPass(pass)`** 新虚方法（默认实现基于现有 `getPassMask()` + 一个 `passFlagFromStringID` helper），`RenderQueue::buildFromScene` 按此过滤
5. **所有 6 个 `buildRenderingItem` 的测试调用点迁移**到新路径，side-channel UBO 注入 block 一并删除

本需求 **不** 做多 camera / 多 light / Camera 持有 `RenderTarget` / Light 带 pass mask / `getSceneLevelResources(pass, target)` 的过滤签名 —— 这些留给 REQ-009。本需求保持 `Scene` 的单 camera / 单 light 假设，`getSceneLevelResources()` 是无参版本。

## 需求

### R1: `passFlagFromStringID` helper

在 `src/core/scene/pass.hpp` 新增自由函数声明：

```cpp
namespace LX_core {

/// 把 pass StringID 映射为 ResourcePassFlag 位。
/// - Pass_Forward  → ResourcePassFlag::Forward
/// - Pass_Deferred → ResourcePassFlag::Deferred
/// - Pass_Shadow   → ResourcePassFlag::Shadow
/// - 其他          → ResourcePassFlag{0}（即所有位为 0）
ResourcePassFlag passFlagFromStringID(StringID pass);

}
```

实现放在新建的 `src/core/scene/pass.cpp`（目前 `pass.hpp` 是 header-only）。实现比较 `pass == Pass_Forward` 等分支返回对应位。

**为什么放在 `scene/pass.hpp` 而不是 `gpu/render_resource.hpp`**：这个 helper 连接 scene 层的 pass 身份（`StringID`）和 resource 层的 pass flag（`ResourcePassFlag`），`pass.hpp` 是 scene 层入口，放在这里不引入 `gpu → scene` 的反向依赖。

### R2: `IRenderable::supportsPass` 虚方法

在 `src/core/scene/object.hpp:50` 的 `IRenderable` 类新增：

```cpp
class IRenderable {
public:
    // ... 现有方法 ...

    /// 该 renderable 是否参与指定 pass。默认实现基于 getPassMask()。
    /// 子类可 override 以提供更细粒度的判断（例如按 material 的 pass entry 是否存在）。
    virtual bool supportsPass(StringID pass) const {
        const auto flag = passFlagFromStringID(pass);
        return (static_cast<uint32_t>(getPassMask()) &
                static_cast<uint32_t>(flag)) != 0;
    }
};
```

`RenderableSubMesh` 不需要 override，走默认实现。

`object.hpp` 需要 `#include "core/scene/pass.hpp"` 来拿到 `passFlagFromStringID` 和 `StringID`（可能已经间接 include 了，实施时确认）。

### R3: `Scene::getSceneLevelResources()` 无参版本

在 `src/core/scene/scene.hpp:30` 的 `Scene` 类新增：

```cpp
class Scene {
public:
    // ... 现有成员保持不变 ...

    /// 返回场景级 descriptor 资源（camera UBO、light UBO 等）。
    /// 由 RenderQueue::buildFromScene 在构造每个 RenderingItem 时自动合并到
    /// item.descriptorResources，替代 VulkanRenderer::initScene 里的手工 push_back。
    ///
    /// 本版本 **无参**：所有 scene 级资源一视同仁地合并到所有 item。
    /// 单 camera / 单 light 假设下这是正确行为。REQ-009 会扩展为
    /// `getSceneLevelResources(pass, target)` 支持多 camera / 多 light 过滤。
    std::vector<IRenderResourcePtr> getSceneLevelResources() const;
};
```

实现（`scene.cpp`）按顺序 push_back：

1. `std::dynamic_pointer_cast<IRenderResource>(camera->getUBO())`（若 `camera` 非空）
2. `std::dynamic_pointer_cast<IRenderResource>(directionalLight->getUBO())`（若 `directionalLight` 非空）

顺序规则：先 camera 后 light，保持和原 `VulkanRenderer::initScene` 里 side-channel 注入的顺序一致，让 descriptor set 的 binding 顺序不变。

### R4: `RenderQueue::buildFromScene(scene, pass)` 新入口

在 `src/core/scene/render_queue.hpp:12` 的 `RenderQueue` 类新增：

```cpp
class RenderQueue {
public:
    // ... 现有方法 addItem / clearItems / sort / getItems / collectUniquePipelineBuildInfos ...

    /// 按 pass 从场景构建 queue 里所有 RenderingItem：
    ///   1. clearItems()
    ///   2. 遍历 scene.getRenderables()
    ///   3. 跳过 renderable->supportsPass(pass) 为 false 的
    ///   4. 为每个匹配的 renderable 构造 RenderingItem（vertex/index buffer、
    ///      descriptor resources、shader info、pass mask、pass、material、pipelineKey）
    ///   5. 把 scene.getSceneLevelResources() 合并到 item.descriptorResources 末尾
    ///   6. sort() 按 PipelineKey 稳定排序
    void buildFromScene(const Scene &scene, StringID pass);
};
```

**实现要点**:

- 把原 `Scene::buildRenderingItemForRenderable(const IRenderablePtr&, StringID pass) const` (`src/core/scene/scene.cpp:6-29`) 的逻辑整段搬到 `render_queue.cpp`，作为**文件内 static free function** 或 `RenderQueue` 的 **private static** 方法：

  ```cpp
  // render_queue.cpp
  namespace {
  RenderingItem makeItemFromRenderable(const IRenderablePtr &renderable,
                                       StringID pass) {
      RenderingItem item;
      if (!renderable) return item;

      item.vertexBuffer = renderable->getVertexBuffer();
      item.indexBuffer  = renderable->getIndexBuffer();
      item.objectInfo   = renderable->getObjectInfo();
      item.descriptorResources = renderable->getDescriptorResources();
      item.shaderInfo   = renderable->getShaderInfo();
      item.passMask     = renderable->getPassMask();
      item.pass         = pass;

      auto sub = std::dynamic_pointer_cast<RenderableSubMesh>(renderable);
      if (sub && sub->mesh && sub->material) {
          item.material    = sub->material;
          StringID objSig  = sub->getRenderSignature(pass);
          StringID matSig  = sub->material->getRenderSignature(pass);
          item.pipelineKey = PipelineKey::build(objSig, matSig);
      }
      return item;
  }
  } // namespace
  ```

- `RenderQueue::buildFromScene` 本体：

  ```cpp
  void RenderQueue::buildFromScene(const Scene &scene, StringID pass) {
      clearItems();
      auto sceneResources = scene.getSceneLevelResources();
      for (const auto &renderable : scene.getRenderables()) {
          if (!renderable) continue;
          if (!renderable->supportsPass(pass)) continue;
          RenderingItem item = makeItemFromRenderable(renderable, pass);
          // 合并 scene 级资源
          item.descriptorResources.insert(item.descriptorResources.end(),
                                          sceneResources.begin(),
                                          sceneResources.end());
          m_items.push_back(std::move(item));
      }
      sort();
  }
  ```

- **Include 方向**: `render_queue.hpp` 目前已经 include `scene.hpp`（为了 `RenderingItem`），不引入新的循环

### R5: 删除 `Scene::buildRenderingItem` 与 `buildRenderingItemForRenderable`

- 删除 `src/core/scene/scene.hpp:57` 的 `RenderingItem buildRenderingItem(StringID pass);` 声明
- 删除 `src/core/scene/scene.hpp:60-62` 的 `RenderingItem buildRenderingItemForRenderable(...) const;` 声明
- 删除 `src/core/scene/scene.cpp:6-35` 里这两个方法的实现

验收: `grep -rn "buildRenderingItem\b" src/core/` 零命中；所有调用点在 R7 迁移后 build 绿。

### R6: `VulkanRenderer` 持有 `FrameGraph` 并真正用于 draw

`src/backend/vulkan/vk_renderer.cpp` 的 `Impl` 类：

**6.1 成员变量改动**（line 265 附近）:

```cpp
// 删除:
// RenderingItem renderItem{};

// 新增:
FrameGraph m_frameGraph;
```

**6.2 `initScene` 重写**（line 117-167）:

```cpp
void initScene(ScenePtr _scene) override {
    scene = _scene;

    // 配置 pass（当前只有 Forward；未来可由外部配置决定）
    m_frameGraph.addPass(FramePass{
        Pass_Forward,
        defaultForwardTarget(),   // 从 device surface/depth format 派生
        {}
    });

    // 为每个 pass 从 scene 构建 queue
    //   RenderQueue::buildFromScene 内部会:
    //   - 按 supportsPass 过滤 renderable
    //   - 合并 scene.getSceneLevelResources() 到每个 item 的 descriptorResources
    //   - 按 PipelineKey 排序
    m_frameGraph.buildFromScene(*scene);

    // 初始上传所有 dirty 资源 + 初始化 push constant
    for (auto &pass : m_frameGraph.getPasses()) {
        for (auto &item : pass.queue.getItems()) {
            resourceManager->syncResource(*cmdBufferMgr, item.vertexBuffer);
            resourceManager->syncResource(*cmdBufferMgr, item.indexBuffer);
            for (auto &res : item.descriptorResources) {
                resourceManager->syncResource(*cmdBufferMgr, res);
            }
            if (item.objectInfo) {
                PC_Draw pc{};
                pc.model = Mat4f::identity();
                pc.enableLighting = 1;
                pc.enableSkinning = 0;
                item.objectInfo->update(pc);
            }
        }
    }
    resourceManager->collectGarbage();

    // 预构建 pipeline（FrameGraph 已经填满）
    auto infos = m_frameGraph.collectAllPipelineBuildInfos();
    resourceManager->preloadPipelines(infos);

    // debug 日志保持
    if (rendererDebugEnabled()) { /* ... */ }
}
```

**重要**: 这段改动**彻底删除** line 121-131 的 camera/light side-channel 注入 block —— 它们已经由 R3 + R4 的 `getSceneLevelResources()` 路径处理。

**6.3 `defaultForwardTarget()` 辅助函数**（新增，文件内私有）:

```cpp
RenderTarget defaultForwardTarget() const {
    // 从 device 派生 swapchain 对应的 RenderTarget。
    // REQ-009 会扩展为：initScene 里把这个 target 填进所有 m_target == nullopt 的
    // Camera；本 REQ 只把它作为 FramePass.target 存好，不用于 Camera 匹配。
    RenderTarget t{};
    // t.colorFormat = toImageFormat(device->getSurfaceFormat().format);
    // t.depthFormat = toImageFormat(device->getDepthFormat());
    // t.sampleCount = 1;
    // 注：toImageFormat 是 VkFormat → ImageFormat 的反向映射。
    //     本 REQ 只用于 FramePass.target 占位，还没有人读它去过滤；
    //     若 toImageFormat 尚不存在可以在本 REQ 新增，或者暂时返回默认构造的
    //     RenderTarget{} —— REQ-009 会真正消费它。
    return t;
}
```

**决定**: 本 REQ **不**强求 `toImageFormat` 存在。如果 `toImageFormat` helper 尚未实现，可以直接返回默认构造的 `RenderTarget{}`，因为 REQ-008 里没有代码路径真正**读取** `pass.target` 用于过滤。REQ-009 会把这件事做完整。这种"先占位、后填真值"的做法明确记在边界约束里。

**6.4 `uploadData` 重写**（line 169-177）:

```cpp
void uploadData() override {
    for (auto &pass : m_frameGraph.getPasses()) {
        for (auto &item : pass.queue.getItems()) {
            resourceManager->syncResource(*cmdBufferMgr, item.vertexBuffer);
            resourceManager->syncResource(*cmdBufferMgr, item.indexBuffer);
            for (auto &res : item.descriptorResources) {
                resourceManager->syncResource(*cmdBufferMgr, res);
            }
        }
    }
    resourceManager->collectGarbage();
}
```

**6.5 `draw` 重写**（line 179 起）:

```cpp
void draw() override {
    // ... 现有 acquire / begin / viewport / scissor / renderpass begin 保持 ...

    for (auto &pass : m_frameGraph.getPasses()) {
        for (auto &item : pass.queue.getItems()) {
            auto &pipeline = resourceManager->getOrCreateRenderPipeline(item);
            cmd->bindPipeline(pipeline);
            cmd->bindResources(*resourceManager, pipeline, item);
            cmd->drawItem(item);
        }
    }

    // ... renderpass end / submit / present 保持 ...
}
```

**副作用**: 场景里第二个及以后的 renderable **终于**会被画出来。目前 `test_render_triangle.cpp` 只有一个三角形所以看不出区别，这个路径要靠 R8 的新测试验证。

### R7: 测试迁移

6 个集成测试文件（+ 1 个共享 helper）需要改动：

**7.1 新建** `src/test/integration/scene_test_helpers.hpp`:

```cpp
#pragma once
#include "core/scene/pass.hpp"
#include "core/scene/render_queue.hpp"
#include "core/scene/scene.hpp"
#include <cassert>

namespace LX_test {

/// 测试辅助：从 scene 构建一个 RenderQueue（按指定 pass），返回第一个 RenderingItem。
/// 用于需要单 item 的测试 setup。assert 队列非空。
inline LX_core::RenderingItem
firstItemFromScene(LX_core::Scene &scene, LX_core::StringID pass) {
    LX_core::RenderQueue q;
    q.buildFromScene(scene, pass);
    assert(!q.getItems().empty() && "scene produced no items for pass");
    return q.getItems().front();
}

}
```

**7.2 迁移调用点**:

| 文件 | 原代码 | 新代码 |
|------|--------|--------|
| `src/test/integration/test_vulkan_command_buffer.cpp:109` | `auto renderItem = scene->buildRenderingItem(LX_core::Pass_Forward);` | `auto renderItem = LX_test::firstItemFromScene(*scene, LX_core::Pass_Forward);` |
| `src/test/integration/test_vulkan_resource_manager.cpp:64` | `auto item = scene->buildRenderingItem(LX_core::Pass_Forward);` | `auto item = LX_test::firstItemFromScene(*scene, LX_core::Pass_Forward);` |
| `src/test/integration/test_vulkan_pipeline.cpp:49` | 同上 | 同上 |
| `src/test/integration/test_pipeline_cache.cpp:54` | 同上 | 同上 |
| `src/test/integration/test_pipeline_build_info.cpp:160` | `return scene->buildRenderingItem(Pass_Forward);` | `return LX_test::firstItemFromScene(*scene, Pass_Forward);` |

每个文件顶部 `#include "scene_test_helpers.hpp"`。

**7.3 删除 side-channel UBO 注入 block**:

`test_vulkan_command_buffer.cpp` 在 line 109 之后有一段 "Match VulkanRenderer::initScene(): inject camera/light UBO resources" 注释 + 手动 push_back。这整段删除，因为 `firstItemFromScene` 走的 `RenderQueue::buildFromScene` 路径已经通过 `scene->getSceneLevelResources()` 合并了 camera/light UBO。

其他测试文件如果有类似的手工注入 block，也一并删除。

**7.4 `test_pipeline_cache.cpp:110` 的 `fg.buildFromScene(*scene)` 保持不变** —— 那个文件本来就走 FrameGraph 路径，是本 REQ 的下游用户而非迁移对象。

**7.5 `test_render_triangle.cpp`** 通过 `renderer->initScene(scene)` 间接调用，不直接调 `buildRenderingItem`，不用改。只需要确认在新 `initScene` 实现下三角形照常绘制（可通过目视或 `test_render_triangle` 本身的 exit code 判断）。

### R8: `test_frame_graph.cpp` 新增 pass mask 过滤 scenario

在 `src/test/integration/test_frame_graph.cpp` 新增测试场景（参考文件内已有的 mock renderable 模式）:

```cpp
// 伪代码 —— 实际以文件内 mock 辅助为准
TEST_SCENARIO("pass mask filtering excludes non-matching renderables") {
    auto rA = makeMockRenderable(ResourcePassFlag::Forward |
                                  ResourcePassFlag::Shadow);
    auto rB = makeMockRenderable(ResourcePassFlag::Forward);

    auto scene = Scene::create(rA);
    scene->addRenderable(rB);

    FrameGraph fg;
    fg.addPass(FramePass{Pass_Forward, {}, {}});
    fg.addPass(FramePass{Pass_Shadow,  {}, {}});
    fg.buildFromScene(*scene);

    const auto &passes = fg.getPasses();
    ASSERT_EQ(passes[0].queue.getItems().size(), 2);  // Forward: 两个都匹配
    ASSERT_EQ(passes[1].queue.getItems().size(), 1);  // Shadow: 只有 rA
}
```

**顺带**: 加一条 scenario 断言 "`FrameGraph::buildFromScene` 多次调用是幂等的，不会重复累加 item" —— 因为 `RenderQueue::buildFromScene` 头部调 `clearItems()`，这个性质应该成立，值得测试锁定。

## 测试

- **`src/test/integration/test_frame_graph.cpp`** — 新增 pass mask 过滤 + 幂等重建两条 scenario (R8)
- **`src/test/integration/scene_test_helpers.hpp`** — 新文件，测试共享 `firstItemFromScene` helper (R7)
- **`src/test/integration/test_vulkan_command_buffer.cpp`** / **`test_vulkan_resource_manager.cpp`** / **`test_vulkan_pipeline.cpp`** / **`test_pipeline_cache.cpp`** / **`test_pipeline_build_info.cpp`** — 迁移到 helper + 删除 side-channel UBO 注入 block (R7)
- **`src/test/test_render_triangle.cpp`** — 验证 `initScene` 新路径下三角形照常绘制 (R6)

## 修改范围

| 文件 | 改动 |
|------|------|
| `src/core/scene/pass.hpp` | 新增 `passFlagFromStringID(StringID)` 声明 |
| `src/core/scene/pass.cpp` | **新** — `passFlagFromStringID` 实现 |
| `src/core/scene/object.hpp` | `IRenderable::supportsPass(pass)` 虚方法 + 默认实现 |
| `src/core/scene/scene.hpp` | 删除 `buildRenderingItem` / `buildRenderingItemForRenderable` 声明；新增 `getSceneLevelResources()` |
| `src/core/scene/scene.cpp` | 同上实现；`getSceneLevelResources` 返回 camera UBO + light UBO |
| `src/core/scene/render_queue.hpp` | 新增 `buildFromScene(scene, pass)` 声明 |
| `src/core/scene/render_queue.cpp` | `buildFromScene` 实现（搬运原 `buildRenderingItemForRenderable` 逻辑为文件内 helper） |
| `src/core/scene/frame_graph.cpp` | `buildFromScene` 里调 `pass.queue.buildFromScene(scene, pass.name)`，不再自己构造 item |
| `src/backend/vulkan/vk_renderer.cpp` | `Impl::renderItem` → `m_frameGraph`；`initScene` / `uploadData` / `draw` 全部改为遍历 frame graph；**删除 side-channel camera/light UBO 注入** |
| `src/test/integration/scene_test_helpers.hpp` | **新** — `firstItemFromScene` helper |
| `src/test/integration/test_vulkan_command_buffer.cpp` | 迁移到 helper + 删除 UBO 注入 block |
| `src/test/integration/test_vulkan_resource_manager.cpp` | 同上 |
| `src/test/integration/test_vulkan_pipeline.cpp` | 同上 |
| `src/test/integration/test_pipeline_cache.cpp` | 同上 |
| `src/test/integration/test_pipeline_build_info.cpp` | 同上 |
| `src/test/integration/test_frame_graph.cpp` | 新增 pass mask 过滤 scenario + 幂等重建 scenario |
| `docs/requirements/finished/007-interning-pipeline-identity.md` | **顶部加 "R9 Superseded by REQ-008" banner**（见"冲突扫描"） |
| `notes/architecture.md` | 数据流图修正（`/update-notes` 增量会覆盖） |
| `notes/subsystems/frame-graph.md` | 调用关系段修正 |
| `notes/subsystems/scene.md` | 调用关系段修正 |

## 边界与约束

- **不**做多 camera / 多 light —— REQ-009 的范围
- **不**让 `Camera` 持有 `RenderTarget` —— REQ-009 的范围
- **不**给 `LightBase` 加 `getPassMask()` / `getUBO()` / `supportsPass()` 抽象 —— REQ-009 的范围
- **不**实现 `getSceneLevelResources(pass, target)` 带过滤的签名 —— REQ-009 的范围
- **不**实现 `Pass_Shadow` / `Pass_Deferred` 的真实绘制路径 —— 本 REQ 只打通架构，`initScene` 仍然只 `addPass(Pass_Forward)`；R8 的 pass mask 测试通过 mock renderable 在 test 内部验证
- **不**支持动态 scene 变更（运行期 add/remove renderable 后自动 rebuild queue）—— 本 REQ 假设 "initScene 时构建一次，之后 scene 不变"
- **不**废除 `ResourcePassFlag`（位标志）—— helper `passFlagFromStringID` 负责 `StringID ↔ ResourcePassFlag` 的翻译，两套表示继续共存
- **`FramePass.target` 在 REQ-008 里只是占位** —— R6 的 `defaultForwardTarget()` 可以返回默认构造的 `RenderTarget{}`；没有代码路径读它用于过滤。REQ-009 才真正消费 target
- **`RenderQueue` 的 include 方向**: `render_queue.hpp` 继续 include `scene.hpp`，不引入新的循环依赖
- **`FrameGraph` 生命周期**: 随 `VulkanRenderer::Impl` 一起，和 scene 绑定，backend shutdown 时一并销毁

## 冲突扫描

- **REQ-007 R9**（`docs/requirements/finished/007-interning-pipeline-identity.md:234-304`）明确把 `Scene::buildRenderingItem(StringID pass)` 作为 normative 对外入口。本 REQ R5 **删除**此入口。
  - **解决**: 实施本 REQ 时在 `finished/007-*.md` 顶部加 banner:
    ```markdown
    > **Partial supersede by REQ-008**：本文档 R9 `Scene::buildRenderingItem(StringID pass)` 对外入口已被 REQ-008 废弃。`RenderingItem` 的构造职责转移到 `RenderQueue::buildFromScene(scene, pass)`。R1–R8 仍然有效（`getRenderSignature(pass)` 的结构化 interning 路径继续使用）。归档保留历史上下文；当前实现以 REQ-008 为准。
    ```
  - 这是对 finished 文档的局部追加修改（只加 banner，不改正文），符合 pre-commit-reviewer 规则里对 "Superseded banner 例外" 的允许
- **REQ-003b** `docs/requirements/finished/003b-pipeline-prebuilding.md:330-348` 引用 `Scene::buildRenderingItem` 和 `buildRenderingItemForRenderable` 作为依赖项。本 REQ 删除这两个方法时，REQ-003b 的 R3 / R7 逻辑仍然成立（FrameGraph 作为扫描入口的意图未变），只是具体调用的方法名变了。**无需加 banner**，因为 REQ-003b 不是 normative 地钉死这两个方法名，只是当作参考调用
- **REQ-005** `docs/requirements/finished/005-unified-material-system.md:280` 提到"`scene.cpp` `buildRenderingItem` 继续调用 `sub->material->getShaderProgramSet() / getRenderState()`"。这行描述已经过时（`getShaderProgramSet()` 在后续清理中被删除），且 REQ-005 的 R 本身不依赖 `buildRenderingItem` 这个方法名。**无需加 banner**
- **REQ-002** `docs/requirements/finished/002-pipeline-key.md:113-143` 描述 `Scene::buildRenderingItem()` 填充 `pipelineKey`。这是 REQ-002 最早引入的流程，已经被 REQ-007 接手并重写过一次签名。本 REQ 删除方法会让 REQ-002 描述的流程从"在 Scene 发生"变为"在 RenderQueue 发生"，但 REQ-002 的核心主张（`RenderingItem.pipelineKey` 字段存在、由某处自动填充）仍然成立。**无需加 banner**

## 依赖

- **REQ-003b**（已完成）— `FrameGraph` / `RenderQueue` / `PipelineCache` / `PipelineBuildInfo` 基础设施
- **REQ-007**（已完成）— `StringID`-based pass constants (`Pass_Forward` 等)、`IRenderable::getRenderSignature(pass)`、`IMaterial::getRenderSignature(pass)` —— 注意 REQ-007 的 R9 会被部分 supersede，见冲突扫描
- **REQ-005**（已完成）— `MaterialInstance` 作为唯一 `IMaterial` 实现

## 下游

- **REQ-009**（规划中）— 多 camera / 多 light / Camera 持有 `RenderTarget` / Light pass mask / `Scene::getSceneLevelResources(pass, target)` 带过滤的版本 / `VulkanRenderer::initScene` 派生 swapchain target 注入 nullopt camera
- **支持真正的多 pass 渲染**（shadow prepass / deferred lighting） —— 本 REQ 打通架构后，添加新 pass 只需要 `m_frameGraph.addPass(FramePass{Pass_Shadow, shadowTarget(), {}})` 即可（REQ-009 落地后才能真正渲染到非 swapchain target）
- **动态 scene 变更** —— 后续可以在 Scene 上加 dirty flag，让 VulkanRenderer 决定是否 rebuild queue

## 实施状态

已完成并通过 `/finish-req` 验证（2026-04-14）。对应 openspec change `2026-04-14-frame-graph-drives-rendering` 已归档，delta 已 sync 到 `openspec/specs/frame-graph` / `render-signature` / `renderer-backend-vulkan`。

**R1–R8 验证结果**（`/finish-req` 阶段，grep/read 逐条对照代码）：
- R1: `passFlagFromStringID` 声明 `src/core/scene/pass.hpp:26`，实现 `src/core/scene/pass.cpp:7`；smoke test 在 `test_frame_graph.cpp::testPassFlagFromStringIDSmoke` ✓
- R2: `IRenderable::supportsPass(pass)` 虚方法声明 `src/core/scene/object.hpp:70`，默认实现基于 `getPassMask() & passFlagFromStringID(pass)` ✓
- R3: `Scene::getSceneLevelResources()` 声明 `src/core/scene/scene.hpp:65`，实现 `src/core/scene/scene.cpp:6`，顺序 camera→light ✓
- R4: `RenderQueue::buildFromScene(scene, pass)` 声明 `src/core/scene/render_queue.hpp:34`，实现 `render_queue.cpp:67` — 含文件内 `makeItemFromRenderable` 静态 helper + 场景级资源合并 + `sort()` ✓
- R5: `Scene::buildRenderingItem*` 已删除，`grep -rn "buildRenderingItem\b" src/` 仅剩 `scene_test_helpers.hpp` 注释中的历史引用 ✓
- R6: `VulkanRenderer::Impl` 持 `m_frameGraph` (`vk_renderer.cpp:283`)；`initScene` 按 `Pass_Forward + defaultForwardTarget()` 配置并 `buildFromScene`；`uploadData` / `draw` 均通过 `getPasses() × queue.getItems()` 双层循环驱动；side-channel camera/light UBO 注入块已删除 ✓
- R7: `src/test/integration/scene_test_helpers.hpp` 新文件提供 `firstItemFromScene`；5 个集成测试 (`test_vulkan_command_buffer` / `test_vulkan_resource_manager` / `test_vulkan_pipeline` / `test_pipeline_cache` / `test_pipeline_build_info`) 全部迁移 ✓
- R8: `test_frame_graph.cpp` 新增 `testPassFlagFromStringIDSmoke` / `testPassMaskFilterExcludesNonMatching` / `testMultiPassRebuildIsIdempotent` 三个场景 — **全部通过** ✓
- 冲突扫描落实: REQ-007 R9 的 "Partial supersede by REQ-008" banner 已追加 ✓

**`/finish-req` 顺带清理**:
- `src/core/scene/scene.cpp` 删除 unused `#include "core/resources/mesh.hpp"` —— 仅 `buildRenderingItemForRenderable` 曾依赖该 include，删除后不影响编译。

**回归测试**（全部绿）:
- `test_frame_graph` — 9 scenarios including 3 new REQ-008 scenarios
- `test_pipeline_build_info`, `test_string_table`, `test_material_instance`, `test_pipeline_identity` — non-GPU 核心层回归
- GPU 依赖测试 (`test_vulkan_*`) 在无显示环境下 SKIP cleanly，编译期验证已在 full build 阶段完成。

**下游**: REQ-009 可以开工（`/draft-req` 已完成 → `/opsx:propose 009` 下一步）。
