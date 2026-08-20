/**
 * Mesh3D 实现
 */
#include "mesh3d.h"
#include "../engine/vulkan_util.h"

#include <cmath>
#include <stdexcept>

// UBO 与着色器中 UniformBufferObject 的布局一致
struct alignas(16) MeshUBO
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

// ============================================================================
// Mesh3D 基类
// ============================================================================

Mesh3D::~Mesh3D()
{
    if (m_device)
    {
        vulkan_util::destroyBuffer(m_device, m_vertexBuffer, m_vertexMemory);
        vulkan_util::destroyBuffer(m_device, m_uboBuffer, m_uboMemory);
    }
}

void Mesh3D::upload(VkDevice device, VkPhysicalDevice pd,
                    VkCommandPool pool, VkQueue queue,
                    VkDescriptorSetLayout descLayout, VkDescriptorPool descPool)
{
    if (m_dirty)
    {
        generateVertices();
        m_dirty = false;
    }

    if (!m_vertices.empty())
    {
        VkDeviceSize size = m_vertices.size() * sizeof(float);
        // 仅在设备变更或大小变化时重建缓冲，避免 GPU use-after-free
        if (m_device != device || m_vertexBuffer == VK_NULL_HANDLE || m_vertexSize != size)
        {
            if (m_device)
                vulkan_util::destroyBuffer(device, m_vertexBuffer, m_vertexMemory);
            m_device = device;
            vulkan_util::createBuffer(device, pd, size,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_vertexBuffer, m_vertexMemory);
            m_vertexSize = size;
        }
        vulkan_util::uploadToBuffer(device, pd, pool, queue,
            m_vertices.data(), size, m_vertexBuffer);
    }

    // UBO（host-visible，便于每帧更新）—— 仅在未创建或设备变化时重建
    if (m_uboBuffer == VK_NULL_HANDLE || m_device != device)
    {
        if (m_uboBuffer)
            vulkan_util::destroyBuffer(device, m_uboBuffer, m_uboMemory);
        m_device = device;
        VkDeviceSize uboSize = sizeof(MeshUBO);
        vulkan_util::createBuffer(device, pd, uboSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_uboBuffer, m_uboMemory);
    }

    // 描述符集
    if (m_descSet == VK_NULL_HANDLE)
    {
        m_device = device;
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &descLayout;
        if (vkAllocateDescriptorSets(device, &ai, &m_descSet) != VK_SUCCESS)
            throw std::runtime_error("Mesh3D: vkAllocateDescriptorSets 失败");

        VkDescriptorBufferInfo bi{};
        bi.buffer = m_uboBuffer;
        bi.offset = 0;
        bi.range = sizeof(MeshUBO);

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = m_descSet;
        w.dstBinding = 0;
        w.dstArrayElement = 0;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.descriptorCount = 1;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    }

    m_uploaded = true;
}

void Mesh3D::draw(VkCommandBuffer cmd, VkPipeline pipeline,
                  VkPipelineLayout layout,
                  const VkViewport& viewport, const VkRect2D& scissor,
                  const glm::mat4& view, const glm::mat4& proj) const
{
    if (m_vertices.empty() || !m_uploaded) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    drawVBOOnly(cmd, layout, view, proj);
}

void Mesh3D::drawVBOOnly(VkCommandBuffer cmd, VkPipelineLayout layout,
                         const glm::mat4& view, const glm::mat4& proj) const
{
    if (m_vertices.empty() || !m_uploaded) return;

    // 更新 UBO（仅在模型矩阵或参数变化时，每帧至多一次）
    if (m_uboDirty && m_uboMemory)
    {
        MeshUBO ubo{ m_model, view, proj };
        void* mapped = nullptr;
        vkMapMemory(m_device, m_uboMemory, 0, sizeof(ubo), 0, &mapped);
        memcpy(mapped, &ubo, sizeof(ubo));
        vkUnmapMemory(m_device, m_uboMemory);
        m_uboDirty = false; // 关键：清除脏标志，避免后续帧重复上传
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                           0, 1, &m_descSet, 0, nullptr);

    VkBuffer bufs[] = { m_vertexBuffer };
    VkDeviceSize offs[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offs);

    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(m_color), m_color);

    vkCmdDraw(cmd, static_cast<uint32_t>(m_vertices.size() / 6), 1, 0, 0);
}

