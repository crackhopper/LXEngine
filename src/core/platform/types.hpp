#pragma once

#include <stdint.h>
#include <cstddef>

typedef std::size_t             usize;

typedef uint8_t                 u8;
typedef uint16_t                u16;
typedef uint32_t                u32;
typedef uint64_t                u64;
typedef int8_t                  i8;
typedef int16_t                 i16;
typedef int32_t                 i32;
typedef int64_t                 i64;
typedef float                   f32;
typedef double                  f64;

// Semantic aliases stay backed by the primitive table above so the project can
// tighten policies later without touching every call site.

// Variable-width quantities and counts.
typedef usize                   ByteCount;
typedef usize                   VertexCount;
typedef usize                   IndexCount;
typedef usize                   ImageCount;
typedef usize                   BindingCount;
typedef usize                   CacheEntryCount;
typedef usize                   MemberCount;
typedef usize                   BoneCount;

// Fixed-width 32-bit sizes and offsets.
typedef u32                     DescriptorCount;
typedef u32                     ByteSize32;
typedef u32                     ResourceByteSize32;
typedef u32                     ByteOffset32;
typedef u32                     MeshIndex32;
typedef u32                     PerDrawByteSize32;

// Fixed-width indices, keys, and ids.
typedef u32                     FrameIndex32;
typedef u32                     QueueFamilyIndex32;
typedef u32                     SwapchainImageIndex32;
typedef u32                     DescriptorSetIndex32;
typedef u32                     DescriptorBindingIndex32;
typedef u32                     DescriptorLookupKey32;
typedef u32                     VertexAttributeLocation32;

// Fixed-width layout and rendering metadata.
typedef u32                     VertexStride32;
typedef u32                     VertexAttributeSize32;
typedef u32                     ShaderStageMask32;
typedef u32                     ImageDimension32;
typedef u32                     DescriptorPoolCount32;
typedef u32                     ApiVersion32;
typedef u32                     MemoryTypeIndex32;
typedef u32                     DescriptorSortKey32;

// Fixed-width narrow metadata.
typedef u8                      SampleCount8;

static const u64                u64_max = UINT64_MAX;
static const i64                i64_max = INT64_MAX;
static const u32                u32_max = UINT32_MAX;
static const i32                i32_max = INT32_MAX;
static const u16                u16_max = UINT16_MAX;
static const i16                i16_max = INT16_MAX;
static const u8                  u8_max = UINT8_MAX;
static const i8                  i8_max = INT8_MAX;

enum class GraphicsAPI {
  None = 0,
  Vulkan = 1,
  OpenGL = 2,
  DirectX = 3,
  Metal = 4,
};
