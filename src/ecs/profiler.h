/**
 * profiler.h —— ECS 性能分析器
 *
 * 用途：测量每个 System 的 onUpdate 耗时，统计帧总耗时，并提供查询接口
 *       供 DebugSystem / UI 显示。
 *
 * 设计：
 *   - Profiler 持有一个 ScopedTimer 工具类：构造时记录起点，析构时累加到
 *     指定 slot。配合 RAII，使用方式极简：
 *       PROFILE_SCOPE(profiler, "InputSystem");
 *   - 每个 slot 累计：sumMs / callCount / maxMs / minMs
 *   - frameBegin/frameEnd 包围一帧；getFrameMs 返回上一帧总耗时
 *   - 采样窗口：默认每 0.5 秒做一次汇总，对外暴露最近窗口平均数据
 *   - 非线程安全：单线程主循环使用
 *
 * 集成方式（VulkanApp::run）：
 *   m_profiler.frameBegin();
 *   m_ecs->updateSystems(dt);   // 内部每个系统用 PROFILE_SCOPE 计时
 *   m_profiler.frameEnd();
 *
 * 系统侧使用：
 *   void InputSystem::onUpdate(Coordinator& c, float dt) override {
 *       PROFILE_SCOPE(m_profiler, name());
 *       // ...
 *   }
 *
 * 系统访问 Profiler：通过 System 基类持有的 Profiler* 指针，由
 *   SystemManager::setProfiler(profiler) 注入。
 */
#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace ecs {

// ============================================================================
// Profiler —— 单线程帧/系统性能采样
// ============================================================================
class Profiler
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    struct Slot
    {
        std::string name;
        double sumMs   = 0.0;     // 窗口内累计毫秒
        uint64_t calls = 0;       // 窗口内调用次数
        double maxMs   = 0.0;     // 窗口内最大单次耗时
        double minMs   = 1e18;    // 窗口内最小单次耗时
        double lastMs  = 0.0;     // 最近一次耗时

        // 上次窗口快照（汇总时拷贝）
        double avgMs   = 0.0;
        double snapMax = 0.0;
        double snapMin = 0.0;
        uint64_t snapCalls = 0;
    };

    Profiler() = default;

    // 帧开始/结束（包围 m_ecs->updateSystems 调用）
    void frameBegin()
    {
        m_frameStart = Clock::now();
    }

    void frameEnd()
    {
        auto end = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - m_frameStart).count();
        m_frameMs = ms;
        m_frameSumMs += ms;
        m_frameCount++;

        // 窗口刷新：每 windowSeconds 秒做一次快照
        m_windowTimer += ms * 0.001;
        if (m_windowTimer >= m_windowSeconds)
        {
            // 拷贝当前累计值到快照，并清零累计
            for (auto& [n, s] : m_slots)
            {
                s.avgMs     = s.calls > 0 ? (s.sumMs / static_cast<double>(s.calls)) : 0.0;
                s.snapMax   = s.maxMs;
                s.snapMin   = s.minMs;
                s.snapCalls = s.calls;
                s.sumMs     = 0.0;
                s.calls     = 0;
                s.maxMs     = 0.0;
                s.minMs     = 1e18;
            }
            m_avgFrameMs = m_frameCount > 0 ? (m_frameSumMs / m_frameCount) : 0.0;
            m_snapFrameCount = m_frameCount;
            m_frameSumMs = 0.0;
            m_frameCount = 0;
            m_windowTimer = 0.0;
        }
    }

    // 由 ScopedTimer 调用：累加一次系统耗时
    void record(const char* name, double ms)
    {
        auto& s = m_slots[std::string(name)];
        if (s.name.empty()) s.name = name;
        s.sumMs += ms;
        s.calls++;
        if (ms > s.maxMs) s.maxMs = ms;
        if (ms < s.minMs) s.minMs = ms;
        s.lastMs = ms;
    }

    // 取所有 slot 的快照（用于 Debug 显示）
    struct SlotSnapshot
    {
        std::string name;
        double avgMs;
        double maxMs;
        double minMs;
        uint64_t calls;
        double lastMs;
    };

    std::vector<SlotSnapshot> snapshot() const
    {
        std::vector<SlotSnapshot> v;
        v.reserve(m_slots.size());
        for (const auto& [n, s] : m_slots)
        {
            v.push_back({ s.name,
                          s.calls > 0 ? (s.sumMs / static_cast<double>(s.calls)) : s.avgMs,
                          s.maxMs > 0 ? s.maxMs : s.snapMax,
                          s.minMs < 1e18 ? s.minMs : s.snapMin,
                          s.calls > 0 ? s.calls : s.snapCalls,
                          s.lastMs });
        }
        // 按 avgMs 降序
        std::sort(v.begin(), v.end(), [](const SlotSnapshot& a, const SlotSnapshot& b) {
            return a.avgMs > b.avgMs;
        });
        return v;
    }

    // 上一帧总耗时（毫秒）
    double getFrameMs() const { return m_frameMs; }
    // 窗口平均帧耗时（毫秒）
    double getAvgFrameMs() const { return m_avgFrameMs; }
    // 窗口内总帧数
    uint64_t getSnapFrameCount() const { return m_snapFrameCount; }

    // 设置采样窗口（秒）。默认 0.5s
    void setWindowSeconds(double s) { m_windowSeconds = s; }

    // 清空所有 slot（场景切换时调用）
    void clear()
    {
        m_slots.clear();
        m_frameMs = m_avgFrameMs = 0.0;
        m_frameSumMs = 0.0;
        m_frameCount = m_snapFrameCount = 0;
        m_windowTimer = 0.0;
    }

private:
    TimePoint m_frameStart;
    double    m_frameMs       = 0.0;
    double    m_frameSumMs    = 0.0;
    uint64_t  m_frameCount    = 0;
    double    m_avgFrameMs    = 0.0;
    uint64_t  m_snapFrameCount = 0;
    double    m_windowSeconds = 0.5;
    double    m_windowTimer   = 0.0;
    std::unordered_map<std::string, Slot> m_slots;
};

// ============================================================================
// ScopedTimer —— RAII 计时器，析构时把耗时写入 Profiler
// 用法：{ PROFILE_SCOPE(profiler, "MyTask"); /* code */ }
// ============================================================================
class ScopedTimer
{
public:
    ScopedTimer(Profiler* p, const char* name)
        : m_profiler(p), m_name(name), m_start(Profiler::Clock::now()) {}

    ~ScopedTimer()
    {
        if (!m_profiler) return;
        auto end = Profiler::Clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - m_start).count();
        m_profiler->record(m_name, ms);
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    Profiler*                  m_profiler;
    const char*                m_name;
    Profiler::TimePoint        m_start;
};

// 便捷宏：自动生成唯一变量名
#define PROFILER_CONCAT_INNER(a, b) a##b
#define PROFILER_CONCAT(a, b)       PROFILER_CONCAT_INNER(a, b)
#define PROFILE_SCOPE(profiler, name) \
    ::ecs::ScopedTimer PROFILER_CONCAT(scopedTimer_, __LINE__)(profiler, name)

} // namespace ecs
