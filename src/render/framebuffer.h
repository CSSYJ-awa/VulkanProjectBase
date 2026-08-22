/**
 * framebuffer.h —— 离屏渲染 RenderTarget（渲染引擎 v1.0.1 扩展模块）
 *
 * 功能：
 *   - 离屏帧缓冲封装：自建 renderPass（颜色附件 + 可选深度附件）与 Framebuffer
 *   - 颜色附件可被采样（sampler + 描述符集），供后处理 / 阴影 / 自定义特效复用
 *   - begin/end 切换渲染目标，语义与 VulkanContext 的主 renderPass 对称
 *
 * 依赖：render/texture.h（RenderDevice）、engine/vulkan_util.h
 */
#pragma once

#include "render/texture.h"

#include <vulkan/vulkan.h>

#include <memory>

// ============================================================================
// RenderTarget —— 离屏渲染目标
// ============================================================================
class RenderTarget
{
public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    // 创建离屏目标。withDepth=true 时附带深度附件（供 3D 内容离屏渲染）。
    // w/h 为像素尺寸；colorFormat 默认 R8G8B8A8_UNORM；
    // depthFormat 默认 D32_SFLOAT（应与主 renderPass 深度格式一致以便管线复用）。
    // samples > 1 时启用 MSAA：颜色/深度附件按 samples 多重采样，
    // 渲染结束后自动 resolve 到可采样的单样本图像（仍可通过 colorView() 采样）。
    static std::unique_ptr<RenderTarget> create(const RenderDevice& dev,
                                                uint32_t w, uint32_t h,
                                                bool withDepth = true,
                                                VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
                                                VkFormat depthFormat = VK_FORMAT_D32_SFLOAT,
                                                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

    // 重新分配尺寸（分辨率变化时调用；会重建颜色/深度资源与 Framebuffer）
    bool resize(const RenderDevice& dev, uint32_t w, uint32_t h);

    // 在录制命令缓冲时切换到本目标：内部 vkCmdBeginRenderPass。
    // clearColor 为颜色附件清除值。
    void begin(VkCommandBuffer cmd,
               const VkClearColorValue& clearColor = {0.0f, 0.0f, 0.0f, 1.0f}) const;
    void end(VkCommandBuffer cmd) const;   // vkCmdEndRenderPass

    // ─── 访问 ────────────────────────────────────────────────────────────
    VkRenderPass   renderPass()   const { return m_renderPass; }
    VkFramebuffer  framebuffer()  const { return m_framebuffer; }
    VkExtent2D     extent()       const { return VkExtent2D{ m_width, m_height }; }
    uint32_t       width()        const { return m_width; }
    uint32_t       height()       const { return m_height; }
    VkFormat       colorFormat()  const { return m_colorFormat; }

    // 颜色附件（可采样，MSAA 时为 resolve 后的单样本图像）
    VkImage         colorImage()    const { return m_colorImage; }
    VkImageView     colorView()     const { return m_colorView; }
    VkSampler       colorSampler()  const { return m_sampler; }
    VkDescriptorSet colorDescriptorSet() const { return m_descSet; }
    VkImage         depthImage()    const { return m_depthImage; }
    VkImageView     depthView()     const { return m_depthView; }
    VkSampleCountFlagBits samples() const { return m_samples; }

private:
    bool createResources(const RenderDevice& dev, uint32_t w, uint32_t h,
                         bool withDepth, VkFormat colorFormat, VkFormat depthFormat,
                         VkSampleCountFlagBits samples);
    void destroy();

    VkDevice         m_device      = VK_NULL_HANDLE;
    VkRenderPass     m_renderPass  = VK_NULL_HANDLE;
    VkFramebuffer    m_framebuffer = VK_NULL_HANDLE;

    // 可采样输出图像（单样本；MSAA 时作为 resolve 目标）
    VkImage          m_colorImage  = VK_NULL_HANDLE;
    VkDeviceMemory   m_colorMemory = VK_NULL_HANDLE;
    VkImageView      m_colorView   = VK_NULL_HANDLE;
    VkSampler        m_sampler     = VK_NULL_HANDLE;
    VkDescriptorSet  m_descSet     = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_ownPool     = VK_NULL_HANDLE;

    // MSAA 多重采样附件（samples > 1 时有效）
    VkSampleCountFlagBits m_samples     = VK_SAMPLE_COUNT_1_BIT;
    VkImage          m_msaaColorImage  = VK_NULL_HANDLE;
    VkDeviceMemory   m_msaaColorMemory = VK_NULL_HANDLE;
    VkImageView      m_msaaColorView   = VK_NULL_HANDLE;

    VkImage          m_depthImage  = VK_NULL_HANDLE;
    VkDeviceMemory   m_depthMemory = VK_NULL_HANDLE;
    VkImageView      m_depthView   = VK_NULL_HANDLE;

    uint32_t         m_width       = 0;
    uint32_t         m_height      = 0;
    VkFormat         m_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat         m_depthFormat = VK_FORMAT_D32_SFLOAT;
    bool             m_hasDepth    = true;
};
