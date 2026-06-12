# Material v3 Source Contract Design

## Goal

`REQ-073-a` hard-cuts Material v3 from C++ per-type material schemas to source-reflected material contracts.

Every Material v3 `.material` declares:

```yaml
bsdf:
  type: matte
  source: assets/shaders/glsl/common/materials/matte.contract.glsl
  parameters: ...
```

`bsdf.parameters` remains the only runtime material truth. `bsdf.source` points to a material contract source that defines the parameter schema, texture rules, material storage ABI, shader variant identity, and stable accessor interface.

## Architecture

The material contract source is the structural authority for renderable material data. It is not a full render pass shader. It is a reflectable source asset that declares:

- declared material type;
- support status and capabilities;
- parameter schema, defaults, allowed envelope kinds, and required fields;
- texture-capable parameters, packed channel selectors, and default texture semantics;
- material storage / SSBO record layout;
- shader variant identity and include path;
- the Material Accessor ABI entry point and return structure.

C++ owns only generic contract machinery:

- load and register `bsdf.source` as a typed resource dependency;
- reflect the material contract source;
- validate `bsdf.parameters` against the reflected contract;
- build `MaterialSignature` from source URI, reflection hash, storage ABI, accessor ABI, pass shader, render state, and target facts;
- pack material instances into source-local storage using reflected layout;
- report diagnostics and invariant violations.

C++ does not define `matte`, `metal`, `uber`, or `substrate` schema/packing classes. Existing `MaterialSurfaceSchema` is removed from the Material v3 positive path.

## Material Accessor ABI

Forward, Deferred, and OfflineRT shaders consume only one stable material accessor interface. They do not branch on material type or source.

Minimum semantic interface:

```glsl
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex,
                                        vec2 uv,
                                        vec3 geometricNormal,
                                        mat3 tangentFrame);
```

`LxMaterialSurface` provides at least:

- `baseColor`
- `alpha`
- `metallic`
- `roughness`
- `normal`
- `ao`
- `emissive`

Each contract source can implement different internal storage and BSDF approximation, but it must expose this stable accessor ABI. Reflection and shader compilation fail if the accessor, return fields, or required bindings are missing.

## Data Flow

1. The `.material` parser requires `bsdf.type`, `bsdf.source`, and `bsdf.parameters`.
2. The parser resolves `bsdf.source` and loads it as a material contract dependency.
3. Contract reflection returns schema, storage layout, accessor ABI, and source signature.
4. The parser validates every parameter field. Unknown fields, missing required fields, kind mismatch, illegal channel selectors, source/type mismatch, unsupported sources, and failed reflection are fatal load errors.
5. `MaterialInstance` stores the source URI, source reflection hash/signature, original envelopes, typed dependencies, and authoring metadata. It does not store workflow or a per-type C++ PBR view.
6. Upload groups materials by source signature and creates source-local material storage. The generic packer writes factors, texture slots, channel selectors, and default texture slots according to reflected layout.
7. RenderWorkQueue expands render path passes by material source variant. A base pass shader can compile as `Forward/pbr + matte.contract.glsl` and `Forward/pbr + metal.contract.glsl`.
8. Final shader reflection and pipeline validation use the variant shader after the contract source has been included.

## Pipeline Identity

`MaterialSignature` includes structural facts:

- source URI;
- source reflection hash;
- material storage ABI hash;
- Material Accessor ABI identity;
- shader variant include/define;
- pass shader;
- render state;
- target signature.

It excludes instance facts:

- parameter values;
- texture presence;
- texture id / slot;
- packed channel selection;
- material URI, handle, or name;
- source workflow such as glTF vs PBRT.

Same source signature means one material record layout. If the engine observes two layouts for the same source signature, that is an internal invariant violation. The implementation must stop with diagnostics; it must not create a second layout or fall back to the old shared record.

## Error Handling

Load-time failures:

- missing `bsdf.type` or `bsdf.source`;
- source URI cannot resolve;
- source is not a material contract;
- source reflection fails;
- source declared type differs from `bsdf.type`;
- source declares unsupported;
- parameter is unknown, missing, wrong kind, or has illegal texture/channel metadata;
- source does not implement the Material Accessor ABI.

Upload-time failures:

- referenced resource has no real typed payload;
- default texture cannot be registered or assigned a stable slot;
- packer cannot write a field required by reflected layout;
- a draw tries to use a source-local material index with the wrong source storage.

Compile/queue failures:

- render pass does not support a source capability;
- variant shader does not include the selected source;
- final shader reflection disagrees with contract storage ABI;
- positive path still includes old shared `lxSceneMaterialRecord` or old `MaterialUBO` material truth;
- `PipelineKey` changes because of parameter values, texture presence, texture ids, material URI, or source workflow.

Diagnostics include material URI, `bsdf.type`, `bsdf.source`, parameter path, source reflection hash, pass name, and render path name when available.

## Downstream Impact

`REQ-073-b` validates source-reflected material storage through bindless tables and indirect draw. It should speak in terms of source-local storage, source signature, material contract variant, and accessor ABI rather than C++ material type records.

`REQ-074` package work persists source URI, source reflection hash, material envelopes, dependencies, and source-local material state. It does not persist GPU handles or bindless slots.

`REQ-075` equivalence compares realtime and OfflineRT output under the same source-reflected contract and accessor ABI.

## Testing Strategy

Negative tests:

- missing `bsdf.source`;
- invalid or non-contract source;
- source/type mismatch;
- unsupported source;
- bad parameter name, missing required parameter, wrong envelope kind, illegal channel selector;
- source missing Material Accessor ABI;
- positive path still using `MaterialSurfaceSchema`, old shared material record, old `MaterialUBO`, or source workflow pipeline splits.

Positive tests:

- `matte`, `metal`, `uber`, and `substrate` contract sources reflect successfully;
- constant-only and factor-plus-texture materials share source signature, record layout, and pipeline key;
- metallic/roughness/AO packed channel selectors are validated and packed;
- missing textures use global default texture slots;
- parameter values, texture ids, texture presence, material URI, and source workflow do not affect `MaterialSignature`;
- Forward, Deferred, and OfflineRT call only the Material Accessor ABI.

Invariant tests:

- fake conflicting layouts for one source signature report engine invariant violation;
- source-local material indices cannot be used with a different source storage.
