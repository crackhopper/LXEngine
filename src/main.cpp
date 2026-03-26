int main(){
  return 0;
}

//   std::vector<Vertex> vertices;
//   std::vector<uint32_t> indices; // 注意更改到了 uint32_t

//   void loadModel() {
//     tinyobj::attrib_t attrib;
//     std::vector<tinyobj::shape_t> shapes;
//     std::vector<tinyobj::material_t> materials;
//     std::string warn;
//     std::string err;

//     if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
//                           MODEL_PATH.c_str())) {
//       throw std::runtime_error(err);
//     }

//     // OBJ 文件，包含： 位置(positions)、法项(normals)、纹理坐标(texture coords)
//     // 保存在 `attrib.vertices` , `attrib.normals` 和 `attrib.texcoords`
//     // (整体的数据)
//     //
//     // `shapes` 包含了所有对象以及它们的面(face)的索引信息。
//     // 每个面包含了一组顶点，每个顶点包含了对应的 position, normal 和 texture
//     // coords 信息。 OBJ文件还可以对每个face定义对应的material 和
//     // texture。我们暂时i忽略这些。

//     // 这里面我们用一个 unorder_map (hashmap)
//     // ，以顶点作为key，来确保顶点数据唯一性。
//     std::unordered_map<Vertex, uint32_t> uniqueVertices{};
//     // 我们接下来要把所有的face拼接到一个顶点数据中。
//     for (const auto &shape : shapes) {
//       for (const auto &index : shape.mesh.indices) {
//         Vertex vertex{};
//         // 由于 attrib.vertices 中是展开保存的数据，所以我们要用 3*idx+i
//         // 的方式取数值
//         vertex.pos = {attrib.vertices[3 * index.vertex_index + 0],
//                       attrib.vertices[3 * index.vertex_index + 1],
//                       attrib.vertices[3 * index.vertex_index + 2]};
//         // 纹理坐标类似
//         // vertex.texCoord = {attrib.texcoords[2 * index.texcoord_index + 0],
//         //                    attrib.texcoords[2 * index.texcoord_index + 1]};
//         vertex.texCoord = {
//             attrib.texcoords[2 * index.texcoord_index + 0],
//             // vulkan的纹理坐标y方向和OBJ文件定义的Y方向并不一样。因此需要反转y坐标
//             1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};

//         vertex.color = {1.0f, 1.0f, 1.0f};

//         if (uniqueVertices.count(vertex) == 0) {
//           uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
//           vertices.push_back(vertex);
//         }
//         indices.push_back(uniqueVertices[vertex]);
//       }
//     }
//   }


//!


//   VkImage colorImage;
//   VkDeviceMemory colorImageMemory;
//   VkImageView colorImageView;

//   void createColorResources() {
//     VkFormat colorFormat = swapChainImageFormat;

//     createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples,
//                 colorFormat, VK_IMAGE_TILING_OPTIMAL,
//                 VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
//                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
//                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage,
//                 colorImageMemory);
//     colorImageView =
//         createImageView(colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
//   }

//   VkImage depthImage;
//   VkDeviceMemory depthImageMemory;
//   VkImageView depthImageView;

//   void createDepthResources() {
//     VkFormat depthFormat = findDepthFormat();
//     createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples,
//                 depthFormat, VK_IMAGE_TILING_OPTIMAL,
//                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
//                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage,
//                 depthImageMemory);

//     depthImageView =
//         createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

//     // 手动显式转化layout （实际上并不需要，renderPass会自动搞定，这里只是演示）
//     transitionImageLayout(depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
//                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
//   }

//   // 判断选择的格式是否支持stencil(模板)
//   bool hasStencilComponent(VkFormat format) {
//     return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
//            format == VK_FORMAT_D24_UNORM_S8_UINT;
//   }


//   VkBuffer stagingBuffer;
//   VkDeviceMemory stagingBufferMemory;

