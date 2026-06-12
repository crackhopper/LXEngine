# Material Source Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `REQ-073-a` by making every Material v3 asset declare `bsdf.source`, reflecting that source as the material contract, validating PBRT-style parameters against it, and removing C++ per-type schema as the positive path.

**Architecture:** `.material` keeps `bsdf.parameters` as the only material truth. `bsdf.source` points to a reflectable material contract source that defines schema, storage ABI, source signature, and the Material Accessor ABI. C++ provides generic reflection, parser validation, source-local signatures, and packing infrastructure only.

**Tech Stack:** C++20, YAML-CPP, LXEngine `SceneResourceTable`, `MaterialResourceParser`, shader compiler/reflection, GLSL common includes, Ninja/CMake integration tests.

---

## File Structure

- Create `src/core/asset/material_contract.hpp`: generic data model for reflected material contracts, parameter rules, texture rules, accessor ABI, storage layout, and diagnostics.
- Create `src/core/asset/material_contract.cpp`: signature composition, invariant checks, parameter lookup helpers.
- Create `src/infra/material_loader/material_contract_reflector.hpp`: reflection API used by material parser and tests.
- Create `src/infra/material_loader/material_contract_reflector.cpp`: first implementation parses a strict YAML-like metadata block embedded in contract source comments, then stores GLSL source URI/hash for later shader compiler integration.
- Modify `src/core/asset/material_instance.hpp/.cpp`: store `bsdf.source`, source reflection hash/signature, and reflected contract reference data; do not add workflow or per-type view.
- Modify `src/infra/material_loader/material_resource_parser.cpp`: require `bsdf.source`, load/reflect contract, validate parameters from reflection, remove positive dependency on `findMaterialSurfaceSchema()`.
- Modify `src/infra/scene_asset/gltf_scene_asset_loader.cpp`: emit explicit `bsdf.source` for generated glTF material instances and stop consulting `MaterialSurfaceSchema`.
- Create `assets/shaders/glsl/common/material_surface.glsl`: stable `LxMaterialSurface` and accessor ABI declarations.
- Create `assets/shaders/glsl/common/materials/matte.contract.glsl`, `metal.contract.glsl`, `uber.contract.glsl`, `substrate.contract.glsl`: initial supported material contract sources.
- Modify Forward / Deferred / OfflineRT PBR shaders: include `material_surface.glsl` and contract source through variant path, then call `lxLoadMaterialSurface`.
- Modify `src/core/scene/scene_gpu_records.*` and `src/core/scene/scene_resource_table*`: add source-local material storage metadata and keep old shared `SceneGpuMaterialRecord` only behind named legacy rejection until `073-b` hard cut.
- Modify `src/test/integration/test_material_v2_parser.cpp`: convert old schema tests into source contract parser tests and legacy rejection tests.
- Add `src/test/integration/test_material_source_contract.cpp`: focused tests for reflection, parser validation, signature identity, invariant checks, and accessor ABI.
- Update `src/test/CMakeLists.txt`: build the new test and remove positive tests that rely on `MaterialSurfaceSchema`.

## Scope Check

This plan implements `REQ-073-a` only. It prepares source-local storage and shader accessor ABI, but full bindless/indirect enforcement, shader directory migration, package serialization, and offline/realtime equivalence remain in `REQ-073-b`, `REQ-074-*`, and `REQ-075-a`.

### Task 1: Add Source Contract Reflection Model

**Files:**
- Create: `src/core/asset/material_contract.hpp`
- Create: `src/core/asset/material_contract.cpp`
- Modify: `src/infra/CMakeLists.txt`
- Test: `src/test/integration/test_material_source_contract.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Write the failing model/signature test**

Create `src/test/integration/test_material_source_contract.cpp` with this initial content:

```cpp
#include "core/asset/material_contract.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

void testSourceSignatureIgnoresInstanceValues() {
  LX_core::MaterialContractReflection a;
  a.sourceUri = LX_core::ResourceUri("assets/shaders/glsl/common/materials/matte.contract.glsl");
  a.declaredType = "matte";
  a.reflectionHash = "hash-a";
  a.storageAbiHash = "storage-a";
  a.accessorAbiHash = "accessor-v1";

  LX_core::MaterialContractReflection b = a;

  EXPECT(a.sourceSignature() == b.sourceSignature(),
         "same reflected source should produce same source signature");
  EXPECT(a.materialSignature(LX_core::StringID("ForwardPbr"),
                             LX_core::StringID("RenderStateOpaque")) ==
             b.materialSignature(LX_core::StringID("ForwardPbr"),
                                 LX_core::StringID("RenderStateOpaque")),
         "same reflected source should produce same material signature");

  b.reflectionHash = "hash-b";
  EXPECT(a.sourceSignature() != b.sourceSignature(),
         "reflection hash must participate in source signature");
}

} // namespace

