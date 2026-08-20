/**
 * VulkanApp —— Vulkan + GLFW 应用封装
 *
 * 封装了 Vulkan 实例、GLFW 窗口和主循环，
 * 提供简洁的接口供 main.cpp 调用。
 *
 * 窗口标题、大小、exe 文件名均通过 config.json 配置：
 *   - exe_name        由 CMake 在配置阶段读取，决定可执行文件名
 *   - window_title    由本程序在运行时读取 config.json 获得
 *   - window_width / window_height  同上
 */
#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

// ============================================================================
// 应用配置（运行时从 config.json 加载，加载失败时使用默认值）
// ============================================================================
struct AppConfig
{
    std::string windowTitle  = "VulkanApp (MinGW + GLFW + GLM)";
    uint32_t    windowWidth  = 800;
    uint32_t    windowHeight = 600;
};

// ============================================================================
// VulkanApp —— 主应用类
// ============================================================================
class VulkanApp final
{
public:
    VulkanApp();
    ~VulkanApp();

    // 禁止拷贝
    VulkanApp(const VulkanApp&) = delete;
    VulkanApp& operator=(const VulkanApp&) = delete;

    // 运行主循环（阻塞，直到窗口关闭）
    void run();

    // 获取内部对象（供后续扩展使用）
    VkInstance   vulkanInstance() const { return m_instance; }
    GLFWwindow*  window()         const { return m_window; }
    const AppConfig& config()     const { return m_config; }

private:
    // ---- 配置加载 ----
    void loadConfig();

    // ---- Vulkan 相关 ----
    void createVulkanInstance();

    // ---- GLFW 相关 ----
    void createWindow();

    // ---- 成员变量 ----
    AppConfig   m_config;
    VkInstance  m_instance = VK_NULL_HANDLE;
    GLFWwindow* m_window   = nullptr;
};
