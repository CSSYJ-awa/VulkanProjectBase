/**
 * Pipelines 实现
 */
#include "pipelines.h"
#include "vulkan_util.h"

#include <stdexcept>
#include <array>

namespace
{
// 加载并附加着色器阶段
VkPipelineShaderStageCreateInfo
makeShaderStage(VkDevice device, const std::string& path, VkShaderStageFlagBits stage)
{
    auto mod = vulkan_util::createShaderModule(device, path);
    VkPipelineShaderStageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage = stage;
    ci.module = mod;
    ci.pName = "main";
    return ci;
}
} // namespace

// ============================================================================
// Pipeline2D
// ============================================================================

Pipeline2D::~Pipeline2D()
{
    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout)   vkDestroyPipelineLayout(m_device, m_layout, nullptr);
}

void Pipeline2D::create(VkDevice device, VkRenderPass renderPass,
                        VkExtent2D /*extent*/, const std::string& shaderDir,
                        VkPrimitiveTopology topology,
                        VkSampleCountFlagBits samples)
{
    m_device = device;
    auto vs = makeShaderStage(device, shaderDir + "/basic.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fs = makeShaderStage(device, shaderDir + "/basic.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = { vs, fs };

    // 顶点输入：vec2 pos + vec3 color
    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = sizeof(float) * 5; // 2 + 3
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attrs;
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset   = 0;
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = sizeof(float) * 2;

    VkPipelineVertexInputStateCreateInfo vis{};
    vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vis.vertexBindingDescriptionCount = 1;
    vis.pVertexBindingDescriptions = &bind;
    vis.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vis.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ias{};
    ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ias.topology = topology;
    ias.primitiveRestartEnable = VK_FALSE;

    VkViewport vp{};
    VkRect2D sc{};
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.pViewports = &vp;
    vps.scissorCount = 1;
    vps.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = samples;

    VkPipelineColorBlendAttachmentState ca{};
    ca.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    ca.blendEnable = VK_TRUE;
    ca.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ca.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ca.colorBlendOp = VK_BLEND_OP_ADD;
    ca.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ca.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    ca.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cbs{};
    cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbs.attachmentCount = 1;
    cbs.pAttachments = &ca;

    // push constants: vec4 color
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.offset = 0;
    pc.size = sizeof(float) * 4;

    VkPipelineLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &pc;

    if (vkCreatePipelineLayout(device, &lci, nullptr, &m_layout) != VK_SUCCESS)
        throw std::runtime_error("Pipeline2D: vkCreatePipelineLayout 失败");

    // 动态状态
    VkDynamicState dyns[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsc{};
    dsc.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsc.dynamicStateCount = 2;
    dsc.pDynamicStates = dyns;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vis;
    pci.pInputAssemblyState = &ias;
    pci.pViewportState = &vps;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &cbs;
    pci.pDynamicState = &dsc;
    pci.layout = m_layout;
    pci.renderPass = renderPass;
    pci.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("Pipeline2D: vkCreateGraphicsPipelines 失败");

    vkDestroyShaderModule(device, vs.module, nullptr);
    vkDestroyShaderModule(device, fs.module, nullptr);
}

// ============================================================================
// Pipeline3D
// ============================================================================

Pipeline3D::~Pipeline3D()
{
    if (m_pipeline)        vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout)          vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descSetLayout)   vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
}

