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

#include "../render/render_device.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

class Texture;   // 前置声明（材质贴图；纹理生命周期由调用方管理）

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

    // 便捷重载（v1.0.1）：从 RenderDevice 一次性获取 device/pd/pool/queue
    void upload(const RenderDevice& dev,
                VkDescriptorSetLayout descLayout, VkDescriptorPool descPool);

    // 轻量绘制：仅设置 UBO（若脏）、描述符、VB、Draw。
    // 用于批量绘制，调用者须保证 pipeline/viewport/scissor 已预先设置。
    void drawVBOOnly(VkCommandBuffer cmd, VkPipelineLayout layout,
                     const glm::mat4& view, const glm::mat4& proj) const;

    // 阴影深度绘制（v1.0.1）：绑定顶点缓冲 + push constant(model) + Draw。
    // 由 ShadowMap::begin 预先绑定深度管线与 lightVP 描述符；
    // depthLayout = ShadowMap::depthLayout()，push constant 偏移 0、大小 64（model）。
    void drawDepth(VkCommandBuffer cmd, VkPipelineLayout depthLayout,
                   const glm::mat4& model) const;

    // 模型变换
    void setModel(const glm::mat4& m) { m_model = m; m_uboDirty = true; }
    const glm::mat4& model() const { return m_model; }

    void setColor(float r, float g, float b, float a = 1.0f);
    const float* color() const { return m_color; }

    // 设置场景光照（世界空间光照方向 = 光传播方向、光颜色、环境光强度）。
    // 仅当值变化时标记 UBO 脏，避免每帧无谓重写。
    void setLight(const glm::vec3& lightDir,
                  const glm::vec3& lightColor,
                  float ambient);

    // ── v1.0.2 材质 / 雾效 / 半球环境光 ────────────────────────────────
    // 绑定纹理（三平面映射 triplanar，无需 UV；nullptr = 纯顶点色）。
    // 纹理由调用方持有，Mesh3D 不拥有。
    void setTexture(const Texture* tex);
    // 三平面采样缩放（>0 启用纹理；0 = 关闭仅顶点色）。默认 0（关闭）。
    void setTextureScale(float scale);
    // 当前绑定纹理（批处理排序用；nullptr = 无材质）
    const Texture* texture() const { return m_texture; }
    // 距离雾：on=true 启用（fogStart→fogEnd 线性过渡到 fogColor）
    void setFog(bool on, const glm::vec3& color = glm::vec3(0.7f),
                float start = 6.0f, float end = 20.0f);
    // 半球环境光：up/down 为上下半球颜色（法线越朝上越接近 up 色）
    void setHemisphere(const glm::vec3& up, const glm::vec3& down);

protected:
    virtual void generateVertices() = 0;

    std::vector<float> m_vertices; // x,y,z, nx,ny,nz, r,g,b  每顶点 9 float
    bool               m_dirty      = true;
    mutable bool       m_uboDirty    = true; // mutable：允许在 const draw() 中清除脏标志
    glm::mat4          m_model       = glm::mat4(1.0f);
    // 缓存上次 draw 使用的 view/proj，用于检测外部变化触发 UBO 刷新
    mutable glm::mat4  m_lastView    = glm::mat4(0.0f);
    mutable glm::mat4  m_lastProj    = glm::mat4(0.0f);
    float              m_color[4]    = { 1.0f, 1.0f, 1.0f, 1.0f };
    // 光照参数缓存（由 RenderSystem3D 每帧注入；变化才触发 UBO 刷新）
    glm::vec3          m_lightDir    = glm::vec3(0.0f, -1.0f, 0.3f);
    glm::vec3          m_lightColor  = glm::vec3(1.0f);
    float              m_ambient     = 0.15f;
    // v1.0.2 材质/雾/半球光参数
    const Texture*     m_texture     = nullptr;
    float              m_texScale    = 0.0f;              // 0 = 关闭纹理
    bool               m_fogOn       = false;
    glm::vec3          m_fogColor    = glm::vec3(0.7f);
    float              m_fogStart    = 6.0f;
    float              m_fogEnd      = 20.0f;
    glm::vec3          m_ambientUp   = glm::vec3(0.5f, 0.55f, 0.6f);
    glm::vec3          m_ambientDown = glm::vec3(0.15f, 0.15f, 0.2f);

private:
    VkDevice           m_device       = VK_NULL_HANDLE;
    VkBuffer           m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory     m_vertexMemory = VK_NULL_HANDLE;
    VkDeviceSize       m_vertexSize   = 0;
    VkBuffer           m_uboBuffer    = VK_NULL_HANDLE;
    VkDeviceMemory     m_uboMemory    = VK_NULL_HANDLE;
    void*              m_uboMapped    = nullptr;  // 持久映射指针（HOST_VISIBLE 缓冲）
    VkDescriptorSet    m_descSet      = VK_NULL_HANDLE;
    VkDescriptorPool   m_descPool     = VK_NULL_HANDLE;  // 描述符集来源池（析构时回收）
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
