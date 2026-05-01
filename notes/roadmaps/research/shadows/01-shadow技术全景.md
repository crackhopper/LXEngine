# 01 · Shadow 技术全景

> 阅读前提：理解光线传播基本概念。

## 1.1 三种主流 shadow 技术

| 方案 | 提出年代 | 原理 | 现状 |
|------|---------|------|------|
| **Shadow Volumes** | 1977 (Crow) | 沿光方向延伸三角形构建影体，stencil buffer 累加测试 | Doom 3 用过，**已淘汰** |
| **Shadow Mapping** | 1978 | 从光源视角渲染 depth；shading 时投影回去比深度 | **当前工业标准** |
| **Raytraced Shadows** | 2018+（硬件加速） | per-pixel 发射光线测试遮挡 | 顶级硬件可用，未普及 |

## 1.2 Shadow Volumes 为什么淘汰

- 需要 *per-triangle* 影体几何，CPU + 顶点处理量爆炸
- Stencil buffer 累加是 fill-rate intensive
- 大场景几百万三角形 × 多光源 = 不可接受
- 同期 shadow mapping 做到了"差不多的画质 + 一个数量级的性能"

唯一保留下来的应用是 *点光源极小场景* 或 *艺术风格化游戏*（少数）。

## 1.3 Shadow Mapping 为什么仍是标准

```
1. 从光源视角渲染场景 → depth buffer (shadow map)
2. 主渲染时，把 fragment 投影到光源空间
3. 比较 fragment depth vs shadow map depth
4. fragment depth > shadow map depth → 在阴影里
```

**优势**：

- **跨硬件**：GLES 2 时代就支持，移动端到 4090 全跑
- **画质可调**：从 hard shadow 到 PCF / VSM / contact-hardening
- **性能可控**：分辨率、过滤、cascade 等都是显式 dial
- **跟 GPU-driven 范式无缝集成**（参见 [03 mesh shader shadow](03-mesh-shader-shadow.md)）

**劣势**：经典痛点（见 [§1.5](#15-shadow-mapping-的经典痛点)）。

## 1.4 Raytraced Shadows：未来但未现在

```
per pixel:
  for each light:
    ray = pixel_position → light_position
    if (ray hit anything between):
      shadow contribution = 0
    else:
      shadow contribution = full
```

**优势**：

- 物理正确（无 aliasing / acne / peter panning）
- soft shadow 自然产生（area light）
- 不需要预渲染 shadow map → 无内存爆炸

**劣势**：

- 需要 *硬件 RT core*（NVIDIA RTX、AMD RDNA 2+、Intel Arc Alchemist+）
- 移动 GPU 普遍不支持
- 即便有硬件，per-pixel × per-light × scene complexity 仍是性能 hotspot
- 需要专门的 *acceleration structure*（BVH），跟 mesh / meshlet 是 *并行的* 资产路径

**结论**：raytracing 是补充，不是替代。LX 路线图建议 *优先实现 shadow mapping*，raytraced 留 future。

## 1.5 Shadow Mapping 的经典痛点

| 问题 | 来源 | 解决思路 |
|------|------|---------|
| **Aliasing**（锯齿） | shadow map 分辨率有限，边缘像素离散化 | PCF（多采样平均） / 高分辨率 / cascade |
| **Shadow Acne**（黑色斑点） | depth 浮点精度，相邻 fragment 自我遮挡 | depth bias / slope-scale bias |
| **Peter Panning**（漂浮） | bias 过大让接触面有缝 | bias 调小 + normal offset |
| **Perspective Aliasing** | 远处 shadow map texel 比屏幕 pixel 大 | CSM（cascaded shadow maps） |
| **跨光源内存爆炸** | N 光源 × M² shadow map = GB 级 | sparse residency（[04](04-sparse-resources.md)） |
| **多光源性能** | 朴素方案每光源一个 pass | mesh shader + indirect（[03](03-mesh-shader-shadow.md)） |

## 1.6 LX 选择

| 决策 | 选什么 | 理由 |
|------|--------|------|
| 主路径 | Shadow Mapping | 跨硬件 + 画质可调 + 跟 mesh shader 协同 |
| 后续可加 | Raytraced (REQ Phase 1 后期) | 补充硬件可用时的"高保真" |
| 不做 | Shadow Volumes | 已淘汰，无投资价值 |

跟 LX 路线图 [REQ-103 / REQ-104 / REQ-109](../../main-roadmap/phase-1-rendering-depth.md) 一致。

## 1.7 Shadow 技术的 4 个维度

未来读其它阴影资料时，按这 4 个维度理解：

| 维度 | 选项 | 影响 |
|------|------|------|
| **类型** | hard / soft / contact-hardening | 画质 |
| **过滤** | nearest / PCF / VSM / EVSM / MSM | 画质 + 性能 |
| **空间划分** | single / cascaded / clustered | 大场景适应性 |
| **光源类型** | directional / point / spot / area | 数据结构（2D map / cubemap / spotlight matrix） |

不同组合产生不同方案。Ch8 选的是 *hard shadow + nearest sampling + per-light cubemap*（point light 路径）。

## 1.8 接下来读什么

- [02 Cubemap Shadow](02-cubemap-shadow.md) — point light 的 6 面 shadow 数据结构
