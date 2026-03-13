#pragma once
#include "../math/vec.hpp"
#include "../platform/types.hpp"
#include <memory>
#include <vector>

namespace LX_core {
// 资源所属的pass
enum class ResourcePassFlag : u32 { 
  Forward = 0x00000001,
  Deferred = 0x00000002,
  Shadow = 0x00000004,
};

// 管线槽位id：主要用来绑定到后端pipeline的槽位上。
enum class PipelineSlotId : u16 {
  None = 0,   // 非pipeline槽位
  CameraUBO,   // 相机UBO参数
  MaterialUBO, // 材质UBO参数
  BoneUBO,     // 骨骼UBO参数
  AlbedoTexture,
  NormalTexture,
  MetallicRoughnessTexture,
  LightUBO,
  ShadowMap,
  Count
};
// 资源槽位类型，后端根据这个走不同的处理流程。
enum class ResourceType : u8 {
  None = 0,
  PushConstant,
  VertexBuffer,
  IndexBuffer,
  VertexShader,
  FragmentShader,
  DescriptorSet,
  Special,
  Count  
};


class IRenderResource {
public:
  virtual ~IRenderResource() = default;

  virtual ResourcePassFlag getPassFlag() const = 0;
  virtual ResourceType getType() const = 0;
  virtual const void *getRawData() const = 0;
  virtual u32 getByteSize() const = 0;

  virtual PipelineSlotId getPipelineSlotId() const {
    return PipelineSlotId::None;
  }

  // 资源的唯一标识符，用于在渲染管线中查找资源
  // 直接使用地址作为句柄
  void* getResourceHandle()const {return (void*)this;}
  

  bool isDirty() const { return isDirty_; }
  void setDirty() { isDirty_ = true; }
  void clearDirty() { isDirty_ = false; }

private:
  bool isDirty_ = false;
};

using IRenderResourcePtr = std::shared_ptr<IRenderResource>;


} // namespace LX_core