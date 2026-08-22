/**
 * instancing.cpp —— 实例化渲染实现
 */
#include "render/instancing.h"

#include "engine/vulkan_util.h"
#include "engine/logger.h"

#include <cstring>
#include <stdexcept>

using namespace vulkan_util;

Instancing::~Instancing()
{
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pipeline)     vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout)       vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
    if (m_descPool)     vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
    if (m_uboBuffer)    vkDestroyBuffer(m_device, m_uboBuffer, nullptr);
    if (m_uboMemory)    vkFreeMemory(m_device, m_uboMemory, nullptr);
    if (m_meshBuffer)   vkDestroyBuffer(m_device, m_meshBuffer, nullptr);
    if (m_meshMemory)   vkFreeMemory(m_device, m_meshMemory, nullptr);
    if (m_indexBuffer)  vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
    if (m_indexMemory)  vkFreeMemory(m_device, m_indexMemory, nullptr);
    if (m_instBuffer)   vkDestroyBuffer(m_device, m_instBuffer, nullptr);
    if (m_instMemory)
    {
        if (m_instMapped) { vkUnmapMemory(m_device, m_instMemory); m_instMapped = nullptr; }
        vkFreeMemory(m_device, m_instMemory, nullptr);
    }
}

void Instancing::create(const RenderDevice& dev, VkRenderPass targetRenderPass,
                        const std::string& shaderDir,
                        VkSampleCountFlagBits samples)
{
    if (!createPipeline(dev, targetRenderPass, shaderDir, samples))
        throw std::runtime_error("Instancing::create 失败");

    VkDeviceSize sz = sizeof(InstanceUBO);
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

    LOG_INFO("Instancing", "create", "实例化管线就绪");
}

void Instancing::uploadMesh(const RenderDevice& dev,
                            const std::vector<float>& vertices,
                            const std::vector<uint32_t>& indices)
{
    if (vertices.empty() || (vertices.size() % 9) != 0 || m_device == VK_NULL_HANDLE) return;

    // 重复调用时先释放旧缓冲，避免句柄覆盖泄漏
    if (m_meshBuffer) { vkDestroyBuffer(m_device, m_meshBuffer, nullptr); m_meshBuffer = VK_NULL_HANDLE; }
    if (m_meshMemory) { vkFreeMemory(m_device, m_meshMemory, nullptr);     m_meshMemory = VK_NULL_HANDLE; }
    if (m_indexBuffer) { vkDestroyBuffer(m_device, m_indexBuffer, nullptr); m_indexBuffer = VK_NULL_HANDLE; }
    if (m_indexMemory) { vkFreeMemory(m_device, m_indexMemory, nullptr);    m_indexMemory = VK_NULL_HANDLE; }

    VkDeviceSize size = vertices.size() * sizeof(float);
    createBuffer(dev.device, dev.physicalDevice, size,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_meshBuffer, m_meshMemory);
    void* dst = nullptr;
    if (m_meshBuffer == VK_NULL_HANDLE ||
        vkMapMemory(dev.device, m_meshMemory, 0, size, 0, &dst) != VK_SUCCESS)
        return;
    std::memcpy(dst, vertices.data(), size);
    vkUnmapMemory(dev.device, m_meshMemory);
    m_meshCount = static_cast<uint32_t>(vertices.size() / 9);

    m_hasIndices = !indices.empty();
    if (m_hasIndices)
    {
        VkDeviceSize isz = indices.size() * sizeof(uint32_t);
        createBuffer(dev.device, dev.physicalDevice, isz,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_indexBuffer, m_indexMemory);
        void* idst = nullptr;
        if (m_indexBuffer == VK_NULL_HANDLE ||
            vkMapMemory(dev.device, m_indexMemory, 0, isz, 0, &idst) != VK_SUCCESS)
            return;
        std::memcpy(idst, indices.data(), isz);
        vkUnmapMemory(dev.device, m_indexMemory);
        m_indexCount = static_cast<uint32_t>(indices.size());
    }
}

