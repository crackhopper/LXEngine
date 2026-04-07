# 小型渲染器 (LX Renderer)

一个基于 Vulkan 的模块化渲染引擎，使用现代 C++20 开发。

## 架构设计

```
src/
├── core/                    # 核心模块（平台无关）
│   ├── gpu/                # GPU 接口定义
│   │   ├── renderer.hpp    # 渲染器抽象接口
│   │   └── render_resource.hpp  # 渲染资源基类
│   ├── math/               # 数学库（Vec3f, Mat4f, Quatf）
│   ├── platform/           # 平台抽象（Window, types）
│   ├── resources/          # 资源类型
│   │   ├── vertex_buffer.hpp   # 顶点缓冲（模板化，支持多种格式）
│   │   ├── index_buffer.hpp     # 索引缓冲
│   │   ├── texture.hpp          # 纹理
│   │   └── shader.hpp           # Shader 标记类
│   └── scene/              # 场景图
│       ├── scene.hpp       # 场景容器
│       ├── camera.hpp      # 相机（含 UBO）
│       ├── light.hpp       # 光照
│       ├── object.hpp      # IRenderable 接口 + PushConstant
│       └── components/     # 组件（Mesh, Material, Skeleton）
│
├── backend/       # 图形后端实现
│   └── vulkan/             # Vulkan 后端
│       ├── vk_renderer.hpp # VulkanRenderer 封装
│       └── details/        # 内部实现
│           ├── vk_device.hpp
│           ├── vk_resource_manager.hpp
│           ├── commands/   # 命令缓冲管理
│           ├── descriptors/ # 描述符管理
│           ├── pipelines/  # 渲染管线（BlinnPhong 等）
│           ├── render_objects/ # RenderPass, Framebuffer, Swapchain
│           └── resources/  # GPU 资源（Buffer, Texture, Shader）
│
├── infra/                  # 基础设施
│   ├── window/             # 窗口管理（SDL/GLFW）
│   ├── gui/                # ImGui 集成
│   └── mesh_loader/        # 网格加载器
│
└── test/                   # 测试程序
    └── test_render_triangle.cpp  # 三角形渲染测试
```

## 核心概念

### 渲染资源体系

```
IRenderResource (抽象基类)
├── VertexBuffer<VType>     # 模板顶点缓冲
├── IndexBuffer             # 索引缓冲
├── UniformBuffer (UBO)     # 统一缓冲
├── CombinedTextureSampler  # 纹理 + 采样器
├── Shader (Vertex/Fragment) # Shader 标记
└── PushConstant (ObjectPC) # Push 常数
```

### 顶点格式

支持多种顶点格式（通过模板）:
- `VertexPos` - 仅位置
- `VertexPosColor` - 位置 + 颜色
- `VertexPosUV` - 位置 + UV
- `VertexNormalTangent` - 法线 + 切线
- `VertexBoneWeight` - 骨骼权重
- `VertexPosNormalUvBone` - 完整格式（位置 + 法线 + UV + 切线 + 骨骼）

### 场景图

```
Scene
├── mesh: IRenderablePtr     # 可渲染对象
├── camera: CameraPtr         # 相机（含 view/proj 矩阵）
└── directionalLight         # 方向光

IRenderable (接口)
└── RenderableSubMesh<VType> # 具体实现
    ├── mesh: MeshPtr<VType> # 网格数据
    ├── material: MaterialPtr # 材质
    ├── skeleton: SkeletonPtr # 骨骼（可选）
    └── objectPC: ObjectPCPtr # Push Constant
```

### 材质系统

```
MaterialBase (抽象)
└── MaterialBlinnPhong      # Blinn-Phong 材质
    ├── MaterialBlinnPhongUBO (baseColor, shininess, etc.)
    ├── albedoMap: CombinedTextureSampler
    └── normalMap: CombinedTextureSampler
```

## 构建

### 前置依赖

- CMake 3.16+
- Visual Studio 2022 或 GCC/Clang (C++20)
- Vulkan SDK 1.4+
- SDL3 或 GLFW

