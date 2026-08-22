/**
 * instancing.h —— 实例化渲染（渲染引擎 v1.0.1 扩展模块）
 *
 * 功能：
 *   - 大量同模型高效渲染：每实例一个 mat4 变换 + 每实例颜色
 *   - 内部维护实例缓冲（HOST_VISIBLE 持久映射），CPU 每帧可更新
 *   - 顶点输入：binding0 = 顶点（pos+normal+color，9 float/顶点，stride 36），
 *     binding1 = 实例矩阵（mat4，instanced），binding2 = 实例颜色（vec4，instanced）
 *   - 光照：N·L 漫反射 + 环境光（与 mesh3d 一致），UBO 提供 view/proj/光照
 *
 * 使用流程：
 *   1. create() 创建实例化管线（instanced.vert/frag）
 *   2. uploadMesh() 上传基准网格顶点；setInstances() 每帧更新实例矩阵/颜色
 *   3. draw(cmd, view, proj, lightParams, count) 批量绘制
 *
 * 依赖：render/texture.h（RenderDevice）、engine/vulkan_util.h
 */
#pragma once

#include "render/texture.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// Instancing —— 实例化渲染
// ============================================================================
class Instancing
{
public:
    Instancing() = default;
    ~Instancing();

    Instancing(const Instancing&) = delete;
    Instancing& operator=(const Instancing&) = delete;

    // 创建实例化管线。targetRenderPass：绘制目标 render pass。
    // shaderDir 需含 instanced.vert/frag .spv。
    // samples：多重采样级别（须与 targetRenderPass 颜色附件一致；v1.0.2 MSAA）。
    void create(const RenderDevice& dev, VkRenderPass targetRenderPass,
                const std::string& shaderDir,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

    // 上传基准网格顶点（9 float/顶点：pos3+normal3+color3）。
    // 可选索引；提供索引时按索引绘制。
    void uploadMesh(const RenderDevice& dev,
                    const std::vector<float>& vertices,
                    const std::vector<uint32_t>& indices = {});

    // 更新实例数据（每帧）。count 与缓冲容量不一致时自动扩容。
    void setInstances(const RenderDevice& dev,
                      const glm::mat4* transforms, size_t count,
                      const glm::vec4* colors = nullptr);

    // 光照参数（每帧或变化时设置；内部写入 UBO）
    void setLight(const glm::vec3& lightDir, const glm::vec3& lightColor, float ambient);

    // 绑定管线 + 描述符 + 绘制 count 个实例。
    // 调用前：目标 render pass 已 begin。
    void draw(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj,
              size_t instanceCount) const;

    size_t capacity() const { return m_capacity; }

private:
    struct InstanceUBO {
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec3 lightDir;
        float     ambient;
        glm::vec3 lightColor;
        float     _pad;
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

    VkBuffer            m_meshBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_meshMemory = VK_NULL_HANDLE;
    uint32_t            m_meshCount  = 0;
    bool                m_hasIndices = false;
    VkBuffer            m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_indexMemory = VK_NULL_HANDLE;
    uint32_t            m_indexCount  = 0;

    VkBuffer            m_instBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_instMemory = VK_NULL_HANDLE;
    void*               m_instMapped = nullptr;   // 实例缓冲持久映射（每帧更新零 map 开销）
    size_t              m_capacity   = 0;

    glm::vec3           m_lightDir  = glm::vec3(0.0f, -1.0f, 0.3f);
    glm::vec3           m_lightColor = glm::vec3(1.0f);
    float               m_ambient   = 0.35f;
};
