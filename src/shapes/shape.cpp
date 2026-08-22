/**
 * Shape 实现
 */
#include "shape.h"
#include "../engine/vulkan_util.h"

#include <cmath>
#include <cstring>

// ============================================================================
// Shape 基类
// ============================================================================

Shape::~Shape()
{
    if (m_device)
        vulkan_util::deferDestroyBuffer(m_device, m_buffer, m_memory);
}

Shape& Shape::operator=(Shape&& o) noexcept
{
    if (this != &o)
    {
        // 先释放自身已有资源（延迟到安全点，避免 GPU use-after-free）
        if (m_device)
            vulkan_util::deferDestroyBuffer(m_device, m_buffer, m_memory);
        // 偷取 o 的资源
        m_vertices           = std::move(o.m_vertices);
        m_lineTopology       = o.m_lineTopology;
        m_lineStripTopology  = o.m_lineStripTopology;
        m_fanTopology        = o.m_fanTopology;
        memcpy(m_color, o.m_color, sizeof(m_color));
        m_dirty              = o.m_dirty;
        m_device             = o.m_device;    o.m_device   = VK_NULL_HANDLE;
        m_buffer             = o.m_buffer;    o.m_buffer   = VK_NULL_HANDLE;
        m_memory             = o.m_memory;    o.m_memory   = VK_NULL_HANDLE;
        m_size               = o.m_size;
    }
    return *this;
}

void Shape::upload(const RenderDevice& dev)
{
    upload(dev.device, dev.physicalDevice, dev.commandPool, dev.queue);
}

void Shape::upload(VkDevice device, VkPhysicalDevice pd,
                   VkCommandPool pool, VkQueue queue)
{
    if (m_dirty)
    {
        generateVertices();
        m_dirty = false;
    }
    if (m_vertices.empty()) return;

    VkDeviceSize size = m_vertices.size() * sizeof(float);

    // 仅在设备变更或大小变化时重建缓冲
    // 避免每帧销毁/重建导致 GPU use-after-free（MAX_FRAMES_IN_FLIGHT > 1 时
    // 旧缓冲可能仍被上一帧的 GPU 命令引用）
    if (m_device != device || m_buffer == VK_NULL_HANDLE || m_size != size)
    {
        if (m_device)
            vulkan_util::deferDestroyBuffer(m_device, m_buffer, m_memory);
        m_device = device;
        vulkan_util::createBuffer(device, pd, size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_buffer, m_memory);
        m_size = size;
    }

    // uploadToBuffer 内部调用 vkQueueWaitIdle，确保 GPU 已完成上一帧后再更新数据
    vulkan_util::uploadToBuffer(device, pd, pool, queue,
        m_vertices.data(), size, m_buffer);
}

void Shape::drawVBOOnly(VkCommandBuffer cmd, VkPipelineLayout layout) const
{
    if (m_vertices.empty() || m_buffer == VK_NULL_HANDLE) return;

    VkBuffer bufs[] = { m_buffer };
    VkDeviceSize offs[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offs);

    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(m_color), m_color);

    vkCmdDraw(cmd, static_cast<uint32_t>(m_vertices.size() / 5), 1, 0, 0);
}

void Shape::setColor(float r, float g, float b, float a)
{
    m_color[0] = r;
    m_color[1] = g;
    m_color[2] = b;
    m_color[3] = a;
}

// ============================================================================
// Line —— 线段
// ============================================================================

Line::Line(float x1, float y1, float x2, float y2,
           float r, float g, float b)
    : m_x1(x1), m_y1(y1), m_x2(x2), m_y2(y2), m_r(r), m_g(g), m_b(b)
{
    m_lineTopology = true;
}

void Line::setEndpoints(float x1, float y1, float x2, float y2)
{
    m_x1 = x1; m_y1 = y1; m_x2 = x2; m_y2 = y2;
    m_dirty = true;
}

void Line::generateVertices()
{
    m_vertices = {
        m_x1, m_y1, m_r, m_g, m_b,
        m_x2, m_y2, m_r, m_g, m_b,
    };
}

// ============================================================================
// Triangle —— 三角形
// ============================================================================

Triangle::Triangle(float x1, float y1, float x2, float y2, float x3, float y3,
                   float r, float g, float b)
    : m_r(r), m_g(g), m_b(b)
{
    m_x[0] = x1; m_y[0] = y1;
    m_x[1] = x2; m_y[1] = y2;
    m_x[2] = x3; m_y[2] = y3;
}

void Triangle::generateVertices()
{
    m_vertices = {
        m_x[0], m_y[0], m_r, m_g, m_b,
        m_x[1], m_y[1], m_r, m_g, m_b,
        m_x[2], m_y[2], m_r, m_g, m_b,
    };
}

// ============================================================================
// Rectangle —— 长方形
// ============================================================================

Rectangle::Rectangle(float cx, float cy, float width, float height,
                     float r, float g, float b)
    : m_cx(cx), m_cy(cy), m_w(width), m_h(height), m_r(r), m_g(g), m_b(b)
{
}

void Rectangle::setSize(float w, float h)
{
    m_w = w; m_h = h;
    m_dirty = true;
}

