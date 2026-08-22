/**
 * skybox.cpp —— 程序化天空 + 环境光实现
 */
#include "render/skybox.h"

#include "engine/vulkan_util.h"
#include "engine/logger.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <stdexcept>

using namespace vulkan_util;

Skybox::~Skybox()
{
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pipeline)     vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout)       vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
    if (m_descPool)     vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
    if (m_uboBuffer)    vkDestroyBuffer(m_device, m_uboBuffer, nullptr);
    if (m_uboMemory)    vkFreeMemory(m_device, m_uboMemory, nullptr);
}

void Skybox::setSunDirection(const glm::vec3& dir)
{
    m_sunDir = glm::normalize(dir);
}

void Skybox::create(const RenderDevice& dev, VkRenderPass targetRenderPass,
                    const std::string& shaderDir,
                    VkSampleCountFlagBits samples)
{
    if (!createPipeline(dev, targetRenderPass, shaderDir, samples))
        throw std::runtime_error("Skybox::create 失败");

    VkDeviceSize sz = sizeof(SkyUBO);
    createBuffer(dev.device, dev.physicalDevice, sz, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_uboBuffer, m_uboMemory);
    if (m_uboBuffer == VK_NULL_HANDLE) throw std::runtime_error("Skybox UBO 创建失败");
    vkMapMemory(dev.device, m_uboMemory, 0, sz, 0, &m_uboMapped);

    VkDescriptorBufferInfo dbi{};
    dbi.buffer = m_uboBuffer; dbi.offset = 0; dbi.range = sz;
    VkWriteDescriptorSet wubo{};
    wubo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wubo.dstSet = m_descSet; wubo.dstBinding = 0; wubo.descriptorCount = 1;
    wubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wubo.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(dev.device, 1, &wubo, 0, nullptr);

    LOG_INFO("Skybox", "create", "程序化天空就绪");
}

void Skybox::draw(VkCommandBuffer cmd, const glm::mat4& view, float aspect) const
{
    if (m_pipeline == VK_NULL_HANDLE) return;

    // 相机旋转逆：dir_world = R^T * dir_view
    glm::mat3 rot = glm::mat3(view);              // 提取旋转部分
    glm::mat4 viewRotInv = glm::mat4(glm::transpose(rot));

    float yaw   = std::atan2(m_sunDir.z, m_sunDir.x);
    float pitch = std::asin(glm::clamp(m_sunDir.y, -1.0f, 1.0f));

    SkyUBO ubo{};
    ubo.viewRotInv    = viewRotInv;
    ubo.topColor      = glm::vec4(m_topColor, 1.0f);
    ubo.bottomColor   = glm::vec4(m_bottomColor, 1.0f);
    ubo.sunColor      = glm::vec4(m_sunColor, 1.0f);
    ubo.params        = glm::vec4(yaw, pitch, m_sunIntensity, m_horizonSpread);
    ubo.screen        = glm::vec4(aspect, 0.0f, 0.0f, 0.0f);
    std::memcpy(m_uboMapped, &ubo, sizeof(ubo));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout,
                            0, 1, &m_descSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

bool Skybox::createPipeline(const RenderDevice& dev, VkRenderPass renderPass,
                            const std::string& shaderDir,
                            VkSampleCountFlagBits samples)
{
    m_device = dev.device;

    VkDescriptorSetLayoutBinding db{};
    db.binding = 0;
    db.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    db.descriptorCount = 1;
    db.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1; dslci.pBindings = &db;
    if (vkCreateDescriptorSetLayout(dev.device, &dslci, nullptr, &m_descSetLayout) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(dev.device, &dpci, nullptr, &m_descPool) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &m_descSetLayout;
    if (vkAllocateDescriptorSets(dev.device, &ai, &m_descSet) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1; plci.pSetLayouts = &m_descSetLayout;
    if (vkCreatePipelineLayout(dev.device, &plci, nullptr, &m_layout) != VK_SUCCESS) return false;

    VkShaderModule vs = createShaderModule(dev.device, shaderDir + "/skybox.vert.spv");
    VkShaderModule fs = createShaderModule(dev.device, shaderDir + "/skybox.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
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
    rs.lineWidth   = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = samples;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                       | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;
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
    pci.pColorBlendState    = &cb;
    pci.pDynamicState       = &dync;
    pci.layout              = m_layout;
    pci.renderPass          = renderPass;
    pci.subpass             = 0;

    VkResult r = vkCreateGraphicsPipelines(dev.device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeline);
    vkDestroyShaderModule(dev.device, vs, nullptr);
    vkDestroyShaderModule(dev.device, fs, nullptr);
    if (r != VK_SUCCESS) { LOG_ERROR("Skybox", "create", "天空管线创建失败"); return false; }
    return true;
}
