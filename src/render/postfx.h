/**
 * postfx.h —— 后处理特效链（渲染引擎 v1.0.1 扩展模块）
 *
 * 功能：
 *   - 全屏后处理管线：把 RenderTarget 的颜色附件作为输入纹理，经特效
 *     shader 处理后输出到当前已 begin 的 render pass
 *   - 内置 7 种单 pass 特效：灰度 / 反色 / 高斯模糊 / 边缘检测 / 锐化 /
 *     近似泛光（bloom）/ 原样
 *   - 色调映射（曝光度）叠加开关
 *   - 单 UBO 控制（模式/强度/曝光/阈值/纹素尺寸），无需逐特效建管线
 *
 * 使用模式（后处理链）：
 *   1. 场景渲染进 RenderTargetA（离屏）
 *   2. PostFx::apply(A, cmd) 在目标 RenderTargetB 的 pass 内绘制特效
 *   3. 如需多特效串联，把 B 作为下一次 apply 的输入（乒乓）
 *
 * 依赖：render/framebuffer.h、render/texture.h
 */
#pragma once

#include "render/framebuffer.h"

#include <vulkan/vulkan.h>

#include <string>

// ============================================================================
// PostFxEffect —— 特效枚举
// ============================================================================
enum class PostFxEffect
{
    kNone       = 0,   // 原样输出
    kGrayscale  = 1,   // 灰度
    kInvert     = 2,   // 反色
    kBlur       = 3,   // 高斯模糊（9-tap 近似）
    kEdgeDetect = 4,   // 边缘检测（Sobel 近似）
    kSharpen    = 5,   // 锐化
    kBloom      = 6    // 近似泛光（阈值高亮 + 邻域采样 + 强度混合）
};

// ============================================================================
// PostFx —— 后处理特效链
// ============================================================================
class PostFx
{
public:
    PostFx() = default;
    ~PostFx();

    PostFx(const PostFx&) = delete;
    PostFx& operator=(const PostFx&) = delete;

    // targetRenderPass：特效绘制目标 render pass（与 RenderTarget::renderPass()
    // 或主 renderPass 同构：1 个颜色附件）。shaderDir 需含 postfx.vert/frag .spv。
    void create(const RenderDevice& dev, VkRenderPass targetRenderPass,
                const std::string& shaderDir);

    // ─── 参数（每次 apply 时写入 UBO） ────────────────────────────────────
    void setEffect(PostFxEffect e)  { m_effect = e; }
    void setIntensity(float v)      { m_intensity = v; }      // 默认 1.0
    void setToneMap(bool on)        { m_toneMap = on; }       // 曝光/色调映射
    void setExposure(float e)       { m_exposure = e; }       // 默认 1.0
    void setBloomThreshold(float t) { m_bloomThreshold = t; } // 默认 0.8

    PostFxEffect effect()    const { return m_effect; }
    float        intensity() const { return m_intensity; }

    // ─── 应用 ──────────────────────────────────────────────────────────────
    // 把 src 的颜色附件作为输入纹理绘制到"当前已 begin 的 render pass"。
    // 调用前：目标 render pass 必须已 vkCmdBeginRenderPass。
    void apply(const RenderTarget& src, VkCommandBuffer cmd) const;

private:
    struct PostFxUBO {
        int    mode;            // PostFxEffect
        float  intensity;       // 特效强度
        float  exposure;        // 色调映射曝光
        float  bloomThreshold;  // 泛光阈值
        float  texelW;          // 1/width
        float  texelH;          // 1/height
        float  _pad0, _pad1;    // std140 对齐
    };

    bool createPipeline(const RenderDevice& dev, VkRenderPass renderPass,
                        const std::string& shaderDir);

    VkDevice            m_device    = VK_NULL_HANDLE;
    VkPipeline          m_pipeline  = VK_NULL_HANDLE;
    VkPipelineLayout    m_layout    = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool    m_descPool  = VK_NULL_HANDLE;
    VkDescriptorSet     m_descSet   = VK_NULL_HANDLE;
    VkBuffer            m_uboBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_uboMemory = VK_NULL_HANDLE;
    void*               m_uboMapped = nullptr;

    PostFxEffect        m_effect        = PostFxEffect::kNone;
    float               m_intensity     = 1.0f;
    bool                m_toneMap       = false;
    float               m_exposure      = 1.0f;
    float               m_bloomThreshold= 0.8f;
    float               m_texelW        = 1.0f;
    float               m_texelH        = 1.0f;
};
