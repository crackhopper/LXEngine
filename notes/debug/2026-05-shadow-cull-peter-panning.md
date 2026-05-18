# 2026-05 Shadow Culling 与接触白边复盘

这次调试像在校准一张从灯光方向拍下来的深度底片。底片里记录的不是颜色，而是“光先碰到哪个表面”。Forward pass 再拿这张底片判断屏幕像素是否被挡光。只要底片里记录的表面和我们肉眼期待的接触表面不是同一个位置，就会出现白边、星点或闪烁。

当前结论是：白边来自 caster 侧写入表面的选择，星点来自 depth compare 的零容差。最终可用的简化组合是：地面 `/plane` 只作为 receiver，不进入 Shadow pass；Shadow pass 使用 `Cull None` 写入 caster 的正反面；方向光保留一个很小的 `shadowBias`，当前测试中 `0.002` 能压住地面花纹，同时不再出现明显接触白边。

## 现象如何演变

| 阶段 | Shadow pass cull | `/plane` 是否写 Shadow pass | `shadowBias` | 观察结果 | 说明 |
|---|---|---|---|---|---|
| 初始状态 | `Front` | 写入 | `0.02` / `0` 都试过 | 平面上花纹少，但 cube / character 在特定朝向接触处有白边 | shadow map 记录 caster 背面深度，接触阴影从背壳开始 |
| 尝试一 | `None` | 写入 | `0` | 白边消失，平面出现大量星点纹理并随相机闪烁 | 正反面都写 depth，receiver 平面也写入 shadow map，自阴影变强 |
| 尝试二 | `Back` | 写入 | `0` | 白边仍被压制，但星点/闪烁仍存在 | 写入光源可见正面深度，caster 接触更贴近；但 receiver 正面也写入 shadow map |
| 尝试三 | `None` | 不写入 | `0` | 白边消失，但 `/plane` 表面仍有明显花纹 | receiver 自写 depth 已排除，剩余花纹来自 caster depth 与 receiver compare 的零容差 |
| 当前可用组合 | `None` | 不写入 | `0.002` | 白边消失，地面花纹被明显压住 | 小 bias 给 depth compare 留出误差余量 |

相关提交：

| Commit | 内容 | 结论 |
|---|---|---|
| `fd76987` | Shadow pass 改为 `Back` | 能减小接触白边，但地面 acne 明显 |
| `43ad703` | Shadow pass 改为 `None` | 白边消失，但 receiver 参与 shadow map 时地面花纹严重 |
| `51d12de` | 恢复 shadow `Front` | 证明 `Front` 是低 acne 基线，但白边仍存在 |
| `2c253e3` | `primitive:plane` receiver-only，Shadow pass 使用 `Cull None` | 白边消失；`shadowBias = 0` 时仍有花纹，`0.002` 后视觉可用 |

## 为什么 Front 会有白边

`Front` culling 的意思是：从光源看过去，正面三角形不写 shadow map，背面三角形写入 depth。这个策略常用于减少接收面自阴影，因为很多闭合物体的背面深度比正面深度更远，屏幕上的表面不容易和 shadow map 里的同一表面发生精度争夺。

但它的代价也很明确：对有厚度的 caster 来说，shadow map 记录的是“背壳”，不是最靠近光源的“前壳”。当 cube 或 character 的朝向让背壳轮廓和接触轮廓之间的距离暴露出来时，Forward pass 会把接触边附近判成未遮挡，于是出现白边。cube 因为是规则硬边盒子，最早暴露问题；character 旋转到脸朝光源后，同样出现白边，说明根因不是 cube mesh 特例，而是 `Front` culling 的几何厚度代价。

这个白边本质上是 peter-panning：阴影和投影物体分离。它不是屏幕输出错位，也不是 shadow map 没读取，而是 shadow map 本身记录了一个有意偏后的表面。

## 当前实现调研：白边从哪里产生

当前阴影链路可以拆成三步：材质决定哪些三角形写 shadow map，方向光为当前相机计算 light-space 矩阵，Forward shader 再把屏幕像素投影回 shadow map 做深度比较。

| 环节 | 当前实现 | 对白边的影响 |
|---|---|---|
| Shadow pass 材质状态 | `assets/materials/blinnphong_*.material` 的 `passes.Shadow.renderState.cullMode` | `Front` 会让闭合 caster 写背面 depth，接触阴影天然后退 |
| Shadow vertex shader | `shadow_depth_only.vert` 使用 `sceneLight.shadowViewProj * worldPos` | 它只负责把 caster 顶点投到 light clip space，不会补偿 front-cull 带来的厚度偏移 |
| Cascade 矩阵 | `DirectionalLight::updateShadowCascadesForCamera()` 用 active camera frustum 包围盒拟合正交投影 | cascade 范围决定 shadow map 世界 texel 大小和 depth range，影响白边的可见宽度 |
| Forward 采样 | `blinnphong_0.frag::sampleShadowMap()` 用 `cascadeViewProj[cascadeIndex]` 投影当前像素 | 当前像素深度和 shadow map 中的背面深度比较，背面更远时接触处容易判成 lit |
| Bias | `shadowParams.y` 先按世界单位保存，再除以 `cascadeDepthRanges[cascadeIndex]` | 正 bias 会进一步把比较点推向 lit 侧，可能放大 peter-panning；设为 0 只能去掉这层额外偏移，不能消除 front-cull 的几何厚度 |
| PCF | shader 固定 3x3 采样，步长来自 `shadowParams.x` | PCF 会把边界平均成灰阶，但不会修正 shadow map 记录的是背面这个事实 |

