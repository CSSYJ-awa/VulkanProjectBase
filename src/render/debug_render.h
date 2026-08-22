/**
 * debug_render.h —— 调试可视化渲染器（v1.0.2）
 *
 * 功能：
 *   - 3D 世界空间线段绘制（LINE_LIST），每顶点颜色
 *   - 便捷图元：线段 / AABB 盒 / 球线框 / 地面网格 / 坐标轴 / 法线
 *   - 每帧 clear() 后收集图元，render() 一次批量上传并绘制
 *
 * 用法（在目标 render pass 内）：
 *   m_debug->clear();
 *   m_debug->box(center, halfExtents, color);
 *   m_debug->grid(10.0f, 1.0f, color);
 *   m_debug->render(cmd, view, proj);   // 批量绘制
 *
 * 依赖：render/render_device.h、engine/vulkan_util.h
 */
#pragma once

#include "render/render_device.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

// ============================================================================
// DebugRenderer —— 调试可视化（世界空间线段）
// ============================================================================
class DebugRenderer
{
public:
    DebugRenderer() = default;
    ~DebugRenderer();

    DebugRenderer(const DebugRenderer&) = delete;
    DebugRenderer& operator=(const DebugRenderer&) = delete;

    // 创建调试管线（debug.vert/frag）。targetRenderPass：绘制目标 render pass。
    // samples：多重采样级别（须与 targetRenderPass 颜色附件一致；v1.0.2 MSAA）。
    void create(const RenderDevice& dev, VkRenderPass targetRenderPass,
                const std::string& shaderDir,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

    // 清空本帧收集的图元（每帧开始调用）
    void clear();

    // ─── 图元收集（世界空间） ────────────────────────────────────────────
    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color);
    void box(const glm::vec3& center, const glm::vec3& halfExtents,
             const glm::vec3& color);
    void aabb(const glm::vec3& minP, const glm::vec3& maxP,
              const glm::vec3& color);
    void sphere(const glm::vec3& center, float radius, const glm::vec3& color,
                int segments = 16);
    void circle(const glm::vec3& center, const glm::vec3& axis, float radius,
                const glm::vec3& color, int segments = 16);
    void grid(float halfSize, float step, const glm::vec3& color);   // XZ 平面
    void axes(const glm::vec3& origin, float size);                  // R/G/B 三轴
    void normal(const glm::vec3& pos, const glm::vec3& dir, float len,
                const glm::vec3& color);

    // 绘制全部收集的线段（绑定管线 + 上传 + Draw；调用前目标 pass 已 begin）
    void render(VkCommandBuffer cmd, const glm::mat4& view,
                const glm::mat4& proj) const;

    size_t lineCount() const { return m_lineCount; }

private:
    bool createPipeline(const RenderDevice& dev, VkRenderPass renderPass,
                        const std::string& shaderDir,
                        VkSampleCountFlagBits samples);
    bool grow(uint32_t minVerts);   // 顶点缓冲扩容（重建 + 重新映射）；失败返回 false
    void destroy();

    VkDevice            m_device     = VK_NULL_HANDLE;
    VkPhysicalDevice    m_physicalDevice = VK_NULL_HANDLE;
    VkPipeline          m_pipeline   = VK_NULL_HANDLE;
    VkPipelineLayout    m_layout     = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool    m_descPool   = VK_NULL_HANDLE;
    VkDescriptorSet     m_descSet    = VK_NULL_HANDLE;
    VkBuffer            m_uboBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory      m_uboMemory  = VK_NULL_HANDLE;
    void*               m_uboMapped  = nullptr;

    VkBuffer            m_vb         = VK_NULL_HANDLE;
    VkDeviceMemory      m_vbMemory   = VK_NULL_HANDLE;
    void*               m_vbMapped   = nullptr;
    uint32_t            m_capacity   = 0;   // 顶点容量
    std::vector<float>  m_vertices;         // pos3 + color3（6 float/顶点）
    size_t              m_lineCount  = 0;
};
