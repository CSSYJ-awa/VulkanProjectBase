/**
 * Logger —— 轻量日志系统（单头 + 单实现，无第三方依赖）
 *
 * 输出格式：
 *   [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [module:section] message
 *   例：[2026-08-21 22:05:12.384] [INFO] [VulkanApp:createDemoScene] 场景创建入口
 *
 * 日志等级：INFO / WARNING / ERROR（用户要求的三级别）
 * 字段：
 *   1) 时间戳 —— 毫秒精度（系统本地时区）
 *   2) 日志等级 —— INFO / WARN / ERROR
 *   3) 写入环节/程序（Module + Section）—— Module 按大模块分
 *      （Config / Vulkan / Pipeline2D / Pipeline3D / Shape / Mesh3D /
 *       Text / UI / App / Engine ...）；Section 为具体函数/事件名
 *   4) 日志内容 —— 可使用 sprintf 风格格式化
 *
 * 其他特性：
 *   · 自动创建 logs/ 目录（可执行文件同级或工作目录），目录不存在
 *     时回退到 %TEMP%，保证日志在最差环境下也能落盘
 *   · 文件名：YYYYMMDD_HHMMSS_{exeName}.log（按启动生成独立日志）
 *   · 线程安全：写文件前加 std::mutex（Vulkan 回调/多线程使用场景）
 *   · 同时输出到控制台（stdout / stderr 按级别分流）和文件
 *   · std::atexit + 静态单例析构双重保障 flush；SetUnhandledExceptionFilter
 *     捕获崩溃，记录 fatal error 并关闭日志
 */
#pragma once

#include <string>
#include <cstdarg>
#include <mutex>
#include <cstdio>

enum class LogLevel
{
    Info    = 0,
    Warning = 1,
    Error   = 2,
};

// ============================================================================
// Logger 主类（单例）
// ============================================================================
class Logger
{
public:
    // 获取单例（懒加载，首次调用会打开日志文件）
    static Logger& instance();

    // 设置 exe 名（在进入 main 后、其他日志前调用，可选：不设默认 "app"）
    void setExeName(const std::string& name);

    // 手动 flush + close（在 VulkanApp 析构里调用，确保最后一条日志写出）
    void shutdown();

    // 通用格式化写入（内部加锁）
    // module   = 模块/大分类（Config / Vulkan / Pipeline2D / ...）
    // section  = 环节/具体函数（loadConfig / createSwapChain / ...）
    // fmt      = 格式化字符串（printf 风格）
    void log(LogLevel lv, const char* module, const char* section,
             const char* fmt, ...);

    // va_list 版本，供宏或其他封装转发
    void logV(LogLevel lv, const char* module, const char* section,
              const char* fmt, va_list ap);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // 懒初始化：第一次写日志时打开文件
    void ensureInitOnce();

    // 把一行写到文件 + 控制台（不加锁，外部已持有 m_mutex）
    void writeLineLocked(LogLevel lv, const char* module, const char* section,
                         const char* msg);

    // —— 成员 ——
    std::mutex       m_mutex;
    FILE*            m_fp              = nullptr;
    std::string      m_logFilePath;
    std::string      m_exeName         = "app";
    bool             m_initialized     = false;
    bool             m_shutdown        = false;
};

// ============================================================================
// 宏（推荐用法，比直接调用 Logger::instance().log() 少打很多字）
// 使用示例：
//   LOG_INFO("VulkanApp", "createDemoScene", "创建了 %zu 个 2D 图形", shapes.size());
//   LOG_WARN("Config", "loadConfig", "未知 aspect_mode=%s，回退默认", mode.c_str());
//   LOG_ERROR("Vulkan", "recreateSwapChain", "vkAcquireNextImageKHR 返回 %d", result);
// ============================================================================

#ifndef LOG_INFO
#define LOG_INFO(module, section, ...)  \
    do { ::Logger::instance().log(LogLevel::Info,    module, section, __VA_ARGS__); } while(0)
#endif

#ifndef LOG_WARN
#define LOG_WARN(module, section, ...)  \
    do { ::Logger::instance().log(LogLevel::Warning, module, section, __VA_ARGS__); } while(0)
#endif

#ifndef LOG_ERROR
#define LOG_ERROR(module, section, ...) \
    do { ::Logger::instance().log(LogLevel::Error,   module, section, __VA_ARGS__); } while(0)
#endif

// 初始化/关闭辅助宏（手动）
#define LOG_INIT(exeName)    ::Logger::instance().setExeName(exeName)
#define LOG_SHUTDOWN()       ::Logger::instance().shutdown()