void Pipeline3D::create(VkDevice device, VkRenderPass renderPass,
                        VkExtent2D /*extent*/, const std::string& shaderDir,
                        VkSampleCountFlagBits samples)
{
    m_device = device;
    auto vs = makeShaderStage(device, shaderDir + "/mesh3d.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fs = makeShaderStage(device, shaderDir + "/mesh3d.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = { vs, fs };

    // 顶点输入：vec3 pos + vec3 normal + vec3 color（每顶点 9 float）
    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = sizeof(float) * 9;
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attrs;
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = 0;
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = sizeof(float) * 3;
    attrs[2].location = 2;
    attrs[2].binding  = 0;
    attrs[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[2].offset   = sizeof(float) * 6;

    VkPipelineVertexInputStateCreateInfo vis{};
    vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vis.vertexBindingDescriptionCount = 1;
    vis.pVertexBindingDescriptions = &bind;
    vis.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vis.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ias{};
    ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ias.primitiveRestartEnable = VK_FALSE;

    VkViewport vp{};
    VkRect2D sc{};
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.pViewports = &vp;
    vps.scissorCount = 1;
    vps.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = samples;

    VkPipelineColorBlendAttachmentState ca{};
    ca.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    ca.blendEnable = VK_TRUE;
    ca.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ca.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ca.colorBlendOp = VK_BLEND_OP_ADD;
    ca.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ca.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    ca.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cbs{};
    cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbs.attachmentCount = 1;
    cbs.pAttachments = &ca;

    // 深度测试：3D 遮挡（与渲染通道深度附件配套）
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    // UBO 描述符集合（binding 0 = MVP+光照；binding 1 = 材质纹理，v1.0.2）
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsci{};
    dsci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsci.bindingCount = 2;
    dsci.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &dsci, nullptr, &m_descSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Pipeline3D: vkCreateDescriptorSetLayout 失败");

    // push constants: vec4 color
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.offset = 0;
    pc.size = sizeof(float) * 4;

    VkPipelineLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &m_descSetLayout;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &pc;

    if (vkCreatePipelineLayout(device, &lci, nullptr, &m_layout) != VK_SUCCESS)
        throw std::runtime_error("Pipeline3D: vkCreatePipelineLayout 失败");

    VkDynamicState dyns[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsc{};
    dsc.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsc.dynamicStateCount = 2;
    dsc.pDynamicStates = dyns;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vis;
    pci.pInputAssemblyState = &ias;
    pci.pViewportState = &vps;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cbs;
    pci.pDynamicState = &dsc;
    pci.layout = m_layout;
    pci.renderPass = renderPass;
    pci.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("Pipeline3D: vkCreateGraphicsPipelines 失败");

    vkDestroyShaderModule(device, vs.module, nullptr);
    vkDestroyShaderModule(device, fs.module, nullptr);
}

// ============================================================================
// PipelineText
// ============================================================================

PipelineText::~PipelineText()
{
    if (m_pipeline)       vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout)         vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descSetLayout)  vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
    if (m_descPool)       vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
}

void PipelineText::create(VkDevice device, VkRenderPass renderPass,
                          VkExtent2D /*extent*/, const std::string& shaderDir,
                          VkSampleCountFlagBits samples)
{
    m_device = device;
    auto vs = makeShaderStage(device, shaderDir + "/text.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    auto fs = makeShaderStage(device, shaderDir + "/text.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineShaderStageCreateInfo stages[] = { vs, fs };

    // 顶点输入：vec2 pos + vec2 tex
    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = sizeof(float) * 4;
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attrs;
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset   = 0;
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset   = sizeof(float) * 2;

    VkPipelineVertexInputStateCreateInfo vis{};
    vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vis.vertexBindingDescriptionCount = 1;
    vis.pVertexBindingDescriptions = &bind;
    vis.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vis.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ias{};
    ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ias.primitiveRestartEnable = VK_FALSE;

    VkViewport vp{};
    VkRect2D sc{};
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.pViewports = &vp;
    vps.scissorCount = 1;
    vps.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = samples;

    VkPipelineColorBlendAttachmentState ca{};
    ca.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    ca.blendEnable = VK_TRUE;
    ca.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ca.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ca.colorBlendOp = VK_BLEND_OP_ADD;
    ca.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ca.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    ca.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cbs{};
    cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbs.attachmentCount = 1;
    cbs.pAttachments = &ca;

    // 采样器描述符
    VkDescriptorSetLayoutBinding samp{};
    samp.binding = 0;
    samp.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samp.descriptorCount = 1;
    samp.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsci{};
    dsci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsci.bindingCount = 1;
    dsci.pBindings = &samp;
    if (vkCreateDescriptorSetLayout(device, &dsci, nullptr, &m_descSetLayout) != VK_SUCCESS)
        throw std::runtime_error("PipelineText: vkCreateDescriptorSetLayout 失败");

    // push constants: vec4 color
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.offset = 0;
    pc.size = sizeof(float) * 4;

    VkPipelineLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &m_descSetLayout;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(device, &lci, nullptr, &m_layout) != VK_SUCCESS)
        throw std::runtime_error("PipelineText: vkCreatePipelineLayout 失败");

    VkDynamicState dyns[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsc{};
    dsc.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsc.dynamicStateCount = 2;
    dsc.pDynamicStates = dyns;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vis;
    pci.pInputAssemblyState = &ias;
    pci.pViewportState = &vps;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &cbs;
    pci.pDynamicState = &dsc;
    pci.layout = m_layout;
    pci.renderPass = renderPass;
    pci.subpass = 0;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("PipelineText: vkCreateGraphicsPipelines 失败");

    vkDestroyShaderModule(device, vs.module, nullptr);
    vkDestroyShaderModule(device, fs.module, nullptr);

    // 描述符池
    VkDescriptorPoolSize dps{};
    dps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dps.descriptorCount = 8;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 8;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &m_descPool) != VK_SUCCESS)
        throw std::runtime_error("PipelineText: vkCreateDescriptorPool 失败");
}

VkDescriptorSet PipelineText::allocateDescriptorSet()
{
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_descSetLayout;
    VkDescriptorSet ds = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &ai, &ds) != VK_SUCCESS)
        throw std::runtime_error("allocateDescriptorSet 失败");
    return ds;
}
