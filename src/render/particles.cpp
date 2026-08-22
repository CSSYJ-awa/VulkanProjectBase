/**
 * particles.cpp —— 2D/3D 粒子系统实现
 */
#include "render/particles.h"

#include "engine/vulkan_util.h"
#include "engine/logger.h"

#include <cmath>
#include <cstring>
#include <random>
#include <stdexcept>

using namespace vulkan_util;

ParticleSystem::~ParticleSystem()
{
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pipeline)     vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout)       vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
    if (m_descPool)     vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
    if (m_uboBuffer)    vkDestroyBuffer(m_device, m_uboBuffer, nullptr);
    if (m_uboMemory)    vkFreeMemory(m_device, m_uboMemory, nullptr);
    if (m_vb)           vkDestroyBuffer(m_device, m_vb, nullptr);
    if (m_vbMemory)     vkFreeMemory(m_device, m_vbMemory, nullptr);
}

void ParticleSystem::create(const RenderDevice& dev, VkRenderPass targetRenderPass,
                            const std::string& shaderDir,
                            VkSampleCountFlagBits samples)
{
    m_device = dev.device;

    // 描述符：binding0 = UBO，binding1 = 粒子纹理 sampler
    VkDescriptorSetLayoutBinding db[2]{};
    db[0].binding = 0;
    db[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    db[0].descriptorCount = 1;
    db[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    db[1].binding = 1;
    db[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    db[1].descriptorCount = 1;
    db[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 2; dslci.pBindings = db;
    if (vkCreateDescriptorSetLayout(dev.device, &dslci, nullptr, &m_descSetLayout) != VK_SUCCESS)
        throw std::runtime_error("ParticleSystem 描述符布局创建失败");

    VkDescriptorPoolSize ps[2]{};
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         ps[0].descriptorCount = 1;
    ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1; dpci.poolSizeCount = 2; dpci.pPoolSizes = ps;
    if (vkCreateDescriptorPool(dev.device, &dpci, nullptr, &m_descPool) != VK_SUCCESS)
        throw std::runtime_error("ParticleSystem 描述符池创建失败");

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &m_descSetLayout;
    if (vkAllocateDescriptorSets(dev.device, &ai, &m_descSet) != VK_SUCCESS)
        throw std::runtime_error("ParticleSystem 描述符集分配失败");

    // UBO
    VkDeviceSize sz = sizeof(ParticleUBO);
    createBuffer(dev.device, dev.physicalDevice, sz, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_uboBuffer, m_uboMemory);
    vkMapMemory(dev.device, m_uboMemory, 0, sz, 0, &m_uboMapped);

    VkDescriptorBufferInfo dbi{};
    dbi.buffer = m_uboBuffer; dbi.offset = 0; dbi.range = sz;
    VkWriteDescriptorSet wubo{};
    wubo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wubo.dstSet = m_descSet; wubo.dstBinding = 0; wubo.descriptorCount = 1;
    wubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wubo.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(dev.device, 1, &wubo, 0, nullptr);

    // 回退白纹理
    uint8_t white[4] = { 255, 255, 255, 255 };
    m_fallbackTex = Texture::create(dev, 1, 1, white);
    m_tex = m_fallbackTex.get();
    writeTexDescriptor(dev);

    // 管线
    {
        VkShaderModule vs = createShaderModule(dev.device, shaderDir + "/particle.vert.spv");
        VkShaderModule fs = createShaderModule(dev.device, shaderDir + "/particle.frag.spv");

        VkVertexInputBindingDescription vib{};
        vib.binding = 0; vib.stride = 32;
        vib.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32_SFLOAT;       attrs[1].offset = 12;
        attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[2].offset = 16;
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vib;
        vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1; plci.pSetLayouts = &m_descSetLayout;
        if (vkCreatePipelineLayout(dev.device, &plci, nullptr, &m_layout) != VK_SUCCESS)
            throw std::runtime_error("ParticleSystem 管线布局创建失败");

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
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = samples;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_TRUE;
        ds.depthWriteEnable = VK_FALSE;   // 粒子不写深度（半透明叠加）
        ds.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable = VK_TRUE;                       // 加法混合（发光效果）
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.colorBlendOp        = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.alphaBlendOp        = VK_BLEND_OP_ADD;
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
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cb;
        pci.pDynamicState       = &dync;
        pci.layout              = m_layout;
        pci.renderPass          = targetRenderPass;
        pci.subpass             = 0;

        VkResult r = vkCreateGraphicsPipelines(dev.device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeline);
        vkDestroyShaderModule(dev.device, vs, nullptr);
        vkDestroyShaderModule(dev.device, fs, nullptr);
        if (r != VK_SUCCESS) throw std::runtime_error("ParticleSystem 管线创建失败");
    }

    LOG_INFO("ParticleSystem", "create", "粒子系统就绪");
}

void ParticleSystem::setCapacity(const RenderDevice& dev, size_t max)
{
    if (max <= m_capacity && !m_pool.empty()) return;
    m_pool.resize(max);
    m_capacity = max;
    if (m_alive > max) m_alive = max;
    rebuildBuffer(dev);
}

void ParticleSystem::setTexture(const RenderDevice& dev, const Texture* tex)
{
    m_tex = tex ? tex : m_fallbackTex.get();
    writeTexDescriptor(dev);
}

void ParticleSystem::spawn(const RenderDevice& dev,
                           const glm::vec3& origin, const glm::vec3& baseVel, float spread,
                           size_t count, float life, float size,
                           const glm::vec4& startColor, const glm::vec4& endColor,
                           float gravity)
{
    if (m_pool.empty()) return;
    std::mt19937 rng(static_cast<uint32_t>(m_pool.size()) * 2654435761u ^ 0x9E3779B9u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    size_t can = count;
    if (m_alive + can > m_capacity) can = m_capacity - m_alive;
    for (size_t i = 0; i < can; ++i)
    {
        Particle p;
        p.position   = origin + glm::vec3(dist(rng), dist(rng), dist(rng)) * spread;
        p.velocity   = baseVel + glm::vec3(dist(rng), dist(rng), dist(rng)) * spread;
        p.size       = size * (0.7f + 0.6f * std::abs(dist(rng)));
        p.life       = life;
        p.maxLife    = life;
        p.gravity    = gravity;
        p.startColor = startColor;
        p.endColor   = endColor;
        p.color      = startColor;
        m_pool[m_alive + i] = p;
    }
    m_alive += can;
    m_dirty = true;
}

void ParticleSystem::update(float dt)
{
    if (m_alive == 0) return;
    size_t w = 0;
    for (size_t i = 0; i < m_alive; ++i)
    {
        Particle p = m_pool[i];
        p.life -= dt;
        if (p.life <= 0.0f) continue;
        p.velocity.y += p.gravity * dt;
        p.position   += p.velocity * dt;
        float t = 1.0f - p.life / p.maxLife;
        if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
        p.color = p.startColor * (1.0f - t) + p.endColor * t;
        m_pool[w++] = p;
    }
    m_alive = w;
    m_dirty = true;
}

void ParticleSystem::draw(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj,
                          float viewportHeight) const
{
    if (m_pipeline == VK_NULL_HANDLE || m_alive == 0 || m_vb == VK_NULL_HANDLE) return;

    ParticleUBO ubo{};
    ubo.view       = view;
    ubo.proj       = proj;
    ubo.sizeScale  = viewportHeight * 0.5f;
    ubo.texEnabled = (m_tex && m_tex != m_fallbackTex.get()) ? 1.0f : 0.0f;
    std::memcpy(m_uboMapped, &ubo, sizeof(ubo));

    // 更新顶点缓冲（pos3 + size1 + color4，8 float/粒子）
    if (m_dirty && m_vbMapped)
    {
        float* dst = static_cast<float*>(m_vbMapped);
        for (size_t i = 0; i < m_alive; ++i)
        {
            const Particle& p = m_pool[i];
            dst[i * 8 + 0] = p.position.x;
            dst[i * 8 + 1] = p.position.y;
            dst[i * 8 + 2] = p.position.z;
            dst[i * 8 + 3] = p.size;
            dst[i * 8 + 4] = p.color.r;
            dst[i * 8 + 5] = p.color.g;
            dst[i * 8 + 6] = p.color.b;
            dst[i * 8 + 7] = p.color.a;
        }
        m_dirty = false;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout,
                            0, 1, &m_descSet, 0, nullptr);
    VkBuffer vb = m_vb;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &off);
    vkCmdDraw(cmd, static_cast<uint32_t>(m_alive), 1, 0, 0);
}

void ParticleSystem::writeTexDescriptor(const RenderDevice& dev)
{
    if (!m_tex) return;
    VkDescriptorImageInfo dii{};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView   = m_tex->view();
    dii.sampler     = m_tex->sampler();
    VkWriteDescriptorSet wds{};
    wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet = m_descSet; wds.dstBinding = 1; wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev.device, 1, &wds, 0, nullptr);
}

void ParticleSystem::rebuildBuffer(const RenderDevice& dev)
{
    if (m_vb) { vkDestroyBuffer(dev.device, m_vb, nullptr); m_vb = VK_NULL_HANDLE; }
    if (m_vbMemory) { vkFreeMemory(dev.device, m_vbMemory, nullptr); m_vbMemory = VK_NULL_HANDLE; }
    if (m_capacity == 0) return;
    VkDeviceSize size = m_capacity * 8 * sizeof(float);
    createBuffer(dev.device, dev.physicalDevice, size,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_vb, m_vbMemory);
    if (m_vb != VK_NULL_HANDLE)
        vkMapMemory(dev.device, m_vbMemory, 0, size, 0, &m_vbMapped);
    m_dirty = true;
}
