# 04A · Shader 反射与 Layout 约束

> 这一章专门回答一个容易混淆的问题：  
> **传统 Vulkan 里，SPIR-V reflection 常用于“自动生成 pipeline layout”；到了 bindless 里，这件事没有消失，但职责变了。**

## 4A.1 先把三层对象分开

我们讨论 bindless 时，最容易把下面三层混在一起：

| 层次 | 它回答的问题 | 在 Vulkan 里对应什么 |
|---|---|---|
| **ShaderReflection** | shader 声明了哪些资源、这些资源长什么样 | SPIR-V 反射结果 |
| **DescriptorSetLayout / PipelineLayout** | GPU 绑定接口长什么样 | `VkDescriptorSetLayout` / `VkPipelineLayout` |
| **Material / ResourceSystem** | 这次 draw 真正要用哪些资源、索引和类型是否匹配 | 材质参数、纹理句柄、buffer 句柄 |

可以把它们想成三层约束：

- **reflection** 像“读图纸”
- **pipeline layout** 像“定义插座标准”
- **material system** 像“检查插头有没有插对”

传统做法里，这三层经常被写成一条自动链路；bindless 之后，前两层会更稳定，第三层会更重要。

## 4A.2 传统路径：reflection 常常直接生成 layout

在传统 per-material descriptor 模型里，shader 通常直接声明自己要哪些资源：

```glsl
layout(set = 0, binding = 0) uniform LocalConstants {
    mat4 model;
    mat4 viewProjection;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D albedoTex;
layout(set = 1, binding = 1) uniform sampler2D normalTex;
```

这时 SPIR-V reflection 很自然就会被拿来做下面这条链：

```text
SPIR-V
  ↓ 反射
得到 set / binding / descriptor type / stage visibility
  ↓
生成 VkDescriptorSetLayout
  ↓
组合成 VkPipelineLayout
```

也就是说，**reflection 在这里既负责“理解 shader”，也常负责“直接长出 layout”**。

这种方式的优点是自动化强，shader 改了，layout 也跟着变。缺点是 layout 很容易跟着 shader 分化，最终把 pipeline layout 数量也带多。

## 4A.3 Bindless 路径：layout 先固定，reflection 改成校验器

到了 bindless，架构重心会倒过来。

我们通常先定义一套全局 ABI，例如：

| 位置 | 约定 |
|---|---|
| `set = 0, binding = 0` | frame / scene 常量 |
| `set = 1, binding = 10` | bindless `COMBINED_IMAGE_SAMPLER` 大数组 |
| `set = 1, binding = 11` | bindless `STORAGE_IMAGE` 大数组 |
| `push constants` | object / material / draw 索引 |

这时流程变成：

```text
引擎先定义固定 DescriptorSetLayout / PipelineLayout
  ↓
SPIR-V reflection 检查 shader 是否遵守这套 ABI
  ↓
材质系统在创建和更新时检查资源句柄是否满足 shader 约束
```

所以 bindless 里 reflection 的职责更像：

| 传统模式 | Bindless 模式 |
|---|---|
| 自动生成 shader 专属 layout | 校验 shader 是否匹配预定义 ABI |
| 合并多个 stage 的资源声明 | 记录 shader 用到了 ABI 的哪些部分 |
| 决定 pipeline layout 长什么样 | 决定“这个 shader 能不能接到现有 pipeline layout 上” |

这就是为什么我们说：**bindless 没有让 reflection 失去价值，而是把它从“合成器”变成了“校验器 + 记录器”。**

## 4A.4 PipelineLayout 到底会不会限制 2D / 3D？

这是最容易误判的地方。

**`VkPipelineLayout` 不直接限制纹理是 2D 还是 3D，也不限制纹理尺寸。**  
它真正限制的是：

- 这个位置是不是 `sampled image`
- 是不是 `storage image`
- 是不是 `uniform buffer`
- 数组长度是多少
- 哪些 shader stage 可见

也就是说，layout 关心的是 **descriptor 大类**，不是纹理的维度和分辨率。

| 约束内容 | 由谁定义 |
|---|---|
| `binding = 10` 是 `COMBINED_IMAGE_SAMPLER[]` | `VkDescriptorSetLayoutBinding` |
| shader 把它当 `sampler2D[]` 还是 `sampler3D[]` 用 | shader 类型系统 / SPIR-V |
| 运行时真正塞进去的是 `VkImageViewType 2D` 还是 `3D` | 资源系统写 descriptor 时决定 |
| 贴图是 `256x256` 还是 `4096x4096` | 资源对象本身决定 |

