module;

#include <cstddef>
#include <stdint.h>

export module LX_New_Common.Platform:Types;

export namespace LX_New_Common {

using usize = std::size_t;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f32 = float;
using f64 = double;
using ShaderStageMask32 = u32;
using ApiVersion32 = u32;

inline constexpr u64 u64_max = UINT64_MAX;
inline constexpr i64 i64_max = INT64_MAX;
inline constexpr u32 u32_max = UINT32_MAX;
inline constexpr i32 i32_max = INT32_MAX;
inline constexpr u16 u16_max = UINT16_MAX;
inline constexpr i16 i16_max = INT16_MAX;
inline constexpr u8 u8_max = UINT8_MAX;
inline constexpr i8 i8_max = INT8_MAX;

enum class GraphicsAPI {
  None = 0,
  Vulkan = 1,
  OpenGL = 2,
  DirectX = 3,
  Metal = 4,
};

} // namespace LX_New_Common