void Mesh3D::setColor(float r, float g, float b, float a)
{
    m_color[0] = r;
    m_color[1] = g;
    m_color[2] = b;
    m_color[3] = a;
}

// ============================================================================
// Cube —— 立方体（6 面，每面 2 三角形）
// ============================================================================

Cube::Cube(float size, float r, float g, float b)
    : m_size(size), m_r(r), m_g(g), m_b(b)
{
}

static void emitQuad(std::vector<float>& v,
                     float x0, float y0, float z0,
                     float x1, float y1, float z1,
                     float x2, float y2, float z2,
                     float x3, float y3, float z3,
                     float r, float g, float b)
{
    // 三角 1: v0 v1 v2
    v.insert(v.end(), { x0, y0, z0, r, g, b });
    v.insert(v.end(), { x1, y1, z1, r, g, b });
    v.insert(v.end(), { x2, y2, z2, r, g, b });
    // 三角 2: v0 v2 v3
    v.insert(v.end(), { x0, y0, z0, r, g, b });
    v.insert(v.end(), { x2, y2, z2, r, g, b });
    v.insert(v.end(), { x3, y3, z3, r, g, b });
}

void Cube::generateVertices()
{
    m_vertices.clear();
    float s = m_size * 0.5f;

    // +X 面（红）
    emitQuad(m_vertices,
             s, -s, -s,  s, -s,  s,  s,  s,  s,  s,  s, -s,
             1.0f, 0.3f, 0.3f);
    // -X 面（青）
    emitQuad(m_vertices,
             -s, -s,  s, -s, -s, -s, -s,  s, -s, -s,  s,  s,
             0.3f, 1.0f, 1.0f);
    // +Y 面（绿）
    emitQuad(m_vertices,
             -s,  s, -s,  s,  s, -s,  s,  s,  s, -s,  s,  s,
             0.3f, 1.0f, 0.3f);
    // -Y 面（紫）
    emitQuad(m_vertices,
             -s, -s,  s,  s, -s,  s,  s, -s, -s, -s, -s, -s,
             1.0f, 0.3f, 1.0f);
    // +Z 面（蓝）
    emitQuad(m_vertices,
             -s, -s,  s,  s, -s,  s,  s,  s,  s, -s,  s,  s,
             0.3f, 0.3f, 1.0f);
    // -Z 面（黄）
    emitQuad(m_vertices,
             s, -s, -s, -s, -s, -s, -s,  s, -s,  s,  s, -s,
             1.0f, 1.0f, 0.3f);
}

// ============================================================================
// Polyhedron —— 正多面体
// ============================================================================

Polyhedron::Polyhedron(Type type, float scale, float r, float g, float b)
    : m_type(type), m_scale(scale), m_r(r), m_g(g), m_b(b)
{
}

static void emitTri(std::vector<float>& v,
                    float ax, float ay, float az,
                    float bx, float by, float bz,
                    float cx, float cy, float cz,
                    float r, float g, float b)
{
    v.insert(v.end(), { ax, ay, az, r, g, b });
    v.insert(v.end(), { bx, by, bz, r, g, b });
    v.insert(v.end(), { cx, cy, cz, r, g, b });
}

