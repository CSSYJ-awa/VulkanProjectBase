/**
 * Mesh3D 实现
 */
#include "mesh3d.h"
#include "../engine/vulkan_util.h"
#include "../render/texture.h"

#include <cmath>
#include <stdexcept>

// UBO 与着色器中 UniformBufferObject 的布局一致（std140）
// 前 3 个 mat4 为模型/视图/投影；后续为光照 + v1.0.2 材质/雾/半球光参数：
//   vec3 lightDir + float ambient + vec3 lightColor + float _pad
//   vec4 fogColor(w=enable/强度) + vec4 fogParams(x=start y=end z=texScale w=texMix)
//   vec4 ambientUp + vec4 ambientDown
struct alignas(16) MeshUBO
{
    glm::mat4 model;       //  0
    glm::mat4 view;        // 64
    glm::mat4 proj;        // 128
    glm::vec3 lightDir;    // 192  世界空间光传播方向（shader 内取反得指向光源方向）
    float     ambient;     // 204  环境光强度（0~1）
    glm::vec3 lightColor;  // 208  光颜色 × 强度
    float     _pad0;       // 220
    glm::vec4 fogColor;    // 224  xyz=雾色  w=雾开关(0/1)
    glm::vec4 fogParams;   // 240  x=fogStart y=fogEnd z=texScale w=texMix
    glm::vec4 ambientUp;   // 256  半球环境光·上
    glm::vec4 ambientDown; // 272  半球环境光·下
};

namespace {

// 共享 1x1 白纹理（无材质时兜底，避免 shader 采样未绑定描述符）
const Texture* ensureWhiteTexture(const RenderDevice& dev)
{
    static std::unique_ptr<Texture> s_white;
    if (!s_white)
    {
        const unsigned char white[4] = { 255, 255, 255, 255 };
        s_white = Texture::create(dev, 1, 1, white);
    }
    return s_white.get();
}

// 组装 RenderDevice（供白纹理创建等内部使用）
RenderDevice makeDev(VkDevice device, VkPhysicalDevice pd,
                     VkCommandPool pool, VkQueue queue)
{
    RenderDevice dev;
    dev.device = device; dev.physicalDevice = pd;
    dev.queue = queue; dev.commandPool = pool;
    return dev;
}

} // namespace

// ============================================================================
// Mesh3D 基类
// ============================================================================

Mesh3D::~Mesh3D()
{
    if (m_device)
    {
        // 延迟到安全点释放（双帧并行下 GPU 可能仍引用这些资源，
        // 直接销毁会造成 use-after-free → DEVICE_LOST）。
        // 注意：延迟释放会复制 device/pool/descSet 到队列，本对象可安全析构。
        vulkan_util::deferDestroyBuffer(m_device, m_vertexBuffer, m_vertexMemory);
        vulkan_util::deferDestroyBuffer(m_device, m_uboBuffer, m_uboMemory, m_uboMapped);
        vulkan_util::deferDestroyDescriptorSet(m_device, m_descPool, m_descSet);
        m_uboMapped = nullptr;
    }
}

void Mesh3D::upload(const RenderDevice& dev,
                    VkDescriptorSetLayout descLayout, VkDescriptorPool descPool)
{
    upload(dev.device, dev.physicalDevice, dev.commandPool, dev.queue,
           descLayout, descPool);
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
            // 修复：用旧 m_device 延迟销毁旧缓冲（Vulkan 不允许跨设备销毁资源；
            // 延迟到安全点，避免 GPU 仍引用旧缓冲时 use-after-free）
            if (m_device && m_vertexBuffer != VK_NULL_HANDLE)
                vulkan_util::deferDestroyBuffer(m_device, m_vertexBuffer, m_vertexMemory);
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
        // 修复：用旧 m_device 延迟销毁旧 UBO（flush 时统一解除持久映射）
        if (m_device && m_uboBuffer != VK_NULL_HANDLE)
        {
            vulkan_util::deferDestroyBuffer(m_device, m_uboBuffer, m_uboMemory, m_uboMapped);
            m_uboMapped = nullptr;
        }
        m_device = device;
        VkDeviceSize uboSize = sizeof(MeshUBO);
        vulkan_util::createBuffer(device, pd, uboSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_uboBuffer, m_uboMemory);
        // 性能优化：持久映射 UBO 内存，避免每帧 vkMapMemory/vkUnmapMemory 的开销
        if (vkMapMemory(device, m_uboMemory, 0, uboSize, 0, &m_uboMapped) != VK_SUCCESS)
        {
            m_uboMapped = nullptr;  // 失败则降级到每帧 map/unmap
        }
        // 强制下次 draw 重新写入 UBO
        m_uboDirty = true;
    }

    // 描述符集
    if (m_descSet == VK_NULL_HANDLE)
    {
        m_device = device;
        m_descPool = descPool;  // 记录来源池，析构时回收描述符集
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

        VkWriteDescriptorSet w[2]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = m_descSet;
        w[0].dstBinding = 0;
        w[0].dstArrayElement = 0;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].descriptorCount = 1;
        w[0].pBufferInfo = &bi;

        // v1.0.2：binding 1 = 材质纹理（无材质时用共享白纹理兜底）
        RenderDevice dev = makeDev(device, pd, pool, queue);
        const Texture* tex = m_texture ? m_texture : ensureWhiteTexture(dev);
        VkDescriptorImageInfo dii{};
        if (tex && tex->valid())
        {
            dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            dii.imageView   = tex->view();
            dii.sampler     = tex->sampler();
        }
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = m_descSet;
        w[1].dstBinding = 1;
        w[1].dstArrayElement = 0;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].descriptorCount = 1;
        w[1].pImageInfo = &dii;
        vkUpdateDescriptorSets(device, 2, w, 0, nullptr);
    }

    m_uploaded = true;
}

