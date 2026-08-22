/**
 * VulkanContext —— Vulkan 渲染后端
 *
 * 封装：物理设备选择、逻辑设备、图形/呈现队列、交换链、
 * 渲染通道、帧缓冲、命令缓冲池、同步对象。
 *
 * 提供 beginFrame / endFrame 接口供 Renderer 在帧内录制绘制命令。
 */
#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "render/render_device.h"

#include <cstdint>
#include <vector>
#include <string>

class VulkanContext
{
public:
    VulkanContext(VkInstance instance, GLFWwindow* window,
                  uint32_t width, uint32_t height);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // 禁止移动（指针被外部持有）
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    // ---- 帧管理 ----
    // 返回值：imageIndex，若交换链需要重建返回 ~0u
    uint32_t beginFrame();
    void     endFrame();

    void     setResized() { m_framebufferResized = true; }

    // ---- 访问器 ----
    VkDevice         device()        const { return m_device; }
    VkPhysicalDevice physicalDevice()const { return m_physicalDevice; }
    VkQueue          graphicsQueue() const { return m_graphicsQueue; }
    VkQueue          presentQueue()  const { return m_presentQueue; }
    VkRenderPass     renderPass()    const { return m_renderPass; }
    VkCommandBuffer  commandBuffer() const;
    VkCommandPool    commandPool()   const { return m_commandPool; }
    VkExtent2D       extent()        const { return m_swapChainExtent; }
    VkFormat         imageFormat()   const { return m_swapChainImageFormat; }
    VkFormat         depthFormat()   const { return m_depthFormat; }
    // 便捷组装 RenderDevice（v1.0.1）：渲染模块共享句柄集合
    RenderDevice     renderDevice()  const
    { return RenderDevice{ m_device, m_physicalDevice, m_graphicsQueue,
                           m_commandPool, 0, VK_NULL_HANDLE }; }
    // 交换链帧缓冲（供渲染链重新 begin 主 pass 时使用）
    VkFramebuffer    framebuffer(uint32_t imageIndex) const
    { return imageIndex < m_swapChainFramebuffers.size() ? m_swapChainFramebuffers[imageIndex] : VK_NULL_HANDLE; }
    uint32_t         currentFrame()  const { return m_currentFrame; }

private:
    // ---- 初始化阶段函数 ----
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();

    // ---- 辅助 ----
    void recreateSwapChain();
    void cleanupSwapChain();

    // ---- 深度缓冲（3D 遮挡） ----
    void     createDepthResources();
    void     cleanupDepthResources();
    VkFormat findDepthFormat();

    VkSurfaceKHR     m_surface             = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice     = VK_NULL_HANDLE;
    VkDevice         m_device             = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue      = VK_NULL_HANDLE;
    VkQueue          m_presentQueue      = VK_NULL_HANDLE;

    // 交换链
    VkSwapchainKHR              m_swapChain        = VK_NULL_HANDLE;
    std::vector<VkImage>        m_swapChainImages;
    std::vector<VkImageView>    m_swapChainImageViews;
    VkFormat                    m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D                  m_swapChainExtent      = {0, 0};

    // 渲染通道 + 帧缓冲
    VkRenderPass                m_renderPass       = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>  m_swapChainFramebuffers;

    // 深度缓冲（与交换链同尺寸，重建时一并重建）
    VkFormat                    m_depthFormat    = VK_FORMAT_UNDEFINED;
    VkImage                     m_depthImage     = VK_NULL_HANDLE;
    VkDeviceMemory              m_depthMemory    = VK_NULL_HANDLE;
    VkImageView                 m_depthImageView = VK_NULL_HANDLE;

    // 命令缓冲
    VkCommandPool               m_commandPool      = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    // 同步
    std::vector<VkSemaphore>    m_imageAvailableSemaphores;
    std::vector<VkSemaphore>    m_renderFinishedSemaphores;
    std::vector<VkFence>        m_inFlightFences;
    std::vector<VkFence>        m_imagesInFlight;

    size_t                      m_currentFrame      = 0;
    uint32_t                    m_currentImageIndex = 0;
    bool                        m_framebufferResized = false;

    // 外部句柄
    VkInstance                  m_instance  = VK_NULL_HANDLE;
    GLFWwindow*                 m_window    = nullptr;
    uint32_t                    m_width     = 0;
    uint32_t                    m_height    = 0;
};
