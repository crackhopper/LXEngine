## ADDED Requirements

### Requirement: Camera matrices shall be correctly computed from position/target/up vectors

The Scene camera SHALL compute view matrix using a lookAt algorithm that:
- Takes camera position, target point, and up vector as inputs
- Computes correct orthonormal basis vectors (forward, right, up)
- Produces a valid view matrix that transforms world coordinates to camera space
- The inverse of this matrix SHALL correctly transform camera-space points to world space

The Scene camera SHALL compute projection matrix with:
- Field of view: 45 degrees
- Aspect ratio: width / height of the viewport (800.0 / 600.0 = 1.333...)
- Near plane: 0.1
- Far plane: 100.0
- Produces a valid projection matrix that transforms camera-space points to clip space

#### Scenario: Camera update produces correct view matrix
- **WHEN** camera position is (0, 0, 3), target is (0, 0, 0), up is (0, 1, 0)
- **THEN** view matrix SHALL transform world point (0, 0, 0) to camera space at (0, 0, -3)

#### Scenario: Camera update produces correct perspective projection
- **WHEN** camera has FOV 45°, aspect 1.333..., near 0.1, far 100
- **THEN** projection matrix SHALL transform point at depth -3 to normalized device coordinates within [-1, 1]

### Requirement: Renderer shall upload scene data to GPU memory correctly

The Renderer SHALL upload vertex buffer data to GPU-accessible memory:
- Buffer SHALL be created with VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
- Buffer SHALL be sized to hold all vertex data
- Data SHALL be copied from host memory to device memory via staging buffer
- Memory SHALL be bound to buffer with VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT

The Renderer SHALL upload index buffer data to GPU-accessible memory:
- Buffer SHALL be created with VK_BUFFER_USAGE_INDEX_BUFFER_BIT
- Buffer SHALL be sized to hold all index data
- Data SHALL be copied from host memory to device memory via staging buffer

#### Scenario: Vertex buffer upload preserves data integrity
- **WHEN** vertex buffer contains 3 vertices with positions (-5,5,0), (5,5,0), (5,-5,0)
- **THEN** after upload, GPU buffer SHALL contain identical vertex data

#### Scenario: Index buffer upload preserves data integrity
- **WHEN** index buffer contains indices {0, 1, 2}
- **THEN** after upload, GPU buffer SHALL contain indices {0, 1, 2}

### Requirement: Renderer shall issue correct draw commands for indexed rendering

The Renderer SHALL issue draw commands that:
- Bind correct vertex buffer to graphics pipeline before drawing
- Bind correct index buffer to graphics pipeline before drawing
- Call vkCmdDrawIndexed with correct index count (3 for triangle)
- First index value of 0

#### Scenario: Draw call renders correct triangle
- **WHEN** renderer has bound vertex buffer with 3 vertices and index buffer with {0, 1, 2}
- **THEN** vkCmdDrawIndexed SHALL be called with indexCount=3, firstIndex=0

### Requirement: Renderer shall validate pipeline state before drawing

The Renderer SHALL validate before issuing draw commands:
- Vertex buffer binding is valid and offset is 0
- Index buffer binding is valid with correct index type (uint32_t)
- Graphics pipeline is bound and valid
- All required descriptor sets are bound

#### Scenario: Draw fails fast on missing vertex buffer
- **WHEN** vertex buffer is not bound before draw
- **THEN** renderer SHALL report validation error before GPU hang
