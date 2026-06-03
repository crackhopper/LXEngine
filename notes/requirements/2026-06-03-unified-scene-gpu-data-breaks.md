# Unified Scene GPU Data Break Changes

This note records accepted break changes from the cleanup that made
`SceneResourceTable` the only authoritative scene GPU data contract.

## Removed APIs

| Removed API | Current replacement |
|---|---|
| `LX_core::offline::OfflineSceneIR` | `LX_core::SceneResourceTable` |
| `LX_core::offline::OfflineRayScene` | `SceneResourceTableUploadView` plus derived acceleration data |
| `LX_core::offline::OfflineRaySceneBuilder` | `SceneResourceTable::buildUploadView()` and `SceneSoftwareBvh::build(...)` |
| `LX_core::offline::OfflineBvhBuilder` | `LX_core::SceneSoftwareBvh` |
| `LX_infra::offline::OfflineSceneCompiler` | `LX_infra::offline::OfflineSceneLoader` |

## Accepted Naming Breaks

| Removed public name | Current name |
|---|---|
| `primary-ray` offline integrator | `software-compute` |

## Deferred Capabilities

- Hardware ray tracing pipeline creation remains an explicit future integrator.
  Recovery rule: add it by deriving BLAS/TLAS/SBT data from
  `SceneResourceTable`, not by adding another scene IR.
- Offline-only YAML fields that do not map to `SceneResourceTable` are
  unsupported. Recovery rule: add the missing record field or resource handle
  to `SceneResourceTable`.

## Preserved Capabilities

- Software BVH remains available through `SceneSoftwareBvh`.
- The offline `software-compute` renderer remains the MVP offline renderer.
- Realtime/offline comparison remains the output regression.
