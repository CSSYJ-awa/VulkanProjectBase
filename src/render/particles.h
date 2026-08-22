/**
 * particles.h —— 2D/3D 粒子系统（渲染引擎 v1.0.1 扩展模块）
 *
 * 功能：
 *   - CPU 粒子模拟：位置/速度/重力/生命周期/颜色渐变（start→end）
 *   - 点精灵渲染（gl_PointSize 公告板，软圆 alpha 衰减），可叠加粒子纹理
 *   - 顶点缓冲 HOST_VISIBLE 持久映射，每帧更新
 *
 * 使用流程：
 *   1. create() 创建粒子管线（particle.vert/frag）
 *   2. setTexture() 可选绑定粒子纹理（如爆炸/光点）
 *   3. 每帧：spawn() 生成 → update(dt) 模拟 → draw(cmd, view, proj) 渲染
 *
 * 依赖：render/texture.h、engine/vulkan_util.h
 */
#pragma once

#include "render/texture.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// ParticleSystem —— CPU 粒子系统
// ============================================================================
class ParticleSystem
{
public:
    struct Particle
    {
        glm::vec3 position;
        float     size;
        glm::vec3 velocity;
        float     life;         // 剩余生命（秒）
        glm::vec4 color;        // 当前颜色（update 中插值）
        glm::vec4 startColor;
        glm::vec4 endColor;
        float     maxLife;      // 初始生命
        float     gravity;      // 重力加速度（负值上浮）
        float     _pad[2];
    };

    ParticleSystem() = default;
    ~ParticleSystem();

    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    // 创建粒子管线。targetRenderPass：绘制目标 render pass。
    // shaderDir 需含 particle.vert/frag .spv。
    // samples：多重采样级别（须与 targetRenderPass 颜色附件一致；v1.0.2 MSAA）。
    void create(const RenderDevice& dev, VkRenderPass targetRenderPass,
                const std::string& shaderDir,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

    // 预分配粒子容量（超出时自动扩缓冲）
    void setCapacity(const RenderDevice& dev, size_t max);

    // 绑定粒子纹理（nullptr = 纯色软圆）
    void setTexture(const RenderDevice& dev, const Texture* tex);

    // 生成粒子：在 origin 附近、初速 baseVel±spread 随机散射
    // 默认参数适用于"爆炸/火花"类快捷生成；精细控制请显式传入各值。
    void spawn(const RenderDevice& dev,
               const glm::vec3& origin,
               const glm::vec3& baseVel = glm::vec3(0.0f),
               float spread = 0.5f,
               size_t count = 1,
               float life = 1.5f,
               float size = 0.05f,
               const glm::vec4& startColor = glm::vec4(1.0f),
               const glm::vec4& endColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
               float gravity = 0.0f);

    // 推进模拟（dt 秒）
    void update(float dt);

    // 绘制全部存活粒子。调用前：目标 render pass 已 begin。
    void draw(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj,
              float viewportHeight) const;

    size_t alive()  const { return m_alive; }
    size_t max()    const { return m_capacity; }
    void   clear()        { m_alive = 0; m_writeHead = 0; }

private:
    struct ParticleUBO {
        glm::mat4 view;
        glm::mat4 proj;
        float     sizeScale;   // = viewportHeight / 2
        float     texEnabled;  // 0 或 1
        float     _pad[2];
    };

    void rebuildBuffer(const RenderDevice& dev);
    void writeTexDescriptor(const RenderDevice& dev);

    VkDevice            m_device    = VK_NULL_HANDLE;
    VkPipeline          m_pipeline  = VK_NULL_HANDLE;
    VkPipelineLayout    m_layout    = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool    m_descPool  = VK_NULL_HANDLE;
    VkDescriptorSet     m_descSet   = VK_NULL_HANDLE;
    VkBuffer            m_uboBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_uboMemory = VK_NULL_HANDLE;
    void*               m_uboMapped = nullptr;

    VkBuffer            m_vb        = VK_NULL_HANDLE;
    VkDeviceMemory      m_vbMemory  = VK_NULL_HANDLE;
    void*               m_vbMapped  = nullptr;
    size_t              m_capacity  = 0;

    // 白色 1x1 纹理（无用户纹理时使用）
    std::unique_ptr<Texture> m_fallbackTex;
    const Texture*     m_tex       = nullptr;
    VkDescriptorSet    m_texSet    = VK_NULL_HANDLE;   // 纹理采样描述符（独立集）

    std::vector<Particle> m_pool;
    size_t              m_alive     = 0;
    size_t              m_writeHead = 0;   // 追加生成的水位
    // draw() 为 const（只读语义），缓冲重传后清 dirty 标志用 mutable
    mutable bool        m_dirty     = true; // 缓冲需重传
};