//   void createTextureImage() {
//     int texWidth, texHeight, texChannels;
//     // 加载图像像素为一个指针
//     // stbi_uc *pixels = stbi_load("textures/texture.jpg"
//     // ,&texWidth,&texHeight,&texChannels, STBI_rgb_alpha);
//     stbi_uc *pixels = stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight,
//                                 &texChannels, STBI_rgb_alpha);

//     mipLevels = static_cast<uint32_t>(
//                     std::floor(std::log2(std::max(texWidth, texHeight)))) +
//                 1;

//     VkDeviceSize imageSize = texWidth * texHeight * 4;

//     if (!pixels) {
//       throw std::runtime_error("failed to load texture image!");
//     }

//     // 创建vkBuffer
//     createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
//                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
//                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//                  stagingBuffer, stagingBufferMemory);
//     // 复制pixel到我们的buffer
//     void *data;
//     vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
//     memcpy(data, pixels, static_cast<size_t>(imageSize));
//     vkUnmapMemory(device, stagingBufferMemory);

//     // 释放掉我们用stb加载的image
//     stbi_image_free(pixels);

//     createImage(
//         texWidth, texHeight, mipLevels, VK_SAMPLE_COUNT_1_BIT,
//         VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
//         VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
//             VK_IMAGE_USAGE_SAMPLED_BIT,
//         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);

//     transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
//                           VK_IMAGE_LAYOUT_UNDEFINED,
//                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
//     copyBufferToImage(stagingBuffer, textureImage,
//                       static_cast<uint32_t>(texWidth),
//                       static_cast<uint32_t>(texHeight));
//     // 当然最后还要cleanup
//     // transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
//     //                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
//     //                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
//     //                       mipLevels);

//     generateMipmaps(textureImage, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight,
//                     mipLevels);

//     vkDestroyBuffer(device, stagingBuffer, nullptr);
//     vkFreeMemory(device, stagingBufferMemory, nullptr);
//   }

//   void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth,
//                        int32_t texHeight, uint32_t mipLevels) {
//     VkFormatProperties formatProperties;
//     vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat,
//                                         &formatProperties);

//     if (!(formatProperties.optimalTilingFeatures &
//           VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
//       throw std::runtime_error(
//           "textureimage format unsupport linear blitting!");
//     }
//     VkCommandBuffer commandBuffer = beginSingleTimeCommands();

//     VkImageMemoryBarrier barrier{};
//     barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
//     barrier.image = image;
//     barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//     barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//     barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//     barrier.subresourceRange.baseArrayLayer = 0;
//     barrier.subresourceRange.layerCount = 1;
//     barrier.subresourceRange.levelCount = 1;

//     // 复用上面的barrier，来做若干次转化

//     int32_t mipWidth = texWidth;
//     int32_t mipHeight = texHeight;

//     for (uint32_t i = 1; i < mipLevels; i++) {
//       // 注意循环从1开始
//       barrier.subresourceRange.baseMipLevel = i - 1;
//       // 上一级mip level计算结束后，其布局为
//       // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
//       barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
//       // 本次执行前，上一级mip level需要切换为
//       // VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
//       barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
//       // 上一级mip level计算结束的动作，VK_ACCESS_TRANSFER_WRITE_BIT;
//       barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
//       // 本次执行前，上一级mip level需要准备好VK_ACCESS_TRANSFER_READ_BIT
//       barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

//       // 记录这个barrier，让布局转化生效。
//       vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
//                            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
//                            nullptr, 1, &barrier);

//       // 布局准备好了，我们可以执行 blit了
//       // source level: i-1 , dest level: i
//       VkImageBlit blit{};

//       // 表示源区域的定义：
//       // 区域覆盖的像素为 [offset0, offset1)。

