## 1. Debug Camera Matrix Calculation

- [ ] 1.1 Add debug output for camera position, target, and up vectors before updateMatrices()
- [ ] 1.2 Add debug output for view matrix after updateMatrices() - print all 16 elements
- [ ] 1.3 Add debug output for projection matrix - verify FOV=45°, aspect=1.333..., near=0.1, far=100
- [ ] 1.4 Verify view matrix correctly transforms world origin to camera space at (0,0,-3)
- [ ] 1.5 If matrices are wrong, examine and fix Mat4f::lookAt or Mat4f::perspective implementation

## 2. Debug Vertex/Index Buffer Upload

- [ ] 2.1 Add debug output to verify vertex buffer size matches expected 3 vertices × stride
- [ ] 2.2 Add debug output to verify index buffer contains {0, 1, 2}
- [ ] 2.3 Verify VK_BUFFER_USAGE_VERTEX_BUFFER_BIT is set on vertex buffer creation
- [ ] 2.4 Verify VK_BUFFER_USAGE_INDEX_BUFFER_BIT is set on index buffer creation
- [ ] 2.5 Verify memory is allocated with VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
- [ ] 2.6 Verify vkCmdBindVertexBuffers is called with correct buffer and offset before draw

## 3. Debug Draw Command Issuance

- [ ] 3.1 Verify vkCmdBindIndexBuffer is called with correct index buffer and indexType=VK_INDEX_TYPE_UINT32
- [ ] 3.2 Verify vkCmdDrawIndexed is called with indexCount=3, firstIndex=0, vertexOffset=0
- [ ] 3.3 Verify graphics pipeline is bound before vkCmdDrawIndexed
- [ ] 3.4 Add validation layer checks to catch any unbound resources before draw

## 4. Fix and Verify

- [ ] 4.1 Apply fixes based on debug findings
- [ ] 4.2 Run test_render_triangle and verify triangle renders correctly
- [ ] 4.3 Remove debug output once fix is confirmed
