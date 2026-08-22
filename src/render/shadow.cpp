/**
 * shadow.cpp —— 方向光阴影映射实现
 */
#include "render/shadow.h"

#include "engine/vulkan_util.h"
#include "engine/logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstring>
#include <stdexcept>

using namespace vulkan_util;

ShadowMap::~ShadowMap()
{
    if (m_device == VK_NULL_HANDLE) return;
    if (m_depthPipeline) vkDestroyPipeline(m_device, m_depthPipeline, nullptr);
    if (m_depthLayout)   vkDestroyPipelineLayout(m_device, m_depthLayout, nullptr);
    if (m_framebuffer)   vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
    if (m_depthPass)     vkDestroyRenderPass(m_device, m_depthPass, nullptr);
    if (m_depthView)     vkDestroyImageView(m_device, m_depthView, nullptr);
    if (m_depthImage)    vkDestroyImage(m_device, m_depthImage, nullptr);
    if (m_depthMemory)   vkFreeMemory(m_device, m_depthMemory, nullptr);
    if (m_depthSampler)  vkDestroySampler(m_device, m_depthSampler, nullptr);
    if (m_descSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
    if (m_descPool)      vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
    if (m_depthDescLayout) vkDestroyDescriptorSetLayout(m_device, m_depthDescLayout, nullptr);
    if (m_depthDescPool)   vkDestroyDescriptorPool(m_device, m_depthDescPool, nullptr);
    if (m_lightVPBuffer) vkDestroyBuffer(m_device, m_lightVPBuffer, nullptr);
    if (m_lightVPMemory) vkFreeMemory(m_device, m_lightVPMemory, nullptr);
}

void ShadowMap::create(const RenderDevice& dev, const std::string& shaderDir, uint32_t size)
{
    m_device = dev.device;
    m_size   = size;
    if (!createDepthResources(dev, size)) throw std::runtime_error("ShadowMap 深度资源创建失败");
    if (!createPipeline(dev, shaderDir))  throw std::runtime_error("ShadowMap 深度管线创建失败");

    // lightVP UBO（持久映射）
    VkDeviceSize sz = sizeof(glm::mat4);
    createBuffer(dev.device, dev.physicalDevice, sz, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_lightVPBuffer, m_lightVPMemory);
    vkMapMemory(dev.device, m_lightVPMemory, 0, sz, 0, &m_lightVPMapped);

    // 写入深度管线 UBO 描述符（binding 0 = lightVP）
    VkDescriptorBufferInfo dbi{};
    dbi.buffer = m_lightVPBuffer; dbi.offset = 0; dbi.range = sz;
    VkWriteDescriptorSet wubo{};
    wubo.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wubo.dstSet          = m_depthUboSet;
    wubo.dstBinding      = 0;
    wubo.descriptorCount = 1;
    wubo.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wubo.pBufferInfo     = &dbi;
    vkUpdateDescriptorSets(dev.device, 1, &wubo, 0, nullptr);
    m_depthUboWritten = true;

    LOG_INFO("ShadowMap", "create", "阴影贴图 %ux%u 就绪", size, size);
}

void ShadowMap::begin(VkCommandBuffer cmd) const
{
    VkClearValue clear{};
    clear.depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo rp{};
    rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass      = m_depthPass;
    rp.framebuffer     = m_framebuffer;
    rp.renderArea      = VkRect2D{ {0,0}, {m_size, m_size} };
    rp.clearValueCount = 1;
    rp.pClearValues    = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_depthPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_depthLayout,
                            0, 1, &m_depthUboSet, 0, nullptr);
    VkViewport vp{ 0, 0, static_cast<float>(m_size), static_cast<float>(m_size), 0, 1 };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{ {0,0}, {m_size, m_size} };
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

void ShadowMap::end(VkCommandBuffer cmd) const
{
    vkCmdEndRenderPass(cmd);
}

void ShadowMap::setLightMatrix(VkCommandBuffer cmd, const glm::mat4& lightVP)
{
    m_lightVP = lightVP;
    if (m_lightVPMapped)
        std::memcpy(m_lightVPMapped, glm::value_ptr(lightVP), sizeof(glm::mat4));
    // 深度管线 UBO 描述符在 create() 中已绑定到 m_lightVPBuffer（持久映射），
    // 每帧只需更新映射内容，无需重新绑描述符。
    (void)cmd;
}

void ShadowMap::bindToScene(VkCommandBuffer cmd, VkPipelineLayout sceneLayout) const
{
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sceneLayout,
                            0, 1, &m_descSet, 0, nullptr);
}

