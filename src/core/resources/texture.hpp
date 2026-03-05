#pragma once
#include "../platform/types.hpp"
#include <memory>
#include <string>
#include <vector>

namespace LX_core {

enum class TextureFormat {
  RGBA8,
  RGB8,
  R8,
  // 以后可以扩展 HDR、Float 等
};

struct TextureDesc {
  u32 width = 0;
  u32 height = 0;
  TextureFormat format = TextureFormat::RGBA8;
};

class Texture {
public:
  Texture(const TextureDesc &desc, std::vector<u8> &&data)
      : m_desc(desc), m_data(std::move(data)) {}

  const TextureDesc &desc() const { return m_desc; }
  const void *data() const { return m_data.data(); }
  size_t size() const { return m_data.size(); }

private:
  TextureDesc m_desc;
  std::vector<u8> m_data; // CPU 内存图像数据
};

using TexturePtr = std::shared_ptr<Texture>; // 共享使用
} // namespace LX_core