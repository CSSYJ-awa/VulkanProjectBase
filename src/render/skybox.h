/**
 * skybox.h —— 天空盒 + 环境光（渲染引擎 v1.0.1 扩展模块）
 *
 * 功能：
 *   - 程序化天空背景：全屏渐变（顶部→地平线→底部）+ 太阳光斑，随相机
 *     yaw/pitch 旋转（无 CubeMap/外部资源依赖）
 *   - 环境光参数（颜色 + 强度），供 mesh3d 光照/场景使用
 *
 * 使用流程：
 *   1. create() 创建天空管线（skybox.vert/frag）
 *   2. 每帧在 3D 场景渲染之前调用 draw(cmd, view, aspect) 绘制背景
 *      （管线关闭深度测试/写入，保证作为背景）
 *   3. 查询 ambientColor()/ambientIntensity() 用于光照
 *
 * 依赖：render/texture.h（RenderDevice）、engine/vulkan_util.h
 */
#pragma once

#include "render/texture.h"

#include <glm/glm.hpp>

#include <string>

// ============================================================================
// Skybox —— 程序化天空 + 环境光
// ============================================================================
class Skybox
{
public:
    Skybox() = default;
    ~Skybox();

    Skybox(const Skybox&) = delete;
    Skybox& operator=(const Skybox&) = delete;

    // 创建天空管线。targetRenderPass：绘制目标 render pass（主 renderPass）。
    // shaderDir 需含 skybox.vert/frag .spv。
    // samples：多重采样级别（须与 targetRenderPass 颜色附件一致；v1.0.2 MSAA）。
    void create(const RenderDevice& dev, VkRenderPass targetRenderPass,
                const std::string& shaderDir,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

    // 绘制程序化天空背景（须在目标 render pass 已 begin 后调用；
    // 建议在 3D 网格绘制之前调用，作为背景）。
    void draw(VkCommandBuffer cmd, const glm::mat4& view, float aspect) const;

    // ─── 外观参数 ──────────────────────────────────────────────────────────
    void setTopColor   (const glm::vec3& c) { m_topColor    = c; }
    void setBottomColor(const glm::vec3& c) { m_bottomColor = c; }
    void setSunColor   (const glm::vec3& c) { m_sunColor    = c; }
    void setSunDirection(const glm::vec3& dir);  // 世界空间
    void setSunIntensity(float v)           { m_sunIntensity = v; }
    void setHorizonSpread(float v)          { m_horizonSpread = v; }  // 地平线过渡宽度

    // ─── 环境光（供场景光照使用） ──────────────────────────────────────────
    void setAmbient(const glm::vec3& color, float intensity)
    { m_ambientColor = color; m_ambientIntensity = intensity; }
    const glm::vec3& ambientColor()    const { return m_ambientColor; }
    float            ambientIntensity() const { return m_ambientIntensity; }

private:
    struct SkyUBO {
        glm::mat4 viewRotInv;    // 相机旋转逆（世界方向恢复）
        glm::vec4 topColor;      // 顶部天空色
        glm::vec4 bottomColor;   // 底部/地平线色
        glm::vec4 sunColor;      // 太阳光颜色
        glm::vec4 params;        // x=sunYaw y=sunPitch z=sunIntensity w=horizonSpread
        glm::vec4 screen;        // x=aspect
    };

    bool createPipeline(const RenderDevice& dev, VkRenderPass renderPass,
                        const std::string& shaderDir,
                        VkSampleCountFlagBits samples);

    VkDevice            m_device    = VK_NULL_HANDLE;
    VkPipeline          m_pipeline  = VK_NULL_HANDLE;
    VkPipelineLayout    m_layout    = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool    m_descPool  = VK_NULL_HANDLE;
    VkDescriptorSet     m_descSet   = VK_NULL_HANDLE;
    VkBuffer            m_uboBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_uboMemory = VK_NULL_HANDLE;
    void*               m_uboMapped = nullptr;

    glm::vec3 m_topColor     = glm::vec3(0.15f, 0.35f, 0.75f);
    glm::vec3 m_bottomColor  = glm::vec3(0.75f, 0.78f, 0.85f);
    glm::vec3 m_sunColor     = glm::vec3(1.0f, 0.95f, 0.85f);
    glm::vec3 m_sunDir       = glm::vec3(0.0f, 1.0f, 0.3f);
    float     m_sunIntensity = 0.0f;
    float     m_horizonSpread = 0.7f;
    glm::vec3 m_ambientColor = glm::vec3(0.35f, 0.4f, 0.5f);
    float     m_ambientIntensity = 0.35f;
};