void Polyhedron::generateVertices()
{
    m_vertices.clear();
    const float s = m_scale;

    if (m_type == Type::Tetrahedron)
    {
        // 正四面体 4 个顶点
        const float a = s;
        const float v0[3] = {  a,  a,  a };
        const float v1[3] = { -a, -a,  a };
        const float v2[3] = { -a,  a, -a };
        const float v3[3] = {  a, -a, -a };
        // 4 个三角面
        emitTri(m_vertices, v0[0],v0[1],v0[2], v1[0],v1[1],v1[2], v2[0],v2[1],v2[2], 0.9f, 0.3f, 0.3f);
        emitTri(m_vertices, v0[0],v0[1],v0[2], v3[0],v3[1],v3[2], v1[0],v1[1],v1[2], 0.3f, 0.9f, 0.3f);
        emitTri(m_vertices, v0[0],v0[1],v0[2], v2[0],v2[1],v2[2], v3[0],v3[1],v3[2], 0.3f, 0.3f, 0.9f);
        emitTri(m_vertices, v1[0],v1[1],v1[2], v3[0],v3[1],v3[2], v2[0],v2[1],v2[2], 0.9f, 0.9f, 0.3f);
    }
    else if (m_type == Type::Octahedron)
    {
        // 正八面体 6 个顶点（坐标轴方向 ±s）
        const float px[3] = { s, 0, 0 };
        const float nx[3] = { -s, 0, 0 };
        const float py[3] = { 0, s, 0 };
        const float ny[3] = { 0, -s, 0 };
        const float pz[3] = { 0, 0, s };
        const float nz[3] = { 0, 0, -s };
        // 8 个面
        emitTri(m_vertices, px[0],px[1],px[2], py[0],py[1],py[2], pz[0],pz[1],pz[2], 1.0f, 0.4f, 0.4f);
        emitTri(m_vertices, px[0],px[1],px[2], pz[0],pz[1],pz[2], ny[0],ny[1],ny[2], 0.4f, 1.0f, 0.4f);
        emitTri(m_vertices, px[0],px[1],px[2], ny[0],ny[1],ny[2], nz[0],nz[1],nz[2], 0.4f, 0.4f, 1.0f);
        emitTri(m_vertices, px[0],px[1],px[2], nz[0],nz[1],nz[2], py[0],py[1],py[2], 1.0f, 1.0f, 0.4f);
        emitTri(m_vertices, nx[0],nx[1],nx[2], pz[0],pz[1],pz[2], py[0],py[1],py[2], 0.9f, 0.6f, 0.9f);
        emitTri(m_vertices, nx[0],nx[1],nx[2], ny[0],ny[1],ny[2], pz[0],pz[1],pz[2], 0.6f, 0.9f, 0.9f);
        emitTri(m_vertices, nx[0],nx[1],nx[2], nz[0],nz[1],nz[2], ny[0],ny[1],ny[2], 0.9f, 0.9f, 0.6f);
        emitTri(m_vertices, nx[0],nx[1],nx[2], py[0],py[1],py[2], nz[0],nz[1],nz[2], 0.6f, 0.6f, 0.9f);
    }
    else // Icosahedron 近似球
    {
        // 使用 UV 球近似（细分），36 个三角形
        const int seg = 12;
        const int ring = 6;
        const float R = s;
        const float PI = 3.14159265358979f;
        for (int r = 0; r < ring; ++r)
        {
            float theta0 = (r * PI / ring) - PI * 0.5f;
            float theta1 = ((r+1) * PI / ring) - PI * 0.5f;
            for (int c = 0; c < seg; ++c)
            {
                float phi0 = c * 2.0f * PI / seg;
                float phi1 = (c+1) * 2.0f * PI / seg;
                auto xyz = [&](float theta, float phi, float& x, float& y, float& z){
                    float ct = std::cos(theta), st = std::sin(theta);
                    float cp = std::cos(phi), sp = std::sin(phi);
                    x = R * ct * cp; y = R * st; z = R * ct * sp;
                };
                float ax,ay,az, bx,by,bz, cx,cy,cz, dx,dy,dz;
                xyz(theta0, phi0, ax, ay, az);
                xyz(theta1, phi0, bx, by, bz);
                xyz(theta1, phi1, cx, cy, cz);
                xyz(theta0, phi1, dx, dy, dz);
                float cr = m_r * (0.5f + 0.5f * (float)c / seg);
                float cg = m_g * (0.5f + 0.5f * (float)r / ring);
                float cb = m_b;
                emitTri(m_vertices, ax,ay,az, bx,by,bz, cx,cy,cz, cr, cg, cb);
                emitTri(m_vertices, ax,ay,az, cx,cy,cz, dx,dy,dz, cr, cg, cb);
            }
        }
    }
}
