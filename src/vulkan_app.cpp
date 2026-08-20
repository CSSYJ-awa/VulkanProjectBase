/**
 * VulkanApp 实现 —— 图像引擎顶层
 */
#include "vulkan_app.h"

#include "engine/vulkan_context.h"
#include "engine/pipelines.h"
#include "engine/vulkan_util.h"
#include "shapes/shape.h"
#include "geometry3d/mesh3d.h"
#include "text/text_renderer.h"
#include "ui/ui_manager.h"
#include "ui/ui_widgets.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// 辅助：定位 config.json 路径（同前实现）
// ============================================================================
static std::string getConfigPath()
{
    {
        std::ifstream test("config.json");
        if (test.is_open()) { test.close(); return "config.json"; }
    }
#ifdef _WIN32
    char exePath[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0)
    {
        std::string p(exePath);
        size_t pos = p.find_last_of("\\/");
        if (pos != std::string::npos)
            return p.substr(0, pos) + "\\config.json";
    }
#endif
    return "config.json";
}

// 资源目录：尝试 cwd/shaders，回退到 exe 同目录
static std::string getShaderDir()
{
    {
        std::ifstream test("shaders/basic.vert.spv");
        if (test.is_open()) { test.close(); return "shaders"; }
    }
#ifdef _WIN32
    char exePath[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0)
    {
        std::string p(exePath);
        size_t pos = p.find_last_of("\\/");
        if (pos != std::string::npos)
            return p.substr(0, pos) + "\\shaders";
    }
#endif
    return "shaders";
}

static std::string getUiConfigPath(const std::string& configured)
{
    {
        std::ifstream test(configured);
        if (test.is_open()) { test.close(); return configured; }
    }
#ifdef _WIN32
    char exePath[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0)
    {
        std::string p(exePath);
        size_t pos = p.find_last_of("\\/");
        if (pos != std::string::npos)
        {
            std::string name = configured;
            size_t sp = name.find_last_of("\\/");
            if (sp != std::string::npos) name = name.substr(sp+1);
            return p.substr(0, pos) + "\\" + name;
        }
    }
#endif
    return configured;
}

// ============================================================================
// 构造 / 析构
// ============================================================================

VulkanApp::VulkanApp()
{
    loadConfig();
    createWindow();
    createVulkanInstance();

    m_ctx = std::make_unique<VulkanContext>(m_instance, m_window,
        m_config.windowWidth, m_config.windowHeight);

    createEngineResources();
    createDemoScene();

    m_ui = std::make_unique<UiManager>();
    if (!m_config.uiConfigPath.empty())
    {
        std::string path = getUiConfigPath(m_config.uiConfigPath);
        m_ui->loadFromFile(path);

        // --- 在此为 UI 控件绑定事件回调（示例模板）---
        // if (auto* root = m_ui->root()) {
        //     if (auto* btn = dynamic_cast<UiButton*>(root->findByName("my_btn"))) {
        //         btn->setClickHandler([this]() {
        //             // 按钮行为
        //         });
        //     }
        //     if (auto* tb = dynamic_cast<UiTextBox*>(root->findByName("my_input"))) {
        //         tb->setInputHandler([this](const std::string& s) {
        //             // 输入变化行为
        //         });
        //     }
        // }
    }

    setupCallbacks();
    m_resourcesReady = true;
    std::cout << "[VulkanApp] 图像引擎初始化完成。" << std::endl;
}

VulkanApp::~VulkanApp()
{
    // 先等待 GPU 空闲
    if (m_ctx) vkDeviceWaitIdle(m_ctx->device());

    m_ui.reset();
    m_titleText.reset();
    m_shapes.clear();
    m_meshes.clear();

    // 销毁 3D 网格共享描述符池（必须在 mesh 析构之后，描述符集从池分配）
    if (m_meshDescPool != VK_NULL_HANDLE && m_ctx)
    {
        vkDestroyDescriptorPool(m_ctx->device(), m_meshDescPool, nullptr);
        m_meshDescPool = VK_NULL_HANDLE;
    }

    m_pipeText.reset();
    m_pipe3D.reset();
    m_pipe2DLineStrip.reset();
    m_pipe2DLine.reset();
    m_pipe2DFan.reset();
    m_pipe2D.reset();
    m_ctx.reset();

    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    if (m_window)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
    std::cout << "[VulkanApp] 已销毁。" << std::endl;
}

