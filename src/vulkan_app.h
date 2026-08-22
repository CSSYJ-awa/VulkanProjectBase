/**
 * VulkanApp —— 图像引擎顶层应用
 *
 * 组合所有引擎模块：
 *   - VulkanContext   渲染后端
 *   - Pipeline2D/3D/Text  图形管线
 *   - Shape 列表     2D 图形（线段/三角/矩形/正方形/圆/波/多边形）
 *   - Mesh3D 列表    3D 几何体（立方体/多面体）
 *   - TextRenderer   文字
 *   - UiManager      JSON 驱动的 UI
 *
 * 窗口标题、大小、exe 名通过 config.json 配置。
 * UI 配置通过 ui_config.json 加载（可选，缺失时仍可运行）。
 */
#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include "ecs/input_state.h"
#include "ecs/time_state.h"
#include "render/texture.h"   // RenderDevice（渲染链模块共享句柄）

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 前置声明
class VulkanContext;
class Pipeline2D;
class Pipeline3D;
class PipelineText;
class Shape;
class Mesh3D;
class TextRenderer;
class UiManager;
struct AppConfig;
struct UiRenderContext;
class Texture;
class RenderTarget;
class PostFx;
class Skybox;
class ShadowMap;
class Instancing;
class ParticleSystem;
class DebugRenderer;

namespace ecs {
    class Coordinator;
    struct EcsVulkanContext;
    class AssetManager;
    class PrefabRegistry;
    class SceneManager;
    class Profiler;
    class InputMapper;
}

struct AppConfig
{
    std::string windowTitle  = "Graphics Engine (Vulkan + MinGW)";
    uint32_t    windowWidth  = 1280;
    uint32_t    windowHeight = 720;
    // "stretch" = 画面随窗口拉伸；"letterbox" = 保持比例，空白填充
    std::string aspectMode  = "stretch";
    // MSAA 多重采样级别（1/2/4/8；v1.0.2，作用于离屏场景 RT）
    uint32_t    msaaSamples = 4;
};

class VulkanApp final
{
public:
    VulkanApp();
    ~VulkanApp();

    VulkanApp(const VulkanApp&) = delete;
    VulkanApp& operator=(const VulkanApp&) = delete;

    void run();

    // 访问器
    VkInstance   vulkanInstance() const { return m_instance; }
    GLFWwindow*  window()         const { return m_window; }
    const AppConfig& config()     const { return m_config; }
    // ECS 协调器访问（用户可在 main.cpp / createDemoScene 后操作实体）
    ecs::Coordinator* ecs()        const { return m_ecs.get(); }
    // v1.0.2 调试可视化访问（用户可在场景/主循环中收集图元）
    DebugRenderer* debugRenderer() const { return m_debug.get(); }
private:
    // ---- 配置 ----
    void loadConfig();

    // ---- Vulkan ----
    void createVulkanInstance();
    void setupDebugMessenger();

    // ---- 窗口 ----
    void createWindow();
    void setupCallbacks();

    // ---- 引擎资源 ----
    void createEngineResources();
    void createDemoScene();   // 用户轻量扩展点（可直接 m_ecs->entity(...) 建实体）
    void registerEcsSystems();        // 注册 ECS 默认系统
    void bindEcsExternalContexts();   // 注入 InputState / EcsVulkanContext 给系统
    void initAssetManager();          // 初始化 AssetManager 的 Vulkan 句柄
    void registerPrefabs();           // 注册常用预制件
    void registerScenes();            // 场景注册（引擎入口：注册你的自定义 Scene）
    void registerInputActions();      // 注册 InputMapper 动作
    void computeRenderArea(); // 计算渲染视口（stretch / letterbox）

    // ---- v1.0.1 渲染链模块 ----
    void createRenderModules();       // 创建 RenderDevice + 离屏/后处理/天空/阴影/实例/粒子模块
    void destroyRenderModules();      // 析构渲染链（创建失败回退时调用）
    void renderShadowPass(VkCommandBuffer cmd);        // 阴影深度 pass
    void renderSceneToRT(VkCommandBuffer cmd, float dt); // 场景 → 离屏 RT
    void beginMainPass(VkCommandBuffer cmd, uint32_t imageIndex); // 重新 begin 主 pass
    glm::mat4 currentCameraView() const;               // ECS 主相机或默认相机

    // ---- 帧渲染 ----
    void renderFrame();
    void draw2DShapes();
    void draw3DMeshes();
    void drawText();
    void drawUI(VkCommandBuffer cmd);
    // ECS 渲染路径（与 m_shapes/m_meshes/m_titleText 并存，可同时使用）
    void drawEcs2D();
    void drawEcs3D();
    void drawEcsText();