//       // 源区域的最小角（offset0）
//       // 包含(x,y,z)的坐标，表示源区域的起始 texel（**inclusive**）
//       // 这里设置为 (0,0,0) 表示从源 mip 的左上角（或原点）开始。
//       blit.srcOffsets[0] = {0, 0, 0};
//       // 源区域的最大角（offset1），表示源区域的结束坐标（**exclusive**）。
//       // 这里表示 到 源 mip 的右下角结束。
//       blit.srcOffsets[1] = {mipWidth, mipHeight, 1};

//       // 指定源图像子资源的“面”(aspect)。对于彩色纹理通常是 COLOR_BIT。
//       blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//       // 源子资源使用的 mip 级别。这里是 i-1，说明源是上一级（更高分辨率）的 mip
//       // 级别。
//       blit.srcSubresource.mipLevel = i - 1;
//       // 源的起始 array layer（针对 array textures 或 cubemaps），
//       // 这里从 layer 0 开始。
//       blit.srcSubresource.baseArrayLayer = 0;
//       // 源使用的层数（从 baseArrayLayer 开始的连续层数）。这里是 1，表示只 blit
//       // 单层。
//       blit.srcSubresource.layerCount = 1;

//       // 目标区域和一些参数定义，可以参见源区域定义来类比。
//       blit.dstOffsets[0] = {0, 0, 0};
//       blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
//                             mipHeight > 1 ? mipHeight / 2 : 1, 1};
//       blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//       blit.dstSubresource.mipLevel = i;
//       blit.dstSubresource.baseArrayLayer = 0;
//       blit.dstSubresource.layerCount = 1;

//       // 执行blit操作
//       vkCmdBlitImage(commandBuffer,
//                      // 源图像，源图像布局
//                      image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
//                      // 目标图像，目标图像布局
//                      image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
//                      1,               // region count
//                      &blit,           /// blit参数列表（对应 region count）
//                      VK_FILTER_LINEAR // 使用的滤波器
//       );
//       // 注意，blit操作必须被提交到具备 graphics能力的队列。

//       // blit结束后，再将layout转化为 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
//       barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
//       barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
//       barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
//       barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

//       // 向pipeline中插入barrier（指定具体的源阶段和目标阶段，然后插入barrier）
//       // barrier中定义了等待的动作
//       vkCmdPipelineBarrier(
//           commandBuffer,
//           VK_PIPELINE_STAGE_TRANSFER_BIT,        // 源阶段阶段掩码（必须完成）
//           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // 目标阶段掩码（要进入）
//           0,            // 队列内同步；如果有其他subpass依赖，这里要指定
//           0, nullptr,   // 用于全局（buffer/image 通用）的内存依赖
//           0, nullptr,   // 用于 buffer 的同步和 layout/ownership 管理
//           1, &barrier); // 用于image同步。

//       // 循环中不断缩小 mipWidth和mipHeight
//       if (mipWidth > 1)
//         mipWidth /= 2;
//       if (mipHeight > 1)
//         mipHeight /= 2;
//     }
//     // 最后1个level，还没有做布局转化，补上
//     barrier.subresourceRange.baseMipLevel = mipLevels - 1;
//     barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
//     barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
//     barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
//     barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

//     vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
//                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
//                          0, nullptr, 1, &barrier);

//     endSingleTimeCommands(commandBuffer);
//   }


//   uint32_t mipLevels;
//   VkImage textureImage;
//   VkDeviceMemory textureImageMemory;

