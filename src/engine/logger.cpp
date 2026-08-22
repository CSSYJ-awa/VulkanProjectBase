/**
 * Logger 实现
 */
#include "logger.h"

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <vector>
#include <cstdio>
#include <string>

#ifdef _WIN32
    #include <windows.h>
    // Windows 10+ 1903 可用 CreateDirectory2；这里用 CreateDirectoryA +
    // 递归方式：依次创建父目录
    static bool createDirRecursive(const std::string& path)
    {
        if (path.empty()) return true;
        std::string p = path;
        // 统一反斜杠为正斜杠
        for (auto& c : p) if (c == '\\') c = '/';

        // 已经存在？
        DWORD attr = GetFileAttributesA(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES &&
            (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return true;

        // 尝试创建父目录
        size_t pos = p.find_last_of('/');
        if (pos != std::string::npos && pos > 0)
        {
            if (!createDirRecursive(p.substr(0, pos))) return false;
        }
        return ::CreateDirectoryA(p.c_str(), nullptr) == TRUE
                || ::GetLastError() == ERROR_ALREADY_EXISTS;
    }
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    static bool createDirRecursive(const std::string& path)
    {
        if (path.empty()) return true;
        struct stat st{};
        if (::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            return true;
        size_t pos = path.find_last_of('/');
        if (pos != std::string::npos && pos > 0)
            if (!createDirRecursive(path.substr(0, pos))) return false;
        return ::mkdir(path.c_str(), 0755) == 0;
    }
#endif

#ifdef _WIN32
    #include <errhandlingapi.h>
    // Windows 崩溃捕获：写入日志
    static LONG WINAPI loggerUnhandledFilter(PEXCEPTION_POINTERS ep);
    static bool g_crashFilterInstalled = false;
#endif

// ============================================================================
// 单例
// ============================================================================
Logger& Logger::instance()
{
    static Logger s_inst;
    return s_inst;
}

Logger::Logger() = default;

Logger::~Logger()
{
    shutdown();
}

void Logger::setExeName(const std::string& name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return;     // 已经打开文件，exe 名不能再改
    if (!name.empty()) m_exeName = name;
}

void Logger::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || m_shutdown) return;
    m_shutdown = true;
    if (m_fp)
    {
        // 结束标记
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm ltm{};
#ifdef _WIN32
        localtime_s(&ltm, &t);
#else
        localtime_r(&t, &ltm);
#endif
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count() % 1000;
        char ts[32] = {0};
        std::snprintf(ts, sizeof(ts),
                      "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                      ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
                      ltm.tm_hour, ltm.tm_min, ltm.tm_sec, (int)ms);

        std::fprintf(m_fp, "[%s] [INFO] [Logger:shutdown] 日志系统正常关闭\n", ts);
        std::fflush(m_fp);
        std::fclose(m_fp);
        m_fp = nullptr;
    }
    m_initialized = false;
}

// ============================================================================
// 日志写入入口（格式化 + 加锁分发）
// ============================================================================
void Logger::log(LogLevel lv, const char* module, const char* section,
                 const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logV(lv, module, section, fmt, ap);
    va_end(ap);
}

void Logger::logV(LogLevel lv, const char* module, const char* section,
                  const char* fmt, va_list ap)
{
    // 1) 先格式化消息到局部缓冲（不加锁）——避免锁内花时间
    char buf[4096];
    std::vsnprintf(buf, sizeof(buf), fmt ? fmt : "", ap);

    // 2) 加锁
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_shutdown) return;       // 已关闭，静默丢弃
    ensureInitOnce();
    writeLineLocked(lv, module ? module : "?", section ? section : "?", buf);
}