glm::mat4 ShadowMap::computeLightVP(const glm::vec3& lightDir,
                                    const glm::vec3& sceneCenter, float radius)
{
    if (radius <= 0.0f) radius = 1e-3f;   // 除零防护（正交投影参数为 0 → NaN 矩阵）
    glm::vec3 dir = glm::normalize(lightDir);
    glm::vec3 up  = (std::abs(dir.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                              : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::mat4 view = glm::lookAt(sceneCenter - dir * radius, sceneCenter, up);
    glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, 0.1f, 2.0f * radius);
    return proj * view;
}

bool ShadowMap::createDepthResources(const RenderDevice& dev, uint32_t size)
{
    // depth-only render pass：initialLayout=ATTACHMENT，finalLayout=READ_ONLY
    VkAttachmentDescription depth{};
    depth.format         = VK_FORMAT_D32_SFLOAT;
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;   // 每帧 loadOp=CLEAR 丢弃内容，无需逐帧布局转换
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &depth;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;
    if (vkCreateRenderPass(dev.device, &rpci, nullptr, &m_depthPass) != VK_SUCCESS)
    { LOG_ERROR("ShadowMap", "create", "深度 render pass 创建失败"); return false; }

    // 深度图像（ATTACHMENT | SAMPLED）
    createImage2D(dev.device, dev.physicalDevice, size, size, VK_FORMAT_D32_SFLOAT,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImage, m_depthMemory);
    if (m_depthImage == VK_NULL_HANDLE) return false;
    m_depthView = createImageView2D(dev.device, m_depthImage, VK_FORMAT_D32_SFLOAT,
                                    VK_IMAGE_ASPECT_DEPTH_BIT);
    if (m_depthView == VK_NULL_HANDLE) return false;

    // 比较采样器（PCF）
    VkSamplerCreateInfo sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter        = VK_FILTER_LINEAR;
    sci.minFilter        = VK_FILTER_LINEAR;
    sci.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.mipLodBias       = 0.0f;
    sci.compareEnable    = VK_TRUE;
    sci.compareOp        = VK_COMPARE_OP_LESS;
    sci.minLod           = 0.0f;
    sci.maxLod           = 1.0f;
    sci.borderColor      = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sci.unnormalizedCoordinates = VK_FALSE;
    if (vkCreateSampler(dev.device, &sci, nullptr, &m_depthSampler) != VK_SUCCESS)
    { LOG_ERROR("ShadowMap", "create", "深度采样器创建失败"); return false; }

    // 描述符（binding0 = 深度采样）
    VkDescriptorSetLayoutBinding db{};
    db.binding = 0;
    db.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    db.descriptorCount = 1;
    db.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1; dslci.pBindings = &db;
    if (vkCreateDescriptorSetLayout(dev.device, &dslci, nullptr, &m_descSetLayout) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(dev.device, &dpci, nullptr, &m_descPool) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &m_descSetLayout;
    if (vkAllocateDescriptorSets(dev.device, &ai, &m_descSet) != VK_SUCCESS) return false;

    VkDescriptorImageInfo dii{};
    dii.imageLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    dii.imageView     = m_depthView;
    dii.sampler       = m_depthSampler;
    VkWriteDescriptorSet wds{};
    wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet = m_descSet; wds.dstBinding = 0; wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev.device, 1, &wds, 0, nullptr);

    // framebuffer
    VkFramebufferCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = m_depthPass;
    fci.attachmentCount = 1; fci.pAttachments = &m_depthView;
    fci.width = size; fci.height = size; fci.layers = 1;
    if (vkCreateFramebuffer(dev.device, &fci, nullptr, &m_framebuffer) != VK_SUCCESS) return false;

    return true;
}

bool ShadowMap::createPipeline(const RenderDevice& dev, const std::string& shaderDir)
{
    VkShaderModule vs = createShaderModule(dev.device, shaderDir + "/shadow.vert.spv");
    VkShaderModule fs = createShaderModule(dev.device, shaderDir + "/shadow.frag.spv");

    // 顶点输入：Mesh3D 布局（stride 36：pos3+normal3+color3），取 pos
    VkVertexInputBindingDescription vib{};
    vib.binding = 0; vib.stride = 36;
    vib.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[1]{};
    attrs[0].binding = 0; attrs[0].location = 0;
    attrs[0].format  = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset  = 0;
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vib;
    vi.vertexAttributeDescriptionCount = 1; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // 描述符（binding0 = lightVP UBO）+ push constant（model mat4）
    VkDescriptorSetLayoutBinding db{};
    db.binding = 0;
    db.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    db.descriptorCount = 1;
    db.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1; dslci.pBindings = &db;
    VkDescriptorSetLayout uboLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(dev.device, &dslci, nullptr, &uboLayout) != VK_SUCCESS) return false;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset = 0; pcr.size = sizeof(glm::mat4);
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1; plci.pSetLayouts = &uboLayout;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(dev.device, &plci, nullptr, &m_depthLayout) != VK_SUCCESS)
    { vkDestroyDescriptorSetLayout(dev.device, uboLayout, nullptr); return false; }

    // 深度管线 UBO 描述符（lightVP）
    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &ps;
    VkDescriptorPool uboPool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(dev.device, &dpci, nullptr, &uboPool) != VK_SUCCESS)
    { vkDestroyDescriptorSetLayout(dev.device, uboLayout, nullptr); return false; }
    VkDescriptorSet uboSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = uboPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &uboLayout;
    if (vkAllocateDescriptorSets(dev.device, &ai, &uboSet) != VK_SUCCESS)
    {
        vkDestroyDescriptorPool(dev.device, uboPool, nullptr);
        vkDestroyDescriptorSetLayout(dev.device, uboLayout, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;   // 阴影 pass 双面渲染，避免背面自阴影裂缝
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthBiasEnable = VK_TRUE;
    rs.depthBiasConstantFactor = 1.25f;
    rs.depthBiasSlopeFactor    = 1.75f;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dync{};
    dync.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dync.dynamicStateCount = 2; dync.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = 2; pci.pStages = stages;
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = &ds;
    pci.pDynamicState       = &dync;
    pci.layout              = m_depthLayout;
    pci.renderPass          = m_depthPass;
    pci.subpass             = 0;

    VkResult r = vkCreateGraphicsPipelines(dev.device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_depthPipeline);
    vkDestroyShaderModule(dev.device, vs, nullptr);
    vkDestroyShaderModule(dev.device, fs, nullptr);
    if (r != VK_SUCCESS) return false;

    // 写入 lightVP UBO 描述符（binding 0）
    // 注意：m_lightVPBuffer 在 create() 中创建，晚于 createPipeline。
    // 这里只保存布局/池/集合句柄，描述符写入在 create() 完成。
    // 为避免重复创建池，这里把 uboSet 关联信息暂存后由 create() 补写。
    // —— 简化：改为在 create() 后由 setLightMatrix 首次调用时写入。
    m_depthDescPool = uboPool;
    m_depthDescLayout = uboLayout;
    m_depthUboSet = uboSet;
    return true;
}
