# REQ-042-c: v0.1.1 — Cascaded Shadow Maps

## 背景

单张 directional shadow map 能证明多 pass 链路，但在较大场景中会很快暴露分辨率不足、近处阴影糊、远处抖动等问题。v0.1.1 的近期渲染目标在 CSM 截止，因此本需求负责把方向光阴影推进到可教学、可调试、可用于后续场景的程度。

## 目标

1. 支持 4 cascade directional shadow。
2. 每个 cascade 有独立 light view-projection 和 depth region。
3. Forward shader 按 view depth 选择 cascade。
4. 提供 cascade split / range 的 debug inspection。
5. 把 CSM 作为 v0.1.1 渲染能力截止点，后续 HDR/G-Buffer/PBR 不进入 active。

## 需求

### R1: Cascade split 计算

基于主 camera 的 near/far 和配置参数计算 cascade splits：

| 参数 | 含义 |
|---|---|
| cascade count | 首版固定 4 或可配置为 1/2/4 |
| split lambda | uniform 与 logarithmic split 的混合系数 |
| shadow distance | 阴影最大覆盖距离 |

split 结果需要稳定可测试。

### R2: Cascade light matrices

每个 cascade 需要生成 light view-projection：

- 从 camera frustum slice 计算世界空间包围体。
- 以 directional light 方向构造 light view。
- 计算 orthographic projection。
- 做 texel snapping 或等价稳定化，减少移动相机时的明显闪烁。

### R3: Layered depth resource

CSM depth 可以实现为：

- texture array，每个 cascade 一层；
- 或多张 depth texture。

无论采用哪种 backend 形态，FrameGraph resource 需要能表达 cascade depth 输出，并让 forward pass 读取。

### R4: Shadow pass per cascade execution

渲染 shadow 时需要为每个 cascade 录制 depth-only draw：

- 复用 `Pass_Shadow` 的 render queue。
- 每个 cascade 使用对应 light VP。
- pipeline 可以复用，per-cascade 数据通过 UBO/push constant/descriptor 更新。

### R5: Forward shader cascade selection

Forward shader 需要：

- 根据 view-space depth 选择 cascade。
- 采样对应 cascade depth。
- 支持每 cascade bias。
- 支持最小 PCF。
- cascade 边界不应出现明显错误跳变。

### R6: Debug inspection

至少提供一种可验证手段：

- 日志或 API 输出 cascade split / matrix。
- DebugDraw 显示 camera frustum slice 或 cascade bounds。
- shader debug mode 显示 cascade index。

首版不要求完整 editor 面板。

### R7: 测试覆盖

覆盖：

- split 计算稳定。
- cascade count 与 depth resource layer/texture 数一致。
- forward pass descriptor 包含 CSM depth 和 cascade data。
- 相机 near/far / shadow distance 改变时 cascade 数据更新。
- smoke test 能渲染 CSM scene。

## 修改范围

- `src/core/frame_graph/*`
- `src/core/scene/camera*`
- `src/core/scene/light*`
- `src/backend/vulkan/*`
- `assets/shaders/glsl/`
- `assets/materials/`
- `src/demos/lxe_editor/` 中必要 debug/test scene 接入
- 相关 tests

## 边界与约束

- 本 REQ 不实现 point / spot shadow。
- 本 REQ 不实现 EVSM/VSM。
- 本 REQ 不实现 shadow atlas。
- 本 REQ 不实现 HDR/Post、PBR 完整管线、G-Buffer/Deferred。
- 本 REQ 不引入 task-based command recording。

## 依赖

- `REQ-042-a`
- `REQ-042-b`

## 后续工作

- CSM 之后的 HDR/Post、PBR、G-Buffer/Deferred 和 CPU task parallel 均保持 pending，等待 v0.1.1 目标完成后再重新进入 active。

## 实施状态

未开始。v0.1.1 的第三项 active requirement，也是近期渲染能力截止点。