// ============================================================================
// 配置加载
// ============================================================================

void VulkanApp::loadConfig()
{
    std::string path = getConfigPath();
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::cout << "[Config] 未找到 config.json，使用默认配置。" << std::endl;
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    std::smatch m;
    if (std::regex_search(content, m, std::regex("\"window_title\"\\s*:\\s*\"([^\"]*)\"")))
        m_config.windowTitle = m[1].str();
    if (std::regex_search(content, m, std::regex("\"window_width\"\\s*:\\s*(\\d+)")))
    {
        try { if (unsigned long v = std::stoul(m[1].str()); v > 0)
            m_config.windowWidth = static_cast<uint32_t>(v); } catch(...) {}
    }
    if (std::regex_search(content, m, std::regex("\"window_height\"\\s*:\\s*(\\d+)")))
    {
        try { if (unsigned long v = std::stoul(m[1].str()); v > 0)
            m_config.windowHeight = static_cast<uint32_t>(v); } catch(...) {}
    }
    if (std::regex_search(content, m, std::regex("\"ui_config\"\\s*:\\s*\"([^\"]*)\"")))
        m_config.uiConfigPath = m[1].str();
    if (std::regex_search(content, m, std::regex("\"aspect_mode\"\\s*:\\s*\"([^\"]*)\"")))
        m_config.aspectMode = m[1].str();

    // 校验 aspect_mode 合法值，非法值回退到 stretch 并警告
    if (m_config.aspectMode != "stretch" && m_config.aspectMode != "letterbox")
    {
        std::cerr << "[Config] 警告: 未知 aspect_mode \"" << m_config.aspectMode
                  << "\"，已回退到默认 \"stretch\"" << std::endl;
        m_config.aspectMode = "stretch";
    }

    std::cout << "[Config] 已加载: " << path << std::endl;
    std::cout << "         标题: " << m_config.windowTitle << std::endl;
    std::cout << "         尺寸: " << m_config.windowWidth
              << "x" << m_config.windowHeight << std::endl;
    std::cout << "         比例模式: " << m_config.aspectMode << std::endl;
}

// ============================================================================
// 主循环
// ============================================================================

void VulkanApp::run()
{
    std::cout << "[主循环] ESC 退出；拖拽 UI 面板；点击按钮。" << std::endl;
    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();
        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
        renderFrame();
        if (m_ui) m_ui->update(0.016f);
    }
}

// ============================================================================
// GLFW 窗口创建
// ============================================================================

void VulkanApp::createWindow()
{
    if (!glfwInit()) throw std::runtime_error("GLFW 初始化失败");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(m_config.windowWidth, m_config.windowHeight,
        m_config.windowTitle.c_str(), nullptr, nullptr);
    if (!m_window) { glfwTerminate(); throw std::runtime_error("GLFW 窗口创建失败"); }
}

// ============================================================================
// Vulkan 实例
// ============================================================================

void VulkanApp::createVulkanInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = m_config.windowTitle.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Graphics Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCount);
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = 0;

    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance 失败");
    std::cout << "[Vulkan] 实例创建成功。" << std::endl;
}

void VulkanApp::setupDebugMessenger()
{
    // 此处不启用验证层，留作扩展点
}

// ============================================================================
// 引擎资源：管线
// ============================================================================

