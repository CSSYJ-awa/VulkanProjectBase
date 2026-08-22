/**
 * event_system.h —— 完整事件系统（v1.0.2）
 *
 * 功能：
 *   - 类型安全发布/订阅（模板，回调签名 void(const E&)）
 *   - 订阅优先级（priority 越小越先执行，稳定排序）
 *   - 一次性监听（subscribeOnce，触发后自动退订）
 *   - 延迟派发队列（enqueue → dispatch，支持跨帧/跨系统排程）
 *   - 全局单例（events::system()）+ 任意独立实例（ECS 注入使用）
 *
 * 用法：
 *   events::system().subscribe<MyEvent>([](const MyEvent& e){ ... }, 10);  // 优先级 10
 *   events::system().enqueue<MyEvent>({ 42 });   // 排队
 *   events::system().dispatch();                 // 主循环末尾统一派发
 *
 * 线程安全：非线程安全。主循环单线程使用。
 */
#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace events {

// 订阅句柄（用于退订）。0 表示无效
using HandlerId = uint64_t;

// ============================================================================
// EventBus —— 完整事件总线（头文件全模板实现，零 cpp）
// ============================================================================
class EventBus
{
public:
    EventBus() = default;
    ~EventBus() = default;

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // ----------------------------------------------------------------------
    // 订阅事件 E：回调签名 void(const E&)
    // priority 越小越先执行（相同优先级保持注册顺序）
    // ----------------------------------------------------------------------
    template <typename E, typename Callback>
    HandlerId subscribe(Callback&& cb, int priority = 0)
    {
        return addHandler<E>(std::forward<Callback>(cb), priority, /*once=*/false);
    }

    // 一次性订阅：事件触发一次后自动退订
    template <typename E, typename Callback>
    HandlerId subscribeOnce(Callback&& cb, int priority = 0)
    {
        return addHandler<E>(std::forward<Callback>(cb), priority, /*once=*/true);
    }

    // 退订：按 id 移除（不影响其他订阅者）
    void unsubscribe(HandlerId id)
    {
        if (id == 0) return;
        for (auto& [type, vec] : m_handlers)
        {
            for (auto it = vec.begin(); it != vec.end(); ++it)
            {
                if (it->id == id) { vec.erase(it); return; }
            }
        }
    }

    // ----------------------------------------------------------------------
    // 同步发布：立即调用所有订阅者（按优先级）
    // ----------------------------------------------------------------------
    template <typename E>
    void publish(const E& event)
    {
        auto it = m_handlers.find(typeid(E));
        if (it == m_handlers.end()) return;
        // 拷贝一份，防 publish 中途退订导致迭代器失效
        auto vec = it->second;
        // 稳定排序：priority 升序，同优先级保持注册顺序
        std::stable_sort(vec.begin(), vec.end(),
                         [](const Entry& a, const Entry& b) { return a.priority < b.priority; });
        for (auto& h : vec)
        {
            if (!h.fn) continue;
            h.fn(static_cast<const void*>(&event));
            if (h.once) unsubscribe(h.id);   // 一次性监听触发后自动退订
        }
    }

    // 便捷：构造并同步发布
    template <typename E, typename... Args>
    void emit(Args&&... args)
    {
        publish<E>(E{ std::forward<Args>(args)... });
    }

    // ----------------------------------------------------------------------
    // 延迟派发：enqueue 排队（闭包捕获类型），dispatch 统一执行
    // 适合跨系统/跨帧排程（如碰撞 Enter 在物理步之后统一派发）
    // ----------------------------------------------------------------------
    template <typename E>
    void enqueue(const E& event)
    {
        m_queue.emplace_back([e = event]() { publish(e); });
    }

    template <typename E, typename... Args>
    void enqueue(Args&&... args)
    {
        enqueue(E{ std::forward<Args>(args)... });
    }

    // 执行全部排队事件（通常主循环帧末调用）
    void dispatch()
    {
        auto q = std::move(m_queue);
        m_queue.clear();
        for (auto& fn : q) fn();
    }

    size_t queued() const { return m_queue.size(); }

    // 清空所有订阅与排队事件（场景切换时调用）
    void clear()
    {
        m_handlers.clear();
        m_queue.clear();
        m_nextId = 0;
    }

    // 当前订阅总数（调试用）
    size_t subscriberCount() const
    {
        size_t n = 0;
        for (const auto& [t, vec] : m_handlers) n += vec.size();
        return n;
    }

private:
    struct Entry
    {
        HandlerId id = 0;
        int       priority = 0;
        bool      once = false;
        std::function<void(const void*)> fn;
    };

    template <typename E, typename Callback>
    HandlerId addHandler(Callback&& cb, int priority, bool once)
    {
        HandlerId id = ++m_nextId;
        auto& vec = m_handlers[typeid(E)];
        auto fn = [f = std::forward<Callback>(cb)](const void* p) mutable {
            f(*static_cast<const E*>(p));
        };
        vec.push_back({ id, priority, once, std::move(fn) });
        return id;
    }

    std::unordered_map<std::type_index, std::vector<Entry>> m_handlers;
    std::vector<std::function<void()>> m_queue;
    HandlerId m_nextId = 0;
};

// ----------------------------------------------------------------------------
// 全局事件总线单例：引擎级事件（窗口事件 / 场景事件 / 用户自定义事件）
// ----------------------------------------------------------------------------
inline EventBus& system()
{
    static EventBus instance;
    return instance;
}

} // namespace events
