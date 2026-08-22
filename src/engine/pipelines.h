/**
 * Pipelines —— 三种图形管线
 *
 *   Pipeline2D:   basic.vert/frag，顶点色 + push constant 颜色
 *   Pipeline3D:   mesh3d.vert/frag，MVP UBO + push constant 颜色
 *   PipelineText: text.vert/frag，纹理采样 + push constant 颜色
 *
 * 每个管线封装 VkPipelineLayout + VkPipeline + （如有）DescriptorSetLayout。
 */
#pragma once

#include <vulkan/vulkan.h>
#include <string>

class Pipeline2D final
{
public:
    Pipeline2D() = default;
    ~Pipeline2D();

    Pipeline2D(const Pipeline2D&) = delete;
    Pipeline2D& operator=(const Pipeline2D&) = delete;

    // shaderDir 应包含 basic.vert.spv / basic.frag.spv
    // topology 指定图元拓扑（默认三角形列表，线段类图形用 LINE_LIST）
    // samples：多重采样级别（v1.0.2 MSAA；默认 1x）
    void create(VkDevice device, VkRenderPass renderPass,
                VkExtent2D extent, const std::string& shaderDir,
                VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

    VkPipeline       pipeline()       const { return m_pipeline; }
    VkPipelineLayout pipelineLayout() const { return m_layout; }

private:
    VkDevice         m_device   = VK_NULL_HANDLE;
    VkPipeline       m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout   = VK_NULL_HANDLE;
};

// ============================================================================
// Pipeline3D —— 3D 网格（MVP UBO）
// ============================================================================
class Pipeline3D final
{
public:
    Pipeline3D() = default;
    ~Pipeline3D();

    Pipeline3D(const Pipeline3D&) = delete;
    Pipeline3D& operator=(const Pipeline3D&) = delete;

    void create(VkDevice device, VkRenderPass renderPass,
                VkExtent2D extent, const std::string& shaderDir,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

    VkPipeline            pipeline()             const { return m_pipeline; }
    VkPipelineLayout      pipelineLayout()       const { return m_layout; }
    VkDescriptorSetLayout descriptorSetLayout()  const { return m_descSetLayout; }

private:
    VkDevice               m_device       = VK_NULL_HANDLE;
    VkPipeline             m_pipeline     = VK_NULL_HANDLE;
    VkPipelineLayout       m_layout       = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_descSetLayout= VK_NULL_HANDLE;
};

// ============================================================================
// PipelineText —— 文字（纹理采样）
// ============================================================================
class PipelineText final
{
public:
    PipelineText() = default;
    ~PipelineText();

    PipelineText(const PipelineText&) = delete;
    PipelineText& operator=(const PipelineText&) = delete;

    void create(VkDevice device, VkRenderPass renderPass,
                VkExtent2D extent, const std::string& shaderDir,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

    VkPipeline            pipeline()             const { return m_pipeline; }
    VkPipelineLayout      pipelineLayout()       const { return m_layout; }
    VkDescriptorSetLayout descriptorSetLayout()  const { return m_descSetLayout; }
    VkDescriptorPool      descriptorPool()       const { return m_descPool; }

    // 为一张字形图集分配描述符集合
    VkDescriptorSet allocateDescriptorSet();

private:
    VkDevice               m_device       = VK_NULL_HANDLE;
    VkPipeline             m_pipeline     = VK_NULL_HANDLE;
    VkPipelineLayout       m_layout       = VK_NULL_HANDLE;
    VkDescriptorSetLayout  m_descSetLayout= VK_NULL_HANDLE;
    VkDescriptorPool       m_descPool     = VK_NULL_HANDLE;
};
