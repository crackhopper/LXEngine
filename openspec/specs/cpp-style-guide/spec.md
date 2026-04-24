# C++ Style Guide Specification

## Purpose

This document defines the C++ coding standards and style guidelines for the game-dev/renderer project. These policies ensure consistent, maintainable, and safe code across the codebase.

## Requirements

### Requirement: No raw pointers for object references

Raw pointers (`T*`) SHALL NOT be used to hold references to other objects. This policy applies to all code paths except the two specific exceptions listed below.

**Rationale**: Raw pointers obscure ownership semantics, invite dangling pointer bugs, and make lifetime requirements unclear. Modern C++ provides superior alternatives.

#### Scenario: Non-owning dependency is represented by reference

- **WHEN** a class stores a mandatory non-owning dependency
- **THEN** it SHALL use `T&` instead of `T*`

#### Exceptions (Allowed Raw Pointer Usage)

1. **Handle/Identifier Pointers**: Pointers used as lookup keys or handles where the pointer value itself is the identifier (not dereferenced to access object state). Example: `void*` keys in maps.

2. **Interop with External C APIs**: When interfacing with C libraries that require pointer parameters for output values (e.g., `void** outPointer` patterns).

#### Required Replacements

| Instead of | Use | When |
|------------|-----|------|
| `T*` for owned objects | `std::unique_ptr<T>` | Single owner creating an object |
| `T*` for shared ownership | `std::shared_ptr<T>` | Multiple owners sharing an object |
| `T*` for non-owning reference | `T&` | Reference (always non-nullable, injected via constructor) |

### Requirement: Ownership semantics are explicit

Ownership relationships SHALL be expressed explicitly through smart pointers or references, never through ambiguous raw-pointer conventions.

1. **Single Ownership**: Use `std::unique_ptr<T>` when a class exclusively owns another object.

2. **Shared Ownership**: Use `std::shared_ptr<T>` when multiple entities share ownership of an object. Prefer `std::weak_ptr<T>` to break cycles or observe without owning.

3. **Non-Owning References**: Use references (`T&`) for mandatory non-owning references. All dependencies MUST be injected via constructor. No setter methods for dependency injection.

4. **No Ownership Transfer Ambiguity**: Function parameters and return types must have explicit ownership semantics. Do not use raw pointers for output parameters when the intent is to transfer ownership.

#### Scenario: Owned collaborator uses unique_ptr

- **WHEN** a class exclusively owns another object
- **THEN** that member SHALL be represented by `std::unique_ptr<T>`

### Requirement: Class members follow constructor-based ownership rules

Class members SHALL be initialized and wired through constructor-based ownership rules.

1. **Member Initialization**: Initialize all member variables in the constructor initializer list. Prefer member references over pointers for required dependencies.

2. **Const Members**: Use `const T&` for dependencies that a class does not modify.

3. **Mutable Members**: Only use `mutable` for cache/lazy-evaluation patterns where logical constness differs from physical constness.

#### Scenario: Required dependency is initialized in constructor

- **WHEN** a class depends on another object for its normal operation
- **THEN** that dependency SHALL be initialized in the constructor rather than via setter injection

### Requirement: Resources use RAII and smart-pointer factories

Resources SHALL be managed through RAII wrappers and smart-pointer factory helpers.

1. **RAII**: All resources (memory, handles, locks) must be managed via RAII wrappers. No manual `new`/`delete` except in factory methods or low-level wrappers.

2. **No Raw `new`/`delete`**: Avoid raw `new` and `delete`. Use `std::make_unique<T>()`, `std::make_shared<T>()`, or container `emplace_back()`.

3. **Smart Pointer Factory**: When creating objects that will be owned via `unique_ptr`, prefer `std::make_unique<T>()` over `new T()`.

#### Scenario: Heap resource is created through make_unique

- **WHEN** code allocates an owned heap object
- **THEN** it SHALL prefer `std::make_unique<T>()` over raw `new`

### Requirement: Type safety avoids low-level unsafe constructs

Type-safety-sensitive code SHALL avoid low-level unsafe constructs except where explicitly allowed.

1. **No `reinterpret_cast`** except in low-level Vulkan backend where required for API conformance. Document the reason.

2. **No `void*`** except as map keys (handles/identifiers) or with explicit documentation.

3. **Prefer `std::optional<T>`** over sentinel values (e.g., `-1`, `nullptr`) for nullable types that are not pointers.

#### Scenario: Nullable non-pointer value uses optional

- **WHEN** a value may be absent without implying pointer semantics
- **THEN** it SHALL use `std::optional<T>` instead of a sentinel constant

### Requirement: Modern C++ language features are used consistently

Modern C++ language features SHALL be used consistently where they clarify intent and safety.