### 构建命令

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Debug
```

### 运行测试

```bash
# 三角形渲染测试
./build/test_render_triangle.exe
```

## 项目状态

### 已实现

- ✅ 核心类型系统（Vec3f, Mat4f, Quatf）
- ✅ 渲染资源抽象（VertexBuffer, IndexBuffer, Texture, Shader）
- ✅ 场景图基础结构（Scene, Camera, IRenderable）
- ✅ Vulkan 后端框架（设备、命令缓冲、描述符、流水线）
- ✅ 材质系统（Blinn-Phong）

### 待实现

- ⏳ 渲染循环完整实现（uploadData, draw 方法）
- ⏳ 骨骼动画系统
- ⏳ PBR 材质
- ⏳ 延迟渲染管线
- ⏳ 阴影映射


## 遗留问题
### 动态pipeline管理、pipeline cache。
下面都是比较混乱的想法摘要。还没有梳理。

1. 定义pipelineKey， 并在 RenderingItem 增加pipelineKey。
2. 增加 通过 Material，网格，推断pipelineKey的能力。
3. 增加 Pipeline 工厂，负责根据 PipelineKey 创建对应 Pipeline 实例。
  - 针对 shader name 和 variant ，最好其实代码里定义动态编译并写入文件夹形成不同的spv文件。这样首先可以从缓存文件夹查找，找不到再编译，然后写入缓存文件夹。

整体对于冬天构建pipeline的思考：
1. 由mesh格式，导出顶点格式
2. pipeline其他待设定变量，先简化处理。准备一个类+默认值。（方便未来数据驱动）
3. 用户自定义UBO或者其他descriptorset。提供一个yaml文件，根据这个我来创建pipeline slot。
4. shader文件，用户自己保证和ubo一致性。我负责根据 mesh 格式，得到预编译定义，从而得到实际的编译后的path。
5. 由 1，2，3，4；我可以定义一个hash和==，作为pipeline key。并根据情况，动态创建对应的pipeline，并cache在resource manager中。


相关类的设想：
```cpp

// 现状：硬编码在 VkPipelineBlinnPhong
// 未来：数据驱动

struct PipelineConfig {
  // Shader
  std::string vertShader = "unlit";    // 映射到 shader name
  std::string fragShader = "unlit";
  
  // 图元
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  
  // 光栅化（默认值）
  RasterizerConfig rasterizer{
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_BACK_BIT,
    // ...
  };
  
  // 深度模板（默认值）
  DepthStencilConfig depthStencil{
    .depthTest = true,
    .depthWrite = true,
    // ...
  };
  
  // 混合（默认值）
  BlendConfig blend{
    .blendEnable = false,
    // ...
  };
};

/**
 * Pipeline 类型枚举 - 标识渲染管线的算法/用途类型
 * 每个类型对应一套完整的渲染策略（顶点处理、片段处理、材质槽位等）
 */
enum class PipelineType : uint8_t {
  None = 0,
  BlinnPhong,      // 基础 Blinn-Phong 光照
  PBR,             // 基于物理的渲染
  Shadow,          // 阴影图生成
  Skybox,          // 天空盒
  PostProcess,    // 后处理
  // 可扩展...
};
/**
 * Shader 变体标识 - 区分同一 PipelineType 下的不同 Shader 组合
 * 
 * 典型场景:
 *   - 不同骨骼权重数量 (non-skinned vs skinned)
 *   - 不同光照模型变体
 *   - 不同顶点格式
 */
struct ShaderVariant {
  std::string baseName;      // e.g., "blinnphong"
  uint32_t variantId;        // e.g., 0 = non-skinned, 1 = skinned
  
  bool operator==(const ShaderVariant& other) const {
    return baseName == other.baseName && variantId == other.variantId;
  }
};
/**
 * Pipeline 查找键 - 唯一确定一个 VulkanPipeline
 * 
 * 设计原则:
 *   (PipelineType, ShaderVariant) → 唯一 VulkanPipeline 实例
 *   同一个 PipelineType 不同 ShaderVariant 会创建不同的 Pipeline 对象
 */
struct PipelineKey {
  PipelineType type;
  ShaderVariant variant;
  
  std::string toString() const {
    return std::to_string(static_cast<uint8_t>(type)) + "_" 
           + variant.baseName + "_" + std::to_string(variant.variantId);
  }
  
  bool operator==(const PipelineKey& other) const {
    return type == other.type && variant == other.variant;
  }
};

/**
 * Pipeline 工厂 - 负责根据 PipelineKey 创建对应 Pipeline 实例
 * 
 * 设计原则：
 *   每种 PipelineType 对应一个 CreateXXX 函数
 *   新增 Pipeline 类型只需添加新函数并注册到 g_creators 表
 */
class PipelineFactory {
public:
  using CreatorFunc = std::function<VulkanPipelinePtr(VulkanDevice&, VkExtent2D)>;
  
  static PipelineFactory& instance() {
    static PipelineFactory inst;
    return inst;
  }
  
  // 注册创建函数
  void registerCreator(PipelineType type, CreatorFunc creator) {
    g_creators[static_cast<uint8_t>(type)] = std::move(creator);
  }
  
  // 创建 Pipeline
  VulkanPipelinePtr create(PipelineType type, VulkanDevice& device, VkExtent2D extent) {
    auto it = g_creators.find(static_cast<uint8_t>(type));
    if (it == g_creators.end()) {
      throw std::runtime_error("Unknown PipelineType: " + std::to_string(static_cast<uint8_t>(type)));
    }
    return it->second(device, extent);
  }
  
private:
  PipelineFactory() {
    // 注册内置 Pipeline 创建函数
    g_creators[static_cast<uint8_t>(PipelineType::BlinnPhong)] = 
        [](VulkanDevice& dev, VkExtent2D ext) { return VkPipelineBlinnPhong::create(dev, ext); };
    // g_creators[static_cast<uint8_t>(PipelineType::PBR)] = ...
  }
  
  std::unordered_map<uint8_t, CreatorFunc> g_creators;
};
```