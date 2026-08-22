/**
 * render_device.h —— 渲染模块共享句柄集合（v1.0.1）
 *
 * 所有渲染功能模块（texture / framebuffer / postfx / shadow / skybox /
 * instancing / particles / mesh_loader 及原有 Shape/Mesh3D 便捷重载）
 * 统一通过 RenderDevice 获得 Vulkan 设备级句柄，避免逐个传参。
 *
 * 该结构体仅承载句柄，不持有所有权；生命周期由创建者（VulkanApp /
 * VulkanContext）保证。
 */
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

struct RenderDevice
{
    VkDevice           device         = VK_NULL_HANDLE;
    VkPhysicalDevice   physicalDevice = VK_NULL_HANDLE;
    VkQueue            queue          = VK_NULL_HANDLE;
    VkCommandPool      commandPool    = VK_NULL_HANDLE;
    uint32_t           queueFamily    = 0;
    // 通用描述符池（可分配 COMBINED_IMAGE_SAMPLER / UNIFORM_BUFFER）；
    // 为空时各模块自建描述符池，互不干扰。
    VkDescriptorPool   descriptorPool = VK_NULL_HANDLE;
};