//   void createImage(
//       uint32_t width, uint32_t height, uint32_t mipLevels,
//       VkSampleCountFlagBits numSamples,
//       VkFormat format,                  // 像素格式，如 R8G8B8A8_SRGB
//       VkImageTiling tiling,             // 内存布局 （决定硬件访问效率）
//       VkImageUsageFlags usage,          // 用途
//       VkMemoryPropertyFlags properties, // 内存属性 (如，HOST_VISIBLE之类的)
//       VkImage &image,                   // 输出: 创建好的图像对象
//       VkDeviceMemory &imageMemory       // 输出: 绑定的显存对象
//   ) {
//     VkImageCreateInfo imageInfo{};
//     imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
//     // 图像类型：二维纹理
//     imageInfo.imageType = VK_IMAGE_TYPE_2D;
//     imageInfo.extent.width = width;
//     imageInfo.extent.height = height;
//     // 图像深度，二维图像为1
//     imageInfo.extent.depth = 1;
//     // mipmap层数
//     imageInfo.mipLevels = mipLevels;
//     // 图像数组层数，单张图片为1
//     imageInfo.arrayLayers = 1;

//     imageInfo.format = format;
//     imageInfo.tiling = tiling;
//     // GPU无法直接使用初始数据，第一次写入会覆盖现有内容。
//     imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
//     imageInfo.usage = usage;
//     // 目标图像是否支持多采样
//     imageInfo.samples = numSamples;
//     // 独占队列访问（仅图形队列）
//     imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

//     if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
//       throw std::runtime_error("failed to create image!");
//     }

//     // 查询图像内存需求
//     VkMemoryRequirements memRequirements;
//     vkGetImageMemoryRequirements(device, image, &memRequirements);

//     // 配置内存分配信息
//     VkMemoryAllocateInfo allocInfo{};
//     allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
//     allocInfo.allocationSize = memRequirements.size;
//     allocInfo.memoryTypeIndex =
//         findMemoryType(memRequirements.memoryTypeBits, properties);

//     if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) !=
//         VK_SUCCESS) {
//       throw std::runtime_error("failed to allocate image memory!");
//     }
//     // 将分配好的显存绑定到图像对象上
//     vkBindImageMemory(device, image, imageMemory, 0);
//   }

//   VkImageView textureImageView;
//   VkSampler textureSampler;
//   void createTextureImageView() {
//     textureImageView = createImageView(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
//                                        VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
//   }

//   void createTextureSampler(bool useMipMap = true) {
//     VkSamplerCreateInfo samplerInfo{};
//     samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
//     // 针对 oversampling (简记：纹理过小)
//     samplerInfo.magFilter = VK_FILTER_LINEAR;
//     // 针对 undersampling (简记：纹理过大)
//     samplerInfo.minFilter = VK_FILTER_LINEAR;

//     // 超出区域的采样方式
//     samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
//     samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
//     samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

//     // 各向异性过滤
//     samplerInfo.anisotropyEnable = VK_TRUE;
//     // 需要查询设备获取最大各向异性采样的限制
//     VkPhysicalDeviceProperties properties{};
//     vkGetPhysicalDeviceProperties(physicalDevice, &properties);
//     // 最多使用的采样点的限制
//     samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
//     // 如果gpu不支持，那么我们也要关闭这个功能
//     // samplerInfo.anisotropyEnable = VK_FALSE;
//     // samplerInfo.maxAnisotropy = 1.0f;

//     // 仅在 address mode 是 clamp to border的时候有效。
//     samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
//     // 使用sampler的坐标范围
//     // - True : [0,texWidth) x [0, texHeight)
//     // - False : [0,1) x [0,1)
//     samplerInfo.unnormalizedCoordinates = VK_FALSE;

//     // 采样结果用来先做 Compare OP，随后的结果用来做filtering
//     // 常用来做 percentage closer filtering (PCF)
//     // https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing
//     samplerInfo.compareEnable = VK_FALSE;
//     samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

//     // mipmap滤波配置
//     if (useMipMap) {
//       samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
//       samplerInfo.minLod = 0.0f; // Optional
//       samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
//       if (GLOBAL_CONTROL_USE_MIPLODBIAS) {
//         samplerInfo.mipLodBias = GLOBAL_CONTROL_MIPLODBIAS;
//       } else {
//         samplerInfo.mipLodBias = 0.0f; // Optional
//       }
//     } else {
//       samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
//       samplerInfo.mipLodBias = 0.0f;
//       samplerInfo.minLod = 0.0f;
//       samplerInfo.maxLod = 0.0f;
//     }

