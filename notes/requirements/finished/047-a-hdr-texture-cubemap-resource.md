# REQ-047-a: HDR Texture And Cubemap Resource

> 2026-05-26 新增：本 REQ 扩展 texture/resource 底座，让引擎能加载 HDR 图像、创建 cubemap、维护 mip chain，并把这些资源绑定到 shader。

## 背景

当前 `TextureFormat` 只有 `RGBA8`、`RGB8` 和 `R8`，`TextureDesc` 只有 width / height / format。`VulkanTexture` 当前按 2D image、单 mip、单 array layer 创建 image view 和 sampler。材质系统已经允许 `TextureCube` reflection binding，并通过 `CombinedTextureSampler` 绑定 material-owned texture，但底层 CPU/GPU 资源还没有 cubemap 和 HDR 格式。

IBL bake 需要从 `assets/env/studio_small_03_2k.hdr` 这类 equirectangular HDR 输入开始，生成 cubemap、irradiance map、prefiltered radiance mips 和 BRDF LUT。因此本 REQ 是 IBL GPU bake 的资源前置。

## 目标

1. 支持 HDR float texture 输入。
2. 支持 TextureCube 资源形状。
3. 支持 mip levels 和 array layers。
4. 支持 Vulkan cubemap image/view/sampler。
5. 保持 material binding 仍按 shader binding name 路由。

## 需求

### R1: TextureDesc 表达 HDR 与 texture shape

扩展 `TextureDesc`。

要求：

- 增加至少 `RGBA16F` 或 `RGBA32F` 格式。
- 表达 `TextureDimension::Texture2D` / `TextureCube`。
- 表达 `mipLevels`。
- 表达 `arrayLayers`，cubemap 必须是 6 layers。
- `expectedTextureByteCount()` 能根据 format、mip、layer 校验 CPU 数据。

### R2: HDR texture loader

提供 HDR 图像加载入口。

要求：

- 支持 `.hdr` equirectangular 输入。
- 输出 float texture 数据和对应 `TextureDesc`。
- 不破坏现有 LDR `TextureLoader` 使用路径。
- 加载失败返回清晰错误，包含资产路径。

### R3: CombinedTextureSampler 支持 texture metadata

`CombinedTextureSampler` 继续作为 material descriptor 资源。

要求：

- backend 可从 sampler 读取 texture shape、format、mip/layer 信息。
- binding name 仍由 `MaterialInstance::getDescriptorResources(pass)` 填入。
- 现有 2D RGBA8 texture 路径不改变行为。

### R4: VulkanTexture 支持 cubemap 和 mip

扩展 Vulkan texture 创建。

要求：

- 创建 2D image 时保持现有行为。
- 创建 cubemap 时使用 6 array layers 和 cube-compatible flag。
- image view 能创建 `VK_IMAGE_VIEW_TYPE_CUBE`。
- sampler 支持 mipmap filter、LOD 范围和 clamp/repeat address mode。
- layout transition 和 copy upload 支持多 mip / 多 layer。

### R5: Shader reflection 与 descriptor 路由保持按名绑定

`TextureCube` binding 不引入硬编码 descriptor slot。

要求：

- shader 反射出的 `TextureCube` 仍归类为 material-owned texture，除非 binding name 被系统保留。
- descriptor update 根据 reflected `(set, binding)` 写入 cube image view。
- 2D 和 cube descriptor 都使用 `CombinedImageSampler` 资源类型，但 backend 通过 texture metadata 选择 image view。

### R6: 测试覆盖

至少覆盖：

- HDR loader 能读取 `assets/env/studio_small_03_2k.hdr`。
- `TextureDesc` 对 RGBA16F/RGBA32F byte size 计算正确。
- cubemap desc 必须有 6 layers。
- Vulkan cubemap texture 可创建 image、image view、sampler。
- shader reflection 中 `samplerCube` 映射到 `TextureCube`。
- 现有 2D texture tests 继续通过。

## 修改范围

- `src/core/asset/texture.hpp`
- `src/infra/texture_loader/`
- `src/backend/vulkan/details/device_resources/texture.*`
- `src/backend/vulkan/details/resource_manager.*`
- `src/backend/vulkan/details/descriptors/`
- `assets/env/`
- `src/test/integration/`

## 边界与约束

- 本 REQ 不生成 IBL bake 产物，只提供资源形状。
- 本 REQ 不实现 local reflection probe。
- 本 REQ 不要求 KTX/DDS 加载。
- 本 REQ 不引入 bindless texture。

## 依赖

- `REQ-046-a`：Post stack 使用 HDR scene color。
- `openspec/specs/material-system/spec.md`
- `openspec/specs/texture-loading/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`

## 后续工作

- `REQ-048-a`：使用 HDR/cubemap 底座执行 IBL GPU bake。
- `REQ-049-a`：PBR material 绑定 IBL cubemap 和 LUT。

## 实施状态

2026-06-14 复核关闭：HDR texture、equirectangular/cubemap 资源和相关测试已完成，当前 IBL/skybox 路线仍依赖这些基础能力。

已完成。

已落地：

- `ImageFormat::RGBA16Float` 已作为 HDR render-target format 进入 core/backend 映射，可供 `REQ-046-a` 的 `scene.hdrColor` 使用。
- `TextureDesc` 已表达 `RGBA16Float` / `RGBA32Float`、`Texture2D` / `TextureCube`、mipLevels 和 arrayLayers；byte-count 校验会覆盖 mip 与 cube faces。
- HDR loader 已能读取 `assets/env/studio_small_03_2k.hdr` 并输出 `RGBA32Float` CPU texture。
- VulkanTexture 已有 cubemap 创建入口，使用 6 array layers、cube-compatible image 和 cube image view。
- `samplerCube` shader reflection 已通过测试锁定为 `TextureCube`，仍按 binding name 路由。
- VulkanTexture 的 `copyFromBuffer` 已按 mip/layer 生成 `VkBufferImageCopy` regions，`test_vulkan_resource_manager` 覆盖 2D mip 与 cubemap mip/layer sampler upload 后的 Vulkan shape。
- material-owned `TextureCube` 端到端 descriptor 路由已通过 `CombinedTextureSampler` + `VulkanResourceManager::syncResource(...)` 测试覆盖；descriptor binding 仍按 shader reflection 的 binding name 写入。
