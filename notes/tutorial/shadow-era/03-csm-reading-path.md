# Forward pass 怎样读 CSM：把四张底片接到 shader

CSM 像把相机前方的视野切成四段，每段用一张更合适的灯光深度底片。近处需要更密的阴影细节，远处可以覆盖更大范围。

## DirectionalLight 保存 cascade 参数

`DirectionalLightData` 是 `LightUBO` 的 CPU/GPU 合同。当前它包含一张 active `shadowViewProj`，也包含四个 cascade 的矩阵与 split。

| 字段 | 当前意义 |
|---|---|
| `shadowViewProj` | 当前正在录制的 shadow pass 使用的 light-space 矩阵 |
| `cascadeViewProj[4]` | forward shader 采样每个 cascade 时使用的矩阵 |
| `cascadeSplits` | view-space 深度切分点 |
| `shadowParams.x` | shadow map size |
| `shadowParams.y` | bias |
| `shadowParams.z` | shadow strength |
| `shadowParams.w` | cascade count |

## FrameGraph read 连接资源名和 shader binding

Forward pass 不直接持有 Vulkan image。它声明读取 `shadow.cascade0` 到 `shadow.cascade3`，并把每个资源映射到 shader binding 名：

| FrameGraph resource | Shader binding |
|---|---|
| `shadow.cascade0` | `ShadowMap0` |
| `shadow.cascade1` | `ShadowMap1` |
| `shadow.cascade2` | `ShadowMap2` |
| `shadow.cascade3` | `ShadowMap3` |

```mermaid
flowchart LR
    fg[FrameGraphRead sampled resource]
    desc[RenderInputDesc bindingPlan]
    cmd[VulkanCommandBuffer bindResources]
    attachment[VulkanResourceManager current-frame attachment]
    shader[blinnphong_0.frag ShadowMap0..3]

    fg --> desc
    desc --> cmd
    cmd --> attachment
    attachment --> shader
```

`FrameGraphSampledResource` 保存的是 resource 名和 binding 名。真正录制 descriptor 时，Vulkan command buffer 再从当前 frame 的 attachment registry 取出 image view 和 sampler。

## 我们已经学会了什么

我们知道 CSM 的四张 shadow map 是 FrameGraph 资源，不是磁盘资产。Forward shader 通过 `ShadowMap0..3` 读取它们，并用 `cascadeSplits` 选择当前像素所属 cascade。

## 下一步

进入 [04 调节阴影时先看哪些边界](04-shadow-tuning-and-limits.md)。
