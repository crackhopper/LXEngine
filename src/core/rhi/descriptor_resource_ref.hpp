#pragma once

#include "core/asset/texture.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <functional>
#include <stdexcept>
#include <variant>
#include <vector>

namespace LX_core {

class GpuResourceRef final {
public:
  GpuResourceRef() = default;
  GpuResourceRef(const IGpuResource &resource)
      : m_resource(std::cref(resource)) {}

  [[nodiscard]] bool isValid() const {
    return std::visit(
        [](const auto &value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, std::monostate>) {
            return false;
          } else {
            return true;
          }
        },
        m_resource);
  }

  [[nodiscard]] const IGpuResource &get() const {
    return std::visit(
        [](const auto &value) -> const IGpuResource & {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, std::monostate>) {
            throw std::logic_error("GpuResourceRef is empty");
          } else {
            return value.get();
          }
        },
        m_resource);
  }

  [[nodiscard]] ResourceCacheIdentity getBackendCacheIdentity() const {
    return get().getBackendCacheIdentity();
  }
  [[nodiscard]] ResourceType getType() const { return get().getType(); }
  [[nodiscard]] StringID getBindingName() const {
    return get().getBindingName();
  }

private:
  std::variant<std::monostate, std::reference_wrapper<const IGpuResource>>
      m_resource;
};

class TextureSamplerRef final {
public:
  TextureSamplerRef() = default;
  TextureSamplerRef(const CombinedTextureSampler &texture)
      : m_texture(std::cref(texture)) {}

  [[nodiscard]] bool isValid() const {
    return std::visit(
        [](const auto &value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, std::monostate>) {
            return false;
          } else {
            return true;
          }
        },
        m_texture);
  }

  [[nodiscard]] const CombinedTextureSampler &get() const {
    return std::visit(
        [](const auto &value) -> const CombinedTextureSampler & {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, std::monostate>) {
            throw std::logic_error("TextureSamplerRef is empty");
          } else {
            return value.get();
          }
        },
        m_texture);
  }

  [[nodiscard]] ResourceCacheIdentity getBackendCacheIdentity() const {
    return get().getBackendCacheIdentity();
  }

private:
  std::variant<std::monostate,
               std::reference_wrapper<const CombinedTextureSampler>>
      m_texture;
};

class DescriptorResourceRef final {
public:
  enum class Kind : u8 {
    Resource,
    TextureArray,
  };

  DescriptorResourceRef() = default;
  DescriptorResourceRef(const IGpuResource &resource)
      : m_kind(Kind::Resource), m_resource(resource) {}

  [[nodiscard]] static DescriptorResourceRef
  textureArray(StringID bindingName, std::vector<TextureSamplerRef> textures) {
    DescriptorResourceRef ref;
    ref.m_kind = Kind::TextureArray;
    ref.m_bindingName = bindingName;
    ref.m_textures = std::move(textures);
    return ref;
  }

  [[nodiscard]] Kind kind() const { return m_kind; }
  [[nodiscard]] bool isResource() const { return m_kind == Kind::Resource; }
  [[nodiscard]] bool isTextureArray() const {
    return m_kind == Kind::TextureArray;
  }

  [[nodiscard]] const GpuResourceRef &resource() const { return m_resource; }
  [[nodiscard]] const std::vector<TextureSamplerRef> &textures() const {
    return m_textures;
  }

  [[nodiscard]] StringID getBindingName() const {
    return isTextureArray() ? m_bindingName : m_resource.getBindingName();
  }

private:
  Kind m_kind = Kind::Resource;
  GpuResourceRef m_resource;
  StringID m_bindingName;
  std::vector<TextureSamplerRef> m_textures;
};

using DescriptorResourceList = std::vector<DescriptorResourceRef>;

} // namespace LX_core