//     if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) !=
//         VK_SUCCESS) {
//       throw std::runtime_error("failed to create texture sampler!");
//     }
//   }



//   LX_graphics::SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
//     LX_graphics::SwapChainSupportDetails details;
//     vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface,
//                                               &details.capabilities);
//     uint32_t formatCount;
//     vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount,
//                                          nullptr);

//     if (formatCount != 0) {
//       details.formats.resize(formatCount);
//       vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount,
//                                            details.formats.data());
//     }

//     uint32_t presentModeCount;
//     vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface,
//                                               &presentModeCount, nullptr);

//     if (presentModeCount != 0) {
//       details.presentModes.resize(presentModeCount);
//       vkGetPhysicalDeviceSurfacePresentModesKHR(
//           device, surface, &presentModeCount, details.presentModes.data());
//     }
//     return details;
//   }

//   VkSurfaceFormatKHR chooseSwapSurfaceFormat(
//       const std::vector<VkSurfaceFormatKHR> &availableFormats) {
//     for (const auto &availableFormat : availableFormats) {
//       if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
//           availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
//         return availableFormat;
//       }
//     }

//     return availableFormats[0];
//   }

//   VkPresentModeKHR chooseSwapPresentMode(
//       const std::vector<VkPresentModeKHR> &availablePresentModes) {
//     for (const auto &availablePresentMode : availablePresentModes) {
//       if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
//         return availablePresentMode;
//       }
//     }

//     return VK_PRESENT_MODE_FIFO_KHR;
//   }

//   VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
//     if (capabilities.currentExtent.width !=
//         std::numeric_limits<uint32_t>::max()) {
//       return capabilities.currentExtent;
//     } else {
//       int width, height;
//       // 使用 SDL3 获取像素级 drawable 尺寸
//       if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
//         throw std::runtime_error(
//             "Failed to query window pixel size for Vulkan");
//       }

//       VkExtent2D actualExtent = {static_cast<uint32_t>(width),
//                                  static_cast<uint32_t>(height)};

//       actualExtent.width =
//           std::clamp(actualExtent.width, capabilities.minImageExtent.width,
//                      capabilities.maxImageExtent.width);
//       actualExtent.height =
//           std::clamp(actualExtent.height, capabilities.minImageExtent.height,
//                      capabilities.maxImageExtent.height);

//       return actualExtent;
//     }
//   }


//   void initVulkan() {
//     createInstance();
//     setupDebugMessenger();
//     createSurface();
//     pickPhysicalDevice();
//     createLogicalDevice();
//     createSwapChain();
//     createImageViews();
//     createRenderPass();
//     createDescriptorSetLayout();
//     createGraphicsPipeline();
//     createCommandPool();
//     createCommandBuffers();

//     createColorResources();
//     createDepthResources();
//     createFramebuffers();

//     createTextureImage();
//     createTextureImageView();
//     createTextureSampler(GLOBAL_CONTROL_MIPMAP);

//     loadModel();
//     createVertexBuffer();
//     createIndexBuffer();
//     createUniformBuffer();
//     createDescriptorPool();
//     createDescriptorSets();
//     createSyncObjects();

//     initImGui(); // 依赖descriptorPool
//   }

//   void initImGui() {
//     ImGui_ImplVulkan_InitInfo info{};
//     info.ApiVersion = VK_API_VERSION_1_3;
//     info.Instance = instance;
//     info.PhysicalDevice = physicalDevice;
//     info.Device = device;

//     LX_graphics::QueryQueueFamilyResult indices = findQueueFamilies(physicalDevice);
//     info.QueueFamily = indices.graphicsFamily.value();
//     info.Queue = graphicsQueue;