void Rectangle::setBounds(float cx, float cy, float w, float h,
                          float r, float g, float b)
{
    m_cx = cx; m_cy = cy; m_w = w; m_h = h;
    m_r = r;   m_g = g;   m_b = b;
    m_dirty = true;
}

void Rectangle::generateVertices()
{
    float hw = m_w * 0.5f;
    float hh = m_h * 0.5f;
    float x0 = m_cx - hw, x1 = m_cx + hw;
    float y0 = m_cy - hh, y1 = m_cy + hh;
    // 两个三角形
    m_vertices = {
        x0, y0, m_r, m_g, m_b,
        x1, y0, m_r, m_g, m_b,
        x1, y1, m_r, m_g, m_b,
        x0, y0, m_r, m_g, m_b,
        x1, y1, m_r, m_g, m_b,
        x0, y1, m_r, m_g, m_b,
    };
}

// ============================================================================
// Square —— 正方形（继承 Rectangle）
// ============================================================================

Square::Square(float cx, float cy, float side, float r, float g, float b)
    : Rectangle(cx, cy, side, side, r, g, b)
{
}

// ============================================================================
// Circle —— 圆
// ============================================================================

Circle::Circle(float cx, float cy, float radius, uint32_t segments,
               float r, float g, float b)
    : m_cx(cx), m_cy(cy), m_radius(radius), m_segments(segments),
      m_r(r), m_g(g), m_b(b)
{
}

void Circle::setRadius(float r) { m_radius = r; m_dirty = true; }

void Circle::generateVertices()
{
    m_vertices.clear();
    if (m_segments < 3) return;
    m_fanTopology = true;

    // TRIANGLE_FAN 布局：中心点 + 各圆周顶点
    // 顶点数 = 1 + m_segments（共 m_segments 个三角形）
    // 相比 TRIANGLE_LIST（3*m_segments 顶点）节省约 67% GPU 顶点传输
    const float pi = 3.14159265358979f;

    // v0 = 中心点
    m_vertices.push_back(m_cx);
    m_vertices.push_back(m_cy);
    m_vertices.push_back(m_r);
    m_vertices.push_back(m_g);
    m_vertices.push_back(m_b);

    for (uint32_t i = 0; i <= m_segments; ++i)
    {
        float a = static_cast<float>(i) * 2.0f * pi / static_cast<float>(m_segments);
        float x = m_cx + m_radius * std::cos(a);
        float y = m_cy + m_radius * std::sin(a);
        m_vertices.push_back(x);
        m_vertices.push_back(y);
        m_vertices.push_back(m_r);
        m_vertices.push_back(m_g);
        m_vertices.push_back(m_b);
    }
}

// ============================================================================
// Wave —— 波形（线段连接的正弦曲线）
// ============================================================================

Wave::Wave(float x1, float y1, float x2, float y2,
           float amplitude, float frequency, uint32_t segments,
           float r, float g, float b)
    : m_x1(x1), m_y1(y1), m_x2(x2), m_y2(y2),
      m_amplitude(amplitude), m_frequency(frequency), m_segments(segments),
      m_r(r), m_g(g), m_b(b)
{
    m_lineStripTopology = true;
}

void Wave::generateVertices()
{
    m_vertices.clear();
    for (uint32_t i = 0; i <= m_segments; ++i)
    {
        float t = static_cast<float>(i) / m_segments;
        float x = m_x1 + (m_x2 - m_x1) * t;
        float y = m_y1 + (m_y2 - m_y1) * t;
        // 在垂直方向上叠加正弦波
        float dx = m_x2 - m_x1;
        float dy = m_y2 - m_y1;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len > 1e-6f)
        {
            float nx = -dy / len;
            float ny =  dx / len;
            float wave = m_amplitude * std::sin(t * m_frequency * 2.0f * 3.14159265358979f);
            x += nx * wave;
            y += ny * wave;
        }
        m_vertices.push_back(x);
        m_vertices.push_back(y);
        m_vertices.push_back(m_r);
        m_vertices.push_back(m_g);
        m_vertices.push_back(m_b);
    }
}

// ============================================================================
// Polygon —— 多边形（fan 三角化）
// ============================================================================

Polygon::Polygon(const std::vector<std::pair<float, float>>& points,
                 float r, float g, float b)
    : m_points(points), m_r(r), m_g(g), m_b(b)
{
}

void Polygon::setPoints(const std::vector<std::pair<float, float>>& points)
{
    m_points = points;
    m_dirty = true;
}

void Polygon::generateVertices()
{
    m_vertices.clear();
    if (m_points.size() < 3) return;
    // 凸多边形：采用 TRIANGLE_FAN 布局（以 points[0] 为扇心）
    // 顶点数 = points.size() + 1（闭合），相比 LIST 节省约 67%
    m_fanTopology = true;

    for (size_t i = 0; i < m_points.size(); ++i)
    {
        m_vertices.push_back(m_points[i].first);
        m_vertices.push_back(m_points[i].second);
        m_vertices.push_back(m_r);
        m_vertices.push_back(m_g);
        m_vertices.push_back(m_b);
    }
    // 闭合点（回到第1个圆周顶点，即 points[1]），形成封闭扇
    m_vertices.push_back(m_points[1].first);
    m_vertices.push_back(m_points[1].second);
    m_vertices.push_back(m_r);
    m_vertices.push_back(m_g);
    m_vertices.push_back(m_b);
}