1. **Use `[[nodiscard]]** on functions that must not ignore return values (e.g., factory methods returning `std::unique_ptr`).

2. **Use `override`** explicitly on overridden virtual methods.

3. **Use `final`** on classes not designed for inheritance.

4. **Prefer `constexpr`** for compile-time constants.

5. **Use `enum class`** instead of unscoped enums.

#### Scenario: Override is declared explicitly

- **WHEN** a derived class overrides a virtual function
- **THEN** the overriding declaration SHALL include `override`

### Requirement: Scalar types default to primitives, with rare alias exceptions

Project code SHALL prefer the shared primitive table in
`src/core/platform/types.hpp` over stacks of semantic typedef aliases.

**Rationale**: Plain aliases such as `VertexCount`, `ImageCount`, or
`DescriptorBindingIndex32` do not create type safety. They are still `usize` or
`u32` to the compiler, while increasing naming surface and refactor cost. The
default policy is therefore:

- ordinary counts and sizes use `usize`
- ordinary 32-bit indices, offsets, bindings, and dimensions use `u32`
- only a small number of protocol-heavy concepts keep dedicated aliases, such
  as `ShaderStageMask32` and `ApiVersion32`

If future work needs true misuse protection, it SHALL use wrapper structs or
strong types instead of adding more typedef aliases.

#### Required Usage Rules

1. **Counts and generic sizes use `usize`**: Collection sizes, element counts,
   cache sizes, and other host-side quantities SHALL use `usize` unless an
   external protocol or binary format requires a narrower width.

2. **Ordinary 32-bit scalar fields use `u32`**: Descriptor set numbers,
   bindings, locations, image dimensions, queue-family indices, offsets, and
   similar fixed-width numeric fields SHALL use `u32` unless they are one of
   the explicitly preserved aliases.

3. **Dedicated aliases stay rare**: Only stable, high-signal concepts whose
   name materially improves readability across subsystem boundaries SHOULD keep
   a dedicated alias. Current examples include `ShaderStageMask32` and
   `ApiVersion32`.

4. **Do not add semantic typedef layers for routine meanings**: New typedefs
   like `FooCount`, `BarIndex32`, or `BazSize32` SHALL NOT be introduced for
   ordinary scalar usage. Prefer variable names such as `bindingIndex`,
   `imageCount`, or `bufferSize` on top of `u32` / `usize`.

5. **Convert at external API boundaries**: When Vulkan, SDL, GLFW, ImGui,
   shaderc, SPIRV-Cross, or similar APIs require exact scalar widths, project
   code SHOULD keep `u32` / `usize` internally and cast at the boundary.

#### Allowed Exceptions

1. **Protocol-heavy aliases**: A dedicated alias MAY be kept when the code is
   primarily about a protocol-specific meaning rather than a routine scalar
   slot, and that meaning is shared across multiple subsystems.

2. **Strongly width-defined storage**: Packed bitfields, atomics, binary file
   layouts, and other storage contracts that are fundamentally width-defined
   MAY use the exact primitive width directly without wrapping it in another
   semantic alias.

#### Scenario: Counts and indices use primitives

```cpp
// CORRECT
usize getVertexCount() const;
usize getImageCount() const;
u32 bindingIndex = 0;

// INCORRECT
VertexCount getVertexCount() const;     // VIOLATION: routine count alias
ImageCount getImageCount() const;       // VIOLATION: routine count alias
DescriptorBindingIndex32 bindingIndex;  // VIOLATION: routine index alias
```

#### Scenario: External API conversion stays at the boundary

```cpp
// CORRECT
usize imageCount = swapchain->getImageCount();
initInfo.ImageCount = static_cast<u32>(imageCount);

// ALSO CORRECT
ShaderStageMask32 mask = static_cast<ShaderStageMask32>(ShaderStage::Vertex);
```

### Requirement: Ownership and dependency patterns follow the style guide

Ownership and dependency patterns SHALL follow the approved examples below.

#### Scenario: Class A creates and owns Class B

```cpp
// CORRECT
class A {
    std::unique_ptr<B> m_b;
public:
    A() : m_b(std::make_unique<B>()) {}
};

// INCORRECT - Raw pointer ownership
class A {
    B* m_b;
public:
    A() : m_b(new B()) {}  // VIOLATION
};
```

#### Scenario: Class A references Class B but does not own it (B outlives A)

```cpp
// CORRECT - Reference for non-owning, non-nullable reference
class A {
    B& m_b;
public:
    A(B& b) : m_b(b) {}
};