    // ---- 输入回调（GLFW 静态 trampoline）----
    static void onFramebufferResize(GLFWwindow* w, int width, int height);
    static void onMouseMove(GLFWwindow* w, double x, double y);
    static void onMouseButton(GLFWwindow* w, int b, int a, int m);
    static void onKey(GLFWwindow* w, int k, int s, int a, int m);
    static void onChar(GLFWwindow* w, unsigned int c);

    // ---- 成员 ----
    AppConfig   m_config;
    VkInstance  m_instance = VK_NULL_HANDLE;

    GLFWwindow* m_window   = nullptr;

    // 引擎模块
    std::unique_ptr<VulkanContext> m_ctx;
    std::unique_ptr<Pipeline2D>     m_pipe2D;         // 填充图元（TRIANGLE_LIST）
    std::unique_ptr<Pipeline2D>     m_pipe2DFan;      // 填充扇（TRIANGLE_FAN：圆/多边形）
    std::unique_ptr<Pipeline2D>     m_pipe2DLine;     // 线段图元
    std::unique_ptr<Pipeline2D>     m_pipe2DLineStrip;// 连续线段图元
    std::unique_ptr<Pipeline3D>     m_pipe3D;
    // 渲染链专用 3D 管线（绑定离屏 RT renderPass，采样级别与 RT 一致；MSAA 时须与
    // m_pipe3D 区分——m_pipe3D 绑定主 renderPass 只能单样本）。
    std::unique_ptr<Pipeline3D>     m_pipe3DRT;
    // 当前生效的 3D 管线（渲染链路径→m_pipe3DRT；传统路径→m_pipe3D）
    Pipeline3D*                     m_pipe3DActive = nullptr;
    std::unique_ptr<PipelineText>   m_pipeText;

    // 3D 网格共享描述符池（createDemoScene 创建，析构释放）
    VkDescriptorPool                m_meshDescPool = VK_NULL_HANDLE;

    // 场景内容（传统容器：与 ECS 路径并存，向后兼容）
    std::vector<std::unique_ptr<Shape>>      m_shapes;
    std::vector<std::unique_ptr<Mesh3D>>     m_meshes;
    std::unique_ptr<TextRenderer>            m_titleText;

    // ECS 协调器（v1.3 新增）
    std::unique_ptr<ecs::Coordinator>         m_ecs;
    // ECS 外部上下文：注入 CameraSystem/InputSystem/SpawnerSystem/DebugSystem
    ecs::InputState                            m_input{};
    ecs::TimeState                             m_time{};
    std::unique_ptr<ecs::EcsVulkanContext>     m_ecsCtx;
    // 资源工厂与预制件（简化场景创建）
    std::unique_ptr<ecs::AssetManager>         m_assets;
    std::unique_ptr<ecs::PrefabRegistry>       m_prefabs;
    // 场景管理 / 性能分析 / 输入动作映射
    std::unique_ptr<ecs::SceneManager>         m_scenes;
    std::unique_ptr<ecs::Profiler>             m_profiler;
    std::unique_ptr<ecs::InputMapper>          m_inputMapper;
    // 鼠标上帧坐标（用于计算 delta）
    double m_lastMouseX = 0.0, m_lastMouseY = 0.0;

    // UI
    std::unique_ptr<UiManager>               m_ui;

    // 渲染区域（letterbox 模式下与窗口尺寸不同）
    VkViewport m_renderViewport = {};
    VkRect2D   m_renderScissor   = {};
    VkExtent2D m_renderExtent    = {0, 0};

    // v1.0.1 渲染链：离屏场景 RT → 后处理 → 主 pass → 2D/UI 叠加
    RenderDevice                      m_renderDev{};
    std::unique_ptr<RenderTarget>     m_sceneRT;      // 离屏场景目标（后处理链输入）
    std::unique_ptr<PostFx>           m_postfx;       // 后处理特效链
    std::unique_ptr<Skybox>           m_skybox;       // 程序化天空 + 环境光
    std::unique_ptr<ShadowMap>        m_shadow;       // 方向光阴影贴图
    std::unique_ptr<Instancing>       m_instancing;   // 实例化渲染
    std::unique_ptr<ParticleSystem>   m_particles;    // CPU 粒子系统
    std::unique_ptr<Texture>          m_particleTex;  // 粒子纹理（演示）
    std::unique_ptr<DebugRenderer>    m_debug;        // v1.0.2 调试可视化
    bool                              m_renderChainEnabled = false;

    // 当前绘制目标视口/裁剪（RT 路径与主 pass 路径切换）
    VkViewport m_activeViewport = {};
    VkRect2D   m_activeScissor  = {};

    bool m_resourcesReady = false;
};