// ============================================================================
// 初始化：首次写日志时创建 logs/ 目录 + 打开文件
// ============================================================================
void Logger::ensureInitOnce()
{
    if (m_initialized) return;

    // 1) 决定日志根目录（依次尝试，保证最差能落盘）
    std::vector<std::string> candidates;

#ifdef _WIN32
    // 候选 1：exe 同级 / logs  （优先，便于用户查找）
    char exePath[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0)
    {
        std::string p(exePath);
        size_t pos = p.find_last_of("\\/");
        if (pos != std::string::npos)
            candidates.push_back(p.substr(0, pos) + "/logs");
    }
    // 候选 2：当前工作目录 / logs
    candidates.push_back("logs");
    // 候选 3：%TEMP%/logs （兜底：系统临时目录一定可写）
    char tmp[MAX_PATH] = {0};
    if (GetTempPathA(MAX_PATH, tmp) > 0)
        candidates.push_back(std::string(tmp) + "/VulkanProjectBase_logs");
#else
    candidates.push_back("./logs");
    const char* tmp = std::getenv("TMPDIR");
    if (tmp) candidates.push_back(std::string(tmp) + "/VulkanProjectBase_logs");
#endif

    std::string logDir;
    for (const auto& c : candidates)
    {
        if (createDirRecursive(c))
        {
            // 目录存在且可写测试：用 tmpfile 不方便，直接尝试打开占位文件
            std::string test = c + "/.write_test";
            FILE* ft = std::fopen(test.c_str(), "wb");
            if (ft) { std::fclose(ft); std::remove(test.c_str()); logDir = c; break; }
        }
    }
    if (logDir.empty()) logDir = ".";   // 最后的兜底：当前目录（不可写时会失败，下面再判断）

    // 2) 构造文件名：YYYYMMDD_HHMMSS_{exeName}.log
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm ltm{};
#ifdef _WIN32
    localtime_s(&ltm, &t);
#else
    localtime_r(&t, &ltm);
#endif
    char prefix[32] = {0};
    std::snprintf(prefix, sizeof(prefix),
                  "%04d%02d%02d_%02d%02d%02d",
                  ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
                  ltm.tm_hour, ltm.tm_min, ltm.tm_sec);

    // 清理 exeName 中的非法文件名字符
    std::string safeName = m_exeName;
    for (auto& ch : safeName)
    {
        if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' ||
            ch == '?'  || ch == '"' || ch == '<' || ch == '>' || ch == '|')
            ch = '_';
    }
    m_logFilePath = logDir + "/" + std::string(prefix) + "_" + safeName + ".log";

    m_fp = std::fopen(m_logFilePath.c_str(), "wb");
    if (!m_fp)
    {
        // 连兜底目录都失败？—— 退到 stderr 单行错误，不抛异常
        std::fprintf(stderr, "[Logger] 无法打开日志文件: %s\n", m_logFilePath.c_str());
        return;     // 降级：后续仅控制台输出
    }

    // 3) 写入首行：系统信息 + 日志路径（让用户确认兜底是否触发）
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    char ts[32] = {0};
    std::snprintf(ts, sizeof(ts),
                  "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
                  ltm.tm_hour, ltm.tm_min, ltm.tm_sec, (int)ms);

    std::fprintf(m_fp,
        "[%s] [INFO] [Logger:init] 日志系统启动\n",
        ts);
    std::fprintf(m_fp,
        "[%s] [INFO] [Logger:init] 日志文件: %s\n",
        ts, m_logFilePath.c_str());
    std::fprintf(m_fp,
        "[%s] [INFO] [Logger:init] 日志格式: [时间戳] [级别] [模块:环节] 内容\n",
        ts);

#ifdef _WIN32
    // 4) 崩溃捕获（只装一次）
    if (!g_crashFilterInstalled)
    {
        ::SetUnhandledExceptionFilter(loggerUnhandledFilter);
        g_crashFilterInstalled = true;
        std::fprintf(m_fp,
            "[%s] [INFO] [Logger:init] Windows SEH 崩溃捕获已安装\n", ts);
    }
#endif
    std::fflush(m_fp);
    m_initialized = true;
}

// ============================================================================
// 实际写一行（内部已锁）
// ============================================================================
void Logger::writeLineLocked(LogLevel lv, const char* module,
                             const char* section, const char* msg)
{
    // 时间戳
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm ltm{};
#ifdef _WIN32
    localtime_s(&ltm, &t);
#else
    localtime_r(&t, &ltm);
#endif
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;

    char ts[32] = {0};
    std::snprintf(ts, sizeof(ts),
                  "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
                  ltm.tm_hour, ltm.tm_min, ltm.tm_sec, (int)ms);

    const char* lvStr = "INFO";
    switch (lv)
    {
    case LogLevel::Info:    lvStr = "INFO"; break;
    case LogLevel::Warning: lvStr = "WARN"; break;
    case LogLevel::Error:   lvStr = "ERROR"; break;
    }

    // 1) 文件
    if (m_fp)
    {
        std::fprintf(m_fp, "[%s] [%s] [%s:%s] %s\n",
                     ts, lvStr, module, section, msg ? msg : "");
        std::fflush(m_fp);
    }

    // 2) 控制台：ERROR → stderr；INFO/WARN → stdout
    FILE* consoleOut = (lv == LogLevel::Error) ? stderr : stdout;
    std::fprintf(consoleOut, "[%s] [%s] [%s:%s] %s\n",
                 ts, lvStr, module, section, msg ? msg : "");
    std::fflush(consoleOut);
}

// ============================================================================
// Windows SEH 崩溃捕获
// ============================================================================
#ifdef _WIN32
static LONG WINAPI loggerUnhandledFilter(PEXCEPTION_POINTERS ep)
{
    // 注意：异常环境下堆/栈可能已损坏，避免大内存分配。
    // 这里只走 Logger 公 API（log / shutdown），不触碰 private 成员。

    DWORD code = ep ? ep->ExceptionRecord->ExceptionCode : 0;
    const char* desc = "UNKNOWN";
    switch (code)
    {
    case 0xC0000005: desc = "ACCESS_VIOLATION";        break;
    case 0xC0000094: desc = "INTEGER_DIVIDE_BY_ZERO"; break;
    case 0xC0000095: desc = "INTEGER_OVERFLOW";       break;
    case 0xC00000FD: desc = "STACK_OVERFLOW";         break;
    case 0xE06D7363: desc = "CPP_EXCEPTION_MSC";      break;
    default: break;
    }

    // 以 char[96] 拼异常描述（避免堆分配）
    char buf[128] = {0};
    std::snprintf(buf, sizeof(buf),
        "捕获未处理 SEH 异常 code=0x%08X (%s) 即将终止进程",
        (unsigned)code, desc);

    // 通过公 API 写日志（内部加锁 + 触发懒初始化）
    Logger::instance().log(LogLevel::Error, "Logger", "SEH", "%s", buf);

    // 控制台兜底输出
    std::fprintf(stderr, "[Logger:SEH] %s\n", buf);
    std::fflush(stderr);

    // 立即关闭日志文件（确保 crash 日志落盘）
    Logger::instance().shutdown();

    // 让系统继续默认处理：弹错/生成转储
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