把这几步连起来看，白边的直接来源是这一组条件同时成立：

1. 物体是有厚度的闭合 caster，例如 cube 或 character。
2. Shadow pass 使用 `Front` culling，只把背面写进 shadow map。
3. 接触区域的前面和背面沿光照方向有明显距离。
4. Forward shader 比较的是 receiver 当前像素深度和 shadow map 里的背面深度。
5. `shadowBias = 0` 只能移除 shader bias，不能把背面 depth 变成前面 depth。

所以 cull mode 确实能“解释并改变”白边：改成 `Back` 或 `None` 后，shadow map 更容易写入靠近光源的表面，白边会消失。它一开始不是安全修复，是因为它同时改变了 receiver 平面的写入行为。后续把 `/plane` 从 Shadow pass 排除后，这个组合才变得可用。

## 为什么 cull 修白边会引入更多渲染错误

当前场景里平面和立方体原本使用同一类 Blinn-Phong shadow pass。也就是说，材质级 `cullMode` 一改，caster 和 receiver 一起变。问题不在 `Cull None` 本身，而在 receiver 也开始写入 shadow map。

| 改法 | 为什么能减小白边 | 为什么会出错 |
|---|---|---|
| `Back` | caster 写靠近光源的正面 depth，接触阴影贴近物体 | receiver 平面正面也写 shadow map；Forward 再读同一平面，容易 acne |
| `None` | 正反面都参与，caster 接触处更容易有近深度 | 如果 receiver 也写 Shadow pass，星点更严重 |
| `Front` | receiver 正面不写入，acne 压力小 | 有厚度 caster 写背面，产生接触白边 |

因此这次调研后的判断是：`cullMode` 必须和 caster / receiver 语义一起看。单独把所有材质改成 `Cull None` 会失败；把 `/plane` 排除出 Shadow pass 后，`Cull None` 可以作为压掉接触白边的简单方案。

更稳妥的修复方向应拆成两个问题：

| 问题 | 需要的能力 |
|---|---|
| receiver 不应该因为自己写 shadow map 而自阴影 | 节点级或材质实例级 `castsShadow` / `receivesShadow` 语义，至少允许 ground receiver 不进入 `Pass_Shadow` |
| 有厚度 caster 使用 `Front` 时产生接触白边 | caster 侧的深度策略需要单独处理，例如更明确的 caster-only depth bias、normal/depth offset，或只对特定 caster 使用不同 shadow 写入策略 |

当前代码已经用 `MaterialInstance::setPassEnabled(Pass_Shadow, false)` 把 `builtin://lxe_editor/primitives/plane` 做成 receiver-only。它还不是完整的 scene YAML / Inspector 作者表面；Light 上已有 `light.castsShadow`，那控制的是光源是否支持 Shadow pass，不是 mesh 节点是否投影。

## 为什么 Back / None 会有星点和闪烁

`Back` 和 `None` 都会让接收平面的正面更容易写入 shadow map。平面本身又在 Forward pass 中读取这张 shadow map。这样同一个表面既是 shadow caster，又是 shadow receiver。

当 `shadowBias = 0` 时，平面像素在 Forward pass 里算出的 `projCoords.z` 和 shadow map 里采样到的 `closestDepth` 非常接近。即使 `/plane` 已经不写 Shadow pass，caster 的正反面深度仍可能落在 receiver 的比较边界附近。任何浮点误差、三角形插值差异、shadow map 分辨率量化、PCF 邻域采样或 cascade 范围轻微变化，都可能让某些采样点一会儿通过、一会儿失败。屏幕上就表现为星点状纹理和闪烁。

这不是普通纹理噪声，而是 depth compare 在同一表面上反复输赢：

```glsl
visibility += (projCoords.z - depthBias) <= closestDepth ? 1.0 : 0.0;
```

当 `depthBias` 为 0，并且 `projCoords.z` 与 `closestDepth` 接近同一个量化边界时，比较结果会变得非常脆弱。`shadowBias = 0.002` 的作用不是“移动模型”，而是给比较留出一个很小的容差，把误差边界上的错误阴影点推回 lit 侧。

## 当前采用的简化方案

当前代码采用一个可解释、改动面较小的组合：