int main() {
  testSourceSignatureIgnoresInstanceValues();
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

- [ ] **Step 2: Add the test target and run it to verify it fails**

Modify `src/test/CMakeLists.txt` by adding:

```cmake
add_executable(test_material_source_contract
    integration/test_material_source_contract.cpp
)
target_link_libraries(test_material_source_contract PRIVATE LXEngineCore)
add_test(NAME test_material_source_contract COMMAND test_material_source_contract)
set_tests_properties(test_material_source_contract PROPERTIES LABELS auto)
```

Run:

```bash
cmake --build build --target test_material_source_contract
```

Expected: compile failure because `core/asset/material_contract.hpp` does not exist.

- [ ] **Step 3: Implement the minimal contract model**

Create `src/core/asset/material_contract.hpp`:

```cpp
#pragma once

#include "core/resource/resource_uri.hpp"
#include "core/utils/string_table.hpp"

#include <optional>
#include <string>
#include <vector>

namespace LX_core {

enum class MaterialContractSupportStatus {
  Supported,
  Unsupported,
};

enum class MaterialContractParameterKind {
  Float,
  Rgb,
  Spectrum,
  Texture,
  Integer,
  Bool,
  String,
  MaterialRef,
  BsdfTable,
};

struct MaterialContractParameter final {
  std::string name;
  bool required = false;
  std::vector<MaterialContractParameterKind> allowedKinds;
};

struct MaterialContractAccessorAbi final {
  std::string entryPoint = "lxLoadMaterialSurface";
  std::vector<std::string> requiredFields{
      "baseColor", "alpha", "metallic", "roughness", "normal", "ao",
      "emissive"};
};

struct MaterialContractReflection final {
  ResourceUri sourceUri;
  std::string declaredType;
  MaterialContractSupportStatus supportStatus =
      MaterialContractSupportStatus::Supported;
  std::string reflectionHash;
  std::string storageAbiHash;
  std::string accessorAbiHash;
  MaterialContractAccessorAbi accessorAbi;
  std::vector<MaterialContractParameter> parameters;

  [[nodiscard]] std::optional<std::reference_wrapper<const MaterialContractParameter>>
  findParameter(std::string_view name) const;
  [[nodiscard]] StringID sourceSignature() const;
  [[nodiscard]] StringID materialSignature(StringID passShaderSignature,
                                           StringID renderStateSignature) const;
};

} // namespace LX_core
```

Create `src/core/asset/material_contract.cpp`:

```cpp
#include "core/asset/material_contract.hpp"

namespace LX_core {

std::optional<std::reference_wrapper<const MaterialContractParameter>>
MaterialContractReflection::findParameter(std::string_view name) const {
  for (const MaterialContractParameter &parameter : parameters) {
    if (parameter.name == name) {
      return std::cref(parameter);
    }
  }
  return std::nullopt;
}

StringID MaterialContractReflection::sourceSignature() const {
  StringID fields[] = {
      StringID(sourceUri.string()),
      StringID(declaredType),
      StringID(reflectionHash),
      StringID(storageAbiHash),
      StringID(accessorAbiHash),
  };
  return GlobalStringTable::get().compose(TypeTag::MaterialRender, fields);
}

StringID MaterialContractReflection::materialSignature(
    StringID passShaderSignature, StringID renderStateSignature) const {
  StringID fields[] = {
      sourceSignature(),
      passShaderSignature,
      renderStateSignature,
  };
  return GlobalStringTable::get().compose(TypeTag::MaterialRender, fields);
}

} // namespace LX_core
```

Modify `src/infra/CMakeLists.txt` or the core library source list that owns `src/core/asset/*.cpp` to include:

```cmake
${PROJECT_SOURCE_DIR}/src/core/asset/material_contract.cpp
```

- [ ] **Step 4: Run the model test**

Run:

```bash
cmake --build build --target test_material_source_contract
./build/src/test/test_material_source_contract
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/asset/material_contract.hpp src/core/asset/material_contract.cpp src/infra/CMakeLists.txt src/test/CMakeLists.txt src/test/integration/test_material_source_contract.cpp
git commit -m "Add material source contract model"
```

### Task 2: Reflect Contract Source Metadata

**Files:**
- Create: `src/infra/material_loader/material_contract_reflector.hpp`
- Create: `src/infra/material_loader/material_contract_reflector.cpp`
- Modify: `src/infra/CMakeLists.txt`
- Test: `src/test/integration/test_material_source_contract.cpp`

- [ ] **Step 1: Add failing reflection tests**

Append to `src/test/integration/test_material_source_contract.cpp`:

```cpp
#include "infra/material_loader/material_contract_reflector.hpp"

void testReflectsContractMetadataBlock() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture spectrum
// parameter: metallic optional float texture
// parameter: roughness optional float texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/matte.contract.glsl"), source);

  EXPECT(result.diagnostics.empty(),
         "valid contract source should not emit diagnostics");
  EXPECT(result.reflection.has_value(),
         "valid contract source should reflect a contract");
  EXPECT(result.reflection.has_value() &&
             result.reflection->declaredType == "matte",
         "contract should reflect declared type");
  EXPECT(result.reflection.has_value() &&
             result.reflection->findParameter("metallic").has_value(),
         "contract should reflect metallic parameter");
}

void testReflectRejectsMissingAccessor() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/bad.contract.glsl"), source);
  EXPECT(!result.diagnostics.empty(),
         "missing Material Accessor ABI should be diagnostic");
  EXPECT(!result.reflection.has_value(),
         "missing accessor should reject reflection");
}
```

Call both functions from `main()` before returning.

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake --build build --target test_material_source_contract
```

Expected: compile failure because `material_contract_reflector.hpp` does not exist.

- [ ] **Step 3: Implement strict comment-block reflection**

Create `src/infra/material_loader/material_contract_reflector.hpp`:

```cpp
#pragma once

#include "core/asset/material_contract.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_infra {

struct MaterialContractReflectionResult final {
  std::optional<LX_core::MaterialContractReflection> reflection;
  std::vector<std::string> diagnostics;
};

[[nodiscard]] MaterialContractReflectionResult
reflectMaterialContractSource(const LX_core::ResourceUri &sourceUri,
                              std::string_view sourceText);

} // namespace LX_infra
```

Create `src/infra/material_loader/material_contract_reflector.cpp` with this parser:

```cpp
#include "infra/material_loader/material_contract_reflector.hpp"

#include <sstream>
#include <string>