void VulkanApp::createEngineResources()
{
    std::string shaderDir = getShaderDir();
    VkExtent2D extent = m_ctx->extent();

    m_pipe2D     = std::make_unique<Pipeline2D>();
    m_pipe2D->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir,
                     VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    m_pipe2DFan  = std::make_unique<Pipeline2D>();
    m_pipe2DFan->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir,
                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN);

    m_pipe2DLine = std::make_unique<Pipeline2D>();
    m_pipe2DLine->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir,
                         VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

    m_pipe2DLineStrip = std::make_unique<Pipeline2D>();
    m_pipe2DLineStrip->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir,
                              VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);

    m_pipe3D = std::make_unique<Pipeline3D>();
    m_pipe3D->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir);

    m_pipeText = std::make_unique<PipelineText>();
    m_pipeText->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir);

    // 创建 3D 网格共享描述符池（供 Mesh3D::upload 使用）
    {
        VkDevice dev = m_ctx->device();
        VkDescriptorPoolSize dps{};
        dps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        dps.descriptorCount = 64; // 预留：最多 64 个 3D 模型
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 64;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &dps;
        if (vkCreateDescriptorPool(dev, &dpci, nullptr, &m_meshDescPool) != VK_SUCCESS)
            throw std::runtime_error("创建 3D 网格描述符池失败");
    }
}

// ============================================================================
// 用户自定义场景入口（示例代码已移除，在此填入你自己的场景）
//
// 典型工作流：
//   1) VkDevice dev = m_ctx->device();
//   2) VkPhysicalDevice pd = m_ctx->physicalDevice();
//   3) VkCommandPool pool = m_ctx->commandPool();
//   4) VkQueue q = m_ctx->graphicsQueue();
//
//   --- 2D ---
//   auto tri = std::make_unique<Triangle>(0, 0.5, -0.5, -0.5, 0.5, -0.5, 1, 0.3, 0.3);
//   tri->upload(dev, pd, pool, q);
//   m_shapes.push_back(std::move(tri));
//
//   --- 3D ---
//   auto cube = std::make_unique<Cube>(1.0, 0, 0, 0, 0.5, 0.8, 0.4);
//   cube->upload(dev, pd, pool, q, m_pipe3D->descriptorSetLayout(), m_meshDescPool);
//   cube->setModel(glm::translate(glm::mat4(1), {0, 0, 0}));
//   m_meshes.push_back(std::move(cube));
//
//   --- 文字 ---
//   m_titleText = std::make_unique<TextRenderer>(
//       "Hello", 24, 24, 24, m_config.windowWidth, m_config.windowHeight, 1, 0.9, 0.2);
//   m_titleText->upload(dev, pd, pool, q);
//
//   --- UI ---
//   在构造函数里 loadFromFile 之后通过 findByName + dynamic_cast 绑定回调。
// ============================================================================
void VulkanApp::createDemoScene()
{
    std::cout << "[VulkanApp] createDemoScene() —— 场景创建入口。" << std::endl;
    std::cout << "             （骨架模式：暂无图形，编辑 src/vulkan_app.cpp 内的 createDemoScene() 添加你自己的内容）"
              << std::endl;
}

// ============================================================================
// 帧渲染
// ============================================================================

void VulkanApp::renderFrame()
{
    uint32_t imageIndex = m_ctx->beginFrame();
    if (imageIndex == ~0u)
    {
        // 交换链重建，跳过本帧
        return;
    }
    VkCommandBuffer cmd = m_ctx->commandBuffer();

    computeRenderArea();

    draw2DShapes();
    draw3DMeshes();
    drawText();
    drawUI(cmd);

    m_ctx->endFrame();
}