//     info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
//     info.ImageCount = MAX_FRAMES_IN_FLIGHT;

//     info.DescriptorPool = descriptorPool;
//     info.DescriptorPoolSize = 0; // 0 表示不用 ImGui 自动创建
//     // info.DescriptorPoolSize = dis

//     ImGui_ImplVulkan_PipelineInfo pipelineInfoMain{};
//     pipelineInfoMain.RenderPass = renderPass;
//     pipelineInfoMain.Subpass = 0; // 主渲染子通道
//     pipelineInfoMain.MSAASamples = msaaSamples;
//     info.PipelineInfoMain = pipelineInfoMain;

//     if (!ImGui_ImplVulkan_Init(&info)) {
//       throw std::runtime_error("failed to initialize ImGui!");
//     }
//   }

//!

//   VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

//   VkSampleCountFlagBits getMaxUsableSampleCount() {
//     VkPhysicalDeviceProperties physicalDeviceProperties;
//     vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

//     VkSampleCountFlags counts =
//         physicalDeviceProperties.limits.framebufferColorSampleCounts &
//         physicalDeviceProperties.limits.framebufferDepthSampleCounts;
//     if (counts & VK_SAMPLE_COUNT_64_BIT) {
//       return VK_SAMPLE_COUNT_64_BIT;
//     }
//     if (counts & VK_SAMPLE_COUNT_32_BIT) {
//       return VK_SAMPLE_COUNT_32_BIT;
//     }
//     if (counts & VK_SAMPLE_COUNT_16_BIT) {
//       return VK_SAMPLE_COUNT_16_BIT;
//     }
//     if (counts & VK_SAMPLE_COUNT_8_BIT) {
//       return VK_SAMPLE_COUNT_8_BIT;
//     }
//     if (counts & VK_SAMPLE_COUNT_4_BIT) {
//       return VK_SAMPLE_COUNT_4_BIT;
//     }
//     if (counts & VK_SAMPLE_COUNT_2_BIT) {
//       return VK_SAMPLE_COUNT_2_BIT;
//     }

//     return VK_SAMPLE_COUNT_1_BIT;
//   }

//!

//   std::atomic<bool> shouldClose{false};
//   std::thread renderThread;
//   std::atomic<bool> renderPaused{false};

//   void renderThreadFunc() {
//     auto lastTime = std::chrono::steady_clock::now();

//     while (!shouldClose.load()) {
//       if (!renderPaused.load()) {
//         auto currentTime = std::chrono::steady_clock::now();
//         auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
//             currentTime - lastTime);

//         if (elapsed.count() >= 8) { // 约120fps
//           try {
//             drawFrame();
//           } catch (const std::exception &e) {
//             std::cerr << "Render error: " << e.what() << std::endl;
//           }
//           lastTime = currentTime;
//         }
//       }
//       std::this_thread::sleep_for(std::chrono::milliseconds(1));
//     }
//   }

//   ImDrawData *drawDataForRenderThread = nullptr;
//   std::mutex drawDataMutex;

//   // 控制主循环是否退出
//   void mainLoop() {
//     // 启动渲染线程
//     renderThread =
//         std::thread(&HelloTriangleApplication::renderThreadFunc, this);

//     SDL_Event event;
//     // 主线程专门处理事件
//     while (!shouldClose.load()) {
//       bool uiMayUpdated = false;
//       // 等待事件，如果没有事件会阻塞
//       while (SDL_PollEvent(&event)) {
//         if(ImGui_ImplSDL3_ProcessEvent(&event)){
//           uiMayUpdated = true;
//         }
//         switch (event.type) {
//         case SDL_EVENT_QUIT:
//           shouldClose.store(true);
//           break;

//         // SDL3 窗口像素大小变化事件
//         case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
//           framebufferResized = true;
//           break;

//         default:
//           break;
//         }
//       }

