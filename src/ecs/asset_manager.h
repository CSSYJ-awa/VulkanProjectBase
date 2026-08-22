/**
 * asset_manager.h —— GPU 资源工厂与缓存
 *
 * 问题背景：
 *   原先 createDemoScene 里每创建一个 Mesh3D/Shape 都要重复传 6~7 个 Vulkan
 *   句柄（device/pd/pool/queue/descLayout/descPool）。AssetManager 把这些
 *   句柄集中持有，对外只暴露 createCube()/createPolyhedron()/createCircle()
 *   等工厂方法，业务代码大幅简化。
 *
 * 设计要点：
 *   - Mesh3D 持有可变状态（model 矩阵、UBO 持久映射、描述符集），不能多实体
 *     共享同一实例；故 AssetManager 不缓存 Mesh3D 实例，而是缓存"模板工厂"。
 *   - 模板（MeshTemplate）封装构造参数，调用 instantiate() 时新建 Mesh3D 并 upload。
 *   - Shape（2D）同理：每实体独立 GPU 资源。
 *   - VulkanContext 弱引用：AssetManager 不持有所有权，由 VulkanApp 保证生命周期
 *     比 AssetManager 长（实际就是 VulkanApp 析构时 AssetManager 先析构）。
 *
 * 用法：
 *   AssetManager assets;
 *   assets.init(device, pd, pool, queue, meshLayout, meshPool);
 *   auto cube = assets.createCube(1.0f, 1.0f, 0.75f, 0.15f);
 *   // cube 是 std::unique_ptr<Mesh3D>，已 upload，可直接挂到组件
 *
 *   // 注册预制模板：
 *   assets.registerMeshTemplate("player_body", []() {
 *       return std::make_unique<Polyhedron>(Polyhedron::Icosahedron, 0.5f, 0.2f, 0.9f, 0.3f);
 *   });
 *   auto body = assets.instantiate("player_body");  // 新实例，已 upload
 */
#pragma once

#include "../geometry3d/mesh3d.h"
#include "../shapes/shape.h"

#include <vulkan/vulkan.h>
#include <memory>
#include <functional>
#include <string>
#include <unordered_map>

namespace ecs {

class AssetManager
{
public:
    AssetManager() = default;
    ~AssetManager() = default;

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // 初始化 Vulkan 句柄（VulkanApp::createEngineResources 之后调用一次）
    void init(VkDevice device,
              VkPhysicalDevice physicalDevice,
              VkCommandPool commandPool,
              VkQueue graphicsQueue,
              VkDescriptorSetLayout meshSetLayout,
              VkDescriptorPool meshPool)
    {
        m_device         = device;
        m_physicalDevice = physicalDevice;
        m_commandPool    = commandPool;
        m_graphicsQueue  = graphicsQueue;
        m_meshSetLayout  = meshSetLayout;
        m_meshPool       = meshPool;
        m_ready           = true;
    }

    // 便捷重载（v1.0.1）：从 RenderDevice 一次性获取 device/pd/pool/queue
    void init(const RenderDevice& dev,
              VkDescriptorSetLayout meshSetLayout,
              VkDescriptorPool meshPool)
    {
        init(dev.device, dev.physicalDevice, dev.commandPool, dev.queue,
             meshSetLayout, meshPool);
    }

    bool ready() const { return m_ready; }

    // ------------------------------------------------------------------
    // 3D 网格工厂
    // ------------------------------------------------------------------

    // 立方体：size 为边长，rgb 为颜色
    std::unique_ptr<Mesh3D> createCube(float size = 1.0f,
                                       float r = 0.8f, float g = 0.6f, float b = 0.4f) const
    {
        auto mesh = std::make_unique<Cube>(size, r, g, b);
        uploadMesh(*mesh);
        return mesh;
    }

    // 多面体
    std::unique_ptr<Mesh3D> createPolyhedron(Polyhedron::Type type,
                                             float scale = 1.0f,
                                             float r = 0.4f, float g = 0.7f, float b = 0.9f) const
    {
        auto mesh = std::make_unique<Polyhedron>(type, scale, r, g, b);
        uploadMesh(*mesh);
        return mesh;
    }

    // 正二十面体（近似球）的快捷调用
    std::unique_ptr<Mesh3D> createSphere(float scale = 1.0f,
                                         float r = 0.4f, float g = 0.7f, float b = 0.9f) const
    {
        return createPolyhedron(Polyhedron::Type::Icosahedron, scale, r, g, b);
    }

    // 正八面体
    std::unique_ptr<Mesh3D> createOctahedron(float scale = 1.0f,
                                              float r = 0.85f, float g = 0.85f, float b = 0.9f) const
    {
        return createPolyhedron(Polyhedron::Type::Octahedron, scale, r, g, b);
    }

    // ------------------------------------------------------------------
    // 2D 形状工厂
    // ------------------------------------------------------------------

    // 圆形（NDC 2D 坐标，用于粒子等）
    std::unique_ptr<Shape> createCircle(float cx, float cy, float radius,
                                        int segments = 24,
                                        float r = 0.9f, float g = 0.6f, float b = 0.2f) const
    {
        auto s = std::make_unique<Circle>(cx, cy, radius, segments, r, g, b);
        uploadShape(*s);
        return s;
    }

    // ------------------------------------------------------------------
    // 模板注册与实例化
    //
    // registerMeshTemplate：注册一个工厂函数（返回未 upload 的 Mesh3D）
    // instantiate：调用工厂创建实例并 upload 到 GPU
    // 用途：常用预设（玩家身体、敌人、子弹）避免重复写参数
    // ------------------------------------------------------------------
    using MeshFactory = std::function<std::unique_ptr<Mesh3D>()>;

    void registerMeshTemplate(const std::string& name, MeshFactory factory)
    {
        m_meshTemplates[name] = std::move(factory);
    }

    std::unique_ptr<Mesh3D> instantiate(const std::string& name) const
    {
        auto it = m_meshTemplates.find(name);
        if (it == m_meshTemplates.end() || !it->second) return nullptr;
        auto mesh = it->second();
        if (mesh) uploadMesh(*mesh);
        return mesh;
    }

    bool hasTemplate(const std::string& name) const
    {
        return m_meshTemplates.find(name) != m_meshTemplates.end();
    }

private:
    void uploadMesh(Mesh3D& mesh) const
    {
        mesh.upload(m_device, m_physicalDevice, m_commandPool, m_graphicsQueue,
                    m_meshSetLayout, m_meshPool);
    }

    void uploadShape(Shape& s) const
    {
        s.upload(m_device, m_physicalDevice, m_commandPool, m_graphicsQueue);
    }

    // Vulkan 句柄（弱引用，VulkanApp 持有所有权）
    VkDevice             m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice     m_physicalDevice = VK_NULL_HANDLE;
    VkCommandPool        m_commandPool    = VK_NULL_HANDLE;
    VkQueue              m_graphicsQueue  = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_meshSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool     m_meshPool       = VK_NULL_HANDLE;

    // 模板注册表
    std::unordered_map<std::string, MeshFactory> m_meshTemplates;

    bool m_ready = false;
};

} // namespace ecs