| 项 | 当前做法 | 目的 |
|---|---|---|
| `primitive:plane` | 加载后关闭 `Pass_Shadow` | 地面只接收阴影，不把自身 depth 写入 shadow map |
| Blinn-Phong Shadow pass | `cullMode: None` | caster 正反面都可写入，接触阴影不再从背壳开始 |
| 方向光 bias | 场景中使用小值，例如 `0.002` | 压住 `Cull None` 下的零容差 depth compare 花纹 |

这个方案不是通用高质量阴影的终点，但它解决了当前目标：先消除接触白边，同时避免地面出现不可接受的 acne。后续如果要成为完整作者能力，应把 `castsShadow` 做成 renderable 级字段，而不是只对内置 plane 特判。

## 这次确认过的事实

| 事实 | 证据 |
|---|---|
| shadow map 输出存在 | debug dump 能导出 `shadow.cascade0` |
| Forward pass 确实读取 shadow map | shadow-only Forward 模式能显示阴影区域 |
| light camera 与 shadow matrix 曾经不一致 | 通过光源相机视角和 shadow dump 对比定位，并已在前序修复中收敛 |
| `shadowBias` 已可调 | CommandBus / Inspector 已支持 `shadowBias` |
| 单独 cull 改动不是最终修复 | `None` / `Back` 消除白边但引入 receiver acne |
| receiver-only plane 是关键约束 | `/plane` 不写 Shadow pass 后，`Cull None` 不再让地面自己和自己比较 |
| `shadowBias = 0` 不可用 | 零容差比较会让 caster depth 与 receiver 投影深度在误差边界上抖动 |
| `shadowBias = 0.002` 当前可用 | 花纹明显减少，接触白边也没有回到 `Cull Front` 的程度 |

## CSM 近处走样为什么还会存在

接触白边解决后，近处阴影仍可能走样。这个问题属于 shadow map 采样密度和投影别名，不是同一个 cull 问题。

当前 DirectionalLight 会用 active camera 的 frustum 计算 cascades。`shadowDistance`、cascade 数、split 分布和 shadow map 分辨率共同决定“近处一个 shadow texel 覆盖多少世界空间”。如果第一段 cascade 覆盖范围仍偏大，或者相机近处地面斜着映射到 light space，近处边缘就会出现锯齿、游泳或 PCF 颗粒。

| 参数 | 影响 |
|---|---|
| `shadowDistance` | 越大，每个 cascade 覆盖越大；近处 texel 世界尺寸可能变大 |
| `shadowCascadeCount` | 越多，近处可以拿到更小覆盖范围 |
| shadow map size | 当前通过 `shadowParams.x` 保存，越大越细，但成本更高 |
| active camera | cascade 按当前 active camera 更新；调试时要确认使用的是 editor camera |

因此 CSM 测试应固定当前相机、固定 bias，再分别比较 `shadowCascadeCount = 1` 和 `4`、不同 `shadowDistance` 的 Forward dump。

## 下一步不要只靠 cull 解决

白边和星点是 shadow mapping 的两个相反压力：

| 压力 | 常见表现 | 不能只靠什么解决 |
|---|---|---|
| Peter-panning | 阴影和物体分离，接触处有白边 | 不能只靠把 cull 改成 `Back` |
| Shadow acne | 平面出现星点、条纹、闪烁 | 不能只靠把 bias 设为 0 |

接下来更合理的方向是把“投影”和“接收”拆开处理，而不是把所有材质的 Shadow pass cull 一刀切。当前代码已经有 `MaterialInstance::setPassEnabled(Pass_Shadow, false)` 这样的底层能力，后续可以把它发展成节点级 `castsShadow` 作者入口：平面作为 receiver 可以不写 shadow map，cube / character 作为 caster 继续写 shadow map。

这条路能避免 receiver 自阴影。当前实验说明：receiver-only plane 配合 `Cull None` 和很小 bias，已经能同时压住白边和花纹。更长期的版本应把这组隐含规则变成显式编辑能力，并补上 rasterizer depth bias / slope-scale bias。

## 我们已经学会了什么

我们确认了这次白边不是 FrameGraph 读写断链，也不是 light matrix 仍然错位，而是 shadow map 记录表面的选择问题。`Front` 稳定但会记录背面，`Back` / `None` 更贴近接触；当 receiver 仍参与 Shadow pass 时会让自阴影失控，当 receiver-only plane 配合小 bias 后，`Cull None` 成为当前更可用的简化方案。

## 下一步

继续排查时应把 CSM 近处走样作为独立问题处理：固定场景后比较 cascade 数、shadow distance 和 shadow map 分辨率。相关实现入口：

- `assets/materials/blinnphong_*.material`
- `assets/shaders/glsl/blinnphong_0.frag`
- `assets/shaders/glsl/shadow_depth_only.vert`
- `src/core/asset/material_instance.cpp`
- `src/core/frame_graph/render_queue.cpp`
- `src/core/scene/light.cpp`
