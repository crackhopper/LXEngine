# Texture 与 CombinedTextureSampler：CPU 图像如何进入 GPU 资源路径

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/asset/texture.hpp](../../../../../src/core/asset/texture.hpp)
出发，关注的问题不是“怎么加载一张图”，而是：为什么项目要把纹理拆成
`Texture`（CPU 数据）和 `CombinedTextureSampler`（`IGpuResource` 适配层）
两个类型，以及这种切分如何让同一份像素数据在多个材质里安全复用。

可以先带着一个问题阅读：`Texture` 明明已经持有图像字节，为什么不让它
直接实现 `IGpuResource`？答案是，纯 CPU 图像属于资源加载侧，而 binding
name 和 dirty 追踪属于材质实例侧，两者生命周期不同，合并会让共享变难。

源码入口：[texture.hpp](../../../../src/core/asset/texture.hpp)

## Texture：CPU 侧的薄图像资源容器

`Texture` 故意只承担一件事：把图像的结构描述和原始像素字节捆在一起。它不是 GPU
对象，也不直接实现 `IGpuResource`，更不知道 Vulkan 的存在。

这条边界回答的是“一张图像在进入渲染路径之前，最少需要暴露哪些事实”：

- `TextureDesc`：宽高和像素格式等结构元数据
- `m_data`：原始像素字节，按 `std::vector<u8>` 持有

因为 `Texture` 是纯 CPU 对象，它可以被 `TextureSharedPtr` 在多个材质、多个采样器
之间复用，而不会把“谁在哪个 binding 上用这张贴图”这件 GPU 路由信息污染进来。
MaterialInstance 侧真正用来绑定的，是下面的 `CombinedTextureSampler`。

## createWhiteTexture：给 descriptor 路径的 1x1 兜底贴图

有些材质 binding 在场景装配时还没拿到真正的纹理，但 backend 的 descriptor 路径
不能接受空槽位。这个辅助函数产出一张全白的 1x1 RGBA8 贴图，作为默认填充：

- 形状合法：跟任何 RGBA8 采样路径兼容，不用单独为缺省态写分支
- 值中性：乘到材质颜色上等于保持原色
- 体积极小：1x1 贴图几乎不占显存，不会干扰 upload 预算

也就是说，它的角色不是“图像内容”，而是“缺省 binding 的占位形状”。

## CombinedTextureSampler：把纹理适配进统一 GPU 资源契约

这个类才是“纹理进入渲染路径”的实际节点。它包一个 `TextureSharedPtr`，再实现
`IGpuResource`，把 CPU 侧图像字节接进和 UBO、vertex buffer 同一条 backend
同步 / 绑定管线。

之所以 `Texture` 自己不实现 `IGpuResource`、要额外套一层 `CombinedTextureSampler`，
原因是这两类信息的归属和生命周期并不一样：

- `Texture` 属于资源加载侧，只要像素内容不变就可以被多处复用
- 绑定身份（`StringID` binding name）属于材质实例侧，不同材质会把同一张纹理
  绑到不同的 shader 变量上

把 binding name 放进采样器包装层之后，同一份 CPU 图像数据就能在多个材质里、以
不同 shader binding 名字各自独立走 descriptor 路径，而不需要复制像素。

`update()` 在改像素的同时显式 `setDirty()`，把“CPU 数据又变了”这个事实传给资源
同步阶段；backend 据此决定是否重新上传。采样器状态目前还是空壳，注释里保留了
后续扩展位。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 推荐阅读顺序

这一页最顺的读法是：

1. 先读 `Texture`，建立“CPU 图像是一个纯数据容器”的边界感
2. 再读 `CombinedTextureSampler`，看 CPU 数据如何通过包装层挂进 `IGpuResource`
3. 最后回到 `createWhiteTexture`，理解缺省 binding 为什么也要产出一张“合法形状”
   的贴图，而不是塞空指针

按这个顺序读，能比较快看清一个反直觉的设计选择：这里 GPU 资源契约并不挂在最底层
的像素容器上，而是挂在带 binding name 的采样器包装上。

## 它和 CPU / GPU 切分的关系

整条纹理路径被故意切成三段：

- `Texture`：CPU 侧，只负责“有哪些像素、多宽多高”
- `CombinedTextureSampler`：桥接层，往 `IGpuResource` 侧导出 binding name、dirty 标记
- backend 的 `device_resources/texture.cpp`：真正把字节上传成 `VkImage` 和采样器

这和 `IGpuResource` 本身的设计思路是一致的：core 层只暴露“backend 需要的最小事实”，
具体上传时机和 Vulkan 对象由 backend 自己决定。配套阅读：

- [IGpuResource：core 层的 GPU 资源统一契约](../rhi/gpu_resource.md)
- [资源上传](../../../subsystems/resource-upload.md)

## 它和材质系统的关系

从材质侧看，这个文件回答的是“材质实例持有的纹理 binding 到底是什么类型”：不是
`TextureSharedPtr`，而是 `CombinedTextureSamplerSharedPtr`。也就是说，材质实例里
出现的永远是“带 binding 身份的纹理”，真正的像素数据通过内部 shared_ptr 复用。

想把这一点接到更外层的材质运行时，可以继续读：

- [MaterialInstance：从模板到运行时账本](material_instance.md)
- [材质系统](../../../subsystems/material-system.md)

这一页只回答“纹理这个资源类在 core 层为什么长成现在这个样子”，不展开 loader、
mipmap 生成或 backend 上传的全流程。
