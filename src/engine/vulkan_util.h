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

// ---- 延迟销毁队列（GPU 资源垃圾回收） ----
//
// 引擎使用双帧缓冲（MAX_FRAMES_IN_FLIGHT=2），GPU 可能仍在执行上一帧的
// 命令缓冲。若在 ECS 更新阶段直接销毁缓冲（如粒子死亡/文字重传/场景切换），
// 会造成 use-after-free → VK_ERROR_DEVICE_LOST。
//
// 正确做法：把待销毁的缓冲登记进延迟队列，推迟 2 帧后（该帧引用的缓冲最迟
// 出现在上一帧，而上一帧的 fence 在 beginFrame 时已被等待完成）再统一释放。
// 队列按 in-flight 槽位分为两组，实现"延迟 2 帧释放"。
// descPool/descSet 非空时，在释放缓冲的同时回收该描述符集（需描述符池
// 以 VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT 创建）。
struct DeferredDestroy
{
    VkDevice           device    = VK_NULL_HANDLE;
    VkBuffer           buffer    = VK_NULL_HANDLE;
    VkDeviceMemory     memory    = VK_NULL_HANDLE;
    void*              mapped    = nullptr;  // 非空则释放前先 vkUnmapMemory
    VkDescriptorPool   descPool  = VK_NULL_HANDLE;  // 非空则释放 descSet
    VkDescriptorSet    descSet   = VK_NULL_HANDLE;
};

// 登记一个待延迟销毁的缓冲（mapped 非空表示该内存当前被持久映射）
void deferDestroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
                        void* mapped = nullptr);

// 登记一个待延迟销毁的描述符集（池必须含 FREE_DESCRIPTOR_SET_BIT 标志）
void deferDestroyDescriptorSet(VkDevice device, VkDescriptorPool pool,
                               VkDescriptorSet set);

// 安全点调用：释放「上一逻辑帧槽位」登记的资源。
// 由 VulkanContext::beginFrame 在 vkWaitForFences 完成后调用——
// 此时上一帧及其引用的缓冲已被 GPU 用完。单线程驱动（主线程），无需加锁。
void flushDeferredDestroy(VkDevice device);

// 设备空闲（vkDeviceWaitIdle）后的收尾调用：释放所有登记的资源。
// 供 VulkanContext 析构等场景使用。
void flushAllDeferredDestroy(VkDevice device);

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
                   VkImage& image, VkDeviceMemory& memory,
                   VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

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
