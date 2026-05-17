# 2026-05 Shadow Culling 与接触白边复盘

这次调试像在校准一张从灯光方向拍下来的深度底片。底片里记录的不是颜色，而是“光先碰到哪个表面”。Forward pass 再拿这张底片判断屏幕像素是否被挡光。只要底片里记录的表面和我们肉眼期待的接触表面不是同一个位置，就会出现白边、星点或闪烁。

当前结论是：`cullMode` 不能作为这次白边的主修复。把 shadow pass 从 `Front` 改到 `None` 或 `Back` 会改变 shadow map 里写入的几何表面，能临时压掉白边，但会让接收平面写入自己的 shadow depth，从而产生星点状 self-shadow acne，并且在相机移动、CSM 覆盖范围更新或 PCF 采样边界变化时闪烁。

## 现象如何演变

| 阶段 | Shadow pass cull | 观察结果 | 说明 |
|---|---|---|---|
| 初始状态 | `Front` | 平面上没有星点，但斜插平面的立方体附近有白边 | shadow map 主要记录 caster 的背面深度，降低 acne，但会产生 peter-panning |
| 尝试一 | `None` | 白边消失，平面出现大量星点纹理并随相机闪烁 | 正反面都可能写 depth，receiver 平面也参与 shadow map，自阴影变强 |
| 尝试二 | `Back` | 白边仍被压制，但星点/闪烁仍存在 | 写入光源可见正面深度，caster 接触更贴近；但 receiver 正面也写入 shadow map |
| 恢复 | `Front` | 回到稳定但有白边的状态 | cull 改动被排除为最终修复方向 |

相关提交：

| Commit | 内容 | 结论 |
|---|---|---|
| `1535dc1` | Shadow pass 改为 `None` | 证明白边和 cull 写入面有关，但引入 acne |
| `68ca02b` | Shadow pass 改为 `Back` | 进一步证明 receiver 自写 depth 会造成星点 |
| `f5fdd0b` / `58c03c2` | 恢复 shadow `Front`，并修正误命中的 Forward cull | 当前回到稳定基线 |

## 为什么 Front 会有白边

`Front` culling 的意思是：从光源看过去，正面三角形不写 shadow map，背面三角形写入 depth。这个策略常用于减少接收面自阴影，因为很多闭合物体的背面深度比正面深度更远，屏幕上的表面不容易和 shadow map 里的同一表面发生精度争夺。

但它的代价也很明确：对有厚度的 caster 来说，shadow map 记录的是“背壳”，不是最靠近光源的“前壳”。立方体斜着穿过平面时，前壳已经接触平面，背壳沿光照方向仍有一个厚度距离。Forward pass 用背壳深度判断遮挡时，接触边缘附近就会被判成未遮挡，于是出现一圈白边。

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

1. 立方体是有厚度的闭合 caster。
2. Shadow pass 使用 `Front` culling，只把背面写进 shadow map。
3. 立方体斜着穿过平面，接触区域的前面和背面沿光照方向有明显距离。
4. Forward shader 比较的是 receiver 当前像素深度和 shadow map 里的背面深度。
5. `shadowBias = 0` 只能移除 shader bias，不能把背面 depth 变成前面 depth。

所以 cull mode 确实能“解释并改变”白边：改成 `Back` 或 `None` 后，shadow map 更容易写入靠近光源的表面，白边会消失。但这不是安全修复，因为它同时改变了 receiver 平面的写入行为。

## 为什么 cull 修白边会引入更多渲染错误

当前场景里平面和立方体使用同一类 Blinn-Phong shadow pass。也就是说，材质级 `cullMode` 一改，caster 和 receiver 一起变。我们没有只让“立方体写正面 depth、平面不写 self-shadow depth”的节点级区分。

| 改法 | 为什么能减小白边 | 为什么会出错 |
|---|---|---|
| `Back` | caster 写靠近光源的正面 depth，接触阴影贴近物体 | receiver 平面正面也写 shadow map；Forward 再读同一平面，容易 acne |
| `None` | 正反面都参与，caster 接触处更容易有近深度 | 双面几何和 receiver 自写 depth 都进入比较，星点更严重 |
| `Front` | receiver 正面不写入，acne 压力小 | 有厚度 caster 写背面，产生 peter-panning 白边 |

因此这次调研后的判断是：`cullMode` 是白边的触发因子之一，但不是合适的修复开关。它同时控制 caster 和 receiver，粒度太粗。

