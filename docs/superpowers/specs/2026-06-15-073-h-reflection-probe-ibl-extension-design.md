# 073-h Reflection Probe IBL Extension Design

Date: 2026-06-16

## Goal

Add reflection probes as a local extension of the environment IBL bake pipeline
created by `REQ-073-g`.

## Decision

Probe support is not part of the first environment HDR bake slice. A probe node
captures a temporary EnvMap from the scene, then reuses the 073g bake pipeline:

```text
ReflectionProbeComponent
  -> graph-authored cubemap capture
  -> temporary probe EnvMap
  -> 073g SH / prefiltered cubemap / BRDF LUT bake path
  -> probe bake manifest + payloads
  -> SceneResourceTable live probe resources
```

The shader formula stays in `common/ibl_lighting.glsl`; probe work extends
resource selection, not lighting math.

## Boundaries

- No DDGI, lightmap, box projection, or dynamic every-frame probe updates.
- Missing probe cache does not implicitly capture during scene load.
- Probe component is not a CameraComponent and is not visible through ordinary
  view camera enumeration.

## Acceptance

- strict scene parser coverage for `reflectionProbe`;
- graph parser and compiler coverage for cubemap face capture;
- strict probe manifest tests;
- tiny Vulkan probe bake smoke;
- live SceneResourceTable probe payloads, not metadata-only records.
