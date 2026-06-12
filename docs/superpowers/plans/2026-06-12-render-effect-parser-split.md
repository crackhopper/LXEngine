# Render Effect Parser Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the transitional `RenderEffectResourceParser` into dedicated `RenderPathGraphResourceParser` and `RenderFeatureResourceParser` APIs.

**Architecture:** Keep the existing YAML parsing behavior, but expose two result types and two parser classes with single-schema responsibility. The graph parser owns `.render-path.yaml` / `lxe.render-path-graph.v1` and records `features.*.uri` dependencies on `RenderPathGraph`; the feature parser owns `.render-feature.yaml` / `lxe.render-feature.v1` and keeps `RenderFeature` as a pure envelope.

**Tech Stack:** C++20, yaml-cpp, CMake/Ninja integration tests.

---

### Task 1: Add Failing Parser Boundary Tests

**Files:**
- Modify: `src/test/integration/test_render_effect_resource_parser.cpp`
- Modify: `src/test/integration/test_technique_pass_contract.cpp`
- Modify: `src/test/integration/test_default_forward_render_path_graph_source.cpp`
- Modify only if still required by compile expectations: `src/test/integration/test_071g_legacy_boundary_removal.cpp`

- [ ] **Step 1: Write the failing test**

Add tests that instantiate `LX_infra::RenderFeatureResourceParser` for feature YAML and `LX_infra::RenderPathGraphResourceParser` for graph YAML. Add schema-boundary checks:

```cpp
LX_infra::RenderFeatureResourceParser featureParser;
const auto graphAsFeature = featureParser.parse("memory://graph", R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes: []
)");
EXPECT(!graphAsFeature.renderFeature.has_value(),
       "feature parser should not parse render path graph schema");

LX_infra::RenderPathGraphResourceParser graphParser;
const auto featureAsGraph = graphParser.parse("memory://feature", R"(
schema: lxe.render-feature.v1
name: ToneMapping
feature: toneMapping
)");
EXPECT(!featureAsGraph.renderPathGraph.has_value(),
       "graph parser should not parse render feature schema");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_render_effect_resource_parser`

Expected: compile failure because the new parser classes and result types do not exist.

### Task 2: Split Parser API And Implementation

**Files:**
- Modify: `src/infra/resource_parsers/render_effect_resource_parser.hpp`
- Modify: `src/infra/resource_parsers/render_effect_resource_parser.cpp`

- [ ] **Step 1: Write minimal implementation**

Replace the combined result with:

```cpp
struct ParsedRenderPathGraphResource final {
  std::optional<LX_core::RenderPathGraph> renderPathGraph;
  std::vector<std::string> diagnostics;
};

struct ParsedRenderFeatureResource final {
  std::optional<LX_core::RenderFeature> renderFeature;
  std::vector<std::string> diagnostics;
};
```

Add `RenderPathGraphResourceParser::parse()` and `RenderFeatureResourceParser::parse()`. Reuse the existing helper logic with separate result aliases so each parser accepts only its own schema and emits a schema diagnostic for the other schema.

- [ ] **Step 2: Run focused test**

Run: `cmake --build build --target test_render_effect_resource_parser && ./build/src/test/test_render_effect_resource_parser`

Expected: parser tests pass.

### Task 3: Update Production And Contract Call Sites

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/test/integration/test_technique_pass_contract.cpp`
- Modify: `src/test/integration/test_default_forward_render_path_graph_source.cpp`
- Modify only if required: `src/test/integration/test_071g_legacy_boundary_removal.cpp`

- [ ] **Step 1: Switch graph consumers**

Include `infra/resource_parsers/render_effect_resource_parser.hpp` and instantiate `RenderPathGraphResourceParser` wherever a render path graph is loaded or tested.

- [ ] **Step 2: Run call-site tests**

Run:

```bash
cmake --build build --target test_technique_pass_contract test_default_forward_render_path_graph_source
./build/src/test/test_technique_pass_contract
./build/src/test/test_default_forward_render_path_graph_source
```

Expected: all pass and no references to the removed combined parser remain in active graph-loading paths.

### Task 4: Verify No Combined Parser Remains

**Files:**
- Modify: none unless search exposes stale references.

- [ ] **Step 1: Search stale names**

Run: `rg -n "RenderEffectResourceParser|ParsedRenderEffectResource" src`

Expected: no results, or only intentionally retained historical text if the codebase still needs it.

- [ ] **Step 2: Run final focused verification**

Run:

```bash
cmake --build build --target test_render_effect_resource_parser test_technique_pass_contract test_default_forward_render_path_graph_source
./build/src/test/test_render_effect_resource_parser
./build/src/test/test_technique_pass_contract
./build/src/test/test_default_forward_render_path_graph_source
```

Expected: all pass.
