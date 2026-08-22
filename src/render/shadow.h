/**
 * shadow.h —— 方向光阴影映射（渲染引擎 v1.0.1 扩展模块）
 *
 * 功能：
 *   - 深度贴图（depth-only render pass）：把场景 3D 网格按光源视角渲染进深度图
 *   - 光空间 view-proj 矩阵计算（正交投影，适合方向光）
 *   - 深度纹理 + 比较采样器（PCF 软阴影基础），供主 3D pass 采样
 *
 * 使用流程：
 *   1. create() 创建深度图 + 深度管线（shadow.vert/frag）
 *   2. 每帧：begin(cmd) → 遍历场景调用 Mesh3D::drawDepth(cmd, depthLayout,
 *      lightVP, model)（内部 push constant 传 model）→ end(cmd)
 *   3. 主 3D pass：bindDescriptor(cmd, layout) 把深度图描述符绑定到阴影管线
 *
 * 依赖：render/texture.h（RenderDevice）、engine/vulkan_util.h
 */
#pragma once

#include "render/texture.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

// ============================================================================
// ShadowMap —— 方向光阴影贴图
// ============================================================================
class ShadowMap
{
public:
    ShadowMap() = default;
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // 创建深度图（size×size 深度纹理 + depth-only render pass + 深度管线）。
    // shaderDir 需含 shadow.vert/frag .spv。
    void create(const RenderDevice& dev, const std::string& shaderDir,
                uint32_t size = 2048);

    // ─── 生成阶段（每帧） ──────────────────────────────────────────────────
    // 进入阴影 pass（深度清除为 1.0）；之后调用 Mesh3D::drawDepth 绘制场景。
    void begin(VkCommandBuffer cmd) const;
    void end(VkCommandBuffer cmd) const;

    // 更新光源 view-proj UBO（每帧一次）并绑定深度管线描述符。
    // 必须在 begin() 之前调用（或在 begin 后、draw 前调用）。
    void setLightMatrix(VkCommandBuffer cmd, const glm::mat4& lightVP);

    // 深度管线的 layout（供 Mesh3D::drawDepth 使用）
    VkPipelineLayout depthLayout() const { return m_depthLayout; }

    // ─── 主 3D pass 绑定 ───────────────────────────────────────────────────
    // 把深度图描述符绑定到 3D 阴影管线（binding 0 = shadow sampler）
    void bindToScene(VkCommandBuffer cmd, VkPipelineLayout sceneLayout) const;

    // 供 3D 阴影管线使用的描述符信息
    VkDescriptorSet       descriptorSet()       const { return m_descSet; }
    VkDescriptorSetLayout descriptorSetLayout() const { return m_descSetLayout; }

    const glm::mat4& lightViewProj() const { return m_lightVP; }
    uint32_t         size()        const { return m_size; }
    VkImageView      depthView()   const { return m_depthView; }

    // 方向光正交 view-proj：以 sceneCenter 为中心、radius 为半包围盒半径
    static glm::mat4 computeLightVP(const glm::vec3& lightDir,
                                    const glm::vec3& sceneCenter,
                                    float radius);

private:
    bool createDepthResources(const RenderDevice& dev, uint32_t size);
    bool createPipeline(const RenderDevice& dev, const std::string& shaderDir);

    VkDevice            m_device      = VK_NULL_HANDLE;
    VkRenderPass        m_depthPass   = VK_NULL_HANDLE;
    VkFramebuffer       m_framebuffer = VK_NULL_HANDLE;

    VkImage             m_depthImage  = VK_NULL_HANDLE;
    VkDeviceMemory      m_depthMemory = VK_NULL_HANDLE;
    VkImageView         m_depthView   = VK_NULL_HANDLE;

    VkSampler           m_depthSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;  // 供主 3D pass 采样（sampler）
    VkDescriptorPool    m_descPool    = VK_NULL_HANDLE;
    VkDescriptorSet     m_descSet     = VK_NULL_HANDLE;      // 主 3D pass 采样描述符

    // 深度管线专用：lightVP UBO
    VkDescriptorSetLayout m_depthDescLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_depthDescPool   = VK_NULL_HANDLE;
    VkDescriptorSet       m_depthUboSet     = VK_NULL_HANDLE;
    bool                  m_depthUboWritten = false;

    VkPipeline          m_depthPipeline = VK_NULL_HANDLE;
    VkPipelineLayout    m_depthLayout   = VK_NULL_HANDLE;

    VkBuffer            m_lightVPBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_lightVPMemory = VK_NULL_HANDLE;
    void*               m_lightVPMapped = nullptr;

    glm::mat4           m_lightVP      = glm::mat4(1.0f);
    uint32_t            m_size         = 2048;
};