//       // 获取窗口当前像素尺寸，决定是否暂停渲染
//       int width = 0, height = 0;
//       if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
//         throw std::runtime_error("Failed to get window pixel size");
//       }
//       ImGuiIO &io = ImGui::GetIO();
//       io.DisplaySize = ImVec2((float)width, (float)height);
//       if (uiMayUpdated) {
//         std::lock_guard<std::mutex> lock(drawDataMutex);
//         ImGui_ImplVulkan_NewFrame();
//         ImGui::NewFrame();
//         ImGui::ShowDemoWindow(); // 自定义UI
//         ImGui::Render();
//         drawDataForRenderThread = ImGui::GetDrawData();
//       }
//       renderPaused.store(width == 0 || height == 0);

//       std::this_thread::sleep_for(std::chrono::milliseconds(1));
//     }

//     if (renderThread.joinable()) {
//       renderThread.join();
//     }

//     vkDeviceWaitIdle(device);
//   }

//   std::vector<VkSemaphore> imageAvailableSemaphores;
//   std::vector<VkSemaphore> renderFinishedSemaphores;
//   std::vector<VkFence> inFlightFences;

//   bool framebufferResized = false;

//   uint32_t currentFrame = 0;

//   void updateUniformBuffer(uint32_t frameIndex, bool rotate = true) {
//     static auto startTime = std::chrono::high_resolution_clock::now();
//     auto currentTime = std::chrono::high_resolution_clock::now();
//     float time = std::chrono::duration<float, std::chrono::seconds::period>(
//                      currentTime - startTime)
//                      .count();

//     UniformBufferObject ubo{};
//     if (!rotate) {
//       ubo.model = glm::mat4(1.0f);
//     } else {
//       ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f),
//                               glm::vec3(0.0f, 0.0f, 1.0f));
//     }
//     // 视图变换：摄像机从 (2,2,2) 看向原点
//     ubo.view =
//         glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
//                     glm::vec3(0.0f, 0.0f, 1.0f));
//     // 投影变换：45° 视角，近平面0.1，远平面10
//     ubo.proj = glm::perspective(
//         glm::radians(45.0f),
//         swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 10.0f);
//     // 由于GLM库默认给OpenGL设计的，Vulkan里需要flip Y坐标。
//     ubo.proj[1][1] *= -1;

//     memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
//   }

//   void drawFrame() {
//     // 等待上一帧完成（注意，第一帧还没画的时候，会直接阻塞死，因此要在
//     // createSyncObject 即初始化位置处理一下）
//     vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE,
//                     UINT64_MAX);

//     // 获取接下来渲染的image索引
//     uint32_t imageIndex;
//     VkResult result = vkAcquireNextImageKHR(
//         device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame],
//         VK_NULL_HANDLE, &imageIndex);

//     if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
//         framebufferResized) {
//       framebufferResized = false;
//       recreateSwapChain();
//       return;
//     } else if (result != VK_SUCCESS) {
//       throw std::runtime_error("failed to acquire swap chain image!");
//     }

//     // 重置fence，让其继续生效（可以下一帧阻塞主线程）。应该立马调用，因为后面的渲染还会用到它。
//     vkResetFences(device, 1, &inFlightFences[currentFrame]);

//     // 重置 command buffer，准备新的绘制命令
//     vkResetCommandBuffer(commandBuffers[currentFrame], 0);

//     // 绘图
//     recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

//     // 更新uniform buffer(只要在提交前更新就行)
//     updateUniformBuffer(currentFrame, GLOBAL_CONTROL_ROTATE);

//     // 准备提交 command buffer
//     VkSubmitInfo submitInfo{};
//     submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

//     VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
//     VkPipelineStageFlags waitStages[] = {
//         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

//     submitInfo.waitSemaphoreCount = 1;
//     // 提交中的同步设定：下面这两个一一对应，管线执行到上面的stage，就wait下面的semaphore
//     submitInfo.pWaitDstStageMask = waitStages;
//     submitInfo.pWaitSemaphores = waitSemaphores;

