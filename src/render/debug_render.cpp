/**
 * debug_render.cpp —— 调试可视化渲染器实现
 */
#include "render/debug_render.h"

#include "engine/vulkan_util.h"
#include "engine/logger.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

using namespace vulkan_util;

namespace {
constexpr float kPi = 3.14159265358979f;
}

DebugRenderer::~DebugRenderer() { destroy(); }

void DebugRenderer::create(const RenderDevice& dev, VkRenderPass targetRenderPass,
                           const std::string& shaderDir,
                           VkSampleCountFlagBits samples)
{
    m_physicalDevice = dev.physicalDevice;
    if (!createPipeline(dev, targetRenderPass, shaderDir, samples))
        throw std::runtime_error("DebugRenderer::create 失败");

    // UBO（view/proj）
    VkDeviceSize uboSize = sizeof(glm::mat4) * 2;
    createBuffer(dev.device, dev.physicalDevice, uboSize,
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_uboBuffer, m_uboMemory);
    vkMapMemory(dev.device, m_uboMemory, 0, uboSize, 0, &m_uboMapped);

    VkDescriptorBufferInfo dbi{};
    dbi.buffer = m_uboBuffer; dbi.offset = 0; dbi.range = uboSize;
    VkWriteDescriptorSet wubo{};
    wubo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wubo.dstSet = m_descSet; wubo.dstBinding = 0; wubo.descriptorCount = 1;
    wubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wubo.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(dev.device, 1, &wubo, 0, nullptr);

    // 顶点缓冲（HOST_VISIBLE 持久映射，容量不足自动扩容）
    m_capacity = 2048;   // 2048 顶点 = 1024 线段
    createBuffer(dev.device, dev.physicalDevice,
                 m_capacity * 6 * sizeof(float),
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_vb, m_vbMemory);
    vkMapMemory(dev.device, m_vbMemory, 0, m_capacity * 6 * sizeof(float), 0, &m_vbMapped);

    LOG_INFO("DebugRenderer", "create", "调试渲染器就绪");
}

void DebugRenderer::clear()
{
    m_vertices.clear();
    m_lineCount = 0;
}

void DebugRenderer::line(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color)
{
    m_vertices.insert(m_vertices.end(), { a.x, a.y, a.z, color.r, color.g, color.b,
                                          b.x, b.y, b.z, color.r, color.g, color.b });
    ++m_lineCount;
}

void DebugRenderer::box(const glm::vec3& c, const glm::vec3& he, const glm::vec3& color)
{
    aabb(c - he, c + he, color);
}

void DebugRenderer::aabb(const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color)
{
    const glm::vec3 p[8] = {
        { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
        { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z },
    };
    const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},   // 底
        {4,5},{5,6},{6,7},{7,4},   // 顶
        {0,4},{1,5},{2,6},{3,7},   // 竖
    };
    for (auto& e : edges)
        line(p[e[0]], p[e[1]], color);
}

void DebugRenderer::circle(const glm::vec3& center, const glm::vec3& axis,
                           float radius, const glm::vec3& color, int segments)
{
    glm::vec3 n = glm::normalize(axis);
    glm::vec3 t1 = std::fabs(n.y) < 0.999f ? glm::normalize(glm::cross(n, glm::vec3(0,1,0)))
                                           : glm::normalize(glm::cross(n, glm::vec3(1,0,0)));
    glm::vec3 t2 = glm::normalize(glm::cross(n, t1));
    glm::vec3 prev = center + t1 * radius;
    for (int i = 1; i <= segments; ++i)
    {
        float a = static_cast<float>(i) / segments * 2.0f * kPi;
        glm::vec3 p = center + (t1 * std::cos(a) + t2 * std::sin(a)) * radius;
        line(prev, p, color);
        prev = p;
    }
}

void DebugRenderer::sphere(const glm::vec3& center, float radius,
                           const glm::vec3& color, int segments)
{
    circle(center, glm::vec3(1,0,0), radius, color, segments);
    circle(center, glm::vec3(0,1,0), radius, color, segments);
    circle(center, glm::vec3(0,0,1), radius, color, segments);
}

void DebugRenderer::grid(float halfSize, float step, const glm::vec3& color)
{
    for (float x = -halfSize; x <= halfSize; x += step)
    {
        line({ x, 0, -halfSize }, { x, 0, halfSize }, color);
        line({ -halfSize, 0, x }, { halfSize, 0, x }, color);
    }
}

void DebugRenderer::axes(const glm::vec3& origin, float size)
{
    line(origin, origin + glm::vec3(size, 0, 0), { 1, 0.2f, 0.2f });
    line(origin, origin + glm::vec3(0, size, 0), { 0.2f, 1, 0.2f });
    line(origin, origin + glm::vec3(0, 0, size), { 0.2f, 0.4f, 1 });
}

