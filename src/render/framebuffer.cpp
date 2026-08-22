/**
 * framebuffer.cpp —— 离屏渲染 RenderTarget 实现
 */
#include "render/framebuffer.h"

#include "engine/vulkan_util.h"
#include "engine/logger.h"

using namespace vulkan_util;

RenderTarget::~RenderTarget() { destroy(); }

std::unique_ptr<RenderTarget> RenderTarget::create(const RenderDevice& dev,
                                                   uint32_t w, uint32_t h,
                                                   bool withDepth, VkFormat colorFormat,
                                                   VkFormat depthFormat,
                                                   VkSampleCountFlagBits samples)
{
    auto rt = std::make_unique<RenderTarget>();
    if (!rt->createResources(dev, w, h, withDepth, colorFormat, depthFormat, samples))
        return nullptr;
    return rt;
}

bool RenderTarget::resize(const RenderDevice& dev, uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) return false;
    VkFormat fmt    = m_colorFormat;
    VkFormat depthFmt = m_depthFormat;
    bool withDepth = m_hasDepth;
    VkSampleCountFlagBits samples = m_samples;
    destroy();
    return createResources(dev, w, h, withDepth, fmt, depthFmt, samples);
}

void RenderTarget::begin(VkCommandBuffer cmd, const VkClearColorValue& clearColor) const
{
    VkClearValue clears[2]{};
    clears[0].color = clearColor;
    if (m_hasDepth) clears[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rp{};
    rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass      = m_renderPass;
    rp.framebuffer     = m_framebuffer;
    rp.renderArea      = VkRect2D{ {0,0}, {m_width, m_height} };
    rp.clearValueCount = m_hasDepth ? 2u : 1u;
    rp.pClearValues    = clears;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
}

void RenderTarget::end(VkCommandBuffer cmd) const
{
    vkCmdEndRenderPass(cmd);
}

bool RenderTarget::createResources(const RenderDevice& dev, uint32_t w, uint32_t h,
                                   bool withDepth, VkFormat colorFormat,
                                   VkFormat depthFormat,
                                   VkSampleCountFlagBits samples)
{
    if (dev.device == VK_NULL_HANDLE || w == 0 || h == 0) return false;
    m_device = dev.device;
    m_width = w; m_height = h;
    m_colorFormat = colorFormat;
    m_depthFormat = depthFormat;
    m_hasDepth = withDepth;
    m_samples = samples;

    const bool msaa = (samples != VK_SAMPLE_COUNT_1_BIT);

    // 1. renderPass：颜色附件（MSAA 时附带 resolve 附件）+ 可选深度附件
    {
        VkAttachmentDescription color{};
        color.format         = colorFormat;
        color.samples        = samples;
        color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp        = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                    : VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout    = msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentDescription resolve{};
        VkAttachmentReference resolveRef{};
        VkAttachmentDescription attachments[3]{};
        uint32_t attachCount = 0;
        VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        attachments[attachCount++] = color;

        if (msaa)
        {
            // resolve 目标：单样本、可采样（后处理输入）
            resolve.format         = colorFormat;
            resolve.samples        = VK_SAMPLE_COUNT_1_BIT;
            resolve.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            resolve.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            resolve.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            resolve.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            resolve.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            resolveRef.attachment  = attachCount;
            resolveRef.layout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[attachCount++] = resolve;
        }

        VkAttachmentReference depthRef{};
        VkAttachmentDescription depth{};
        if (withDepth)
        {
            depth.format         = depthFormat;
            depth.samples        = samples;
            depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthRef.attachment  = attachCount;
            depthRef.layout      = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments[attachCount++] = depth;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;
        subpass.pResolveAttachments  = msaa ? &resolveRef : nullptr;
        subpass.pDepthStencilAttachment = withDepth ? &depthRef : nullptr;

        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = attachCount;
        ci.pAttachments    = attachments;
        ci.subpassCount    = 1;
        ci.pSubpasses      = &subpass;
        if (vkCreateRenderPass(dev.device, &ci, nullptr, &m_renderPass) != VK_SUCCESS)
        { destroy(); return false; }
    }

    // 2. MSAA 颜色附件（多重采样，仅 msaa 时创建）
    if (msaa)
    {
        createImage2D(dev.device, dev.physicalDevice, w, h, colorFormat,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      m_msaaColorImage, m_msaaColorMemory, samples);
        if (m_msaaColorImage == VK_NULL_HANDLE) { destroy(); return false; }
        m_msaaColorView = createImageView2D(dev.device, m_msaaColorImage,
                                            colorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        if (m_msaaColorView == VK_NULL_HANDLE) { destroy(); return false; }
    }

    // 3. 可采样输出图像（单样本；MSAA 时作为 resolve 目标）
    createImage2D(dev.device, dev.physicalDevice, w, h, colorFormat,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_colorImage, m_colorMemory);
    if (m_colorImage == VK_NULL_HANDLE) { destroy(); return false; }
    m_colorView = createImageView2D(dev.device, m_colorImage, colorFormat,
                                    VK_IMAGE_ASPECT_COLOR_BIT);
    if (m_colorView == VK_NULL_HANDLE) { destroy(); return false; }

    // 3. 采样器
    VkSamplerCreateInfo sci{};
    sci.sType      = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter  = VK_FILTER_LINEAR;
    sci.minFilter  = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod     = 1.0f;
    if (vkCreateSampler(dev.device, &sci, nullptr, &m_sampler) != VK_SUCCESS)
    { destroy(); return false; }

    // 4. 描述符集（COMBINED_IMAGE_SAMPLER，供后处理采样）
    m_descSetLayout = Texture::createDefaultLayout(dev.device);
    if (m_descSetLayout == VK_NULL_HANDLE) { destroy(); return false; }

    VkDescriptorPool pool = dev.descriptorPool;
    bool ownPool = false;
    if (pool == VK_NULL_HANDLE)
    {
        VkDescriptorPoolSize psize{};
        psize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        psize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pci.maxSets = 1; pci.poolSizeCount = 1; pci.pPoolSizes = &psize;
        if (vkCreateDescriptorPool(dev.device, &pci, nullptr, &pool) != VK_SUCCESS)
        { destroy(); return false; }
        ownPool = true;
    }
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool; ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_descSetLayout;
    if (vkAllocateDescriptorSets(dev.device, &ai, &m_descSet) != VK_SUCCESS)
    { if (ownPool) vkDestroyDescriptorPool(dev.device, pool, nullptr);
      destroy(); return false; }
    if (ownPool) m_ownPool = pool;

    VkDescriptorImageInfo dii{};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView   = m_colorView;
    dii.sampler     = m_sampler;
    VkWriteDescriptorSet wds{};
    wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet = m_descSet; wds.dstBinding = 0; wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev.device, 1, &wds, 0, nullptr);

    // 5. 深度图像（可选，MSAA 时用多重采样）
    if (withDepth)
    {
        createImage2D(dev.device, dev.physicalDevice, w, h, depthFormat,
                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImage, m_depthMemory,
                      samples);
        if (m_depthImage == VK_NULL_HANDLE) { destroy(); return false; }
        m_depthView = createImageView2D(dev.device, m_depthImage, depthFormat,
                                        VK_IMAGE_ASPECT_DEPTH_BIT);
        if (m_depthView == VK_NULL_HANDLE) { destroy(); return false; }
    }

    // 6. Framebuffer
    // 附件顺序须与 renderPass 附件索引一致：
    //   MSAA:  [0]=MSAA 颜色, [1]=resolve 颜色, [2]=深度
    //   1x:    [0]=颜色,        [1]=深度
    VkImageView attachments[3]{};
    uint32_t attachCount = 0;
    if (msaa)
        attachments[attachCount++] = m_msaaColorView;
    attachments[attachCount++] = m_colorView;
    if (withDepth) attachments[attachCount++] = m_depthView;

    VkFramebufferCreateInfo fci{};
    fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass      = m_renderPass;
    fci.attachmentCount = attachCount;
    fci.pAttachments    = attachments;
    fci.width           = w; fci.height = h; fci.layers = 1;
    if (vkCreateFramebuffer(dev.device, &fci, nullptr, &m_framebuffer) != VK_SUCCESS)
    { destroy(); return false; }

    LOG_INFO("RenderTarget", "create", "%ux%u depth=%d samples=%u", w, h, withDepth,
             static_cast<uint32_t>(samples));
    return true;
}

void RenderTarget::destroy()
{
    if (m_device == VK_NULL_HANDLE) return;
    if (m_framebuffer)  vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
    if (m_renderPass)   vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    if (m_depthView)    vkDestroyImageView(m_device, m_depthView, nullptr);
    if (m_depthImage)   vkDestroyImage(m_device, m_depthImage, nullptr);
    if (m_depthMemory)  vkFreeMemory(m_device, m_depthMemory, nullptr);
    if (m_colorView)    vkDestroyImageView(m_device, m_colorView, nullptr);
    if (m_sampler)      vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_descSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
    if (m_colorImage)   vkDestroyImage(m_device, m_colorImage, nullptr);
    if (m_colorMemory)  vkFreeMemory(m_device, m_colorMemory, nullptr);
    if (m_msaaColorView)    vkDestroyImageView(m_device, m_msaaColorView, nullptr);
    if (m_msaaColorImage)   vkDestroyImage(m_device, m_msaaColorImage, nullptr);
    if (m_msaaColorMemory)  vkFreeMemory(m_device, m_msaaColorMemory, nullptr);
    if (m_ownPool)      vkDestroyDescriptorPool(m_device, m_ownPool, nullptr);
    m_framebuffer = VK_NULL_HANDLE;
    m_renderPass  = VK_NULL_HANDLE;
    m_colorView   = VK_NULL_HANDLE;
    m_colorImage  = VK_NULL_HANDLE;
    m_colorMemory = VK_NULL_HANDLE;
    m_sampler     = VK_NULL_HANDLE;
    m_descSetLayout = VK_NULL_HANDLE;
    m_descSet     = VK_NULL_HANDLE;
    m_msaaColorView   = VK_NULL_HANDLE;
    m_msaaColorImage  = VK_NULL_HANDLE;
    m_msaaColorMemory = VK_NULL_HANDLE;
    m_samples     = VK_SAMPLE_COUNT_1_BIT;
    m_depthView   = VK_NULL_HANDLE;
    m_depthImage  = VK_NULL_HANDLE;
    m_depthMemory = VK_NULL_HANDLE;
    m_ownPool     = VK_NULL_HANDLE;
}