void VulkanApp::computeRenderArea()
{
    VkExtent2D ext = m_ctx->extent();

    if (m_config.aspectMode == "letterbox")
    {
        // 保持 config 中初始宽高比，不足部分用清除色填充
        float targetAspect = static_cast<float>(m_config.windowWidth) /
                             static_cast<float>(m_config.windowHeight ? m_config.windowHeight : 1);
        float winAspect    = static_cast<float>(ext.width) /
                             static_cast<float>(ext.height ? ext.height : 1);

        if (winAspect > targetAspect)
        {
            // 窗口偏宽 → 左右留白
            m_renderExtent.width  = static_cast<uint32_t>(static_cast<float>(ext.height) * targetAspect);
            m_renderExtent.height = ext.height;
        }
        else
        {
            // 窗口偏高 → 上下留白
            m_renderExtent.width  = ext.width;
            m_renderExtent.height = static_cast<uint32_t>(static_cast<float>(ext.width) / targetAspect);
        }
        // 确保渲染区域至少 1x1，否则零尺寸视口会导致 GPU 崩溃
        m_renderExtent.width  = std::max(1u, std::min(m_renderExtent.width, ext.width));
        m_renderExtent.height = std::max(1u, std::min(m_renderExtent.height, ext.height));

        m_renderViewport.x = (static_cast<float>(ext.width)  - static_cast<float>(m_renderExtent.width )) * 0.5f;
        m_renderViewport.y = (static_cast<float>(ext.height) - static_cast<float>(m_renderExtent.height)) * 0.5f;
    }
    else
    {
        // stretch：视口 = 全屏
        m_renderExtent = ext;
        m_renderViewport.x = 0.0f;
        m_renderViewport.y = 0.0f;
    }
    m_renderViewport.width    = static_cast<float>(m_renderExtent.width);
    m_renderViewport.height   = static_cast<float>(m_renderExtent.height);
    m_renderViewport.minDepth = 0.0f;
    m_renderViewport.maxDepth = 1.0f;

    m_renderScissor.offset = { static_cast<int32_t>(m_renderViewport.x),
                                static_cast<int32_t>(m_renderViewport.y) };
    m_renderScissor.extent  = m_renderExtent;
}

void VulkanApp::draw2DShapes()
{
    // 按拓扑分组绘制，每组只 BindPipeline/Viewport/Scissor 一次
    // 显著减少命令缓冲中的冗余命令（尤其 shape 数量多时）
    VkCommandBuffer cmd = m_ctx->commandBuffer();
    VkPipelineLayout layout = m_pipe2D->pipelineLayout();

    enum Group { kFilled, kFan, kLines, kLineStrip };
    auto drawGroup = [&](Group g) {
        VkPipeline pipe = VK_NULL_HANDLE;
        switch (g) {
        case kFilled:    pipe = m_pipe2D->pipeline();         break;
        case kFan:       pipe = m_pipe2DFan->pipeline();      break;
        case kLines:     pipe = m_pipe2DLine->pipeline();     break;
        case kLineStrip: pipe = m_pipe2DLineStrip->pipeline();break;
        }
        // 每组仅设置一次公共状态
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdSetViewport(cmd, 0, 1, &m_renderViewport);
        vkCmdSetScissor(cmd, 0, 1, &m_renderScissor);

        for (auto& s : m_shapes)
        {
            bool match = false;
            switch (g) {
            case kFilled:    match = !s->isLineStripTopology() && !s->isLineTopology() && !s->isFanTopology(); break;
            case kFan:       match =  s->isFanTopology(); break;
            case kLines:     match =  s->isLineTopology(); break;
            case kLineStrip: match =  s->isLineStripTopology(); break;
            }
            if (match)
                s->drawVBOOnly(cmd, layout);
        }
    };

    drawGroup(kFilled);
    drawGroup(kFan);
    drawGroup(kLines);
    drawGroup(kLineStrip);
}

