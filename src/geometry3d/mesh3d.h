/**
 * Mesh3D —— 3D 网格基类 + 具体几何体
 *
 * 抽象基类 Mesh3D：管理顶点缓冲、UBO（model/view/proj）、描述符集。
 * 派生类只需在 generateVertices() 中填充 m_vertices（vec3 pos + vec3 color）。
 *
 * 派生类层次：
 *   Mesh3D (abstract)
 *     ├── Cube        立方体
 *     └── Polyhedron  多面体（正四面体/正八面体等可参数化构造）
 *
 * 使用 GLM 表示矩阵/向量，与 CMakeLists.txt 中已链接的 GLM 头文件库一致。
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

class Mesh3D
{
public:
    Mesh3D() = default;
    virtual ~Mesh3D();

    Mesh3D(const Mesh3D&) = delete;
    Mesh3D& operator=(const Mesh3D&) = delete;

    // 创建顶点缓冲 + UBO + 描述符集
    void upload(VkDevice device, VkPhysicalDevice pd,
                VkCommandPool pool, VkQueue queue,
                VkDescriptorSetLayout descLayout, VkDescriptorPool descPool);

    // 录制绘制命令（需要 view/proj 组合写入 UBO）
    void draw(VkCommandBuffer cmd, VkPipeline pipeline,
              VkPipelineLayout layout,
              const VkViewport& viewport, const VkRect2D& scissor,
              const glm::mat4& view, const glm::mat4& proj) const;

    // 轻量绘制：仅设置 UBO（若脏）、描述符、VB、Draw。
    // 用于批量绘制，调用者须保证 pipeline/viewport/scissor 已预先设置。
    void drawVBOOnly(VkCommandBuffer cmd, VkPipelineLayout layout,
                     const glm::mat4& view, const glm::mat4& proj) const;

    // 模型变换
    void setModel(const glm::mat4& m) { m_model = m; m_uboDirty = true; }
    const glm::mat4& model() const { return m_model; }

    void setColor(float r, float g, float b, float a = 1.0f);
    const float* color() const { return m_color; }

protected:
    virtual void generateVertices() = 0;

    std::vector<float> m_vertices; // x,y,z, r,g,b  每顶点 6 float
    bool               m_dirty      = true;
    mutable bool       m_uboDirty    = true; // mutable：允许在 const draw() 中清除脏标志
    glm::mat4          m_model       = glm::mat4(1.0f);
    float              m_color[4]    = { 1.0f, 1.0f, 1.0f, 1.0f };

private:
    VkDevice           m_device       = VK_NULL_HANDLE;
    VkBuffer           m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory     m_vertexMemory = VK_NULL_HANDLE;
    VkDeviceSize       m_vertexSize   = 0;
    VkBuffer           m_uboBuffer    = VK_NULL_HANDLE;
    VkDeviceMemory     m_uboMemory    = VK_NULL_HANDLE;
    VkDescriptorSet    m_descSet      = VK_NULL_HANDLE;
    bool               m_uploaded     = false;
};

// ============================================================================
// Cube —— 立方体
// ============================================================================
class Cube : public Mesh3D
{
public:
    Cube(float size = 1.0f,
         float r = 0.8f, float g = 0.6f, float b = 0.4f);
protected:
    void generateVertices() override;
private:
    float m_size;
    float m_r, m_g, m_b;
};

// ============================================================================
// Polyhedron —— 多面体
// 支持类型：tetrahedron（正四面体）、octahedron（正八面体）、
//          icosahedron（正二十面体近似球）
// ============================================================================
class Polyhedron : public Mesh3D
{
public:
    enum class Type
    {
        Tetrahedron,
        Octahedron,
        Icosahedron
    };

    Polyhedron(Type type, float scale = 1.0f,
               float r = 0.4f, float g = 0.7f, float b = 0.9f);
protected:
    void generateVertices() override;
private:
    Type  m_type;
    float m_scale;
    float m_r, m_g, m_b;
};
