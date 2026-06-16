# Khronos Neutral Environment

用于对齐 Khronos glTF Sample Viewer 的 neutral 环境光资源。

## 来源

- **Repository**: https://github.com/KhronosGroup/glTF-Sample-Environments
- **Asset path**: `neutral/ggx/specular.ktx2`
- **Raw LFS URL**: https://media.githubusercontent.com/media/KhronosGroup/glTF-Sample-Environments/main/neutral/ggx/specular.ktx2
- **SHA-256**: `f4766016d86d33019dbe56b42e93d5de4f8926f6724771b8a6c7218db35539c5`
- **License note**: GitHub API reports `license: null` for the source repository, and the repository has no top-level `LICENSE` file at the time this asset was imported. Treat this as a reference asset whose redistribution status must be checked before external release packaging.

The upstream README states that these environments are used for image-based
lighting in the official glTF 2.0 Sample Viewer and were prefiltered with
Khronos glTF IBL Sampler.

## Files

| File | Role | Format |
|---|---|---|
| `ggx/specular.ktx2` | GGX prefiltered specular environment cubemap | KTX2, 1024x1024, 6 faces, 11 mips, `VK_FORMAT_R16G16B16A16_SFLOAT` |

## LXEngine Support

Current support is intentionally narrow:

- `TextureLoader::loadKtx2Cubemap()` accepts uncompressed KTX2 cubemaps with
  `VK_FORMAT_R16G16B16A16_SFLOAT`.
- BasisU, supercompressed KTX2, texture arrays, 3D textures, and generic KTX2
  material textures are not supported by this path.
- The asset is prepared for `REQ-073-f` skybox/environment work and later
  `REQ-073-h` IBL lighting. It is not yet wired into scene loading by default.
