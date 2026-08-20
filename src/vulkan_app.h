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

struct AppConfig
{
    std::string windowTitle  = "Graphics Engine (Vulkan + MinGW)";
    uint32_t    windowWidth  = 1280;
    uint32_t    windowHeight = 720;
    std::string uiConfigPath = "ui_config.json";
    // "stretch" = 画面随窗口拉伸；"letterbox" = 保持比例，空白填充
    std::string aspectMode  = "stretch";
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
    void createDemoScene();   // 用户自定义场景入口（在此往 m_shapes/m_meshes/m_titleText 添加内容）
    void computeRenderArea(); // 计算渲染视口（stretch / letterbox）

    // ---- 帧渲染 ----
    void renderFrame();
    void draw2DShapes();
    void draw3DMeshes();
    void drawText();
    void drawUI(VkCommandBuffer cmd);

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
    std::unique_ptr<PipelineText>   m_pipeText;

    // 3D 网格共享描述符池（createDemoScene 创建，析构释放）
    VkDescriptorPool                m_meshDescPool = VK_NULL_HANDLE;

    // 场景内容
    std::vector<std::unique_ptr<Shape>>      m_shapes;
    std::vector<std::unique_ptr<Mesh3D>>     m_meshes;
    std::unique_ptr<TextRenderer>            m_titleText;

    // UI
    std::unique_ptr<UiManager>               m_ui;

    // 渲染区域（letterbox 模式下与窗口尺寸不同）
    VkViewport m_renderViewport = {};
    VkRect2D   m_renderScissor   = {};
    VkExtent2D m_renderExtent    = {0, 0};

    bool m_resourcesReady = false;
};
