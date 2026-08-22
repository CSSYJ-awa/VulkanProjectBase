/**
 * system.h —— System 基类与 SystemManager
 *
 * System = 一段对一组具备特定组件的实体执行相同操作的逻辑。
 *
 * 设计：
 *   - System 基类只定义 onUpdate(dt) 接口（逻辑系统，如 Movement/Lifetime）
 *   - 渲染系统通常需要 VulkanContext，故提供 onUpdate(dt) 默认空实现，
 *     额外的渲染入口由派生类自定义（如 RenderSystem3D::render(ctx, ...)）
 *   - SystemManager 持有所有 System 实例，按优先级顺序调用 update
 *
 * 与传统 System 不同（如 entityx 的 System::update(EntityManager& ...)）：
 *   EnTT 不强制 System 接口，而是用 entt::view 在普通函数里查询。
 *   本项目仍保留 System 基类用于：统一管理生命周期、便于按依赖顺序调用、
 *   便于调试时打印系统列表。
 *
 * v1.7 工程化扩展：
 *   - 性能采样：System 基类持有 Profiler*（由 SystemManager::setProfiler 注入），
 *     系统内用 PROFILE_SCOPE(m_profiler, name()) 计时（见 profiler.h）
 *   - 启停：setEnabled(false) 的系统在 update 中被跳过（运行时开关，如调试系统）
 *   - 优先级：priority() 越小越先执行（稳定排序，默认同注册顺序）
 *   - 重复注册检查：同一类型只允许注册一次，重复注册告警并返回 nullptr
 */
#pragma once

#include "ecs_types.h"
#include "../engine/logger.h"
#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <type_traits>
#include <typeindex>
#include <unordered_set>
#include <utility>

namespace ecs {

class Coordinator;  // 前置声明
class Profiler;     // 前置声明（完整定义见 profiler.h）

// ============================================================================
// System 基类
// ============================================================================
class System
{
public:
    virtual ~System() = default;

    // 每帧调用一次。dt = 距上一帧的时间（秒）
    // 默认空实现：渲染系统不需要 onUpdate（只用 render），逻辑系统重写
    virtual void onUpdate(Coordinator& coord, float dt) { (void)coord; (void)dt; }

    // 系统 display name（调试用）
    virtual const char* name() const = 0;

    // ---- v1.7：性能采样注入（SystemManager::setProfiler 调用） ----
    void setProfiler(Profiler* p) { m_profiler = p; }
    Profiler* profiler() const { return m_profiler; }

    // ---- v1.7：启停 / 优先级 ----
    void setEnabled(bool on) { m_enabled = on; }
    bool enabled() const { return m_enabled; }
    void setPriority(int p) { m_priority = p; }
    int  priority() const { return m_priority; }

    // 注：m_profiler/m_enabled/m_priority 用 protected，便于派生系统
    // 在 PROFILE_SCOPE(m_profiler, ...) 与条件逻辑中直接访问。
protected:
    Profiler* m_profiler = nullptr;  // 全局采样器（可为 null：不采样）
    bool      m_enabled  = true;     // false 时 update 跳过该系统
    int       m_priority = 0;        // 越小越先执行（稳定排序）
};

// ============================================================================
// SystemManager —— 注册/持有系统、按优先级顺序调用
// ============================================================================
class SystemManager
{
public:
    // 注册一个系统（按注册顺序调用）。同一类型注册多次会重复调用
    // v1.7：加入重复注册检查——同一类型只能注册一次，重复则告警并返回 nullptr
    template <typename T, typename... Args>
    T* registerSystem(Args&&... args)
    {
        static_assert(std::is_base_of_v<System, T>, "T must derive from System");
        std::type_index ti(typeid(T));
        if (m_types.find(ti) != m_types.end())
        {
            LOG_WARN("ECS", "registerSystem",
                     "重复注册系统类型 %s 已忽略（同类系统只能注册一次）",
                     typeid(T).name());
            return nullptr;
        }
        m_types.insert(ti);

        auto sys = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = sys.get();
        if (m_profiler) ptr->setProfiler(m_profiler);
        m_systems.push_back(std::move(sys));
        return ptr;
    }

    // 调用所有系统的 onUpdate（v1.7：按优先级稳定排序，跳过被禁用的系统）
    void update(Coordinator& coord, float dt)
    {
        std::vector<System*> order;
        order.reserve(m_systems.size());
        for (auto& s : m_systems) order.push_back(s.get());
        std::stable_sort(order.begin(), order.end(),
                         [](const System* a, const System* b) {
                             return a->priority() < b->priority();
                         });
        for (auto* s : order)
            if (s->enabled())
                s->onUpdate(coord, dt);
    }

    // 查找某类型的系统（若有多个，返回第一个）；找不到返回 nullptr
    template <typename T>
    T* find() const
    {
        static_assert(std::is_base_of_v<System, T>, "T must derive from System");
        for (auto& s : m_systems)
            if (auto* p = dynamic_cast<T*>(s.get())) return p;
        return nullptr;
    }

    // ---- v1.7：查询某类型是否已注册 ----
    template <typename T>
    bool isRegistered() const
    {
        static_assert(std::is_base_of_v<System, T>, "T must derive from System");
        return m_types.count(typeid(T)) != 0;
    }

    // ---- v1.7：按显示名启停系统；找不到返回 false ----
    bool setEnabledByName(const char* name, bool on)
    {
        for (auto& s : m_systems)
            if (std::strcmp(s->name(), name) == 0) { s->setEnabled(on); return true; }
        return false;
    }

    // ---- v1.7：按显示名设置优先级（越小越先执行）；找不到返回 false ----
    bool setPriorityByName(const char* name, int priority)
    {
        for (auto& s : m_systems)
            if (std::strcmp(s->name(), name) == 0) { s->setPriority(priority); return true; }
        return false;
    }

    // ---- v1.7：注入全局 Profiler（注册后调用；随后注册的系统自动带上） ----
    void setProfiler(Profiler* p)
    {
        m_profiler = p;
        for (auto& s : m_systems) s->setProfiler(p);
    }

    std::vector<std::string> listNames() const
    {
        std::vector<std::string> v;
        v.reserve(m_systems.size());
        for (auto& s : m_systems)
            v.emplace_back(s->name());
        return v;
    }

private:
    std::vector<std::unique_ptr<System>> m_systems;
    std::unordered_set<std::type_index>  m_types;     // v1.7：已注册类型集合
    Profiler* m_profiler = nullptr;                    // v1.7：全局采样器
};

} // namespace ecs
