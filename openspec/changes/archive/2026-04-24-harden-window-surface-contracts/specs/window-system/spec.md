## ADDED Requirements

### Requirement: Window resize updates are backend-consistent
All window backend implementations SHALL surface resize and minimize state through the public `updateSize` contract. A backend MUST NOT keep working resize logic hidden only in a private implementation path.

#### Scenario: SDL and GLFW both update public size state
- **WHEN** the engine polls `updateSize` on either backend
- **THEN** the caller receives current close, width, and height state through the same public contract

### Requirement: Graphics handles use one ownership model
All window backends SHALL expose graphics handles through one ownership model understood by renderer initialization and teardown. A backend MUST NOT return a heap-allocated wrapper where another backend returns a raw handle value for the same API.

#### Scenario: createGraphicsHandle has backend-independent meaning
- **WHEN** the renderer requests a Vulkan graphics handle from SDL or GLFW
- **THEN** both backends return equivalent handle semantics and support matching destruction behavior
