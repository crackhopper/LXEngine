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
  ImageDimension32 width = 0;
  ImageDimension32 height = 0;
  TextureFormat format = TextureFormat::RGBA8;
};

/*
@source_analysis.section Texture：CPU 侧的薄图像资源容器
`Texture` 故意只承担一件事：把图像的结构描述和原始像素字节捆在一起。它不是 GPU
对象，也不直接实现 `IGpuResource`，更不知道 Vulkan 的存在。

这条边界回答的是“一张图像在进入渲染路径之前，最少需要暴露哪些事实”：

- `TextureDesc`：宽高和像素格式等结构元数据
- `m_data`：原始像素字节，按 `std::vector<u8>` 持有

因为 `Texture` 是纯 CPU 对象，它可以被 `TextureSharedPtr` 在多个材质、多个采样器
之间复用，而不会把“谁在哪个 binding 上用这张贴图”这件 GPU 路由信息污染进来。
MaterialInstance 侧真正用来绑定的，是下面的 `CombinedTextureSampler`。
*/
class Texture {
public:
  Texture(const TextureDesc &desc, std::vector<u8> &&data)
      : m_desc(desc), m_data(std::move(data)) {}

  const TextureDesc &desc() const { return m_desc; }
  const void *data() const { return m_data.data(); }
  ByteCount size() const { return m_data.size(); }

  void update(const std::vector<u8> &data) { m_data = data; }

private:
  TextureDesc m_desc;
  std::vector<u8> m_data; // CPU 内存图像数据
};

using TextureSharedPtr = std::shared_ptr<Texture>; // 共享使用

/*
@source_analysis.section createWhiteTexture：给 descriptor 路径的 1x1 兜底贴图
有些材质 binding 在场景装配时还没拿到真正的纹理，但 backend 的 descriptor 路径
不能接受空槽位。这个辅助函数产出一张全白的 1x1 RGBA8 贴图，作为默认填充：

- 形状合法：跟任何 RGBA8 采样路径兼容，不用单独为缺省态写分支
- 值中性：乘到材质颜色上等于保持原色
- 体积极小：1x1 贴图几乎不占显存，不会干扰 upload 预算

也就是说，它的角色不是“图像内容”，而是“缺省 binding 的占位形状”。
*/
static TextureSharedPtr createWhiteTexture(ImageDimension32 width = 1,
                                           ImageDimension32 height = 1) {
  return std::make_shared<Texture>(
      TextureDesc{width, height, TextureFormat::RGBA8},
      std::vector<u8>(width * height * 4, 255));
}

/*
@source_analysis.section CombinedTextureSampler：把纹理适配进统一 GPU 资源契约
这个类才是“纹理进入渲染路径”的实际节点。它包一个 `TextureSharedPtr`，再实现
`IGpuResource`，把 CPU 侧图像字节接进和 UBO、vertex buffer 同一条 backend
同步 / 绑定管线。

之所以 `Texture` 自己不实现 `IGpuResource`、要额外套一层 `CombinedTextureSampler`，
原因是这两类信息的归属和生命周期并不一样：

- `Texture` 属于资源加载侧，只要像素内容不变就可以被多处复用
- 绑定身份（`StringID` binding name）属于材质实例侧，不同材质会把同一张纹理
  绑到不同的 shader 变量上

把 binding name 放进采样器包装层之后，同一份 CPU 图像数据就能在多个材质里、以
不同 shader binding 名字各自独立走 descriptor 路径，而不需要复制像素。

`update()` 在改像素的同时显式 `setDirty()`，把“CPU 数据又变了”这个事实传给资源
同步阶段；backend 据此决定是否重新上传。采样器状态目前还是空壳，注释里保留了
后续扩展位。
*/
class CombinedTextureSampler : public IGpuResource {
public:
  explicit CombinedTextureSampler(TextureSharedPtr texture)
      : m_texture(texture) {}

  TextureSharedPtr texture() const { return m_texture; }

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
  ResourceByteSize32 getByteSize() const override {
    return static_cast<ResourceByteSize32>(m_texture->size());
  }

  StringID getBindingName() const override { return m_bindingName; }

private:
  TextureSharedPtr m_texture;
  StringID m_bindingName;
};

using CombinedTextureSamplerSharedPtr =
    std::shared_ptr<CombinedTextureSampler>;
} // namespace LX_core
