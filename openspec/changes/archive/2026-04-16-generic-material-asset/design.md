## Context

The material system now supports multi-buffer slots, pass-aware descriptor resources, and the ownership contract. But creating a new material type still requires writing a C++ loader that hardcodes defaults. A YAML-based material asset file can externalize defaults and metadata while keeping ownership and runtime interface governed by shader reflection.

Currently no YAML library is in the project. The texture loader (`TextureLoader`) loads from file paths using stb_image but has no placeholder/default textures.

## Goals / Non-Goals

**Goals:**
- YAML material asset format that works with any shader, not just blinnphong_0
- Generic loader: yaml → compile shader → reflect → build template → apply defaults → return MaterialInstance
- Placeholder textures (`white`, `black`, `normal`) for default resource references
- Per-pass variant declarations and parameter overrides in the yaml
- Validation: yaml parameter names must exist in shader reflection

**Non-Goals:**
- Replacing `loadBlinnPhongMaterial()` (it stays as a code-path option)
- Editor UI schema, display names, parameter grouping (authoring metadata is optional fields, not required for first version)
- Complex resource import graphs or sampler state customization
- Material graph authoring

## Decisions

### D1: yaml-cpp via FetchContent

**Choice**: `yaml-cpp` (well-maintained, C++17, header-only option available).
**Alternatives considered**:
- `rapidyaml`: faster but less mature API, less widespread
- Custom parser: unnecessary complexity for a well-defined format
- JSON: less human-friendly for authoring

Added via CMake FetchContent in `src/infra/CMakeLists.txt`.

### D2: Material asset file schema

```yaml
# example: materials/blinnphong_lit.mat.yaml
shader: blinnphong_0          # shader family name (used to find .vert/.frag)

variants:                      # global shader variants (apply to all passes)
  USE_UV: true
  USE_LIGHTING: true

passes:
  Forward:
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
    variants:                  # pass-level variant overrides (merged with global)
      USE_NORMAL_MAP: true
    parameters:                # pass-level parameter overrides
      MaterialUBO.enableNormal: 1
    resources:
      normalMap: "textures/brick_normal.png"
  Shadow:
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true

parameters:                    # global defaults (all passes)
  MaterialUBO.baseColor: [0.8, 0.8, 0.8]
  MaterialUBO.shininess: 12.0
  MaterialUBO.specularIntensity: 1.0
  MaterialUBO.enableAlbedo: 1
  MaterialUBO.enableNormal: 0

resources:                     # global default resources
  albedoMap: "textures/brick_albedo.png"
  normalMap: normal            # built-in placeholder
```

Key rules:
- `shader` is required
- `parameters` uses `bindingName.memberName` format (aligns with REQ-032 R5)
- `resources` maps binding name → file path or placeholder name
- `passes` is optional; if omitted, a single Forward pass is assumed
- `variants` at top level are global; per-pass variants merge on top
- `renderState` fields map directly to `RenderState` struct members

### D3: Generic loader flow

```
loadGenericMaterial(yamlPath) → MaterialInstanceSharedPtr
  1. Parse YAML
  2. Resolve shader path from shader name
  3. For each pass: merge global + per-pass variants
  4. Compile shader with variants per pass
  5. Reflect bindings and vertex inputs
  6. Build MaterialTemplate with pass entries
  7. Create MaterialInstance
  8. Apply global default parameters (setParameter or setFloat etc.)
  9. Apply per-pass parameter overrides
  10. Load and bind default resources (textures)
  11. Return instance
```

Step 9 (per-pass parameter overrides): Since `setParameter` writes to shared buffer slots and buffer slots are shared across passes (REQ-032 validates cross-pass consistency), per-pass parameter overrides in first version simply overwrite the global default. True per-pass-divergent buffers would require separate slots, which is out of scope.

### D4: Placeholder textures

A `PlaceholderTextures` utility provides:
- `getWhite()` → 1x1 RGBA (255,255,255,255)
- `getBlack()` → 1x1 RGBA (0,0,0,255)
- `getNormal()` → 1x1 RGBA (128,128,255,255) — flat normal in tangent space

These are lazily created `CombinedTextureSamplerSharedPtr` singletons. The generic loader resolves placeholder names before falling back to file path loading.

### D5: Validation

- Parameter binding names in yaml must exist in reflection. If not → FATAL.
- Resource binding names in yaml must exist in reflection. If not → FATAL.
- Unknown yaml keys are ignored (forward-compat for future authoring metadata).
- Shader compilation failure → FATAL with diagnostic.

## Risks / Trade-offs

- **[Risk]** yaml-cpp adds a dependency.
  → Mitigation: FetchContent, no system install required. Well-maintained library.

- **[Risk]** Per-pass parameter overrides in first version just overwrite shared buffer, not truly per-pass.
  → Mitigation: Documented limitation. True per-pass divergent buffers need separate slots (future work).

- **[Risk]** No sampler state in first version — all textures use engine default sampler.
  → Mitigation: Acceptable for first version per REQ-033 R6.
