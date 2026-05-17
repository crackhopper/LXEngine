# REQ-042-b: v0.1.1 — Directional shadow map 与 depth-only pass

## 背景

`REQ-042-a` 完成后，引擎可以表达“一个 pass 写离屏 depth，后续 pass 读取它”。本需求在这个能力上实现第一条真实 multiple pass 渲染路径：方向光 shadow map。

首版 shadow 的目标是验证 FrameGraph v1、depth-only pipeline、scene-level light data、material pass 和 forward shader 之间的协作，而不是追求复杂阴影质量。

## 目标

1. 增加 `Pass_Shadow` 的 depth-only 渲染路径。
2. 为方向光生成 shadow view-projection。
3. Forward pass 能采样 shadow map 并影响受光结果。
4. 支持 hard shadow 和最小 PCF。
5. 提供能在 editor/test scene 中观察的阴影效果。

## 需求

### R1: Shadow FrameGraph path

渲染路径至少包含：

```text
Pass_Shadow:
  writes shadow.depth

Pass_Forward:
  reads shadow.depth
  writes swapchain.color / swapchain.depth
```

`Pass_Shadow` 使用 `REQ-042-a` 提供的 offscreen depth target。

### R2: Depth-only pipeline

Shadow pass 需要 depth-only pipeline：

- vertex shader 输出 light clip space position。
- fragment shader 可以为空或最小实现。
- render state 关闭 color attachment。
- depth test/write 开启。
- 支持当前 mesh vertex layout。

### R3: Shadow caster pass participation

Renderable 是否进入 `Pass_Shadow` 应沿用 pass-aware 机制：

- material template 可声明 `Shadow` pass。
- 或提供 fallback depth-only shadow caster material/pass。
- disabled / unsupported pass 不进入 shadow queue。

### R4: Directional light shadow data

方向光需要提供 shadow 所需数据：

| 数据 | 用途 |
|---|---|
| light direction | 构建 light view |
| shadow view-projection | shadow pass 与 forward shader 共用 |
| shadow map size | 创建 depth target |
| shadow strength / bias | 控制采样结果 |

首版可以只支持一个主方向光投射阴影。

### R5: Forward shader shadow sampling

Forward shader 需要采样 shadow map：

- 从 world position 或 light-space position 得到 shadow lookup。
- 支持 depth comparison。
- 支持最小 bias，避免明显 acne。
- 支持 hard shadow。
- 支持 3x3 PCF 或等价最小软化。

### R6: Editor / scene integration

现有 editor 场景搭建能力需要能创建可观察的 shadow 场景：

- 至少一个 ground receiver。
- 至少一个 caster。
- 一个 directional light。
- camera 能看到阴影。

可以通过内置测试 scene 或 tutorial scene 体现，不要求新增复杂 UI。

### R7: 测试覆盖

覆盖：

- FrameGraph 中存在 shadow → forward 的 resource 依赖。
- `Pass_Shadow` queue 只包含支持 shadow 的 renderable。
- depth-only pipeline build desc 不包含 color attachment。
- forward pass 的 descriptor resources 包含 shadow map resource。
- smoke test 能运行 shadow scene，不因缺失 depth target 或 descriptor 失败。

## 修改范围

- `assets/shaders/glsl/`
- `assets/materials/`
- `src/core/scene/light*`
- `src/core/frame_graph/*`
- `src/backend/vulkan/*`
- `src/demos/lxe_editor/` 中必要 scene/test scene 接入
- 相关 tests

## 边界与约束

- 本 REQ 不实现 CSM；只做单张 directional shadow map。
- 本 REQ 不实现 point / spot shadow。
- 本 REQ 不实现 cascades debug overlay。
- 本 REQ 不实现 HDR/Post 或 deferred。
- 本 REQ 不要求 shadow atlas。

## 依赖

- `REQ-042-a`
- 当前 pass-aware material / render queue / scene light data

## 后续工作

- `REQ-042-c` Cascaded Shadow Maps。
- 未来 PBR、多光源 shadow、shadow atlas 后置到 pending / 后续 roadmap。

## 实施状态

未开始。v0.1.1 的第二项 active requirement。
