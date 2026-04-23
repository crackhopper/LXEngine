#pragma once
#include "core/rhi/gpu_resource.hpp"
#include "core/platform/types.hpp"
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

  void update(const std::vector<u8> &data) { m_data = data; }

private:
  TextureDesc m_desc;
  std::vector<u8> m_data; // CPU 内存图像数据
};

using TexturePtr = std::shared_ptr<Texture>; // 共享使用

static TexturePtr createWhiteTexture(u32 width = 1, u32 height = 1) {
  return std::make_shared<Texture>(
      TextureDesc{width, height, TextureFormat::RGBA8},
      std::vector<u8>(width * height * 4, 255));
}

// 组合纹理采样器，包含纹理指针和采样器信息。
// 它直接实现 IGpuResource，因为 material/backend 绑定阶段需要拿到的
// 是“可上传、可按 binding name 路由”的资源，而不是纯 Texture 数据对象。
// TODO: 暂时空余采样器信息。
class CombinedTextureSampler : public IGpuResource {
public:
  explicit CombinedTextureSampler(TexturePtr texture) : m_texture(texture) {}

  TexturePtr texture() const { return m_texture; }

  void update(const std::vector<u8> &data) {
    m_texture->update(data);
    setDirty();
  }

  /// `MaterialInstance::getDescriptorResources()` fills this with the binding
  /// name resolved from the template before handing the texture off to the
  /// backend descriptor path. Empty until the material routes it.
  void setBindingName(StringID name) { m_bindingName = name; }

  ResourceType getType() const override {
    return ResourceType::CombinedImageSampler;
  }
  const void *getRawData() const override { return m_texture->data(); }
  u32 getByteSize() const override { return m_texture->size(); }

  StringID getBindingName() const override { return m_bindingName; }

private:
  TexturePtr m_texture;
  StringID m_bindingName;
};

using CombinedTextureSamplerPtr = std::shared_ptr<CombinedTextureSampler>;
} // namespace LX_core
