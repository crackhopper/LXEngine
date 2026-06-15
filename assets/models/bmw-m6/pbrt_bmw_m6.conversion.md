# PBRT BMW M6 Conversion Report

## Outputs

- Scene: `assets/models/bmw-m6/pbrt_bmw_m6.scene.yaml`
- Meshes: 114
- Runtime materials: 28
- PBRT source materials: 28

## Current Renderer Input

Use the generated scene file for current realtime/offline rendering. It references OBJ meshes, Material v2 PBRT surface materials, and the copied HDR environment.

Every generated runtime material declares `bsdf.source` explicitly under
`assets://shaders/glsl/common/materials/*.contract.glsl`. The source contract,
not a C++ material type fallback, is the structural authority for parameter
validation, storage layout, and shader accessor binding.

## Source-Preserving Input

Each scene material binding contains `offline.pbrtSourceMaterialUri`. Those YAML files preserve PBRT material types, parameters, resource references, and source line ranges.

## Precision Loss Today

- `CarPaint`: PBRT substrate layered diffuse/specular model retained in Material v2; realtime layered BRDF support may still be approximate
- `HeadLightGlass`: PBRT glass transmission/refraction retained in Material v2; realtime transparent dielectric support may still be approximate
- `HeadLightGlass01`: PBRT glass transmission/refraction retained in Material v2; realtime transparent dielectric support may still be approximate
- `LEATHER-white`: Fourier BSDF table retained in Material v2; realtime Fourier BSDF evaluation may still be unsupported
- `LEATHER`: PBRT mix material references retained in Material v2; realtime nested material mixing may still be unsupported
- `LightGlass`: PBRT glass transmission/refraction retained in Material v2; realtime transparent dielectric support may still be approximate
- `LogoSilver`: spectral eta/k retained as Material v2 spectrum resources; realtime spectral conductor support may still be approximate
- `WheelRim`: spectral eta/k retained as Material v2 spectrum resources; realtime spectral conductor support may still be approximate
- `WindscreenGlass`: PBRT glass transmission/refraction retained in Material v2; realtime transparent dielectric support may still be approximate
- `carbonfibre`: PBRT substrate layered diffuse/specular model retained in Material v2; realtime layered BRDF support may still be approximate
- `floor`: PBRT substrate layered diffuse/specular model retained in Material v2; realtime layered BRDF support may still be approximate
- `grillmetal`: spectral eta/k retained as Material v2 spectrum resources; realtime spectral conductor support may still be approximate
- `shinychrome`: spectral eta/k retained as Material v2 spectrum resources; realtime spectral conductor support may still be approximate

## Future Full-Fidelity Rendering Route

1. Add a renderer-side PBRT source material loader that consumes `lxe.pbrtMaterialSource.v1` YAML directly.
2. Implement spectral metal support using preserved `eta`/`k` SPD resources.
3. Implement PBRT glass/transmission and transparent shadow behavior for window and lamp meshes.
4. Implement Fourier BSDF loading for `bsdfs/leather.bsdf` instead of using the PBR fallback.
5. Implement substrate/clearcoat car paint, preserving separate diffuse/specular lobes and anisotropic roughness.
6. Add environment importance sampling for `textures/sky.exr` so offline path tracing converges on the PBRT lighting setup.
7. Switch `offline-pbrt-reference` from runtime approximation to source material consumption once those renderer modules exist.