void VulkanApp::draw3DMeshes()
{
    // 无网格直接返回
    if (m_meshes.empty()) return;

    VkCommandBuffer cmd = m_ctx->commandBuffer();

    // 先设置一次公共状态：Pipeline / Viewport / Scissor
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipe3D->pipeline());
    vkCmdSetViewport(cmd, 0, 1, &m_renderViewport);
    vkCmdSetScissor(cmd, 0, 1, &m_renderScissor);

    // 默认 view/proj：相机 (0,0,3) 看向原点
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 3.0f),
                                 glm::vec3(0, 0, 0),
                                 glm::vec3(0, 1, 0));
    float aspect = static_cast<float>(m_renderExtent.width) /
                   static_cast<float>(m_renderExtent.height ? m_renderExtent.height : 1);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    // 翻转 Y（GLM 默认 OpenGL 坐标，Vulkan 需要 Y 翻转）
    proj[1][1] *= -1.0f;

    VkPipelineLayout layout = m_pipe3D->pipelineLayout();
    for (auto& m : m_meshes)
    {
        // 注意：若需要模型矩阵每帧变化（例如旋转、移动），在 draw3DMeshes 开头
        //       或 createDemoScene 外的自定义 update() 中 setModel()。
        //       这里只执行 draw：UBO 仅在 m_uboDirty 为 true 时更新。
        m->drawVBOOnly(cmd, layout, view, proj);
    }
}

void VulkanApp::drawText()
{
    VkCommandBuffer cmd = m_ctx->commandBuffer();
    if (!m_titleText) return;

    // 文本渲染使用 TRIANGLE_LIST 填充管线
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeText->pipeline());
    vkCmdSetViewport(cmd, 0, 1, &m_renderViewport);
    vkCmdSetScissor(cmd, 0, 1, &m_renderScissor);
    // TextRenderer::draw 内部仍会重复 bind pipeline/viewport/scissor，
    // 但 VkCmdBindPipeline 同值再绑开销很小（驱动通常优化），保留代码一致性
    m_titleText->draw(cmd, m_pipeText->pipeline(), m_pipeText->pipelineLayout(),
                      m_renderViewport, m_renderScissor);
}

void VulkanApp::drawUI(VkCommandBuffer cmd)
{
    if (!m_ui || !m_ui->root()) return;
    UiRenderContext ctx;
    ctx.device          = m_ctx->device();
    ctx.physicalDevice  = m_ctx->physicalDevice();
    ctx.commandPool     = m_ctx->commandPool();
    ctx.queue           = m_ctx->graphicsQueue();
    ctx.commandBuffer   = cmd;
    ctx.pipelineFilled  = m_pipe2D->pipeline();
    ctx.pipelineLine    = m_pipe2DLine->pipeline();
    ctx.pipelineLayout  = m_pipe2D->pipelineLayout();
    ctx.extent          = m_renderExtent;
    ctx.viewport        = m_renderViewport;
    ctx.scissor         = m_renderScissor;
    m_ui->draw(ctx);
}

// ============================================================================
// GLFW 回调
// ============================================================================

void VulkanApp::onFramebufferResize(GLFWwindow* w, int width, int height)
{
    VulkanApp* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (app && app->m_ctx) app->m_ctx->setResized();
}

void VulkanApp::onMouseMove(GLFWwindow* w, double x, double y)
{
    VulkanApp* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (!app || !app->m_ui) return;

    // 将窗口鼠标坐标转换为渲染区域坐标
    // letterbox 模式下渲染区域在窗口中居中，存在偏移
    double rx = x - static_cast<double>(app->m_renderViewport.x);
    double ry = y - static_cast<double>(app->m_renderViewport.y);

    app->m_ui->onMouseMove(rx, ry);
}

void VulkanApp::onMouseButton(GLFWwindow* w, int b, int a, int m)
{
    VulkanApp* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (app && app->m_ui) app->m_ui->onMouseButton(b, a, m);
}

void VulkanApp::onKey(GLFWwindow* w, int k, int s, int a, int m)
{
    VulkanApp* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (app && app->m_ui) app->m_ui->onKey(k, s, a, m);
}

void VulkanApp::onChar(GLFWwindow* w, unsigned int c)
{
    VulkanApp* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (app && app->m_ui) app->m_ui->onChar(c);
}

void VulkanApp::setupCallbacks()
{
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, onFramebufferResize);
    glfwSetCursorPosCallback(m_window, onMouseMove);
    glfwSetMouseButtonCallback(m_window, onMouseButton);
    glfwSetKeyCallback(m_window, onKey);
    glfwSetCharCallback(m_window, onChar);
}
