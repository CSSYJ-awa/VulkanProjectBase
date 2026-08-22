/**
 * postfx.cpp —— 后处理特效链实现
 */
#include "render/postfx.h"

#include "engine/vulkan_util.h"
#include "engine/logger.h"

#include <cstring>
#include <stdexcept>

using namespace vulkan_util;

PostFx::~PostFx()
{
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pipeline)     vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout)       vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
    if (m_descPool)     vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
    if (m_uboBuffer)    vkDestroyBuffer(m_device, m_uboBuffer, nullptr);
    if (m_uboMemory)    vkFreeMemory(m_device, m_uboMemory, nullptr);
}

void PostFx::create(const RenderDevice& dev, VkRenderPass targetRenderPass,
                    const std::string& shaderDir)
{
    if (!createPipeline(dev, targetRenderPass, shaderDir))
        throw std::runtime_error("PostFx::create 失败");

    // UBO 缓冲（持久映射）
    VkDeviceSize size = sizeof(PostFxUBO);
    createBuffer(dev.device, dev.physicalDevice, size,
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_uboBuffer, m_uboMemory);
    if (m_uboBuffer == VK_NULL_HANDLE)
        throw std::runtime_error("PostFx::create UBO 缓冲创建失败");
    vkMapMemory(dev.device, m_uboMemory, 0, size, 0, &m_uboMapped);

    // 写入 UBO 描述符（固定）
    VkDescriptorBufferInfo dbi{};
    dbi.buffer = m_uboBuffer; dbi.offset = 0; dbi.range = sizeof(PostFxUBO);
    VkWriteDescriptorSet wubo{};
    wubo.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wubo.dstSet          = m_descSet;
    wubo.dstBinding      = 1;
    wubo.descriptorCount = 1;
    wubo.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wubo.pBufferInfo     = &dbi;
    vkUpdateDescriptorSets(dev.device, 1, &wubo, 0, nullptr);

    LOG_INFO("PostFx", "create", "后处理管线就绪");
}

void PostFx::apply(const RenderTarget& src, VkCommandBuffer cmd) const
{
    if (m_pipeline == VK_NULL_HANDLE) return;

    // 1. 更新输入纹理描述符
    VkDescriptorImageInfo dii{};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView   = src.colorView();
    dii.sampler     = src.colorSampler();
    VkWriteDescriptorSet wds{};
    wds.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet          = m_descSet;
    wds.dstBinding      = 0;
    wds.descriptorCount = 1;
    wds.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds.pImageInfo      = &dii;
    vkUpdateDescriptorSets(m_device, 1, &wds, 0, nullptr);

    // 2. 更新 UBO
    PostFxUBO ubo{};
    ubo.mode           = static_cast<int>(m_effect);
    ubo.intensity      = m_intensity;
    ubo.exposure       = m_toneMap ? m_exposure : 0.0f;
    ubo.bloomThreshold = m_bloomThreshold;
    ubo.texelW         = src.width()  ? 1.0f / src.width()  : 1.0f;
    ubo.texelH         = src.height() ? 1.0f / src.height() : 1.0f;
    std::memcpy(m_uboMapped, &ubo, sizeof(ubo));

    // 3. 绘制全屏三角形
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout,
                            0, 1, &m_descSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

bool PostFx::createPipeline(const RenderDevice& dev, VkRenderPass renderPass,
                            const std::string& shaderDir)
{
    m_device = dev.device;

    // 描述符布局：binding0 = sampler(scene)，binding1 = UBO
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 2;
    dslci.pBindings    = bindings;
    if (vkCreateDescriptorSetLayout(dev.device, &dslci, nullptr, &m_descSetLayout) != VK_SUCCESS)
    { LOG_ERROR("PostFx", "create", "描述符布局创建失败"); return false; }

    VkDescriptorPoolSize psize[2]{};
    psize[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; psize[0].descriptorCount = 1;
    psize[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         psize[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1; dpci.poolSizeCount = 2; dpci.pPoolSizes = psize;
    if (vkCreateDescriptorPool(dev.device, &dpci, nullptr, &m_descPool) != VK_SUCCESS)
    { LOG_ERROR("PostFx", "create", "描述符池创建失败"); return false; }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descPool; ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_descSetLayout;
    if (vkAllocateDescriptorSets(dev.device, &ai, &m_descSet) != VK_SUCCESS)
    { LOG_ERROR("PostFx", "create", "描述符集分配失败"); return false; }

    // 管线
    VkShaderModule vs = createShaderModule(dev.device, shaderDir + "/postfx.vert.spv");
    VkShaderModule fs = createShaderModule(dev.device, shaderDir + "/postfx.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

    VkPipelineLayoutCreateInfo plci{};
    plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts    = &m_descSetLayout;
    if (vkCreatePipelineLayout(dev.device, &plci, nullptr, &m_layout) != VK_SUCCESS)
    { vkDestroyShaderModule(dev.device, vs, nullptr);
      vkDestroyShaderModule(dev.device, fs, nullptr);
      LOG_ERROR("PostFx", "create", "管线布局创建失败"); return false; }

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;  // 无顶点输入

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                       | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = 2;
    pci.pStages             = stages;
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pColorBlendState    = &cb;
    pci.pDynamicState       = &dyn;
    pci.layout              = m_layout;
    pci.renderPass          = renderPass;
    pci.subpass             = 0;

    VkResult r = vkCreateGraphicsPipelines(dev.device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeline);
    vkDestroyShaderModule(dev.device, vs, nullptr);
    vkDestroyShaderModule(dev.device, fs, nullptr);
    if (r != VK_SUCCESS)
    { LOG_ERROR("PostFx", "create", "后处理管线创建失败"); return false; }
    return true;
}
