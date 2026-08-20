/**
 * VulkanUtil —— Vulkan 资源创建辅助函数
 *
 * 提供：缓冲区创建、着色器模块加载、图形管线构建等通用工具。
 * 这些是无状态函数，供引擎各模块复用。
 */
#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace vulkan_util
{

// ---- 查询 ----
uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t typeFilter,
                        VkMemoryPropertyFlags props);

// ---- 缓冲区 ----
void createBuffer(VkDevice device, VkPhysicalDevice pd, VkDeviceSize size,
                  VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                  VkBuffer& buffer, VkDeviceMemory& memory);

void destroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory);

// 把主机数据拷贝到 GPU 缓冲（用于顶点/索引/统一缓冲一次性上传）
void uploadToBuffer(VkDevice device, VkPhysicalDevice pd,
                    VkCommandPool cmdPool, VkQueue queue,
                    const void* data, VkDeviceSize size,
                    VkBuffer dstBuffer);

// ---- 着色器 ----
// 从 .spv 文件创建 VkShaderModule，文件读取失败抛异常
VkShaderModule createShaderModule(VkDevice device, const std::string& spvPath);

// 从内存中的 SPIR-V 字节码创建 VkShaderModule
VkShaderModule createShaderModuleFromMemory(VkDevice device,
                                             const std::vector<uint8_t>& code);

// ---- 图像 ----
void createImage2D(VkDevice device, VkPhysicalDevice pd,
                   uint32_t width, uint32_t height,
                   VkFormat format, VkImageUsageFlags usage,
                   VkMemoryPropertyFlags props,
                   VkImage& image, VkDeviceMemory& memory);

VkImageView createImageView2D(VkDevice device, VkImage image,
                              VkFormat format, VkImageAspectFlags aspect);

// 把主机数据拷贝到图像（用于字形图集等）
void transitionImageLayout(VkDevice device, VkCommandPool pool, VkQueue queue,
                           VkImage image, VkFormat format,
                           VkImageLayout oldLayout, VkImageLayout newLayout);

void copyBufferToImage(VkDevice device, VkCommandPool pool, VkQueue queue,
                       VkBuffer buffer, VkImage image, uint32_t w, uint32_t h);

// 单次命令缓冲辅助（提交并等待完成）
VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool pool);
void endSingleTimeCommands(VkDevice device, VkCommandPool pool,
                           VkQueue queue, VkCommandBuffer cmd);

// ---- push constants ----
template <typename T>
void pushConstants(VkCommandBuffer cmd, VkPipelineLayout layout,
                   VkShaderStageFlags stage, const T& value)
{
    vkCmdPushConstants(cmd, layout, stage, 0, sizeof(T), &value);
}

} // namespace vulkan_util
