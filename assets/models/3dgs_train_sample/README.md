# 3DGS Train Sample

## Asset

| Field | Value |
|---|---|
| Name | Voxel51 Gaussian Splatting train scene, iteration 7000 |
| File | `point_cloud.ply` |
| Size | 183,988,515 bytes |
| Vertex count | 741,883 Gaussian splats |
| Format | Binary little-endian PLY with 3DGS properties |
| Source URL | https://huggingface.co/datasets/Voxel51/gaussian_splatting |
| Source file | `FO_dataset/train/point_cloud/iteration_7000/point_cloud.ply` |
| License | Apache-2.0, as declared by the Hugging Face dataset page |
| SHA-256 | `9f5c643fa18cb5ed6a11441f073133ed1034e3b0656bce5b7b5db3db49161366` |

## Purpose

This asset is the repository's representative 3D Gaussian Splatting PLY
fixture. It is intentionally larger than the existing mesh test assets because
3DGS source PLY files store per-splat position, normal placeholder, spherical
harmonic coefficients, opacity, anisotropic scale, and quaternion rotation at
full precision.

The current renderer does not render this file yet. The active 3DGS
requirements use it as the acceptance asset for loader, resource, Vulkan render
pass, editor scene, and tutorial work.

## Header Shape

The file uses the GraphDeco-style 3DGS property layout:

```text
format binary_little_endian 1.0
element vertex 741883
property float x
property float y
property float z
property float nx
property float ny
property float nz
property float f_dc_0
property float f_dc_1
property float f_dc_2
property float f_rest_0 ... f_rest_44
property float opacity
property float scale_0
property float scale_1
property float scale_2
property float rot_0
property float rot_1
property float rot_2
property float rot_3
```

`nx`, `ny`, and `nz` are retained from the source file for compatibility with
the original data. Runtime 3DGS rendering should treat the Gaussian covariance
as coming from `scale_*` and `rot_*`, not from mesh normals.