//     // 具体提交的 command buffer
//     submitInfo.commandBufferCount = 1;
//     submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

//     // 执行结束后，signal下面的semaphore；主要通知GPU其他队列或者 swap chain 等
//     VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
//     submitInfo.signalSemaphoreCount = 1;
//     submitInfo.pSignalSemaphores = signalSemaphores;

//     // 这里是，当command buffer执行完后，signal下面的semaphore
//     // 注意，提交结束就释放了fence，这导致 presentQueue 可能还在使用
//     // renderFinishedSemaphore 的时候就进入了下一帧渲染，从而触发了
//     // renderFinishedSemaphore
//     // 的使用。但validator无法判断这么细的逻辑，只看到两个队列使用semaphone于是报错。
//     // 这里有个讨论帖子：
//     // https://www.reddit.com/r/vulkan/comments/1me8ubj/vulkan_validation_error/
//     // 最好做法还是，直接每个swap chain image具备一套信号量。
//     if (vkQueueSubmit(graphicsQueue, 1, &submitInfo,
//                       inFlightFences[currentFrame]) != VK_SUCCESS) {
//       throw std::runtime_error("failed to submit draw command buffer!");
//     }

//     // 呈现前等待信号量（renderFinish)
//     VkPresentInfoKHR presentInfo{};
//     presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
//     presentInfo.waitSemaphoreCount = 1;
//     presentInfo.pWaitSemaphores = signalSemaphores;

//     // 要呈现的 swap chain 的配置
//     VkSwapchainKHR swapChains[] = {swapChain};
//     presentInfo.swapchainCount = 1;
//     presentInfo.pSwapchains = swapChains;
//     presentInfo.pImageIndices = &imageIndex;

//     // 呈现调用的返回值。当多 swap chain的时候使用。单个可以用返回值。
//     presentInfo.pResults = nullptr; // Optional

//     // 呈现 swap chain 中的 image
//     vkQueuePresentKHR(presentQueue, &presentInfo);

//     currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
//   }

//   void cleanup() {
//     cleanupSwapChain();

//     vkDestroyImageView(device, textureImageView, nullptr);
//     vkDestroyImage(device, textureImage, nullptr);
//     vkFreeMemory(device, textureImageMemory, nullptr);

//     for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
//       vkDestroyBuffer(device, uniformBuffers[i], nullptr);
//       vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
//     }

//     vkDestroyDescriptorPool(device, descriptorPool, nullptr);
//     vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

//     vkDestroyBuffer(device, indexBuffer, nullptr);
//     vkFreeMemory(device, indexBufferMemory, nullptr);

//     vkDestroyBuffer(device, vertexBuffer, nullptr);
//     vkFreeMemory(device, vertexBufferMemory, nullptr);

//     vkDestroyPipeline(device, graphicsPipeline, nullptr);
//     vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

//     vkDestroyRenderPass(device, renderPass, nullptr);

//     for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
//       vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
//       vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
//       vkDestroyFence(device, inFlightFences[i], nullptr);
//     }

//     vkDestroyCommandPool(device, commandPool, nullptr);

//     vkDestroyDevice(device, nullptr);

//     if (enableValidationLayers) {
//       DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
//     }

//     vkDestroySurfaceKHR(instance, surface, nullptr);
//     vkDestroyInstance(instance, nullptr);

//     SDL_DestroyWindow(window);

//     SDL_Quit();
//   }
// };

// int main() {
//   expSetEnvVK();
//   std::cout << "Hello Triangle!" << std::endl;

//   HelloTriangleApplication app;

//   try {
//     app.run();
//   } catch (const std::exception &e) {
//     std::cerr << e.what() << std::endl;
//     return EXIT_FAILURE;
//   }
//   std::cout << "Goodbye Triangle!" << std::endl;
//   return EXIT_SUCCESS;
// }