void DebugRenderer::normal(const glm::vec3& pos, const glm::vec3& dir,
                           float len, const glm::vec3& color)
{
    line(pos, pos + glm::normalize(dir) * len, color);
}

void DebugRenderer::render(VkCommandBuffer cmd, const glm::mat4& view,
                           const glm::mat4& proj) const
{
    if (m_pipeline == VK_NULL_HANDLE || m_lineCount == 0) return;

    // 上传顶点（容量不足时重建）
    const size_t vertCount = m_vertices.size() / 6;
    if (vertCount > m_capacity)
    {
        // 懒扩容失败（分配/映射失败）：跳过本帧绘制，避免越界写旧缓冲
        if (!const_cast<DebugRenderer*>(this)->grow(vertCount)) return;
    }
    std::memcpy(m_vbMapped, m_vertices.data(), m_vertices.size() * sizeof(float));

    // UBO
    struct VP { glm::mat4 view; glm::mat4 proj; };
    VP vp{ view, proj };
    std::memcpy(m_uboMapped, &vp, sizeof(vp));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout,
                            0, 1, &m_descSet, 0, nullptr);
    VkBuffer bufs[] = { m_vb };
    VkDeviceSize offs[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offs);
    vkCmdDraw(cmd, static_cast<uint32_t>(vertCount), 1, 0, 0);
}

bool DebugRenderer::grow(uint32_t minVerts)
{
    if (m_device == VK_NULL_HANDLE) return false;
    uint32_t newCap = std::max(m_capacity * 2, minVerts);
    VkDeviceSize size = newCap * 6 * sizeof(float);
    VkBuffer nb = VK_NULL_HANDLE; VkDeviceMemory nm = VK_NULL_HANDLE;
    createBuffer(m_device, m_physicalDevice, size,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 nb, nm);
    if (nb == VK_NULL_HANDLE || nm == VK_NULL_HANDLE)
    {
        if (nb) vkDestroyBuffer(m_device, nb, nullptr);
        if (nm) vkFreeMemory(m_device, nm, nullptr);
        return false;
    }
    void* mapped = nullptr;
    if (vkMapMemory(m_device, nm, 0, size, 0, &mapped) != VK_SUCCESS)
    {
        vkDestroyBuffer(m_device, nb, nullptr);
        vkFreeMemory(m_device, nm, nullptr);
        return false;
    }
    if (m_vbMapped) vkUnmapMemory(m_device, m_vbMemory);
    if (m_vb) vkDestroyBuffer(m_device, m_vb, nullptr);
    if (m_vbMemory) vkFreeMemory(m_device, m_vbMemory, nullptr);
    m_vb = nb; m_vbMemory = nm; m_capacity = newCap;
    m_vbMapped = mapped;
    return true;
}

bool DebugRenderer::createPipeline(const RenderDevice& dev, VkRenderPass renderPass,
                                   const std::string& shaderDir,
                                   VkSampleCountFlagBits samples)
{
    m_device = dev.device;

    // 描述符：binding 0 = UBO（view/proj）
    VkDescriptorSetLayoutBinding db{};
    db.binding = 0;
    db.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    db.descriptorCount = 1;
    db.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
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

    // 顶点输入：pos3 (loc0) + color3 (loc1)，stride 24
    VkVertexInputBindingDescription bind{};
    bind.binding = 0; bind.stride = 24; bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = 12;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;   // 线框不需要剔除
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = samples;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;    // 调试线不写入深度
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

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

    VkShaderModule vs = createShaderModule(dev.device, shaderDir + "/debug.vert.spv");
    VkShaderModule fs = createShaderModule(dev.device, shaderDir + "/debug.frag.spv");

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
    if (r != VK_SUCCESS) { LOG_ERROR("DebugRenderer", "create", "调试管线创建失败"); return false; }
    return true;
}

void DebugRenderer::destroy()
{
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pipeline)     vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout)       vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_descSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
    if (m_descPool)     vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
    if (m_uboBuffer)    vkDestroyBuffer(m_device, m_uboBuffer, nullptr);
    if (m_uboMemory)    vkFreeMemory(m_device, m_uboMemory, nullptr);
    if (m_vb)           vkDestroyBuffer(m_device, m_vb, nullptr);
    if (m_vbMemory)
    {
        if (m_vbMapped) vkUnmapMemory(m_device, m_vbMemory);
        vkFreeMemory(m_device, m_vbMemory, nullptr);
    }
    m_device = VK_NULL_HANDLE;
    m_vbMapped = m_uboMapped = nullptr;
    m_pipeline = VK_NULL_HANDLE;
    m_layout = VK_NULL_HANDLE;
    m_descSetLayout = VK_NULL_HANDLE;
    m_descPool = VK_NULL_HANDLE;
    m_uboBuffer = VK_NULL_HANDLE;
    m_uboMemory = VK_NULL_HANDLE;
    m_vb = VK_NULL_HANDLE;
    m_vbMemory = VK_NULL_HANDLE;
}
