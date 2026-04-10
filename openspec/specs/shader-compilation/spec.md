## ADDED Requirements

### Requirement: Load GLSL source from file system
ShaderCompiler SHALL load GLSL source code from a given file path. It MUST support `.vert` and `.frag` file extensions for vertex and fragment stages respectively.

#### Scenario: Load vertex and fragment shader files
- **WHEN** ShaderCompiler is given paths to `pbr.vert` and `pbr.frag`
- **THEN** both files are read successfully and their source contents are available for compilation

#### Scenario: File not found
- **WHEN** ShaderCompiler is given a path to a non-existent shader file
- **THEN** compilation fails with a descriptive error message containing the file path

### Requirement: Inject variant macros before compilation
ShaderCompiler SHALL inject `#define` directives for each enabled `ShaderVariant` before compiling the GLSL source. Disabled variants MUST NOT be defined.

#### Scenario: Single variant enabled
- **WHEN** compiling a shader with variant `HAS_NORMAL_MAP` enabled
- **THEN** the compiled SPIR-V reflects code paths gated by `#ifdef HAS_NORMAL_MAP`

#### Scenario: Multiple variants with mixed enable states
- **WHEN** compiling with `HAS_NORMAL_MAP` enabled and `HAS_METALLIC_ROUGHNESS` disabled
- **THEN** only `HAS_NORMAL_MAP` is defined; code paths under `HAS_METALLIC_ROUGHNESS` are excluded

#### Scenario: No variants enabled
- **WHEN** compiling with all variants disabled (or empty variant list)
- **THEN** no extra macros are injected and compilation produces the base shader variant

### Requirement: Compile GLSL to SPIR-V via shaderc
ShaderCompiler SHALL use shaderc to compile GLSL source code into SPIR-V bytecode. It MUST support vertex and fragment shader stages.

#### Scenario: Successful compilation
- **WHEN** valid GLSL source with correct macros is provided
- **THEN** compilation succeeds and returns non-empty SPIR-V bytecode (`vector<uint32_t>`)

#### Scenario: Compilation error in GLSL
- **WHEN** GLSL source contains a syntax error
- **THEN** compilation fails and the error message includes the shaderc diagnostic output

### Requirement: Produce ShaderStageCode for each compiled stage
ShaderCompiler SHALL produce a `ShaderStageCode` struct for each compiled stage, containing the `ShaderStage` enum value and the SPIR-V bytecode.

#### Scenario: Compile vertex and fragment stages
- **WHEN** both `.vert` and `.frag` sources are compiled successfully
- **THEN** two `ShaderStageCode` entries are returned: one with `ShaderStage::Vertex` and one with `ShaderStage::Fragment`

### Requirement: ShaderImpl fulfills IShader interface
`ShaderImpl` SHALL implement `IShader` and MUST be constructible from compiled `ShaderStageCode` entries and reflection bindings. It SHALL provide `getAllStages()`, `getReflectionBindings()`, `findBinding()`, and `getProgramHash()`.

#### Scenario: getAllStages returns compiled stages
- **WHEN** a ShaderImpl is constructed with vertex + fragment ShaderStageCode
- **THEN** `getAllStages()` returns both stages with their respective bytecode

#### Scenario: getProgramHash is deterministic
- **WHEN** the same GLSL source and same variant set are compiled twice
- **THEN** `getProgramHash()` returns the same value both times
