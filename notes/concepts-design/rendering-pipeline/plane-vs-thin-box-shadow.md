# Plane 与薄盒：为什么阴影接收面需要厚度

Shadow map 像一张从灯光方向拍摄的深度底片。现实里的地板不是一张没有厚度的纸，而是一块有上表面、下表面和侧边的板。当前 LXEngine 的 `primitive:plane` 也按这个心智模型处理：它在视觉上仍是 1x1 的平面，但网格结构是一块很薄的 box。

## 单面 plane 在 shadow pass 里缺少背面语义

单面 plane 只有一组朝上的三角形。它适合做简单可视化，但不适合同时参与“投影”和“接收”这两件事。

| 几何 | 光源看到什么 | Shadow pass 的问题 |
|---|---|---|
| 单面 plane | 只有一个没有厚度的表面 | cull mode 一变，它可能完全不写 depth，或写入和接收面几乎相同的 depth |
| 薄 box | 上表面、下表面和侧边都是明确三角形 | depth map 看到的是封闭体的一部分，front/back face culling 有稳定含义 |

当前 Blinn-Phong 的 Shadow pass 使用 `Front` culling。这个设置依赖一个隐含假设：参与 shadow map 的物体最好是封闭体。封闭体有“正面”和“背面”的几何厚度；单面 plane 没有这个厚度，所以它在不同 cull 策略下会表现得很极端。

当前 `Mesh` 会用 `isClosedVolume()` 标记这个拓扑语义。薄 box plane 是封闭体；`patch:triangle`、`patch:square`、`patch:circle` 是纯面片，标记为非封闭。

## 白边和星点来自两种相反压力

阴影调试里常见的两个问题方向相反：

| 问题 | 产生方式 | 表现 |
|---|---|---|
| Peter-panning | shadow map 写入的 caster depth 比真实接触面更靠后 | 物体接触处出现亮边，阴影像被推开 |
| Shadow acne | receiver 自己写入 shadow map，又在 Forward pass 读取自己 | 接收面出现星点、条纹或随相机闪烁 |

把 Shadow pass 的 cull mode 从 `Front` 改成 `Back` 或 `None`，可以让 caster 的阴影更贴近接触面。但如果接收面仍是同一个单面 plane，它也会开始把自己的正面 depth 写进 shadow map。Forward pass 再拿同一个表面做深度比较时，微小浮点误差就会让比较结果一会儿通过、一会儿失败，于是出现星点和闪烁。

薄 box 不能神奇消除所有 bias 问题，但它让接收面不再是“零厚度特例”。当我们使用 front/back culling、normal bias 或未来的 caster/receiver 开关时，薄 box 有明确的几何侧面和背面可以参与判断。

## 当前 primitive plane 的约定

`builtin://lxe_editor/primitives/plane` 保持这些作者可见语义：

| 属性 | 当前约定 |
|---|---|
| XZ footprint | `[-0.5, 0.5] x [-0.5, 0.5]` |
| 上表面 | local `y = 0` |
| 厚度方向 | 向下延伸 |
| 下表面 | local `y = -0.02` |
| 网格结构 | 6 个面、24 个 per-face 顶点、36 个 index |

这意味着已有场景把 plane 放在 `translation.y = 0` 时，上表面仍然在世界 `y = 0`。视觉上它仍像一张地面平面，但 shadow pass 看到的是一块薄板。

```yaml
mesh:
  uri: builtin://lxe_editor/primitives/plane  # -> buildPlaneMesh(): thin box
transform:
  translation: [0.0, 0.0, 0.0]               # -> top surface stays at y=0
```

## 纯面片是另一类对象

我们仍然保留非封闭几何。Toolbar 中的 `Patches` 行提供：

| Patch | Mesh URI | Shadow 语义 |
|---|---|---|
| Triangle | `builtin://lxe_editor/patches/triangle` | 非封闭，只走 Forward，不进入 Shadow pass |
| Square | `builtin://lxe_editor/patches/square` | 非封闭，只走 Forward，不进入 Shadow pass |
| Circle | `builtin://lxe_editor/patches/circle` | 非封闭，只走 Forward，不进入 Shadow pass |

这些 patch 可以显示、可以接收其他封闭 caster 的阴影，但默认不 cast shadow。原因正是这次阴影问题暴露出来的事实：单面面片既作为 caster 又作为 receiver 时，最容易让同一张面在 shadow map 和 Forward pass 中互相比较，产生 acne 和闪烁。

## 为什么不是简单调 bias

Bias 是深度比较里的容差，不是几何补洞工具。当前 Forward shader 会把世界单位的 `shadowBias` 按 cascade depth range 转成 shadow-map depth 空间：

```glsl
float depthBias = (worldBias + slopeBias) / cascadeDepthRange;
visibility += (projCoords.z - depthBias) <= closestDepth ? 1.0 : 0.0;
```

Bias 太小，receiver 自阴影容易出现；bias 太大，阴影会脱离物体。薄 box 的作用不是替代 bias，而是避免单面 plane 在 shadow pass 里成为零厚度的特殊几何，让后续 bias、culling 和 caster/receiver 语义都有更稳定的基础。

## 我们已经学会了什么

我们把 `primitive:plane` 从“单面纸片”改成“薄板”。它保持作者看到的地面高度和 footprint，同时让 shadow map 看到封闭几何。这样做不是最终的阴影技术解法，但它去掉了最脆弱的模型输入：零厚度接收面。

## 下一步

继续阅读：

- [Shadow Pass：只写深度的光源视角](shadow-pass.md)
- [CSM：把方向光阴影分成四段](cascaded-shadow-maps.md)
- [Shadow culling、接触白边与星点闪烁复盘](../../debug/2026-05-shadow-cull-peter-panning.md)
