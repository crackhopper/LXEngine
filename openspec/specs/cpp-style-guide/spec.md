# C++ Style Guide Specification

## Purpose

This document defines the C++ coding standards and style guidelines for the game-dev/renderer project. These policies ensure consistent, maintainable, and safe code across the codebase.

## Core Principles

### Zero Raw Pointers for Object References

Raw pointers (`T*`) SHALL NOT be used to hold references to other objects. This policy applies to all code paths except the two specific exceptions listed below.

**Rationale**: Raw pointers obscure ownership semantics, invite dangling pointer bugs, and make lifetime requirements unclear. Modern C++ provides superior alternatives.

#### Exceptions (Allowed Raw Pointer Usage)

1. **Handle/Identifier Pointers**: Pointers used as lookup keys or handles where the pointer value itself is the identifier (not dereferenced to access object state). Example: `void*` keys in maps.

2. **Interop with External C APIs**: When interfacing with C libraries that require pointer parameters for output values (e.g., `void** outPointer` patterns).

#### Required Replacements

| Instead of | Use | When |
|------------|-----|------|
| `T*` for owned objects | `std::unique_ptr<T>` | Single owner creating an object |
| `T*` for shared ownership | `std::shared_ptr<T>` | Multiple owners sharing an object |
| `T*` for non-owning reference | `T&` | Reference (always non-nullable, injected via constructor) |

### Ownership Guidelines

1. **Single Ownership**: Use `std::unique_ptr<T>` when a class exclusively owns another object.

2. **Shared Ownership**: Use `std::shared_ptr<T>` when multiple entities share ownership of an object. Prefer `std::weak_ptr<T>` to break cycles or observe without owning.

3. **Non-Owning References**: Use references (`T&`) for mandatory non-owning references. All dependencies MUST be injected via constructor. No setter methods for dependency injection.

4. **No Ownership Transfer Ambiguity**: Function parameters and return types must have explicit ownership semantics. Do not use raw pointers for output parameters when the intent is to transfer ownership.

### Class Member Guidelines

1. **Member Initialization**: Initialize all member variables in the constructor initializer list. Prefer member references over pointers for required dependencies.

2. **Const Members**: Use `const T&` for dependencies that a class does not modify.

3. **Mutable Members**: Only use `mutable` for cache/lazy-evaluation patterns where logical constness differs from physical constness.

### Resource Management

1. **RAII**: All resources (memory, handles, locks) must be managed via RAII wrappers. No manual `new`/`delete` except in factory methods or low-level wrappers.

2. **No Raw `new`/`delete`**: Avoid raw `new` and `delete`. Use `std::make_unique<T>()`, `std::make_shared<T>()`, or container `emplace_back()`.

3. **Smart Pointer Factory**: When creating objects that will be owned via `unique_ptr`, prefer `std::make_unique<T>()` over `new T()`.

### Type Safety

1. **No `reinterpret_cast`** except in low-level Vulkan backend where required for API conformance. Document the reason.

2. **No `void*`** except as map keys (handles/identifiers) or with explicit documentation.

3. **Prefer `std::optional<T>`** over sentinel values (e.g., `-1`, `nullptr`) for nullable types that are not pointers.

### Modern C++ Features

1. **Use `[[nodiscard]]** on functions that must not ignore return values (e.g., factory methods returning `std::unique_ptr`).

2. **Use `override`** explicitly on overridden virtual methods.

3. **Use `final`** on classes not designed for inheritance.

4. **Prefer `constexpr`** for compile-time constants.

5. **Use `enum class`** instead of unscoped enums.

## Guideline Scenarios

### Scenario: Class A creates and owns Class B

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

### Scenario: Class A references Class B but does not own it (B outlives A)

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

### Scenario: Dependency injection

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

## Enforcement

- **Linter**: Configured to warn on raw pointer usage (excluding documented exceptions)
- **Code Review**: Reviewers should verify ownership semantics are clear
- **Architecture Review**: New classes must document ownership of member objects

## Layer Dependency Rules

### Core Layer Isolation

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
// src/core/resources/render_target.hpp
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

### GPU Object Return Type Convention

GPU objects (objects wrapping Vulkan handles) MUST be returned via `std::unique_ptr<T>` from creation/factory functions, NOT by value. This ensures consistent ownership semantics and prevents object slicing.

```cpp
// CORRECT - Return unique_ptr for GPU objects
std::unique_ptr<VulkanCommandBuffer> allocateBuffer();
std::unique_ptr<VulkanBuffer> createBuffer(...);

// INCORRECT - Return by value (slicing risk, ownership ambiguous)
VulkanCommandBuffer allocateBuffer();  // VIOLATION
VulkanBuffer createBuffer(...);         // VIOLATION
```

### CommandBuffer Design Principle

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