// INCORRECT - Raw pointer for non-nullable reference
class A {
    B* m_b;  // VIOLATION
public:
    A(B* b) : m_b(b) {}
};
```

#### Scenario: Dependency injection

```cpp
// CORRECT - Constructor injection with reference
class A {
    B& m_b;
public:
    A(B& b) : m_b(b) {}  // All dependencies via constructor
};

// INCORRECT - Setter injection with pointer (PROHIBITED)
class A {
    B* m_b = nullptr;
public:
    void setB(B* b) { m_b = b; }  // VIOLATION - setter injection not allowed
};

// INCORRECT - Pointer for non-nullable dependency (PROHIBITED)
class A {
    B* m_b;
public:
    A(B* b) : m_b(b) {}  // VIOLATION - use reference
};
```

### Requirement: The style guide is enforced in tooling and review

The style guide SHALL be enforced through tooling and review practices.

- **Linter**: Configured to warn on raw pointer usage (excluding documented exceptions)
- **Code Review**: Reviewers should verify ownership semantics are clear
- **Architecture Review**: New classes must document ownership of member objects

#### Scenario: Review rejects ambiguous ownership

- **WHEN** a code review encounters a new member with ambiguous ownership semantics
- **THEN** the change MUST be revised before approval

### Requirement: Layer dependencies remain isolated

Layer dependencies SHALL remain isolated according to the architecture boundaries below.

#### Core Layer Isolation

The core layer (`src/core/`) SHALL NOT depend on any external libraries (Vulkan, SDL, GLFW, shaderc, SPIRV-Cross, etc.). Core defines platform-agnostic interfaces, math, resource types, and scene structures using only C++ standard library types.

**Rationale**: Core types are consumed by all layers. External API dependencies would leak platform specifics upward and prevent backend substitution.

#### Allowed Dependencies by Layer

| Layer | Directory | Allowed Dependencies |
|-------|-----------|---------------------|
| **core** | `src/core/` | C++ standard library only |
| **infra** | `src/infra/` | core + external libraries (shaderc, SPIRV-Cross, stb, tinyobj, SDL/GLFW) |
| **backend** | `src/backend/` | core + graphics API (Vulkan) |

#### When Core Needs a Concept from an External API

If core needs to express a concept that maps to an external type (e.g., image formats, sample counts), it SHALL define its own enum or struct. The backend provides the mapping.

```cpp
// CORRECT — core defines its own enum
// src/core/frame_graph/render_target.hpp
namespace LX_core {
enum class ImageFormat : uint8_t { RGBA8, BGRA8, D32Float, D24S8 };
}

// backend maps to Vulkan
// src/backend/vulkan/...
VkFormat toVkFormat(LX_core::ImageFormat fmt);

// INCORRECT — core uses Vulkan type
#include <vulkan/vulkan.h>  // VIOLATION in core layer
VkFormat getFormat() const;
```

## Relationship to Vulkan Backend

The Vulkan backend (`src/backend/vulkan/`) follows these guidelines:
- `VulkanDevice` owns resources via `unique_ptr`
- All other Vulkan objects receive `VulkanDevice&` references (device outlives dependents)
- Handle types (VkDevice, VkBuffer, etc.) are not subject to this policy (opaque handles)
- `VulkanResourceManager` owns its render pass, pipeline, and command buffer manager

#### GPU Object Return Type Convention

GPU objects (objects wrapping Vulkan handles) MUST be returned via `std::unique_ptr<T>` from creation/factory functions, NOT by value. This ensures consistent ownership semantics and prevents object slicing.

```cpp
// CORRECT - Return unique_ptr for GPU objects
std::unique_ptr<VulkanCommandBuffer> allocateBuffer();
std::unique_ptr<VulkanBuffer> createBuffer(...);

// INCORRECT - Return by value (slicing risk, ownership ambiguous)
VulkanCommandBuffer allocateBuffer();  // VIOLATION
VulkanBuffer createBuffer(...);         // VIOLATION
```

#### CommandBuffer Design Principle

`VulkanCommandBuffer` MUST NOT hold references to manager objects (e.g., `VulkanResourceManager`). If a command buffer needs to access a manager for operations, the manager reference should be passed as a parameter to the operation rather than stored as a member.

**Rationale**: Command buffers are recording handles - they should be stateless with respect to managers. Holding manager references creates unnecessary coupling and complicates lifetime management.

```cpp
// CORRECT - Manager passed as operation parameter
void bindResources(VulkanResourceManager& resourceManager, ...);

// INCORRECT - Storing manager reference (PROHIBITED)
class VulkanCommandBuffer {
    VulkanResourceManager* m_resourceManager;  // VIOLATION
};
```

#### Scenario: Core layer defines its own external concepts

- **WHEN** core needs to express a concept that later maps to Vulkan or another external API
- **THEN** core SHALL define its own enum or struct instead of depending on external API types