void Instancing::setInstances(const RenderDevice& dev,
                              const glm::mat4* transforms, size_t count,
                              const glm::vec4* colors)
{
    if (count == 0 || !transforms) return;

    // 实例缓冲布局：mat4 + vec4 颜色（对齐 64+16=80）
    const VkDeviceSize perInst = sizeof(glm::mat4) + sizeof(glm::vec4);
    VkDeviceSize need = perInst * count;
    if (count > m_capacity || m_instBuffer == VK_NULL_HANDLE)
    {
        if (m_instBuffer) { vkDestroyBuffer(m_device, m_instBuffer, nullptr); m_instBuffer = VK_NULL_HANDLE; }
        if (m_instMemory)
        {
            if (m_instMapped) { vkUnmapMemory(m_device, m_instMemory); m_instMapped = nullptr; }
            vkFreeMemory(m_device, m_instMemory, nullptr);
            m_instMemory = VK_NULL_HANDLE;
        }
        createBuffer(dev.device, dev.physicalDevice, need,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_instBuffer, m_instMemory);
        // 性能优化：持久映射实例缓冲，每帧更新零 vkMapMemory/vkUnmapMemory 开销
        vkMapMemory(dev.device, m_instMemory, 0, need, 0, &m_instMapped);
        m_capacity = count;
    }

    if (m_instMapped == nullptr) return;   // 扩容/映射失败时安全退出
    uint8_t* p = static_cast<uint8_t*>(m_instMapped);
    for (size_t i = 0; i < count; ++i)
    {
        std::memcpy(p, &transforms[i], sizeof(glm::mat4));
        glm::vec4 c = colors ? colors[i] : glm::vec4(1.0f);
        std::memcpy(p + sizeof(glm::mat4), &c, sizeof(glm::vec4));
        p += perInst;
    }
}

void Instancing::setLight(const glm::vec3& lightDir, const glm::vec3& lightColor, float ambient)
{
    m_lightDir = lightDir; m_lightColor = lightColor; m_ambient = ambient;
}

void Instancing::draw(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj,
                      size_t instanceCount) const
{
    if (m_pipeline == VK_NULL_HANDLE || m_meshBuffer == VK_NULL_HANDLE ||
        instanceCount == 0 || instanceCount > m_capacity)
        return;

    InstanceUBO ubo{};
    ubo.view       = view;
    ubo.proj       = proj;
    ubo.lightDir   = m_lightDir;
    ubo.ambient    = m_ambient;
    ubo.lightColor = m_lightColor;
    std::memcpy(m_uboMapped, &ubo, sizeof(ubo));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout,
                            0, 1, &m_descSet, 0, nullptr);

    VkBuffer vbs[2] = { m_meshBuffer, m_instBuffer };
    VkDeviceSize offsets[2] = { 0, 0 };
    vkCmdBindVertexBuffers(cmd, 0, 2, vbs, offsets);
    if (m_hasIndices)
    {
        vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_indexCount, static_cast<uint32_t>(instanceCount), 0, 0, 0);
    }
    else
    {
        vkCmdDraw(cmd, m_meshCount, static_cast<uint32_t>(instanceCount), 0, 0);
    }
}

bool Instancing::createPipeline(const RenderDevice& dev, VkRenderPass renderPass,
                                const std::string& shaderDir,
                                VkSampleCountFlagBits samples)
{
    m_device = dev.device;

    // 描述符：binding0 = UBO（view/proj/光照）
    VkDescriptorSetLayoutBinding db{};
    db.binding = 0;
    db.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    db.descriptorCount = 1;
    db.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
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

    // 顶点输入：binding0 网格（stride36），binding1 实例 mat4，binding2 实例 vec4
    VkVertexInputBindingDescription vbs[3]{};
    vbs[0].binding = 0; vbs[0].stride = 36; vbs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vbs[1].binding = 1; vbs[1].stride = sizeof(glm::mat4); vbs[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    vbs[2].binding = 2; vbs[2].stride = sizeof(glm::vec4); vbs[2].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[8]{};
    // 网格：pos(0) normal(1) color(2)
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = 12;
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[2].offset = 24;
    // 实例 mat4（locations 3..6）
    for (int i = 0; i < 4; ++i)
    {
        attrs[3 + i].location = 3 + i;
        attrs[3 + i].binding  = 1;
        attrs[3 + i].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[3 + i].offset   = static_cast<uint32_t>(sizeof(glm::vec4) * i);
    }
    // 实例颜色
    attrs[7].location = 7; attrs[7].binding = 2; attrs[7].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[7].offset = 0;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 3; vi.pVertexBindingDescriptions = vbs;
    vi.vertexAttributeDescriptionCount = 8; vi.pVertexAttributeDescriptions = attrs;

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
    ms.rasterizationSamples = samples;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

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

    VkShaderModule vs = createShaderModule(dev.device, shaderDir + "/instanced.vert.spv");
    VkShaderModule fs = createShaderModule(dev.device, shaderDir + "/instanced.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

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
    pci.renderPass          = renderPass;
    pci.subpass             = 0;

    VkResult r = vkCreateGraphicsPipelines(dev.device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeline);
    vkDestroyShaderModule(dev.device, vs, nullptr);
    vkDestroyShaderModule(dev.device, fs, nullptr);
    if (r != VK_SUCCESS) { LOG_ERROR("Instancing", "create", "实例化管线创建失败"); return false; }
    return true;
}