void Mesh3D::drawVBOOnly(VkCommandBuffer cmd, VkPipelineLayout layout,
                         const glm::mat4& view, const glm::mat4& proj) const
{
    if (m_vertices.empty() || !m_uploaded) return;

    // 检测 view/proj 是否自上次绘制后变化（相机移动、窗口缩放等）
    // 若变化则强制 UBO 刷新，避免模型仍用旧矩阵渲染
    if (view != m_lastView || proj != m_lastProj)
    {
        m_uboDirty = true;
        m_lastView = view;
        m_lastProj = proj;
    }

    // 更新 UBO（仅在模型矩阵或 view/proj 变化时，每帧至多一次）
    if (m_uboDirty && m_uboMemory)
    {
        MeshUBO ubo{ m_model, view, proj,
                     m_lightDir, m_ambient, m_lightColor, 0.0f,
                     glm::vec4(m_fogColor, m_fogOn ? 1.0f : 0.0f),
                     glm::vec4(m_fogStart, m_fogEnd, m_texScale, 0.8f),
                     glm::vec4(m_ambientUp, 1.0f),
                     glm::vec4(m_ambientDown, 1.0f) };
        if (m_uboMapped)
        {
            // 持久映射：直接 memcpy，无需 vkMapMemory/vkUnmapMemory
            memcpy(m_uboMapped, &ubo, sizeof(ubo));
        }
        else
        {
            // 降级路径：每帧 map/unmap（兼容不支持持久映射的驱动）
            void* mapped = nullptr;
            if (vkMapMemory(m_device, m_uboMemory, 0, sizeof(ubo), 0, &mapped) == VK_SUCCESS)
            {
                memcpy(mapped, &ubo, sizeof(ubo));
                vkUnmapMemory(m_device, m_uboMemory);
            }
        }
        m_uboDirty = false; // 关键：清除脏标志，避免后续帧重复上传
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                           0, 1, &m_descSet, 0, nullptr);

    VkBuffer bufs[] = { m_vertexBuffer };
    VkDeviceSize offs[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offs);

    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(m_color), m_color);

    vkCmdDraw(cmd, static_cast<uint32_t>(m_vertices.size() / 9), 1, 0, 0);
}

void Mesh3D::drawDepth(VkCommandBuffer cmd, VkPipelineLayout depthLayout,
                       const glm::mat4& model) const
{
    if (m_vertices.empty() || m_vertexBuffer == VK_NULL_HANDLE) return;

    VkBuffer bufs[] = { m_vertexBuffer };
    VkDeviceSize offs[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offs);
    vkCmdPushConstants(cmd, depthLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(glm::mat4), &model);
    vkCmdDraw(cmd, static_cast<uint32_t>(m_vertices.size() / 9), 1, 0, 0);
}

void Mesh3D::setLight(const glm::vec3& lightDir,
                      const glm::vec3& lightColor,
                      float ambient)
{
    if (m_lightDir != lightDir || m_lightColor != lightColor || m_ambient != ambient)
    {
        m_lightDir   = lightDir;
        m_lightColor = lightColor;
        m_ambient    = ambient;
        m_uboDirty   = true;
    }
}

void Mesh3D::setTexture(const Texture* tex)
{
    m_texture = tex;
    // 若已 upload：立即重写 binding 1 描述符（帧外设置，安全）
    if (m_descSet != VK_NULL_HANDLE && m_device)
    {
        RenderDevice dev;
        dev.device = m_device;   // 仅白纹理创建需要 device（已存在的白纹理无需重建）
        dev.physicalDevice = VK_NULL_HANDLE;
        const Texture* t = tex ? tex : ensureWhiteTexture(dev);
        VkDescriptorImageInfo dii{};
        if (t && t->valid())
        {
            dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            dii.imageView   = t->view();
            dii.sampler     = t->sampler();
        }
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = m_descSet; w.dstBinding = 1; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &dii;
        vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    }
    m_uboDirty = true;
}

