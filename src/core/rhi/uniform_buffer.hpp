#pragma once
// UBO注意：确保std140对齐
// | 类型              | 对齐方式         | 说明                       |
// | --------------- | ------------ | ------------------------ |
// | `float`/`int`   | 4 bytes      | 标量占 4 bytes              |
// | `vec2`          | 8 bytes      | 2 * 4 bytes              |
// | `vec3` / `vec4` | 16 bytes     | vec3 自动填充到 vec4 对齐       |
// | `mat4`          | 16 bytes 对齐  | 每列 16 bytes，矩阵本身 16 字节对齐 |
// | struct          | 结构体对齐到最大成员对齐 | 结构体整体也要 16 bytes 对齐 |