# REQ-045-c: Procedural Audio And FrameGraph Integration

> 2026-05-19 新增：本 REQ 规划 procedural shader 的第二阶段能力：真实 audio channel、post-process/fullscreen pass，以及和 shadow/forward/offscreen 等其他渲染技术的组合。

## 背景

Shadertoy 完整生态不只包含单个 fragment shader。常见效果会使用：

| Shadertoy 概念 | 引擎侧含义 |
|---|---|
| `iChannel0` audio | 每帧更新的 spectrum / waveform texture |
| Buffer A/B/C/D | 多个 offscreen pass |
| 上一帧 feedback | ping-pong history texture |
| Image pass | 最终 compositing / present pass |

当前 LXEngine 已有 FrameGraph read/write 概念和 sampled resource 表达，但还没有面向 procedural/post-process 的完整 authoring 面。

## 目标

1. 支持真实或预烘焙 audio spectrum texture。
2. 支持 procedural shader 作为 fullscreen/post-process pass。
3. 支持 procedural pass 读取其他 pass 输出，例如 scene color、shadow depth、history buffer。
4. 让 gallery demo 可以展示“procedural shader 与传统 mesh/shadow/lighting 组合”的能力。

## 需求

### R1: Audio channel 资源模型

新增 audio channel 抽象，把频谱/波形数据作为 GPU texture 暴露给 shader。

要求：

- v1 可以先支持预烘焙音频数据或 fake spectrum。
- API 表达应能升级到真实 microphone/file audio FFT。
- shader binding 名推荐使用 `iChannel0` 或 `AudioSpectrum`。
- 资源更新路径需要明确 CPU pixels -> `Texture` -> `CombinedTextureSampler` -> descriptor sync。

### R2: Dynamic texture update path

当前 texture 多为加载后静态资源。audio spectrum 需要每帧更新。

要求：

- 明确 `Texture` 是否允许更新 CPU pixel storage。
- backend resource manager 能识别 dirty texture 并重新上传。
- 更新不重建 pipeline。
- 多帧资源生命周期与现有 Vulkan resource manager 规则一致。

### R3: Fullscreen procedural pass

在 scene mesh 路径之外提供 fullscreen/pass 级 procedural draw。

要求：

- 支持一个 pass 写 offscreen color 或 swapchain color。
- pass 可以使用 material-style reflected parameters。
- 不要求节点选择/transform。
- 与 scene-embedded procedural material 并存。

### R4: FrameGraph sampled input 与 history

procedural pass 可以读取 FrameGraph 中早前 pass 写出的资源。

要求：

- 允许读取 scene color、shadow depth 或自定义 offscreen color。
- 支持至少一个 ping-pong history resource，用于上一帧 feedback。
- 保持显式 pass 顺序；自动重排留给未来。

### R5: Gallery demo 展示组合能力

新增一个 gallery scene 或 demo preset：

- 普通 mesh + directional shadow。
- scene color 经过 procedural fullscreen pass 叠加。
- audio/fake spectrum 控制 glow、beam 或 distortion。

### R6: 测试覆盖

至少覆盖：

- FrameGraph compile 能表达 procedural pass 读取早前资源。
- dynamic texture dirty/upload 路径有单元或 integration 测试。
- fullscreen procedural pass 的 pipeline build desc 可生成。
- gallery 配置能加载并保持 pass 顺序。

## 修改范围

- `src/core/frame_graph/`
- `src/core/asset/texture.*`
- `src/backend/vulkan/details/resource_manager.*`
- `src/backend/vulkan/`
- `src/demos/lxe_editor/`
- `assets/materials/`
- `assets/shaders/glsl/`
- `src/test/integration/`

## 边界与约束

- 本 REQ 不要求自动 FrameGraph DAG reorder。
- 本 REQ 不要求实时 microphone 首版必须完成；可先用预烘焙或 fake spectrum。
- 本 REQ 不要求完整 Shadertoy import tool。
- 本 REQ 不改变普通 mesh material 的现有 descriptor 合同。

## 依赖

- `REQ-045-a`
- `REQ-045-b`
- `openspec/specs/frame-graph/spec.md`
- `openspec/specs/texture-loading/spec.md`

## 后续工作

- Shadertoy import/conversion tool。
- Shader hot reload 和 gallery thumbnail capture。
- Bindless texture / descriptor indexing 路线。

## 实施状态

2026-06-14 复核关闭：audio channel / dynamic texture 路径已完成。旧 fullscreen procedural FrameGraph 分支已由标准 HDR/post-process 栈取代，不再作为 active procedural 分支维护。

Audio channel 与 dynamic texture 路径已完成，2026-05-19。Fullscreen procedural 的旧 FrameGraph 表达已在 `REQ-046-a` 的标准 HDR/post-process 迁移中删除；后续不再扩展独立 fullscreen procedural 分支。

已落地：

- `AudioSpectrumTexture`：提供 `iChannel0` 形状的 fake spectrum / waveform RGBA8 动态纹理。
- `Texture::update()`：校验像素字节数，成功更新后由 `CombinedTextureSampler::update()` 标记 dirty，沿用现有 backend texture sync 路径。
- procedural scene metadata：`proceduralMaterial.audioChannelBinding` 可在 scene YAML 中声明并 round-trip。
- editor runtime：加载 procedural 节点材质时自动替换 `iChannel0`，每帧更新 `time`、`resolution`、`audioBands` 和 fake audio texture。
- `rtr_shadertoy_quantum_core`：shader/material 反射并绑定 `iChannel0`。
- FrameGraph：旧 fullscreen procedural metadata 已删除，后续 fullscreen 处理统一走 `Pass_PostProcess` 与标准 post-process stack。

首版边界：

- fullscreen procedural 旧分支不再保留；backend fullscreen draw 改由 `REQ-046-a` 的标准 post-process 迁移处理。
- history ping-pong 目前通过显式 read/write 资源模型预留表达，不做自动资源轮转。
