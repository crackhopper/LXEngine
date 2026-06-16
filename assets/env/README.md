# Studio Small 03 (2K HDR)

用于 IBL 环境光照的 HDRI 环境贴图。

## 来源

- **URL**: https://polyhaven.com/a/studio_small_03
- **License**: CC0 1.0 Universal (Public Domain)
- **来源平台**: Poly Haven

## 用途

- REQ-028: IBL 环境贴图输入
- 环境光照和反射测试

## 文件列表

| 文件 | 大小 |
|---|---:|
| studio_small_03_2k.hdr | 6.4 MB |
| khronos/neutral/ggx/specular.ktx2 | 64 MB |

**总计**: 70.4 MB

## 规格

- 分辨率: 2K
- 格式: Radiance HDR (.hdr)

## Khronos Neutral

`khronos/neutral/` 保存 Khronos glTF Sample Viewer 的 neutral IBL 参考环境。
详见 `khronos/neutral/README.md`。当前只接入了 GGX specular KTX2 cubemap，
用于后续环境贴图 skybox 和 IBL lighting 工作。
