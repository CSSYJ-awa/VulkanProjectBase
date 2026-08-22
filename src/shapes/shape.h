/**
 * Shape —— 2D 图形系统
 *
 * 抽象基类 Shape：负责顶点缓冲管理、绘制命令录制。
 * 派生类只需在 generateVertices() 中填充 m_vertices（位置 x,y + 颜色 r,g,b）。
 *
 * 坐标系：NDC（normalized device coordinates），x/y ∈ [-1, 1]。
 * 颜色：每顶点 RGB，整体 alpha 通过 push constant 控制。
 *
 * 派生类层次：
 *   Shape (abstract)
 *     ├── Line          线段
 *     ├── Triangle      三角形
 *     ├── Rectangle     长方形
 *     ├── Square        正方形（继承自 Rectangle）
 *     ├── Circle        圆
 *     ├── Wave          波形
 *     └── Polygon       多边形（由任意点构造）
 */
#pragma once

#include "../render/render_device.h"

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <string>

class Shape
{
public:
    Shape() = default;
    virtual ~Shape();

    Shape(const Shape&) = delete;
    Shape& operator=(const Shape&) = delete;
    // 移动语义：转移 GPU 资源所有权，源对象置空（不会释放资源）
    Shape(Shape&& o) noexcept { *this = std::move(o); }
    Shape& operator=(Shape&& o) noexcept;

    // 重建 GPU 顶点缓冲（顶点数据变化后调用）
    void upload(VkDevice device, VkPhysicalDevice pd,
                VkCommandPool pool, VkQueue queue);

    // 便捷重载（v1.0.1）：从 RenderDevice 一次性获取 device/pd/pool/queue
    void upload(const RenderDevice& dev);

    // 轻量绘制：仅绑定 VB + push constant + Draw。
    // 用于批量绘制，调用者须保证 pipeline/viewport/scissor 已预先设置。
    void drawVBOOnly(VkCommandBuffer cmd, VkPipelineLayout layout) const;

    // 颜色控制（push constant alpha 调制）
    void setColor(float r, float g, float b, float a = 1.0f);
    const float* color() const { return m_color; }

    // 是否为线框/线段图元（决定使用哪个 pipeline）
    bool isLineTopology() const { return m_lineTopology; }
    // 是否为连续线段（LINE_STRIP）
    bool isLineStripTopology() const { return m_lineStripTopology; }
    // 是否为三角扇（TRIANGLE_FAN）
    bool isFanTopology() const { return m_fanTopology; }

protected:
    // 派生类实现：填充 m_vertices
    virtual void generateVertices() = 0;

    std::vector<float> m_vertices; // x,y, r,g,b  每顶点 5 float
    bool               m_lineTopology = false;
    bool               m_lineStripTopology = false;
    bool               m_fanTopology = false;
    float              m_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool               m_dirty = true; // 顶点需重建

private:
    VkDevice         m_device  = VK_NULL_HANDLE;
    VkBuffer         m_buffer  = VK_NULL_HANDLE;
    VkDeviceMemory   m_memory  = VK_NULL_HANDLE;
    VkDeviceSize     m_size    = 0;
};

// ============================================================================
// 像素坐标到 NDC 的辅助转换
// ============================================================================
inline void pixelToNdc(float px, float py, uint32_t w, uint32_t h,
                       float& nx, float& ny)
{
    nx = 2.0f * px / static_cast<float>(w) - 1.0f;
    ny = 2.0f * py / static_cast<float>(h) - 1.0f; // Vulkan NDC Y 向下
}

// 从窗口尺寸构造全屏视口/裁剪框
inline VkViewport makeViewport(VkExtent2D ext)
{
    VkViewport vp{};
    vp.width  = static_cast<float>(ext.width);
    vp.height = static_cast<float>(ext.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    return vp;
}

inline VkRect2D makeScissor(VkExtent2D ext)
{
    VkRect2D sc{};
    sc.extent = ext;
    return sc;
}

// ============================================================================
// 具体图形
// ============================================================================

class Line : public Shape
{
public:
    Line(float x1, float y1, float x2, float y2,
         float r = 1.0f, float g = 1.0f, float b = 1.0f);
    void setEndpoints(float x1, float y1, float x2, float y2);
protected:
    void generateVertices() override;
private:
    float m_x1, m_y1, m_x2, m_y2;
    float m_r, m_g, m_b;
};

class Triangle : public Shape
{
public:
    Triangle(float x1, float y1, float x2, float y2, float x3, float y3,
             float r = 1.0f, float g = 1.0f, float b = 1.0f);
protected:
    void generateVertices() override;
private:
    float m_x[3], m_y[3];
    float m_r, m_g, m_b;
};

class Rectangle : public Shape
{
public:
    Rectangle() = default;
    // (cx, cy) 为中心，width/height 为尺寸（NDC 单位）
    Rectangle(float cx, float cy, float width, float height,
              float r = 1.0f, float g = 1.0f, float b = 1.0f);
    void setSize(float w, float h);
    // 更新位置/尺寸/颜色，不销毁缓冲（避免 GPU use-after-free）
    void setBounds(float cx, float cy, float w, float h,
                   float r, float g, float b);
protected:
    void generateVertices() override;
    float m_cx = 0.0f, m_cy = 0.0f, m_w = 1.0f, m_h = 1.0f;
    float m_r = 1.0f, m_g = 1.0f, m_b = 1.0f;
};

// Square 继承自 Rectangle，强制 width == height
class Square : public Rectangle
{
public:
    Square(float cx, float cy, float side,
           float r = 1.0f, float g = 1.0f, float b = 1.0f);
};

class Circle : public Shape
{
public:
    Circle(float cx, float cy, float radius, uint32_t segments = 32,
           float r = 1.0f, float g = 1.0f, float b = 1.0f);
    void setRadius(float r);
protected:
    void generateVertices() override;
private:
    float    m_cx, m_cy, m_radius;
    uint32_t m_segments;
    float    m_r, m_g, m_b;
};

class Wave : public Shape
{
public:
    // 从 (x1,y1) 到 (x2,y2) 绘制正弦波，amplitude 为振幅，frequency 为频率
    Wave(float x1, float y1, float x2, float y2,
         float amplitude = 0.1f, float frequency = 8.0f,
         uint32_t segments = 64,
         float r = 1.0f, float g = 1.0f, float b = 1.0f);
protected:
    void generateVertices() override;
private:
    float    m_x1, m_y1, m_x2, m_y2;
    float    m_amplitude, m_frequency;
    uint32_t m_segments;
    float    m_r, m_g, m_b;
};

class Polygon : public Shape
{
public:
    // 由任意点序列构造多边形（fan 三角化：以第一个点为中心）
    Polygon(const std::vector<std::pair<float, float>>& points,
            float r = 1.0f, float g = 1.0f, float b = 1.0f);
    void setPoints(const std::vector<std::pair<float, float>>& points);
protected:
    void generateVertices() override;
private:
    std::vector<std::pair<float, float>> m_points;
    float m_r, m_g, m_b;
};