void Mesh3D::setTextureScale(float scale)
{
    if (m_texScale != scale) { m_texScale = scale; m_uboDirty = true; }
}

void Mesh3D::setFog(bool on, const glm::vec3& color, float start, float end)
{
    if (m_fogOn != on || m_fogColor != color ||
        m_fogStart != start || m_fogEnd != end)
    {
        m_fogOn = on; m_fogColor = color;
        m_fogStart = start; m_fogEnd = end;
        m_uboDirty = true;
    }
}

void Mesh3D::setHemisphere(const glm::vec3& up, const glm::vec3& down)
{
    if (m_ambientUp != up || m_ambientDown != down)
    {
        m_ambientUp = up; m_ambientDown = down;
        m_uboDirty = true;
    }
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

// 写一个顶点（位置 + 法线 + 颜色，共 9 float）
static void pushV(std::vector<float>& v,
                  float x, float y, float z,
                  float nx, float ny, float nz,
                  float r, float g, float b)
{
    v.insert(v.end(), { x, y, z, nx, ny, nz, r, g, b });
}

// 写一个三角形。法线参数传 (0,0,0) 时自动计算：
//   面法线 = 归一化叉积 (b-a)×(c-a)，并按"面中心朝外"修正朝向
//   （适用于中心位于原点的凸体，可容忍顺时针/逆时针绕序差异）。
static void emitTri(std::vector<float>& v,
                    float ax, float ay, float az,
                    float bx, float by, float bz,
                    float cx, float cy, float cz,
                    float nx, float ny, float nz,
                    float r, float g, float b)
{
    if (nx == 0.0f && ny == 0.0f && nz == 0.0f)
    {
        float ux = bx - ax, uy = by - ay, uz = bz - az;
        float vx = cx - ax, vy = cy - ay, vz = cz - az;
        nx = uy * vz - uz * vy;
        ny = uz * vx - ux * vz;
        nz = ux * vy - uy * vx;
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-9f) { nx /= len; ny /= len; nz /= len; }
        // 面中心朝外修正（凸体中心在原点：法线应与面中心向量同向）
        float mx = (ax + bx + cx) / 3.0f;
        float my = (ay + by + cy) / 3.0f;
        float mz = (az + bz + cz) / 3.0f;
        if (nx * mx + ny * my + nz * mz < 0.0f) { nx = -nx; ny = -ny; nz = -nz; }
    }
    pushV(v, ax, ay, az, nx, ny, nz, r, g, b);
    pushV(v, bx, by, bz, nx, ny, nz, r, g, b);
    pushV(v, cx, cy, cz, nx, ny, nz, r, g, b);
}

// 四边形（两三角形，共用同一法线；法线 0,0,0 时自动计算）
static void emitQuad(std::vector<float>& v,
                     float x0, float y0, float z0,
                     float x1, float y1, float z1,
                     float x2, float y2, float z2,
                     float x3, float y3, float z3,
                     float nx, float ny, float nz,
                     float r, float g, float b)
{
    emitTri(v, x0, y0, z0, x1, y1, z1, x2, y2, z2, nx, ny, nz, r, g, b);
    emitTri(v, x0, y0, z0, x2, y2, z2, x3, y3, z3, nx, ny, nz, r, g, b);
}

void Cube::generateVertices()
{
    m_vertices.clear();
    float s = m_size * 0.5f;

    // +X 面（红）
    emitQuad(m_vertices,
             s, -s, -s,  s, -s,  s,  s,  s,  s,  s,  s, -s,
             0, 0, 0,
             1.0f, 0.3f, 0.3f);
    // -X 面（青）
    emitQuad(m_vertices,
             -s, -s,  s, -s, -s, -s, -s,  s, -s, -s,  s,  s,
             0, 0, 0,
             0.3f, 1.0f, 1.0f);
    // +Y 面（绿）
    emitQuad(m_vertices,
             -s,  s, -s,  s,  s, -s,  s,  s,  s, -s,  s,  s,
             0, 0, 0,
             0.3f, 1.0f, 0.3f);
    // -Y 面（紫）
    emitQuad(m_vertices,
             -s, -s,  s,  s, -s,  s,  s, -s, -s, -s, -s, -s,
             0, 0, 0,
             1.0f, 0.3f, 1.0f);
    // +Z 面（蓝）
    emitQuad(m_vertices,
             -s, -s,  s,  s, -s,  s,  s,  s,  s, -s,  s,  s,
             0, 0, 0,
             0.3f, 0.3f, 1.0f);
    // -Z 面（黄）
    emitQuad(m_vertices,
             s, -s, -s, -s, -s, -s, -s,  s, -s,  s,  s, -s,
             0, 0, 0,
             1.0f, 1.0f, 0.3f);
}

