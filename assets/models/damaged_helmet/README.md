# DamagedHelmet

PBR 主测试模型，用于验证 metallic-roughness 工作流、法线贴图、AO 和自发光。

## 来源

- **URL**: https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet/glTF
- **License**: Creative Commons Attribution-NonCommercial 4.0 (CC BY-NC 4.0)
- **原始作者**: theblueturtle_ (Sketchfab), glTF conversion by ctxwing

## 用途

- REQ-011: glTF + PBR 元数据加载测试输入
- REQ-019: Demo 场景 PBR 资产

## 文件列表

| 文件 | 大小 |
|---|---:|
| DamagedHelmet.gltf | 4.5 KB |
| DamagedHelmet.bin | 546 KB |
| Default_albedo.jpg | 914 KB |
| Default_AO.jpg | 354 KB |
| Default_emissive.jpg | 95 KB |
| Default_metalRoughness.jpg | 1.3 MB |
| Default_normal.jpg | 506 KB |
| reference/khronos_screenshot.png | 28 KB |

**总计**: ~3.7 MB

## 几何信息

- 1 mesh, 15,452 triangles
- 5 张纹理贴图 (albedo, AO, emissive, metalRoughness, normal)

## Reference 渲染图

`reference/khronos_screenshot.png` 是 Khronos Damaged Helmet README 中的官方
reference 渲染图，用于和 LXEngine 的当前渲染结果做视觉诊断对比。它随模型放置，
不是运行时加载资产。
