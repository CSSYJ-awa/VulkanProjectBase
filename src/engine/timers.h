/**
 * timers.h —— 定时器 / 延时任务系统（v1.0.2）
 *
 * 功能：
 *   - after(秒, 回调)    ：一次性延时回调
 *   - every(秒, 回调)    ：循环定时回调（返回 TimerId 可取消）
 *   - everyFrame(回调)   ：每帧回调（回调参数为帧 dt）
 *   - cancel / clear / count
 *
 * 用法：
 *   timers::system().after(2.0f, [](){ ... });            // 2 秒后执行
 *   auto id = timers::system().every(0.5f, [](){ ... });  // 每 0.5 秒回调
 *   timers::system().cancel(id);
 *
 * 时间驱动：VulkanApp 主循环调用 timers::system().update(m_time.dt)（受全局时间缩放影响）。
 * 线程安全：非线程安全，主循环单线程使用。
 */
#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace timers {

using TimerId = uint64_t;

class TimerSystem
{
public:
    TimerSystem() = default;
    ~TimerSystem() = default;

    TimerSystem(const TimerSystem&) = delete;
    TimerSystem& operator=(const TimerSystem&) = delete;

    // 一次性延时（seconds 秒后执行一次）
    TimerId after(float seconds, std::function<void()> cb)
    {
        return add(seconds, 0.0f, /*repeat=*/false, std::move(cb), nullptr);
    }

    // 循环定时（每 seconds 秒执行一次；seconds<=0 时每帧执行）
    TimerId every(float seconds, std::function<void()> cb)
    {
        return add(seconds, seconds, /*repeat=*/true, std::move(cb), nullptr);
    }

    // 每帧回调（不受间隔约束；回调参数为帧 dt）
    TimerId everyFrame(std::function<void(float)> cb)
    {
        return add(0.0f, 0.0f, /*repeat=*/true, nullptr, std::move(cb));
    }

    void cancel(TimerId id)
    {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
            if (it->id == id) { m_entries.erase(it); return; }
    }

    // 推进所有定时器（传入已缩放 dt；通常 = m_time.dt）
    void update(float dt)
    {
        if (dt <= 0.0f) return;
        for (size_t i = 0; i < m_entries.size();)
        {
            Entry& e = m_entries[i];
            e.remaining -= dt;
            if (e.interval <= 0.0f)
            {
                // 每帧回调
                if (e.frameCb) e.frameCb(dt);
                ++i;
                continue;
            }
            if (e.remaining <= 0.0f)
            {
                if (e.cb) e.cb();
                if (e.repeat)
                {
                    e.remaining += e.interval;   // 循环：重置剩余时间（保留盈余）
                    ++i;
                }
                else
                {
                    m_entries.erase(m_entries.begin() + static_cast<long>(i));  // 一次性：移除
                }
            }
            else { ++i; }
        }
    }

    void clear() { m_entries.clear(); m_nextId = 0; }
    size_t count() const { return m_entries.size(); }

private:
    struct Entry
    {
        TimerId  id = 0;
        float    remaining = 0.0f;
        float    interval  = 0.0f;
        bool     repeat = false;
        std::function<void()>        cb;
        std::function<void(float)>   frameCb;
    };

    TimerId add(float delay, float interval, bool repeat,
                std::function<void()> cb, std::function<void(float)> frameCb)
    {
        TimerId id = ++m_nextId;
        m_entries.push_back({ id, delay, interval, repeat, std::move(cb), std::move(frameCb) });
        return id;
    }

    std::vector<Entry> m_entries;
    TimerId m_nextId = 0;
};

// 全局定时器单例
inline TimerSystem& system()
{
    static TimerSystem instance;
    return instance;
}

} // namespace timers