namespace LX_infra {
namespace {

LX_core::MaterialContractParameterKind kindFromToken(const std::string &token) {
  if (token == "float") return LX_core::MaterialContractParameterKind::Float;
  if (token == "rgb") return LX_core::MaterialContractParameterKind::Rgb;
  if (token == "spectrum") return LX_core::MaterialContractParameterKind::Spectrum;
  if (token == "texture") return LX_core::MaterialContractParameterKind::Texture;
  if (token == "integer") return LX_core::MaterialContractParameterKind::Integer;
  if (token == "bool") return LX_core::MaterialContractParameterKind::Bool;
  if (token == "string") return LX_core::MaterialContractParameterKind::String;
  if (token == "materialRef") return LX_core::MaterialContractParameterKind::MaterialRef;
  return LX_core::MaterialContractParameterKind::BsdfTable;
}

std::string stripPrefix(std::string line) {
  const std::string prefix = "// ";
  if (line.rfind(prefix, 0) == 0) {
    return line.substr(prefix.size());
  }
  if (line.rfind("//", 0) == 0) {
    return line.substr(2);
  }
  return line;
}

} // namespace

MaterialContractReflectionResult
reflectMaterialContractSource(const LX_core::ResourceUri &sourceUri,
                              std::string_view sourceText) {
  MaterialContractReflectionResult result;
  LX_core::MaterialContractReflection reflection;
  reflection.sourceUri = sourceUri;

  bool inBlock = false;
  bool sawBlock = false;
  std::istringstream input(std::string(sourceText));
  std::string line;
  while (std::getline(input, line)) {
    const std::string stripped = stripPrefix(line);
    if (stripped == "LX_MATERIAL_CONTRACT_BEGIN") {
      inBlock = true;
      sawBlock = true;
      continue;
    }
    if (stripped == "LX_MATERIAL_CONTRACT_END") {
      inBlock = false;
      continue;
    }
    if (!inBlock) {
      continue;
    }

    if (stripped.rfind("type:", 0) == 0) {
      reflection.declaredType = stripped.substr(6);
    } else if (stripped.rfind("status:", 0) == 0) {
      const std::string status = stripped.substr(8);
      reflection.supportStatus =
          status == "supported"
              ? LX_core::MaterialContractSupportStatus::Supported
              : LX_core::MaterialContractSupportStatus::Unsupported;
    } else if (stripped.rfind("reflectionHash:", 0) == 0) {
      reflection.reflectionHash = stripped.substr(16);
    } else if (stripped.rfind("storageAbiHash:", 0) == 0) {
      reflection.storageAbiHash = stripped.substr(16);
    } else if (stripped.rfind("accessorAbiHash:", 0) == 0) {
      reflection.accessorAbiHash = stripped.substr(17);
    } else if (stripped.rfind("parameter:", 0) == 0) {
      std::istringstream tokens(stripped.substr(11));
      LX_core::MaterialContractParameter parameter;
      std::string requiredToken;
      tokens >> parameter.name >> requiredToken;
      parameter.required = requiredToken == "required";
      std::string kindToken;
      while (tokens >> kindToken) {
        parameter.allowedKinds.push_back(kindFromToken(kindToken));
      }
      reflection.parameters.push_back(std::move(parameter));
    }
  }

  if (!sawBlock) {
    result.diagnostics.push_back(sourceUri.string() +
                                 ": missing LX_MATERIAL_CONTRACT block");
  }
  if (reflection.declaredType.empty()) {
    result.diagnostics.push_back(sourceUri.string() + ": missing type");
  }
  if (reflection.reflectionHash.empty() || reflection.storageAbiHash.empty() ||
      reflection.accessorAbiHash.empty()) {
    result.diagnostics.push_back(sourceUri.string() +
                                 ": missing reflection/storage/accessor hash");
  }
  if (sourceText.find("lxLoadMaterialSurface") == std::string_view::npos) {
    result.diagnostics.push_back(sourceUri.string() +
                                 ": missing Material Accessor ABI entry");
  }
  if (reflection.parameters.empty()) {
    result.diagnostics.push_back(sourceUri.string() +
                                 ": contract must declare parameters");
  }

  if (result.diagnostics.empty()) {
    result.reflection = std::move(reflection);
  }
  return result;
}

} // namespace LX_infra
```

Add `material_contract_reflector.cpp` to the infra library source list.

- [ ] **Step 4: Run reflection tests**

Run:

```bash
cmake --build build --target test_material_source_contract
./build/src/test/test_material_source_contract
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/infra/material_loader/material_contract_reflector.hpp src/infra/material_loader/material_contract_reflector.cpp src/infra/CMakeLists.txt src/test/integration/test_material_source_contract.cpp
git commit -m "Reflect material contract source metadata"
```

### Task 3: Require `bsdf.source` In Material Parser

**Files:**
- Modify: `src/infra/material_loader/material_resource_parser.cpp`
- Modify: `src/core/asset/material_instance.hpp`
- Modify: `src/core/asset/material_instance.cpp`
- Test: `src/test/integration/test_material_v2_parser.cpp`
- Test: `src/test/integration/test_material_source_contract.cpp`

- [ ] **Step 1: Add failing parser tests for missing and mismatched source**

Add to `src/test/integration/test_material_source_contract.cpp`:

```cpp
#include "infra/material_loader/material_resource_parser.hpp"
#include "core/scene/scene_resource_table.hpp"

bool diagnosticsContain(const std::vector<std::string> &diagnostics,
                        const std::string &needle) {
  for (const std::string &diagnostic : diagnostics) {
    if (diagnostic.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void testMaterialParserRequiresBsdfSource() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/no-source.material"),
      R"yaml(
schema: material.v3
bsdf:
  type: matte
  parameters:
    Kd:
      type: rgb
      value: [1.0, 1.0, 1.0]
)yaml");
  EXPECT(!parsed.diagnostics.empty(), "missing bsdf.source should fail");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.source"),
         "diagnostic should name bsdf.source");
}
```

Call it from `main()`.

- [ ] **Step 2: Run the test to verify it fails for the right reason**

Run:

```bash
cmake --build build --target test_material_source_contract
./build/src/test/test_material_source_contract
```

Expected: FAIL because current parser accepts materials without `bsdf.source` and uses `MaterialSurfaceSchema`.

- [ ] **Step 3: Add source fields to `MaterialInstance`**

In `src/core/asset/material_instance.hpp`, add public methods:

```cpp
void setMaterialSourceUri(ResourceUri sourceUri);
[[nodiscard]] const ResourceUri &getMaterialSourceUri() const;
void setMaterialSourceSignature(StringID signature);
[[nodiscard]] StringID getMaterialSourceSignature() const;
void setMaterialSourceReflectionHash(std::string hash);
[[nodiscard]] const std::string &getMaterialSourceReflectionHash() const;
```

Add private members:

```cpp
ResourceUri m_materialSourceUri;
StringID m_materialSourceSignature;
std::string m_materialSourceReflectionHash;
```

In `src/core/asset/material_instance.cpp`, implement:

```cpp
void MaterialInstance::setMaterialSourceUri(ResourceUri sourceUri) {
  m_materialSourceUri = std::move(sourceUri);
  markMaterialStateDirty();
}

const ResourceUri &MaterialInstance::getMaterialSourceUri() const {
  return m_materialSourceUri;
}

void MaterialInstance::setMaterialSourceSignature(StringID signature) {
  m_materialSourceSignature = signature;
  markMaterialStateDirty();
}

StringID MaterialInstance::getMaterialSourceSignature() const {
  return m_materialSourceSignature;
}

void MaterialInstance::setMaterialSourceReflectionHash(std::string hash) {
  m_materialSourceReflectionHash = std::move(hash);
  markMaterialStateDirty();
}

const std::string &MaterialInstance::getMaterialSourceReflectionHash() const {
  return m_materialSourceReflectionHash;
}
```

Update clone paths to copy these three members.

- [ ] **Step 4: Enforce `bsdf.source` before schema validation**

In `MaterialResourceParser::parse`, after reading `bsdf.type`, add:

```cpp
if (!bsdfNode["source"] || !bsdfNode["source"].IsScalar()) {
  addDiagnostic(result, uri, "bsdf.source",
                "missing scalar material contract source");
  return result;
}
const LX_core::ResourceUri sourceUri =
    table.resolveUri(uri, LX_core::ResourceUri(bsdfNode["source"].as<std::string>()));
