## 1. Forward Shader Contract Spec

- [x] 1.1 Finalize the `forward-shader-variant-contract` spec for supported variants, logical dependencies, baseColor composition, and strict shader-contract shrink behavior
- [x] 1.2 Review the `material-system`, `scene-node-validation`, and `shader-reflection` deltas against REQ-023 and align terminology with existing specs

## 2. Loader And Shader Implementation

- [x] 2.1 Update `blinn_phong_material_loader` to construct the forward variant subset, validate illegal combinations, and persist the validated set into shader compilation and `RenderPassEntry::shaderSet.variants`
- [x] 2.2 Update `blinnphong_0.vert` and `blinnphong_0.frag` so vertex inputs, `Bones` UBO usage, and lighting paths shrink strictly according to the enabled variants
- [x] 2.3 Apply the baseColor composition rules so unlit and lit paths both derive their source color from the same variant-controlled multiplier contract

## 3. Validation And Reflection

- [x] 3.1 Extend `SceneNode` structural validation to enforce forward-shader mesh and skeleton requirements derived from the active variant set
- [x] 3.2 Adjust shader reflection plumbing so reflected vertex input contracts match the actual variant-controlled GLSL declarations for each compiled shader

## 4. Verification

- [x] 4.1 Add non-GPU tests for loader fatal failures on illegal variant combinations such as `USE_NORMAL_MAP` without `USE_LIGHTING` or `USE_UV`
- [x] 4.2 Add non-GPU shader compile/reflection tests covering vertex color, UV, lighting, normal-map, and skinning contract shrink behavior
- [x] 4.3 Add core/infra validation tests covering `SceneNode` fatal failures for missing `inColor`, `inUV`, `inNormal`, `inTangent`, `inBoneIDs`, `inBoneWeights`, and missing `Skeleton/Bones`
