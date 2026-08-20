/**
 * VulkanApp 实现
 */
#include "vulkan_app.h"

#include <iostream>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <sstream>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// 辅助：定位 config.json 路径
// 优先级：
//   1. 当前工作目录下的 config.json（开发模式，cwd 通常是项目根目录，
//      能立即反映用户对 config.json 的修改，无需重新构建）
//   2. exe 同目录的 config.json（部署模式，最终用户机器上无源码时使用）
// ============================================================================
static std::string getConfigPath()
{
    // 优先尝试 cwd 下的 config.json
    {
        std::ifstream test("config.json");
        if (test.is_open())
        {
            test.close();
            return "config.json";
        }
    }
#ifdef _WIN32
    // 回退到 exe 同目录
    char exePath[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0)
    {
        std::string p(exePath);
        size_t pos = p.find_last_of("\\/");
        if (pos != std::string::npos)
        {
            return p.substr(0, pos) + "\\config.json";
        }
    }
#endif
    // 最终回退（即使不存在，loadConfig 会处理失败情况）
    return "config.json";
}

// ============================================================================
// 构造 / 析构
// ============================================================================

VulkanApp::VulkanApp()
{
    loadConfig();
    createWindow();
    createVulkanInstance();
    std::cout << "[VulkanApp] 初始化完成。" << std::endl;
}

VulkanApp::~VulkanApp()
{
    // 先销毁 Vulkan 实例，再销毁窗口
    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        std::cout << "[Vulkan] 实例已销毁。" << std::endl;
    }

    if (m_window)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        std::cout << "[GLFW] 窗口已销毁。" << std::endl;
    }

    glfwTerminate();
    std::cout << "[GLFW] 已终止。" << std::endl;
}

// ============================================================================
// 配置加载（运行时从 config.json 读取，覆盖默认值）
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

    // window_title (UTF-8 字符串，GLFW 3.4+ 原生支持)
    if (std::regex_search(content, m,
            std::regex("\"window_title\"\\s*:\\s*\"([^\"]*)\"")))
    {
        m_config.windowTitle = m[1].str();
    }

    // window_width (正整数)
    if (std::regex_search(content, m,
            std::regex("\"window_width\"\\s*:\\s*(\\d+)")))
    {
        try
        {
            unsigned long v = std::stoul(m[1].str());
            if (v > 0) m_config.windowWidth = static_cast<uint32_t>(v);
        }
        catch (...) { /* 解析失败保持默认值 */ }
    }

    // window_height (正整数)
    if (std::regex_search(content, m,
            std::regex("\"window_height\"\\s*:\\s*(\\d+)")))
    {
        try
        {
            unsigned long v = std::stoul(m[1].str());
            if (v > 0) m_config.windowHeight = static_cast<uint32_t>(v);
        }
        catch (...) { /* 解析失败保持默认值 */ }
    }

    std::cout << "[Config] 已加载: " << path << std::endl;
    std::cout << "         标题: " << m_config.windowTitle << std::endl;
    std::cout << "         尺寸: " << m_config.windowWidth
              << "x" << m_config.windowHeight << std::endl;
}

// ============================================================================
// 主循环
// ============================================================================

void VulkanApp::run()
{
    std::cout << "[主循环] 按 ESC 退出..." << std::endl;

    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();

        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
    }
}

// ============================================================================
// GLFW 窗口创建
// ============================================================================

void VulkanApp::createWindow()
{
    std::cout << "[GLFW] 初始化 GLFW..." << std::endl;

    if (!glfwInit())
    {
        throw std::runtime_error("GLFW 初始化失败");
    }

    // 告知 GLFW 不要创建 OpenGL 上下文
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_window = glfwCreateWindow(
        m_config.windowWidth,
        m_config.windowHeight,
        m_config.windowTitle.c_str(),
        nullptr, nullptr);

    if (!m_window)
    {
        glfwTerminate();
        throw std::runtime_error("GLFW 窗口创建失败");
    }

    std::cout << "[GLFW] 窗口创建成功 ("
              << m_config.windowWidth << "x"
              << m_config.windowHeight << ")。" << std::endl;
}

// ============================================================================
// Vulkan 实例创建
// ============================================================================

void VulkanApp::createVulkanInstance()
{
    // --- 应用程序信息 ---
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = m_config.windowTitle.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "No Engine";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    // --- 实例创建信息 ---
    VkInstanceCreateInfo createInfo{};
    createInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // --- 获取 GLFW 所需的 Vulkan 扩展 ---
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> enabledExtensions(
        glfwExtensions, glfwExtensions + glfwExtensionCount);

    std::cout << "[Vulkan] GLFW 要求 " << glfwExtensionCount << " 个实例扩展：" << std::endl;
    for (uint32_t i = 0; i < glfwExtensionCount; ++i)
    {
        std::cout << "         " << glfwExtensions[i] << std::endl;
    }

    createInfo.enabledExtensionCount   = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    // 不使用验证层（开发时可启用）
    createInfo.enabledLayerCount    = 0;
    createInfo.ppEnabledLayerNames  = nullptr;

    // --- 创建实例 ---
    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(
            "vkCreateInstance 失败，错误码: " +
            std::to_string(static_cast<int>(result)));
    }

    std::cout << "[Vulkan] 实例创建成功。" << std::endl;
}