所以在 bindless 里，同一个 `(set, binding)` 被不同 shader 分别声明为 `sampler2D[]` / `sampler3D[]`，从 **layout 兼容性** 上看常常是允许的，因为 descriptor 大类仍然是同一类 sampled image。

真正的风险被推迟到了“应用自己保证语义正确”这一层：

- shader A 把 slot 5 当 `2D` 采
- shader B 把 slot 5 当 `3D` 采
- layout 仍然兼容
- **但如果资源系统没有保证 slot 5 的实际 view 类型与当前 shader 一致，就会出现 UB**

这也是 bindless 常说的那句老话：**类型安全从“layout 结构”转移到了“资源管理约定”。**

## 4A.5 为什么 texture 没有“固定大小”，UBO 却有？

这两类资源的“刚性”来源不同。

| 资源 | layout 层面关心什么 | shader 访问时真正依赖什么 |
|---|---|---|
| Texture | descriptor 类别、数组长度 | image view 类型、格式、采样方式 |
| UBO | descriptor 类别、数组长度 | **block 内存布局、offset、stride、总大小范围** |

Texture 在 shader 里是通过采样接口访问的，shader 不需要知道“底层内存是几字节一条”。因此：

- 2D / 3D / cube 要求的是“视图兼容”
- 256² / 1024² / 4K 不会反映到 pipeline layout

而 UBO 不一样。shader 会按照 `std140` / `std430` 之类的静态布局去读内存，所以：

- block 成员顺序固定
- offset / stride 固定
- 我们传给 GPU 的数据大小至少要覆盖 shader 读取范围

因此 UBO 看起来更“硬”，texture 看起来更“软”。

## 4A.6 Bindless 下，reflection 和材质系统怎么配合

在 bindless 模式里，更稳妥的职责分工通常是：

| 模块 | 负责什么 |
|---|---|
| **固定 PipelineLayout** | 定义全局 ABI：有哪些 set / binding / push constant |
| **ShaderReflection** | 读取 shader 资源声明，验证它是否符合 ABI，并记录需要的资源种类 |
| **MaterialTemplate / MaterialInstance** | 把“逻辑参数”映射到具体资源句柄，并验证句柄类型是否满足 shader 约束 |

对应到实际时序，通常会分成四次校验：

1. **shader 编译/导入时**
   检查 `set/binding`、descriptor type、push constant 范围是否符合引擎 ABI。
2. **material template 创建时**
   检查材质声明的字段是否覆盖 shader 真正需要的资源。
3. **material instance 写值时**
   检查传入的是不是正确类型的资源句柄，例如 `Texture2D`、`Texture3D`、`StorageImage`。
4. **draw / dispatch 前**
   在 debug 构建下做最后的断言，例如索引是否有效、资源是否已上传。

这样做的目标不是“运行时再猜 shader 想要什么”，而是**把错误尽量前移，在资源进入 drawcall 之前就对齐 ABI 与语义约束**。

## 4A.7 对 LX Engine 的直接启发

如果 LX Engine 将来进入 bindless 路线，那么我们更值得追求的不是：

- “每次都从 reflection 重新生成一套 pipeline layout”

而是：

- “定义少量稳定的全局 layout”
- “让 reflection 产出结构化约束”
- “让材质系统和 shader 编译层用这些约束做验证”

也就是说，bindless 下真正重要的自动化不是“自动长 layout”，而是：

| 我们真正想自动化的东西 | 为什么更重要 |
|---|---|
| shader ABI 校验 | 防止 shader 偷偷偏离全局约定 |
| 材质参数与资源类型校验 | 防止 `sampler2D` / `sampler3D`、sampled / storage 混用 |
| 句柄索引有效性校验 | 防止 slot 越界、已释放资源被引用 |
| 反射结果缓存与登记 | 让 pipeline / material 创建只依赖结构化元数据 |

从工程角度看，这比“再自动生成一套 layout”更接近 bindless 的真实价值。

## 继续阅读

- [04 · Pipeline 与 Shader 变量策略](04-Pipeline与Shader策略.md)
- [06 · LX Engine 演进路径](06-LXEngine演进路径.md)
- [openspec/specs/shader-reflection/spec.md](../../../../openspec/specs/shader-reflection/spec.md)
- [openspec/specs/renderer-backend-vulkan/spec.md](../../../../openspec/specs/renderer-backend-vulkan/spec.md)