```

Temporarily keep schema validation after this step; this task only establishes the mandatory field and instance storage.

Set the instance fields after creation:

```cpp
instance->setMaterialSourceUri(sourceUri);
instance->setMaterialSourceReflectionHash("unreflected-contract");
instance->setMaterialSourceSignature(LX_core::StringID(sourceUri.string()));
```

- [ ] **Step 5: Run parser test**

Run:

```bash
cmake --build build --target test_material_source_contract
./build/src/test/test_material_source_contract
```

Expected: PASS for missing source rejection.

- [ ] **Step 6: Commit**

```bash
git add src/core/asset/material_instance.hpp src/core/asset/material_instance.cpp src/infra/material_loader/material_resource_parser.cpp src/test/integration/test_material_source_contract.cpp
git commit -m "Require material contract source in parser"
```

### Task 4: Replace `MaterialSurfaceSchema` Validation With Reflected Contract Validation

**Files:**
- Modify: `src/infra/material_loader/material_resource_parser.cpp`
- Modify: `src/infra/material_loader/material_resource_parser.hpp`
- Modify: `src/test/integration/test_material_v2_parser.cpp`
- Modify: `src/test/integration/test_material_source_contract.cpp`
- Modify: `src/infra/scene_asset/gltf_scene_asset_loader.cpp`

- [ ] **Step 1: Add failing tests for unknown parameter and source/type mismatch**

Add to `src/test/integration/test_material_source_contract.cpp`:

```cpp
void testMaterialParserRejectsUnknownParameterFromContract() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/bad-param.material"),
      R"yaml(
schema: material.v3
bsdf:
  type: matte
  source: memory://materials/matte.contract.glsl
  parameters:
    Unknown:
      type: float
      value: 1.0
)yaml");
  EXPECT(!parsed.diagnostics.empty(), "unknown contract parameter should fail");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.parameters.Unknown"),
         "diagnostic should name unknown parameter");
}
```

Call it from `main()`.

- [ ] **Step 2: Introduce parser dependency on a reflector callback**

Change `MaterialResourceParser` in `material_resource_parser.hpp`:

```cpp
using MaterialContractReflector =
    std::function<MaterialContractReflectionResult(
        const LX_core::ResourceUri &, std::string_view)>;

class MaterialResourceParser final {
public:
  explicit MaterialResourceParser(MaterialContractReflector reflector =
                                      reflectMaterialContractSource);
  [[nodiscard]] ParsedMaterialResource parse(... ) const;
private:
  MaterialContractReflector m_reflector;
};
```

Include:

```cpp
#include "infra/material_loader/material_contract_reflector.hpp"
#include <functional>
```

Implement constructor in `.cpp`:

```cpp
MaterialResourceParser::MaterialResourceParser(MaterialContractReflector reflector)
    : m_reflector(std::move(reflector)) {}
```

- [ ] **Step 3: Reflect source and validate against reflected parameters**

In `MaterialResourceParser::parse`, replace:

```cpp
const LX_core::MaterialSurfaceSchema *schema =
    LX_core::findMaterialSurfaceSchema(bsdfType);
```

with source text loading and reflection. If resource table cannot yet provide source bytes, add a parser-local helper for `memory://` and filesystem source URIs:

```cpp
std::string sourceText = loadMaterialContractSourceText(sourceUri, result);
if (!result.diagnostics.empty()) {
  return result;
}
const auto reflected = m_reflector(sourceUri, sourceText);
for (const std::string &diagnostic : reflected.diagnostics) {
  result.diagnostics.push_back("bsdf.source: " + diagnostic);
}
if (!reflected.reflection.has_value()) {
  return result;
}
const auto &contract = *reflected.reflection;
if (contract.declaredType != bsdfType) {
  addDiagnostic(result, uri, "bsdf.source",
                "material contract declared type does not match bsdf.type");
  return result;
}
if (contract.supportStatus != LX_core::MaterialContractSupportStatus::Supported) {
  addDiagnostic(result, uri, "bsdf.source",
                "material contract source is unsupported");
  return result;
}
```

Replace `hasSchemaParameter(*schema, parameterName)` with:

```cpp
if (!contract.findParameter(parameterName).has_value()) {
  addDiagnostic(result, uri, "bsdf.parameters." + parameterName,
                "unknown BSDF parameter for material contract source");
}
```

Replace schema parameter loop with `contract.parameters`.

- [ ] **Step 4: Map envelope kinds to contract kinds**

Add helper in `material_resource_parser.cpp`:

```cpp
bool isAllowedContractKind(const LX_core::MaterialContractParameter &parameter,
                           LX_core::MaterialEnvelopeKind kind) {
  const auto converted = materialEnvelopeKindToContractKind(kind);
  return std::find(parameter.allowedKinds.begin(), parameter.allowedKinds.end(),
                   converted) != parameter.allowedKinds.end();
}
```

Use it where old parser used `isAllowedKind(parameter, envelope->kind)`.

- [ ] **Step 5: Run parser tests**

Run:

```bash
cmake --build build --target test_material_source_contract test_material_v2_parser
./build/src/test/test_material_source_contract
./build/src/test/test_material_v2_parser
```

Expected: new source contract tests pass; old schema-positive tests fail until converted in the next step.

- [ ] **Step 6: Convert old schema-positive tests into legacy rejection or source contract tests**

In `src/test/integration/test_material_v2_parser.cpp`, replace direct `findMaterialSurfaceSchema()` expectations with contract reflection tests using `reflectMaterialContractSource`. Keep only negative references to `MaterialSurfaceSchema`, for example:

```cpp
void testMaterialV3DoesNotAcceptSchemaOnlyMaterial() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const auto parsed = parser.parse(table, LX_core::ResourceUri("memory://legacy.material"),
      R"yaml(
schema: material.v3
bsdf:
  type: matte
  parameters:
    Kd:
      type: rgb
      value: [1.0, 1.0, 1.0]
)yaml");
  EXPECT(!parsed.diagnostics.empty(),
         "Material v3 requires bsdf.source and must not use C++ schema fallback");
}
```

- [ ] **Step 7: Commit**

```bash
git add src/infra/material_loader/material_resource_parser.hpp src/infra/material_loader/material_resource_parser.cpp src/infra/scene_asset/gltf_scene_asset_loader.cpp src/test/integration/test_material_v2_parser.cpp src/test/integration/test_material_source_contract.cpp
git commit -m "Validate materials through source contracts"
```

### Task 5: Add Built-in Contract Sources And Accessor ABI Header

