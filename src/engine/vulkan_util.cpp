/**
 * VulkanUtil 实现
 */
#include "vulkan_util.h"

#include <fstream>
#include <stdexcept>
#include <iostream>

namespace vulkan_util
{

uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t typeFilter,
                        VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1 << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("未找到合适的内存类型");
}

// ============================================================================
// 缓冲区
// ============================================================================

void createBuffer(VkDevice device, VkPhysicalDevice pd, VkDeviceSize size,
                  VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                  VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &buffer) != VK_SUCCESS)
        throw std::runtime_error("vkCreateBuffer 失败");

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, buffer, &mr);

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(pd, mr.memoryTypeBits, props);
    if (vkAllocateMemory(device, &ai, nullptr, &memory) != VK_SUCCESS)
        throw std::runtime_error("vkAllocateMemory 失败");

    vkBindBufferMemory(device, buffer, memory, 0);
}

void destroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
{
    if (buffer)  vkDestroyBuffer(device, buffer, nullptr);
    if (memory)  vkFreeMemory(device, memory, nullptr);
}

// ----------------------------------------------------------------------------
// 单次命令缓冲辅助
// ----------------------------------------------------------------------------

VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool pool)
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void endSingleTimeCommands(VkDevice device, VkCommandPool pool,
                           VkQueue queue, VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

// ----------------------------------------------------------------------------
// 把数据上传到 GPU 缓冲（暂存缓冲中转）
// ----------------------------------------------------------------------------

void uploadToBuffer(VkDevice device, VkPhysicalDevice pd,
                    VkCommandPool cmdPool, VkQueue queue,
                    const void* data, VkDeviceSize size,
                    VkBuffer dstBuffer)
{
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    createBuffer(device, pd, size,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);

    void* mapped = nullptr;
    vkMapMemory(device, stagingMem, 0, size, 0, &mapped);
    memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(device, stagingMem);

    VkCommandBuffer cmd = beginSingleTimeCommands(device, cmdPool);
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, staging, dstBuffer, 1, &region);
    endSingleTimeCommands(device, cmdPool, queue, cmd);

    destroyBuffer(device, staging, stagingMem);
}

// ============================================================================
// 着色器模块
// ============================================================================

static std::vector<uint8_t> readFileBytes(const std::string& path)
{
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("无法读取着色器文件: " + path);
    size_t size = static_cast<size_t>(f.tellg());
    std::vector<uint8_t> buf(size);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

VkShaderModule createShaderModule(VkDevice device, const std::string& spvPath)
{
    auto code = readFileBytes(spvPath);
    return createShaderModuleFromMemory(device, code);
}

VkShaderModule createShaderModuleFromMemory(VkDevice device,
                                            const std::vector<uint8_t>& code)
{
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m;
    if (vkCreateShaderModule(device, &ci, nullptr, &m) != VK_SUCCESS)
        throw std::runtime_error("vkCreateShaderModule 失败");
    return m;
}

// ============================================================================
// 图像
// ============================================================================

void createImage2D(VkDevice device, VkPhysicalDevice pd,
                   uint32_t width, uint32_t height,
                   VkFormat format, VkImageUsageFlags usage,
                   VkMemoryPropertyFlags props,
                   VkImage& image, VkDeviceMemory& memory)
{
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.extent = { width, height, 1 };
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.format = format;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device, &ci, nullptr, &image) != VK_SUCCESS)
        throw std::runtime_error("vkCreateImage 失败");

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, image, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(pd, mr.memoryTypeBits, props);
    if (vkAllocateMemory(device, &ai, nullptr, &memory) != VK_SUCCESS)
        throw std::runtime_error("vkAllocateMemory (image) 失败");
    vkBindImageMemory(device, image, memory, 0);
}

VkImageView createImageView2D(VkDevice device, VkImage image,
                              VkFormat format, VkImageAspectFlags aspect)
{
    VkImageViewCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image = image;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ci.format = format;
    ci.subresourceRange.aspectMask = aspect;
    ci.subresourceRange.baseMipLevel = 0;
    ci.subresourceRange.levelCount = 1;
    ci.subresourceRange.baseArrayLayer = 0;
    ci.subresourceRange.layerCount = 1;
    VkImageView v;
    if (vkCreateImageView(device, &ci, nullptr, &v) != VK_SUCCESS)
        throw std::runtime_error("vkCreateImageView 失败");
    return v;
}

void transitionImageLayout(VkDevice device, VkCommandPool pool, VkQueue queue,
                           VkImage image, VkFormat /*format*/,
                           VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkCommandBuffer cmd = beginSingleTimeCommands(device, pool);

    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage, dstStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr,
                        1, &b);
    endSingleTimeCommands(device, pool, queue, cmd);
}

void copyBufferToImage(VkDevice device, VkCommandPool pool, VkQueue queue,
                       VkBuffer buffer, VkImage image, uint32_t w, uint32_t h)
{
    VkCommandBuffer cmd = beginSingleTimeCommands(device, pool);
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { w, h, 1 };
    vkCmdCopyBufferToImage(cmd, buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    endSingleTimeCommands(device, pool, queue, cmd);
}

} // namespace vulkan_util
