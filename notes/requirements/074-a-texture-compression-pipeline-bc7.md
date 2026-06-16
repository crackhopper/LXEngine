# REQ-074-a: Texture Compression Pipeline With BC7

> 2026-06-13 更新：本 REQ 在 `SceneResourceTable` 管理纹理资源、realtime material path hard cut 和 OfflineRT config hard cut 之后，引入纹理编码策略和 BC7 上传路径，降低显存占用和采样带宽压力。它是 `REQ-073-a` 的后续性能与资源质量工作，不重新定义 Material v3 的参数合同。

## 背景

`REQ-073-a` 扩展了 Material v3 PBRT-style 参数合同，使 `Kd`、metallic、roughness、AO、emissive、normal 等参数通过 source-reflected material contract 的 factor、texture slot 和 channel selector 进入 realtime shader。随着 Helmet、BMW M6 和后续大型资产都走 resource table / bindless texture table，未压缩 RGBA 纹理会带来显存和带宽压力。

当前纹理路径需要补齐的能力：

| 问题 | 影响 |
|---|---|
| texture resource metadata 缺少压缩/编码策略 | loader、package 和 Vulkan upload 无法判断应上传什么 GPU format |
| baseColor/emissive 与 normal/ORM 的 color space 不同 | 统一当作 RGBA 会造成采样或压缩质量问题 |
| BC7 需要设备 format support 检查 | 不检查会在不支持设备上创建 image 失败或行为不明确 |
| mip 生成和压缩顺序未定义 | 画质、性能和 package cache 都不稳定 |
| 默认纹理也要参与 resource identity / upload | white、black、flatNormal 不能绕过资源表形成隐藏路径 |

本 REQ 首版选择 BC7 作为 desktop 高质量压缩目标。移动端 ASTC/ETC、KTX2/BasisU 转码、运行时流式纹理不是本轮目标。

## 目标

1. 为 texture resource 增加明确的编码策略 metadata。
2. 支持 BC7 sRGB / UNORM 格式选择和 Vulkan 上传。
3. 区分 color texture 与 data texture 的 color space。
4. 明确 mip 生成和压缩 payload 边界。
5. 不支持 BC7 的设备输出明确 unsupported/fallback diagnostic。
6. shader 采样路径不关心源图是否压缩。

## 非目标

- 不修改 Material v3 的参数合同、`bsdf.source` contract 或 source-reflected 上传合同。
- 不要求所有平台都支持 BC7。
- 不实现 ASTC、ETC2、BC1/BC3、BasisU、KTX2 或 GPU runtime transcoding。
- 不实现完整 texture streaming / virtual texture。
- 不把 BC7 编码器选择硬编码成不可替换的唯一方案。

## 需求

### R1: Texture Encoding Metadata

Texture resource metadata SHALL 记录纹理编码策略。

最低字段：

| 字段 | 说明 |
|---|---|
| `sourceFormat` | 源图像通道和位深，例如 RGBA8 |
| `semantic` | `baseColor`、`emissive`、`normal`、`orm`、`roughness`、`metallic`、`genericData` 等 |
| `colorSpace` | `srgb` 或 `linear` |
| `mipPolicy` | none / generate / source-provided |
| `compressionPolicy` | none / bc7-preferred / bc7-required |
| `targetFormat` | resolved GPU format，例如 `BC7_SRGB_BLOCK` 或 `BC7_UNORM_BLOCK` |
| `compressedState` | source / compressed / uploaded |

metadata SHALL 属于 resource table 的资源事实，不能只存在于 Vulkan backend 局部变量中。

### R2: BC7 Format Selection

首版 SHALL 支持 BC7。

规则：

- baseColor、emissive 等颜色纹理默认使用 sRGB BC7。
- normal、metallic、roughness、AO、ORM 等数据纹理默认使用 linear / UNORM BC7。
- default white / black / flatNormal 可以生成压缩或非压缩内置版本，但必须记录实际上传 format。
- 如果 source texture 已经是压缩格式，loader 必须识别并避免重复有损压缩，或输出明确 unsupported。

### R3: Device Capability Probe

Vulkan backend SHALL 在上传前检查目标格式支持。

要求：

- 检查 `VK_FORMAT_BC7_SRGB_BLOCK` 和 `VK_FORMAT_BC7_UNORM_BLOCK` 的 sampled image 支持。
- 如果 `compressionPolicy=bc7-required` 且设备不支持，load/upload SHALL fail-fast。
- 如果 `compressionPolicy=bc7-preferred` 且设备不支持，可以走明确 fallback，但 diagnostics 必须记录 fallback format。
- 不能静默把 BC7 metadata 当作普通 RGBA 上传。

### R4: Compression Stage

纹理压缩 SHALL 是 resource load/upload pipeline 中明确的 task。

任务输出：

- compressed byte payload。
- block dimensions / mip levels。
- target format。
- diagnostics。
- content hash 或 cache key。

编码器选择可以在实现计划中确定，但必须满足：