**Files:**
- Create: `assets/shaders/glsl/common/material_surface.glsl`
- Create: `assets/shaders/glsl/common/materials/matte.contract.glsl`
- Create: `assets/shaders/glsl/common/materials/metal.contract.glsl`
- Create: `assets/shaders/glsl/common/materials/uber.contract.glsl`
- Create: `assets/shaders/glsl/common/materials/substrate.contract.glsl`
- Test: `src/test/integration/test_material_source_contract.cpp`

- [ ] **Step 1: Add failing asset reflection test**

Add to `test_material_source_contract.cpp`:

```cpp
#include <fstream>
#include <sstream>

std::string readText(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void testBuiltinMaterialContractsReflect() {
  const std::vector<std::pair<std::string, std::string>> contracts = {
      {"assets/shaders/glsl/common/materials/matte.contract.glsl", "matte"},
      {"assets/shaders/glsl/common/materials/metal.contract.glsl", "metal"},
      {"assets/shaders/glsl/common/materials/uber.contract.glsl", "uber"},
      {"assets/shaders/glsl/common/materials/substrate.contract.glsl", "substrate"},
  };
  for (const auto &[path, type] : contracts) {
    const auto result = LX_infra::reflectMaterialContractSource(
        LX_core::ResourceUri(path), readText(path));
    EXPECT(result.reflection.has_value(), (path + " should reflect").c_str());
    EXPECT(result.reflection.has_value() &&
               result.reflection->declaredType == type,
           (path + " should declare expected type").c_str());
    EXPECT(result.reflection.has_value() &&
               result.reflection->findParameter("metallic").has_value(),
           (path + " should expose metallic").c_str());
  }
}
```

Call it from `main()`.

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake --build build --target test_material_source_contract
./build/src/test/test_material_source_contract
```

Expected: FAIL because contract files do not exist.

- [ ] **Step 3: Add stable accessor ABI header**

Create `assets/shaders/glsl/common/material_surface.glsl`:

```glsl
#ifndef LX_MATERIAL_SURFACE_GLSL
#define LX_MATERIAL_SURFACE_GLSL

struct LxMaterialSurface {
  vec3 baseColor;
  float alpha;
  float metallic;
  float roughness;
  vec3 normal;
  float ao;
  vec3 emissive;
};

#endif
```

- [ ] **Step 4: Add four built-in contract sources**

Create `assets/shaders/glsl/common/materials/matte.contract.glsl`:

```glsl
#include "common/material_surface.glsl"

// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-source-contract-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture spectrum
// parameter: metallic optional float texture
// parameter: roughness optional float texture
// parameter: uroughness optional float texture
// parameter: vroughness optional float texture
// parameter: ao optional float texture
// parameter: emissive optional rgb texture
// parameter: normalScale optional float
// parameter: normalmap optional texture
// LX_MATERIAL_CONTRACT_END

LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv,
                                        vec3 geometricNormal,
                                        mat3 tangentFrame) {
  LxMaterialSurface surface;
  surface.baseColor = vec3(1.0);
  surface.alpha = 1.0;
  surface.metallic = 0.0;
  surface.roughness = 0.5;
  surface.normal = normalize(geometricNormal);
  surface.ao = 1.0;
  surface.emissive = vec3(0.0);
  return surface;
}
```

Create the other three files by changing `type` and hashes:

```glsl
// metal.contract.glsl uses:
// type: metal
// reflectionHash: metal-source-contract-v1
// storageAbiHash: metal-storage-v1
```

```glsl
// uber.contract.glsl uses:
// type: uber
// reflectionHash: uber-source-contract-v1
// storageAbiHash: uber-storage-v1
```

```glsl
// substrate.contract.glsl uses:
// type: substrate
// reflectionHash: substrate-source-contract-v1
// storageAbiHash: substrate-storage-v1
```

Each file must include the same parameter list and implement `lxLoadMaterialSurface` with the same return fields. Keep the deterministic stub body shown above until Task 8 wires real storage reads.

- [ ] **Step 5: Run asset reflection test**

Run:

```bash
cmake --build build --target test_material_source_contract
./build/src/test/test_material_source_contract
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add assets/shaders/glsl/common/material_surface.glsl assets/shaders/glsl/common/materials src/test/integration/test_material_source_contract.cpp
git commit -m "Add built-in material contract sources"
```

### Task 6: Add Source-reflected Material Signatures To Pipeline Identity

**Files:**
- Modify: `src/core/asset/material_instance.cpp`
- Modify: `src/test/integration/test_pipeline_identity.cpp`
- Modify: `src/test/integration/test_material_source_contract.cpp`

- [ ] **Step 1: Add failing signature test**

Add to `test_material_source_contract.cpp`:

```cpp
void testMaterialInstanceSignatureUsesSourceSignature() {
  auto material = LX_core::MaterialInstance::createUnique(
      LX_core::MaterialTemplate::create("matte"));
  material->setMaterialSourceUri(
      LX_core::ResourceUri("assets/shaders/glsl/common/materials/matte.contract.glsl"));
  material->setMaterialSourceReflectionHash("matte-source-contract-v1");
  material->setMaterialSourceSignature(LX_core::StringID("matte-source-signature"));

  const LX_core::StringID forward = material->getPipelineSignature(LX_core::Pass_Forward);
  material->setMaterialEnvelope(LX_core::StringID("Kd"), LX_core::MaterialParameterEnvelope{});
  const LX_core::StringID afterValueChange =
      material->getPipelineSignature(LX_core::Pass_Forward);

  EXPECT(forward == afterValueChange,
         "material parameter values must not alter material pipeline signature");
}
```

- [ ] **Step 2: Run to verify current behavior fails or ignores source**

Run:

```bash
cmake --build build --target test_material_source_contract
./build/src/test/test_material_source_contract
```

Expected: FAIL if `getPipelineSignature` ignores source signature or if test cannot compile due missing pass include.

- [ ] **Step 3: Update `MaterialInstance::getPipelineSignature`**

In `src/core/asset/material_instance.cpp`, change the method to compose source signature when present:

```cpp
StringID MaterialInstance::getPipelineSignature(StringID pass) const {
  StringID passSig = m_template->getPipelineSignature(pass);
  if (m_materialSourceSignature.isValid()) {
    StringID fields[] = {m_materialSourceSignature, passSig};
    return GlobalStringTable::get().compose(TypeTag::MaterialRender, fields);
  }
  return passSig;
}
```

If `StringID` has no `isValid()`, use `m_materialSourceSignature.id != 0` or the repo’s established validity check.

- [ ] **Step 4: Run signature tests**

Run:

```bash
cmake --build build --target test_material_source_contract test_pipeline_identity
./build/src/test/test_material_source_contract
./build/src/test/test_pipeline_identity
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/asset/material_instance.cpp src/test/integration/test_material_source_contract.cpp src/test/integration/test_pipeline_identity.cpp
git commit -m "Include material source signature in pipeline identity"
```

### Task 7: Source-local Packing And Default Texture Semantics

**Files:**
- Create: `src/core/asset/material_contract_packer.hpp`
- Create: `src/core/asset/material_contract_packer.cpp`
- Modify: `src/core/scene/scene_resource_table_upload_view.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Test: `src/test/integration/test_material_source_contract.cpp`
- Test: `src/test/integration/test_scene_resource_upload_view_v2.cpp`