// ============================================================================
// Polyhedron —— 正多面体
// ============================================================================

Polyhedron::Polyhedron(Type type, float scale, float r, float g, float b)
    : m_type(type), m_scale(scale), m_r(r), m_g(g), m_b(b)
{
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
        // 4 个三角面（法线自动计算）
        emitTri(m_vertices, v0[0],v0[1],v0[2], v1[0],v1[1],v1[2], v2[0],v2[1],v2[2], 0,0,0, 0.9f, 0.3f, 0.3f);
        emitTri(m_vertices, v0[0],v0[1],v0[2], v3[0],v3[1],v3[2], v1[0],v1[1],v1[2], 0,0,0, 0.3f, 0.9f, 0.3f);
        emitTri(m_vertices, v0[0],v0[1],v0[2], v2[0],v2[1],v2[2], v3[0],v3[1],v3[2], 0,0,0, 0.3f, 0.3f, 0.9f);
        emitTri(m_vertices, v1[0],v1[1],v1[2], v3[0],v3[1],v3[2], v2[0],v2[1],v2[2], 0,0,0, 0.9f, 0.9f, 0.3f);
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
        // 8 个面（法线自动计算）
        emitTri(m_vertices, px[0],px[1],px[2], py[0],py[1],py[2], pz[0],pz[1],pz[2], 0,0,0, 1.0f, 0.4f, 0.4f);
        emitTri(m_vertices, px[0],px[1],px[2], pz[0],pz[1],pz[2], ny[0],ny[1],ny[2], 0,0,0, 0.4f, 1.0f, 0.4f);
        emitTri(m_vertices, px[0],px[1],px[2], ny[0],ny[1],ny[2], nz[0],nz[1],nz[2], 0,0,0, 0.4f, 0.4f, 1.0f);
        emitTri(m_vertices, px[0],px[1],px[2], nz[0],nz[1],nz[2], py[0],py[1],py[2], 0,0,0, 1.0f, 1.0f, 0.4f);
        emitTri(m_vertices, nx[0],nx[1],nx[2], pz[0],pz[1],pz[2], py[0],py[1],py[2], 0,0,0, 0.9f, 0.6f, 0.9f);
        emitTri(m_vertices, nx[0],nx[1],nx[2], ny[0],ny[1],ny[2], pz[0],pz[1],pz[2], 0,0,0, 0.6f, 0.9f, 0.9f);
        emitTri(m_vertices, nx[0],nx[1],nx[2], nz[0],nz[1],nz[2], ny[0],ny[1],ny[2], 0,0,0, 0.9f, 0.9f, 0.6f);
        emitTri(m_vertices, nx[0],nx[1],nx[2], py[0],py[1],py[2], nz[0],nz[1],nz[2], 0,0,0, 0.6f, 0.6f, 0.9f);
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
                // 平滑法线：球面顶点法线 = 归一化位置
                auto smoothTri = [&](float ax2,float ay2,float az2,
                                     float bx2,float by2,float bz2,
                                     float cx2,float cy2,float cz2,
                                     float r2,float g2,float b2)
                {
                    auto norm = [&](float x,float y,float z,float& nx,float& ny,float& nz){
                        float l = std::sqrt(x*x + y*y + z*z);
                        if (l > 1e-9f) { nx = x/l; ny = y/l; nz = z/l; }
                        else { nx = 0.0f; ny = 0.0f; nz = 1.0f; }
                    };
                    float anx,any,anz, bnx,bny,bnz, cnx,cny,cnz;
                    norm(ax2,ay2,az2,anx,any,anz);
                    norm(bx2,by2,bz2,bnx,bny,bnz);
                    norm(cx2,cy2,cz2,cnx,cny,cnz);
                    pushV(m_vertices, ax2,ay2,az2, anx,any,anz, r2,g2,b2);
                    pushV(m_vertices, bx2,by2,bz2, bnx,bny,bnz, r2,g2,b2);
                    pushV(m_vertices, cx2,cy2,cz2, cnx,cny,cnz, r2,g2,b2);
                };
                smoothTri(ax,ay,az, bx,by,bz, cx,cy,cz, cr, cg, cb);
                smoothTri(ax,ay,az, cx,cy,cz, dx,dy,dz, cr, cg, cb);
            }
        }
    }
}
