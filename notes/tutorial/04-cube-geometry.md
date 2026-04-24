# 04 · Cube Geometry

> 构造一个每面有独立法线、共 24 顶点 / 36 索引的立方体。`VertexPosNormalUvBone` 是项目里现成的顶点类型。

## 为什么是 24 顶点而不是 8

几何上立方体只有 8 个顶点，但光照需要**每个面独立法线**。如果 8 顶点共用，一个顶点被 3 个面共享，3 个方向的法线求平均会得到斜对角方向 —— 结果每一面都像个球面。

正确做法：$6 \times 4 = 24$ 顶点，每面 4 顶点共享同一法线。

## 顶点类型

用 `src/core/rhi/vertex_buffer.hpp:420` 定义的 `VertexPosNormalUvBone`：

```cpp
struct VertexPosNormalUvBone {
    Vec3f pos;        // location 0
    Vec3f normal;     // location 1
    Vec2f uv;         // location 2
    Vec4f tangent;    // location 3
    Vec4i boneIDs;    // location 4
    Vec4f boneWeights;// location 5
};
```

教程不需要 UV / tangent / bone，但顶点布局是 pipeline 身份的一部分，必须把**所有字段**填上。未用字段赋零即可。

## 构造函数

构造函数签名：

```cpp
VertexPosNormalUvBone(Vec3f p, Vec3f n, Vec2f u, Vec4f t, Vec4i bid, Vec4f bw);
```

为了减少噪声我们写一个小帮手：

```cpp
namespace {
VertexPosNormalUvBone v(float x, float y, float z,
                        float nx, float ny, float nz) {
    return VertexPosNormalUvBone(
        /* pos        */ {x, y, z},
        /* normal     */ {nx, ny, nz},
        /* uv         */ {0.0f, 0.0f},
        /* tangent    */ {1.0f, 0.0f, 0.0f, 1.0f},
        /* boneIDs    */ {0, 0, 0, 0},
        /* boneWeights*/ {0.0f, 0.0f, 0.0f, 0.0f});
}
}
```

## 立方体数据

以原点为中心、边长为 1 的立方体（$\pm 0.5$）：

```cpp
std::vector<VertexPosNormalUvBone> cubeVertices = {
    // +X face  (法线 +X)
    v( 0.5f, -0.5f, -0.5f,  1, 0, 0),
    v( 0.5f,  0.5f, -0.5f,  1, 0, 0),
    v( 0.5f,  0.5f,  0.5f,  1, 0, 0),
    v( 0.5f, -0.5f,  0.5f,  1, 0, 0),

    // -X face
    v(-0.5f, -0.5f,  0.5f, -1, 0, 0),
    v(-0.5f,  0.5f,  0.5f, -1, 0, 0),
    v(-0.5f,  0.5f, -0.5f, -1, 0, 0),
    v(-0.5f, -0.5f, -0.5f, -1, 0, 0),

    // +Y face (top)
    v(-0.5f,  0.5f, -0.5f,  0, 1, 0),
    v(-0.5f,  0.5f,  0.5f,  0, 1, 0),
    v( 0.5f,  0.5f,  0.5f,  0, 1, 0),
    v( 0.5f,  0.5f, -0.5f,  0, 1, 0),

    // -Y face (bottom)
    v(-0.5f, -0.5f,  0.5f,  0, -1, 0),
    v(-0.5f, -0.5f, -0.5f,  0, -1, 0),
    v( 0.5f, -0.5f, -0.5f,  0, -1, 0),
    v( 0.5f, -0.5f,  0.5f,  0, -1, 0),

    // +Z face (front)
    v(-0.5f, -0.5f,  0.5f,  0, 0, 1),
    v( 0.5f, -0.5f,  0.5f,  0, 0, 1),
    v( 0.5f,  0.5f,  0.5f,  0, 0, 1),
    v(-0.5f,  0.5f,  0.5f,  0, 0, 1),

    // -Z face (back)
    v( 0.5f, -0.5f, -0.5f,  0, 0, -1),
    v(-0.5f, -0.5f, -0.5f,  0, 0, -1),
    v(-0.5f,  0.5f, -0.5f,  0, 0, -1),
    v( 0.5f,  0.5f, -0.5f,  0, 0, -1),
};
```

## 索引

每面 2 个三角形 = 6 索引，共 36。顺序必须是**逆时针 = front face**（和项目已有三角形测试的 winding 保持一致，否则会被 back-face cull 吃掉 —— 见 `src/test/test_render_triangle.cpp` 的 winding 注释）。

每面 4 个顶点按 `(bottom-left, top-left, top-right, bottom-right)` 的顺序写进 vertex 数组 → 索引模式固定：

```cpp
std::vector<uint32_t> cubeIndices;
cubeIndices.reserve(36);
for (uint32_t face = 0; face < 6; ++face) {
    uint32_t base = face * 4;
    // 三角形 1: bl, tl, tr
    cubeIndices.push_back(base + 0);
    cubeIndices.push_back(base + 1);
    cubeIndices.push_back(base + 2);
    // 三角形 2: bl, tr, br
    cubeIndices.push_back(base + 0);
    cubeIndices.push_back(base + 2);
    cubeIndices.push_back(base + 3);
}
```

!!! warning "如果看不到立方体，先查 winding"
    项目默认开启 back-face culling。如果运行后立方体整个空白，把索引顺序试成 `(0, 2, 1, 0, 3, 2)` 再跑一次；若对了说明 winding 反了。也可以设置环境变量 `LX_RENDER_DISABLE_CULL=1` 临时绕开。

## 构造 Mesh

```cpp
auto vb = VertexBuffer<VertexPosNormalUvBone>::create(std::move(cubeVertices));
auto ib = IndexBuffer::create(std::move(cubeIndices));
auto mesh = Mesh::create(vb, ib);
```

`Mesh::create` 返回 `MeshSharedPtr`，可直接作为 `SceneNode` 的几何输入。

---

## 下一步

网格就绪。下一章写 `main()`，把 Scene / Camera / Light / Mesh / Material 串起来，并在主循环里旋转 model 矩阵。

→ [05-app-main.md](05-app-main.md)