- [ ] **Step 1: Add failing packer invariant/default tests**

Add to `test_material_source_contract.cpp`:

```cpp
#include "core/asset/material_contract_packer.hpp"

void testPackerUsesDefaultTextureSlots() {
  LX_core::MaterialContractReflection contract;
  contract.sourceUri = LX_core::ResourceUri("memory://materials/matte.contract.glsl");
  contract.declaredType = "matte";
  contract.reflectionHash = "matte-reflect-v1";
  contract.storageAbiHash = "matte-storage-v1";
  contract.accessorAbiHash = "material-surface-v1";

  LX_core::MaterialContractDefaultTextureSlots defaults;
  defaults.white = 1;
  defaults.black = 2;
  defaults.flatNormal = 3;

  LX_core::MaterialContractPackInput input;
  input.contract = contract;
  input.defaultTextureSlots = defaults;

  const auto packed = LX_core::packMaterialContractRecord(input);
  EXPECT(packed.diagnostics.empty(), "default-only material should pack");
  EXPECT(packed.record.defaultWhiteTextureSlot == 1,
         "packer should preserve white default slot");
  EXPECT(packed.record.defaultFlatNormalTextureSlot == 3,
         "packer should preserve flatNormal default slot");
}
```

- [ ] **Step 2: Run to verify it fails**

Run:

```bash
cmake --build build --target test_material_source_contract
```

Expected: compile failure because `material_contract_packer.hpp` does not exist.

- [ ] **Step 3: Implement generic packer shell**

Create `src/core/asset/material_contract_packer.hpp`:

```cpp
#pragma once

#include "core/asset/material_contract.hpp"

#include <string>
#include <vector>

namespace LX_core {

struct MaterialContractDefaultTextureSlots final {
  u32 white = 0xffffffffu;
  u32 black = 0xffffffffu;
  u32 flatNormal = 0xffffffffu;
};

struct SourceLocalMaterialRecord final {
  StringID sourceSignature;
  u32 defaultWhiteTextureSlot = 0xffffffffu;
  u32 defaultBlackTextureSlot = 0xffffffffu;
  u32 defaultFlatNormalTextureSlot = 0xffffffffu;
  std::vector<u8> bytes;
};

struct MaterialContractPackInput final {
  MaterialContractReflection contract;
  MaterialContractDefaultTextureSlots defaultTextureSlots;
};

struct MaterialContractPackResult final {
  SourceLocalMaterialRecord record;
  std::vector<std::string> diagnostics;
};

[[nodiscard]] MaterialContractPackResult
packMaterialContractRecord(const MaterialContractPackInput &input);

} // namespace LX_core
```

Create `src/core/asset/material_contract_packer.cpp`:

```cpp
#include "core/asset/material_contract_packer.hpp"

namespace LX_core {

MaterialContractPackResult
packMaterialContractRecord(const MaterialContractPackInput &input) {
  MaterialContractPackResult result;
  result.record.sourceSignature = input.contract.sourceSignature();
  result.record.defaultWhiteTextureSlot = input.defaultTextureSlots.white;
  result.record.defaultBlackTextureSlot = input.defaultTextureSlots.black;
  result.record.defaultFlatNormalTextureSlot =
      input.defaultTextureSlots.flatNormal;
  if (input.defaultTextureSlots.white == 0xffffffffu ||
      input.defaultTextureSlots.black == 0xffffffffu ||
      input.defaultTextureSlots.flatNormal == 0xffffffffu) {
    result.diagnostics.push_back(
        "material contract packer requires stable default texture slots");
  }
  return result;
}

} // namespace LX_core
```

Add this file to the core build source list.

- [ ] **Step 4: Add source-local storage to upload view**

In `scene_resource_table_upload_view.hpp`, add:

```cpp
struct SceneSourceLocalMaterialStorageView final {
  StringID sourceSignature;
  u32 recordOffset = 0;
  u32 recordCount = 0;
};
std::span<const SceneSourceLocalMaterialStorageView> sourceMaterialStorages;
```

Keep existing `materials` span for legacy tests until `073-b`; do not claim it is the Material v3 final path.

- [ ] **Step 5: Run packer and upload tests**

Run:

```bash
cmake --build build --target test_material_source_contract test_scene_resource_upload_view_v2
./build/src/test/test_material_source_contract
./build/src/test/test_scene_resource_upload_view_v2
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/asset/material_contract_packer.hpp src/core/asset/material_contract_packer.cpp src/core/scene/scene_resource_table_upload_view.hpp src/core/scene/scene_resource_table.cpp src/test/integration/test_material_source_contract.cpp src/test/integration/test_scene_resource_upload_view_v2.cpp
git commit -m "Prepare source-local material packing"
```

### Task 8: Wire Material Accessor ABI Into Forward, Deferred, And OfflineRT Shaders

**Files:**
- Modify: `assets/shaders/glsl/techniques/Forward/pbr.frag`
- Modify: `assets/shaders/glsl/techniques/Deferred/pbr_gbuffer.frag`
- Modify: `assets/shaders/glsl/techniques/OfflineRT/offline_pbr_direct_ray.comp`
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Add shader audit test**

In `test_shader_compiler.cpp`, add a source audit that loads these shader files and asserts:

```cpp
EXPECT(fileContains("assets/shaders/glsl/techniques/Forward/pbr.frag",
                    "lxLoadMaterialSurface"),
       "Forward PBR shader should use Material Accessor ABI");
EXPECT(!fileContains("assets/shaders/glsl/techniques/Forward/pbr.frag",
                     "struct lxSceneMaterialRecord"),
       "Forward PBR shader should not declare legacy shared material record");
```

Repeat the same assertions for Deferred GBuffer and OfflineRT direct ray.

- [ ] **Step 2: Run to verify it fails**

Run:

```bash
cmake --build build --target test_shader_compiler
./build/src/test/test_shader_compiler
```

Expected: FAIL because shaders still declare `lxSceneMaterialRecord`.

- [ ] **Step 3: Replace shared material record reads in Forward shader**

In `pbr.frag`, include:

```glsl
#include "common/material_surface.glsl"
#include LX_MATERIAL_CONTRACT_SOURCE
```

Replace the block that reads `lxSceneMaterialRecord material = materials[vMaterialIndex];` through emissive setup with:

```glsl
LxMaterialSurface surface =
    lxLoadMaterialSurface(vMaterialIndex, vUV, normalize(vNormal), vTBN);

vec4 albedo = vec4(surface.baseColor, surface.alpha);
float metallic = clamp(surface.metallic, 0.0, 1.0);
float roughness = clamp(surface.roughness, 0.04, 1.0);
float ao = clamp(surface.ao, 0.0, 1.0);
vec3 N = normalize(surface.normal);
```

Use `surface.emissive` when filling `pbrInput.emissive`.

- [ ] **Step 4: Replace Deferred and OfflineRT reads**

In Deferred GBuffer, load `surface` and write:

```glsl
outAlbedoAlpha = vec4(surface.baseColor, surface.alpha);
outNormalRoughness = vec4(normalize(surface.normal) * 0.5 + 0.5,
                          clamp(surface.roughness, 0.04, 1.0));
outMaterial = vec4(clamp(surface.metallic, 0.0, 1.0),
                   clamp(surface.ao, 0.0, 1.0), 0.0, 0.0);
outAlbedoAlpha.rgb += surface.emissive;
```

In OfflineRT direct ray, replace material record sampling with `lxLoadMaterialSurface(materialIndex, hit.uv, hit.normal, hit.tangentFrame)` and feed the returned values into `LxPbrDirectInput`.

- [ ] **Step 5: Run shader tests**

Run:

```bash
cmake --build build --target test_shader_compiler
./build/src/test/test_shader_compiler
```

Expected: PASS for source audit. Shader compilation may still require Task 9 variant include plumbing if `LX_MATERIAL_CONTRACT_SOURCE` is not yet defined.

- [ ] **Step 6: Commit**

```bash
git add assets/shaders/glsl/techniques/Forward/pbr.frag assets/shaders/glsl/techniques/Deferred/pbr_gbuffer.frag assets/shaders/glsl/techniques/OfflineRT/offline_pbr_direct_ray.comp src/test/integration/test_shader_compiler.cpp
git commit -m "Route PBR shaders through material accessor ABI"
```

### Task 9: Add Shader Variant Include Plumbing For `bsdf.source`

**Files:**
- Modify: `src/infra/shader_compiler/*`
- Modify: `src/core/asset/shader.hpp`
- Modify: `src/core/frame_graph/render_queue.cpp`
- Test: `src/test/integration/test_pipeline_build_info.cpp`
- Test: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Add failing variant test**

In `test_pipeline_build_info.cpp`, construct two material instances with same base pass shader but different `materialSourceSignature`, then assert compile key/debug info differs by source signature and not by texture presence:

```cpp
EXPECT(matteBuildInfo.shaderVariantKey != metalBuildInfo.shaderVariantKey,
       "different bsdf.source should produce different shader variant key");
EXPECT(texturedMatteBuildInfo.shaderVariantKey == matteBuildInfo.shaderVariantKey,
       "texture presence must not produce shader variant key");
```

- [ ] **Step 2: Run to verify it fails**

Run:

```bash
cmake --build build --target test_pipeline_build_info
./build/src/test/test_pipeline_build_info
```

Expected: FAIL because shader variant key does not include source contract include.

- [ ] **Step 3: Add material source include to variant description**

Extend the shader variant data structure with:

```cpp
std::optional<ResourceUri> materialContractSource;
StringID materialSourceSignature;
```

When building render work from a material, populate it from `MaterialInstance::getMaterialSourceUri()` and `getMaterialSourceSignature()`.

- [ ] **Step 4: Teach shader compiler to define `LX_MATERIAL_CONTRACT_SOURCE`**

Before preprocessing GLSL, if `materialContractSource` is set, inject:

```glsl
#define LX_MATERIAL_CONTRACT_SOURCE "common/materials/matte.contract.glsl"
```

Use the resolved include path from the resource URI, not a hardcoded type map.

- [ ] **Step 5: Run variant tests**

Run:

```bash
cmake --build build --target test_pipeline_build_info test_shader_compiler
./build/src/test/test_pipeline_build_info
./build/src/test/test_shader_compiler
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/infra/shader_compiler src/core/asset/shader.hpp src/core/frame_graph/render_queue.cpp src/test/integration/test_pipeline_build_info.cpp src/test/integration/test_shader_compiler.cpp
git commit -m "Compile shaders with material contract source variants"
```

### Task 10: Migrate Generated Assets And Converter Output To Explicit `bsdf.source`

**Files:**
- Modify: `src/infra/scene_asset/gltf_scene_asset_loader.cpp`
- Modify: `src/infra/offline/offline_scene_loader.cpp`
- Modify: `src/test/integration/test_gltf_scene_asset_loader.cpp`
- Modify: `src/test/integration/test_lxe_pbrt_scene_convert.py`
- Modify: `data/scenes/bmw-m6/pbrt_bmw_m6.conversion.md`

- [ ] **Step 1: Add failing glTF source assertion**

In `test_gltf_scene_asset_loader.cpp`, after checking `getBsdfType()`, add:

```cpp
expect(result.material->getMaterialSourceUri().string().find("uber.contract.glsl") !=
           std::string::npos,
       "DamagedHelmet generated material should declare explicit bsdf.source");
```

- [ ] **Step 2: Run to verify it fails**

Run:

```bash
cmake --build build --target test_gltf_scene_asset_loader
./build/src/test/test_gltf_scene_asset_loader
```

Expected: FAIL because generated materials do not set source yet.

- [ ] **Step 3: Set source in glTF material generation**

In `gltf_scene_asset_loader.cpp`, when creating the generated Material v3 instance, set:

```cpp
material->setMaterialSourceUri(LX_core::ResourceUri(
    "assets/shaders/glsl/common/materials/uber.contract.glsl"));
material->setMaterialSourceReflectionHash("uber-source-contract-v1");
material->setMaterialSourceSignature(LX_core::StringID(
    "assets/shaders/glsl/common/materials/uber.contract.glsl#uber-source-contract-v1"));
```

Generated material YAML or reports must also write:

```yaml
bsdf:
  type: uber
  source: assets/shaders/glsl/common/materials/uber.contract.glsl
```

- [ ] **Step 4: Update PBRT/BMW converter expected output**

In `test_lxe_pbrt_scene_convert.py`, assert converted materials contain:

```python
assert "source: assets/shaders/glsl/common/materials/" in material_text
```

Map PBRT types:

```python
expected_source = {
    "matte": "matte.contract.glsl",
    "metal": "metal.contract.glsl",
    "uber": "uber.contract.glsl",
    "substrate": "substrate.contract.glsl",
}
```

Unsupported `glass`, `fourier`, and `mix` must be reported as unsupported if no source is selected.

- [ ] **Step 5: Run asset tests**

Run:

```bash
cmake --build build --target test_gltf_scene_asset_loader
./build/src/test/test_gltf_scene_asset_loader
python3 src/test/integration/test_lxe_pbrt_scene_convert.py
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/infra/scene_asset/gltf_scene_asset_loader.cpp src/infra/offline/offline_scene_loader.cpp src/test/integration/test_gltf_scene_asset_loader.cpp src/test/integration/test_lxe_pbrt_scene_convert.py data/scenes/bmw-m6/pbrt_bmw_m6.conversion.md
git commit -m "Emit explicit material contract sources from asset loaders"
```

### Task 11: Remove C++ Schema Positive Path And Add Legacy Audit

**Files:**
- Delete: `src/core/asset/material_surface_schema.hpp`
- Delete: `src/core/asset/material_surface_schema.cpp`
- Modify: `src/infra/CMakeLists.txt`
- Modify: `src/test/integration/test_071g_legacy_boundary_removal.cpp`
- Modify: `src/test/integration/test_material_v2_parser.cpp`

- [ ] **Step 1: Add failing rg/audit test for positive `MaterialSurfaceSchema` use**

In `test_071g_legacy_boundary_removal.cpp`, add `MaterialSurfaceSchema` to the production positive-path ban list with an allowlist only for named negative tests:

```cpp
AuditRule{
    .token = "MaterialSurfaceSchema",
    .allowedPaths = {
        "notes/requirements/073-a-material-v3-pbrt-metallic-extension.md",
        "docs/superpowers/specs/2026-06-12-material-source-contract-design.md",
        "test_071g_legacy_boundary_removal.cpp",
    },
    .message = "Material v3 must use bsdf.source reflection, not C++ schema fallback",
}
```

- [ ] **Step 2: Run audit to verify it fails**

Run:

```bash
cmake --build build --target test_071g_legacy_boundary_removal
./build/src/test/test_071g_legacy_boundary_removal
```

Expected: FAIL because production code still includes `MaterialSurfaceSchema`.

- [ ] **Step 3: Delete schema files and remove includes**

Delete:

```bash
git rm src/core/asset/material_surface_schema.hpp src/core/asset/material_surface_schema.cpp
```

Remove the source from build files. Replace includes with `material_contract.hpp` or parser reflection headers. Any remaining positive code that asks `findMaterialSurfaceSchema(bsdfType)` must instead use the reflected contract attached to the material.

- [ ] **Step 4: Run audit and parser tests**

Run:

```bash
cmake --build build --target test_071g_legacy_boundary_removal test_material_source_contract test_material_v2_parser
./build/src/test/test_071g_legacy_boundary_removal
./build/src/test/test_material_source_contract
./build/src/test/test_material_v2_parser
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/infra/CMakeLists.txt src/test/integration/test_071g_legacy_boundary_removal.cpp src/test/integration/test_material_v2_parser.cpp
git add -u src/core/asset/material_surface_schema.hpp src/core/asset/material_surface_schema.cpp
git commit -m "Remove material surface schema positive path"
```

### Task 12: Final Verification And Notes Sync

**Files:**
- Modify if drift appears: `notes/requirements/073-a-material-v3-pbrt-metallic-extension.md`
- Modify if drift appears: `notes/requirements/073-b-bindless-indirect-material-path-hard-cut.md`

- [ ] **Step 1: Run targeted build and auto tests**

Run:

```bash
cmake --build build --target test_material_source_contract test_material_v2_parser test_shader_compiler test_pipeline_build_info test_gltf_scene_asset_loader test_lxe_pbrt_scene_convert test_071g_legacy_boundary_removal
./build/src/test/test_material_source_contract
./build/src/test/test_material_v2_parser
./build/src/test/test_shader_compiler
./build/src/test/test_pipeline_build_info
./build/src/test/test_gltf_scene_asset_loader
python3 src/test/integration/test_lxe_pbrt_scene_convert.py
./build/src/test/test_071g_legacy_boundary_removal
```

Expected: all pass.

- [ ] **Step 2: Run broader auto suite**

Run:

```bash
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
```

Expected: PASS or failures documented with exact test names and diagnostics if unrelated to this requirement.

- [ ] **Step 3: Run source audits**

Run:

```bash
rg -n "findMaterialSurfaceSchema|MaterialSurfaceSchema|MaterialUBO|struct lxSceneMaterialRecord" src assets/shaders/glsl -g '!*.spv'
```

Expected:

- no `findMaterialSurfaceSchema`;
- no production `MaterialSurfaceSchema`;
- `MaterialUBO` only in named negative audits or unrelated explicit legacy diagnostics;
- `struct lxSceneMaterialRecord` not in Forward/Deferred/OfflineRT positive PBR shaders.

- [ ] **Step 4: Update requirement status**

In `notes/requirements/073-a-material-v3-pbrt-metallic-extension.md`, change:

```markdown
## 实施状态

未实施。
```

to:

```markdown
## 实施状态

已实施：

- Material v3 requires explicit `bsdf.source`.
- Material contracts are reflected from source metadata.
- Forward / Deferred / OfflineRT consume the Material Accessor ABI.
- C++ `MaterialSurfaceSchema` is removed from the positive path.
```

- [ ] **Step 5: Commit final status**

```bash
git add notes/requirements/073-a-material-v3-pbrt-metallic-extension.md notes/requirements/073-b-bindless-indirect-material-path-hard-cut.md
git commit -m "Close material source contract requirement"
```

## Self-Review

Spec coverage:

- `bsdf.source` required: Tasks 3 and 4.
- Source-reflected contract: Tasks 1, 2, 4, and 5.
- Material Accessor ABI: Tasks 5 and 8.
- MaterialSignature source identity: Task 6.
- Factor/texture/default texture path: Tasks 5 and 7.
- Loader/converter source emission: Task 10.
- C++ schema hard cut: Task 11.
- Tests and audits: Tasks 1 through 12.

Placeholder scan:

- No unresolved markers or open-ended "add tests" steps.
- Every task includes paths, commands, and expected outcomes.

Type consistency:

- `MaterialContractReflection`, `MaterialContractParameter`, `MaterialContractPacker`, and `MaterialInstance` source accessors are introduced before later tasks use them.
- Source signatures use `StringID` consistently with existing `PipelineKey`/`MaterialSignature` code.