更稳妥的修复方向应拆成两个问题：

| 问题 | 需要的能力 |
|---|---|
| receiver 不应该因为自己写 shadow map 而自阴影 | 节点级或材质实例级 `castsShadow` / `receivesShadow` 语义，至少允许 ground receiver 不进入 `Pass_Shadow` |
| 有厚度 caster 使用 `Front` 时产生接触白边 | caster 侧的深度策略需要单独处理，例如更明确的 caster-only depth bias、normal/depth offset，或只对特定 caster 使用不同 shadow 写入策略 |

当前代码已经有 `MaterialInstance::setPassEnabled(Pass_Shadow, false)`，但它还没有成为 scene YAML / Inspector 里的 renderable 级作者表面。Light 上已有 `light.castsShadow`，那控制的是光源是否支持 Shadow pass，不是 mesh 节点是否投影。

## 为什么 Back / None 会有星点和闪烁

`Back` 和 `None` 都会让接收平面的正面更容易写入 shadow map。平面本身又在 Forward pass 中读取这张 shadow map。这样同一个表面既是 shadow caster，又是 shadow receiver。

当 `shadowBias = 0` 时，平面像素在 Forward pass 里算出的 `projCoords.z` 和 shadow map 里同一平面写入的 depth 非常接近。任何浮点误差、三角形插值差异、shadow map 分辨率量化、PCF 邻域采样或 cascade 范围轻微变化，都可能让某些采样点一会儿通过、一会儿失败。屏幕上就表现为星点状纹理和闪烁。

这不是普通纹理噪声，而是 depth compare 在同一表面上反复输赢：

```glsl
visibility += (projCoords.z - depthBias) <= closestDepth ? 1.0 : 0.0;
```

当 `depthBias` 为 0，并且 `projCoords.z` 与 `closestDepth` 来自同一张几何面时，比较结果会变得非常脆弱。

## 这次确认过的事实

| 事实 | 证据 |
|---|---|
| shadow map 输出存在 | debug dump 能导出 `shadow.cascade0` |
| Forward pass 确实读取 shadow map | shadow-only Forward 模式能显示阴影区域 |
| light camera 与 shadow matrix 曾经不一致 | 通过光源相机视角和 shadow dump 对比定位，并已在前序修复中收敛 |
| `shadowBias` 已可调 | CommandBus / Inspector 已支持 `shadowBias` |
| cull 改动不是最终修复 | `None` / `Back` 消除白边但引入 receiver acne |

## 下一步不要再靠 cull 解决

白边和星点是 shadow mapping 的两个相反压力：

| 压力 | 常见表现 | 不能只靠什么解决 |
|---|---|---|
| Peter-panning | 阴影和物体分离，接触处有白边 | 不能只靠把 cull 改成 `Back` |
| Shadow acne | 平面出现星点、条纹、闪烁 | 不能只靠把 bias 设为 0 |

接下来更合理的方向是把“投影”和“接收”拆开处理，而不是把所有材质的 Shadow pass cull 一刀切。当前代码已经有 `MaterialInstance::setPassEnabled(Pass_Shadow, false)` 这样的底层能力，后续可以把它发展成节点级 `castsShadow` 作者入口：平面作为 receiver 可以不写 shadow map，cube / character 作为 caster 继续写 shadow map。

这条路能避免 receiver 自阴影，但是否能同时解决 `Front` culling 的接触白边，还需要继续验证。若 caster 仍用 `Front`，厚度导致的 peter-panning 仍可能存在；如果只对 caster 使用更贴近前表面的写入策略，就必须同时给 receiver acne 一个明确解法，例如最小 normal bias、slope-scale bias 或 receiver 不参与 Shadow pass。

## 我们已经学会了什么

我们确认了这次白边不是 FrameGraph 读写断链，也不是 light matrix 仍然错位，而是 shadow map 记录表面的选择问题。`Front` 稳定但会记录背面，`Back` / `None` 更贴近接触但会让 receiver 自阴影失控。

## 下一步

继续排查时应保留 `Front` 作为稳定基线，再单独设计 caster / receiver 的作者语义和 bias 策略。相关实现入口：

- `assets/materials/blinnphong_*.material`
- `assets/shaders/glsl/blinnphong_0.frag`
- `assets/shaders/glsl/shadow_depth_only.vert`
- `src/core/asset/material_instance.cpp`
- `src/core/frame_graph/render_queue.cpp`
