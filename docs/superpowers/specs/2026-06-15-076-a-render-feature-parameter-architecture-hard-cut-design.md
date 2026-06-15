# 076-a RenderFeature Parameter Architecture Hard Cut Design

Date: 2026-06-15

## Goal

Close the architecture gap where shader UBO members are satisfied by C++ manual
binding writes instead of by RenderFeature parameters. This implements
`REQ-076-a`.

## Problem

`PostProcessUBO.gamma` exists in shader reflection and C++ static binding
metadata. Runtime writes it through `createStandardPostProcessMaterial()`.
That satisfies descriptor completeness but does not prove
`feature.toneMapping.gamma` was consumed.

The same failure mode can affect skybox, probe bake and IBL lighting.

## Decision

RenderFeature parameter schema gains binding/member metadata:

```yaml
parameters:
  gamma:
    kind: float
    value: 2.2
    binding: PostProcessUBO
    member: gamma
    required: true
```

RenderWorkCompiler preparation validates:

```text
RenderPathGraph feature dependency
  -> live RenderFeature payload
  -> shader reflection binding/member
  -> RenderInputDesc binding plan
  -> backend descriptor write
```

Runtime-derived values must also be explicit. For PostProcess, target format
derives `outputEncodingMode`; it must not be hidden by changing `gamma`.

## Hard Cuts

- Default path cannot rely on manual `MaterialInstance` creation to carry
  feature UBOs.
- C++ hardcoded effect parameters are rejected except for explicitly declared
  runtime-derived schema fields.
- Placeholder feature payloads cannot satisfy graph dependencies.

## Acceptance

- feature/schema/reflection mismatch rejects the input;
- PostProcess has separate `gamma` and `outputEncodingMode`;
- skybox and IBL feature parameters use the same validation path;
- rg audit proves manual PostProcess/Skybox feature binding is not the default
  positive path.