- 可在 Linux CI 或开发环境中构建/运行，或提供明确 optional capability。
- 支持 BC7 sRGB/UNORM payload 输出。
- 失败时返回 diagnostics，不写半初始化 texture resource。

### R5: Mip Generation Policy

mip 生成 SHALL 在压缩策略中明确。

规则：

- source-provided mip 优先保留。
- generate mip 时，先从 linearized source 生成 mip，再按目标 color space / BC7 格式压缩。
- normal map mip 生成必须保持合理归一化策略，不能直接当普通 color averaging 而不记录。
- 不支持 mip 的临时路径必须明确 metadata 和 diagnostic。

### R6: Compressed Payload Boundary

本 REQ SHALL 明确压缩阶段输出哪种 payload 和 metadata，供后续 package/cache 需求消费。本 REQ 不实现 package restore。

首版建议：

```text
source asset
  -> decoded canonical pixels / metadata
  -> generated mip chain
  -> compressed BC7 payload
  -> Vulkan upload consumes compressed payload
```

要求：

- compressed payload hash 应进入 texture resource metadata。
- Vulkan upload consumes the compressed payload directly when the backend supports the resolved format。
- 后续 `REQ-074-c/d` 决定 compressed payload 在 package 中的 section/chunk 组织。
- 如果 backend 不支持 resolved compressed format，本 REQ 只要求 load/upload 输出 fallback/recompress/unsupported diagnostic，不要求 package fallback。

### R7: Resource Deduplication

同一 canonical texture URI + encoding policy SHALL 只压缩和上传一次。

规则：

- 多个材质引用同一纹理时共享 resource handle 和 bindless slot。
- 同一源纹理以不同 semantic/colorSpace 使用时，可以形成不同 encoded resource identity；必须在 metadata 中可诊断。
- 默认纹理共享固定 resource identity。

### R8: Shader Transparency

Shader SHALL 不感知纹理是否压缩。

要求：

- source-reflected material record 仍只保存 bindless texture index 和 channel/semantic 信息。
- shader 采样路径不分 BC7 / RGBA。
- 压缩格式差异由 Vulkan image format 和 sampler 负责。

## 测试

### T1: Metadata Resolution

构造 baseColor、normal、ORM 三类纹理，断言：

- baseColor target format 为 sRGB BC7。
- normal/ORM target format 为 UNORM BC7。
- semantic、colorSpace、mipPolicy、compressionPolicy 被记录到 resource metadata。

### T2: Vulkan Format Support Gate

用 capability probe 覆盖：

- 支持 BC7 时创建 compressed sampled image。
- 不支持 BC7 且 policy 为 required 时 fail-fast。
- 不支持 BC7 且 policy 为 preferred 时走明确 fallback，并输出 diagnostics。

### T3: Compression Task

输入一张 RGBA texture，运行 compression task，断言：

- 输出 BC7 block payload。
- mip level metadata 正确。
- content hash 稳定。
- 失败不会注册可上传 texture。

### T4: Dedup And Bindless Slot

两个材质引用同一 baseColor texture，断言：

- resource table 只创建一个 encoded texture resource。
- GPU upload 只上传一次。
- bindless slot 相同。

### T5: Compressed Payload Upload

上传压缩 payload：

- 使用 resolved BC7 format 创建 sampled image。
- 不重新解码 source image。
- unsupported format 走明确 diagnostic。

### T6: Shader Sampling Is Unchanged

同一材质分别使用未压缩 fallback 与 BC7 upload，在容许阈值内输出一致；shader 侧不需要额外 variant 或分支。

## 修改范围

- `src/core/resource/resource_metadata.*`
- `src/core/scene/scene_resource_table*`
- `src/core/scene/scene_resource_table_upload_view*`
- texture loader / image decode 相关 infra 文件
- Vulkan texture/image upload 路径
- `IGpuResourceTable` / Vulkan GPU resource table
- default texture 注册路径
- texture/resource integration tests

## 边界与约束

- BC7 是首版 desktop target，不代表所有平台唯一格式。
- 对 unsupported device 必须有明确诊断。
- 压缩不能改变 material layout 或 shader binding contract。
- 不允许 material-local placeholder texture 绕过 resource table。
- normal / data texture 不得按 sRGB 采样或压缩。

## 依赖

- `REQ-073-a`: Material v3 PBRT source contract、metallic extension 和默认纹理集合。
- `REQ-073-j`: realtime material path hard cut and smoke，保证 texture/default texture 进入 clean realtime 默认路径。
- `REQ-074-i`: OfflineRT config hard cut，保证后续 texture/package 工作面对同一套 clean SceneResourceTable / RenderPathGraph 默认路径。

## 后续工作

- `REQ-074-b`: package canonical state readiness gate。
- `REQ-074-c`: LxScenePackage file format。
- ASTC/ETC2/mobile texture policy。
- KTX2 / BasisU source and package format。
- Texture streaming / virtual texture。
- BC1/BC3 或 mixed compression policy。
- Artist-facing compression quality presets。

## 实施状态

未实施。
