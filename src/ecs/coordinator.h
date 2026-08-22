/**
 * coordinator.h —— ECS 协调器
 *
 * Coordinator 封装 EnTT 的 entt::registry + SystemManager + EventBus，对外提供：
 *   - createEntity()             创建实体（只发 ID，不挂任何组件）
 *   - entity(name)               返回 EntityBuilder（链式 API，见下）
 *   - destroyEntity(e)           销毁实体（及其所有组件，并发布 EntityDestroyed 事件）
 *   - addComponent<T>(e, args...) 挂载组件
 *   - removeComponent<T>(e)      卸载组件
 *   - getComponent<T>(e)          访问组件（可修改）
 *   - hasComponent<T>(e)          查询
 *   - view<T...>()                获取多组件视图（系统遍历用）
 *   - updateSystems(dt)           调用所有系统的 onUpdate
 *   - systems()                   访问 SystemManager（注册/查找系统）
 *   - events()                    访问 EventBus（订阅/发布事件）
 *   - registry()                  直接访问底层 entt::registry（高级用法）
 *
 * 链式 API 示例：
 *   auto player = coord.entity("player")
 *       .with<Transform>({ {0,0,0}, {0,0,0}, {1,1,1} })
 *       .with<Mesh3DComponent>(std::move(mesh))
 *       .with<Input>({ 4.0f, 120.0f, true })
 *       .build();
 *
 * 设计权衡：
 *   - 不把所有 entt::registry API 全部转发，避免冗余；常用的高频 API 封装，
 *     低频的（如 on_construct 信号、group 等）通过 registry() 暴露
 *   - createEntity 返回 ecs::Entity 而非 entt::entity，对外隔离
 *   - 与 system.h 协同：updateSystems 内部调 SystemManager::update(*this, dt)
 *   - EventBus 集成在 Coordinator 中，所有系统通过 coord.events() 访问
 */
#pragma once

#include "ecs_types.h"
#include "system.h"
#include "event_bus.h"
#include "components.h"

#include <entt/entt.hpp>
#include <algorithm>
#include <utility>
#include <vector>

namespace ecs {

// ============================================================================
// 标准事件：实体销毁。LifetimeSystem/destroyEntity 自动发布
// 任何关心实体生命周期的系统可订阅此事件清理外部状态
// 例如 SpawnerSystem 订阅以更新粒子计数，无需 SpawnerRef 组件
// ============================================================================
struct EntityDestroyed
{
    Entity entity;
};

// ============================================================================
// 组件变更事件（v1.0.2）
// 调用 Coordinator::autoPublishComponentEvents<T>() 后自动发布：
//   ComponentAdded<T>   —— 组件被挂载（含组件指针，回调期内有效）
//   ComponentRemoved<T> —— 组件被移除（实体可能随后被销毁）
// 用法：
//   coord.autoPublishComponentEvents<Health>();
//   coord.events().subscribe<ecs::ComponentAdded<Health>>(
//       [](const auto& ev){ /* ev.entity / ev.component */ });
// ============================================================================
template <typename T>
struct ComponentAdded
{
    Entity    entity;
    const T*  component;   // 指向 registry 中的组件（回调期内有效，勿长期持有）
};

template <typename T>
struct ComponentRemoved
{
    Entity entity;
};

// 前置声明：EntityBuilder 在 Coordinator 完整定义后实现模板方法
class EntityBuilder;
class Coordinator;

class Coordinator
{
public:
    Coordinator() = default;
    ~Coordinator() = default;

    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;

    // ------------------------------------------------------------------
    // 实体管理
    // ------------------------------------------------------------------

    // 创建一个空实体（无组件），返回其句柄
    Entity createEntity()
    {
        Entity e = m_registry.create();
        m_entityCache.push_back(e);
        return e;
    }

    // 创建带 Tag 的实体
    Entity createEntity(const std::string& name)
    {
        Entity e = m_registry.create();
        m_registry.emplace<Tag>(e, Tag{ name });
        m_entityCache.push_back(e);
        return e;
    }

    // 链式创建：coord.entity("player").with<...>().with<...>().build();
    // 方法实现在文件末尾（EntityBuilder 完整定义之后）
    EntityBuilder entity(const std::string& name = "");

    // 销毁实体（及其所有组件）
    // 同时发布 EntityDestroyed 事件，订阅者可清理外部状态
    // （例如 SpawnerSystem 减少粒子计数，无需 SpawnerRef 组件）
    void destroyEntity(Entity e)
    {
        m_events.publish<EntityDestroyed>({ e });
        m_registry.destroy(e);
        auto it = std::find(m_entityCache.begin(), m_entityCache.end(), e);
        if (it != m_entityCache.end()) m_entityCache.erase(it);
    }

    // 实体是否有效（未被销毁）
    bool valid(Entity e) const
    {
        return m_registry.valid(e);
    }

    // ------------------------------------------------------------------
    // 组件管理
    // ------------------------------------------------------------------

    // 挂载组件（args 传给组件构造函数）
    template <typename T, typename... Args>
    T& addComponent(Entity e, Args&&... args)
    {
        return m_registry.emplace<T>(e, std::forward<Args>(args)...);
    }

    // 挂载或替换组件（若已存在则覆盖）
    template <typename T, typename... Args>
    T& replaceComponent(Entity e, Args&&... args)
    {
        return m_registry.emplace_or_replace<T>(e, std::forward<Args>(args)...);
    }

    // 移除组件（不存在时静默）
    template <typename T>
    void removeComponent(Entity e)
    {
        m_registry.erase<T>(e);
    }

    // 访问组件（可修改）。不存在时 UB（请先 hasComponent 检查）
    template <typename T>
    T& getComponent(Entity e)
    {
        return m_registry.get<T>(e);
    }

    // 访问组件（只读）
    template <typename T>
    const T& getComponent(Entity e) const
    {
        return m_registry.get<T>(e);
    }

    // 是否拥有该组件
    template <typename T>
    bool hasComponent(Entity e) const
    {
        return m_registry.all_of<T>(e);
    }

    // 是否拥有 ALL OF (T...) 这些组件
    template <typename... T>
    bool hasAllComponents(Entity e) const
    {
        return m_registry.all_of<T...>(e);
    }

    // 是否拥有 ANY OF (T...) 这些组件
    template <typename... T>
    bool hasAnyComponent(Entity e) const
    {
        return m_registry.any_of<T...>(e);
    }

    // 获取多组件视图（系统遍历用）
    // 例：auto view = coord.view<Transform, Mesh3DComponent>();
    //     for (auto e : view) { auto& [t, m] = view.get(e); ... }
    template <typename... T>
    auto view()
    {
        return m_registry.view<T...>();
    }

    template <typename... T>
    auto view() const
    {
        return m_registry.view<T...>();
    }

    // ------------------------------------------------------------------
    // 系统管理
    // ------------------------------------------------------------------

    SystemManager& systems() { return m_systemMgr; }
    const SystemManager& systems() const { return m_systemMgr; }

    // 每帧调用：调用所有系统的 onUpdate
    void updateSystems(float dt)
    {
        m_systemMgr.update(*this, dt);
    }

    // ------------------------------------------------------------------
    // 事件总线
    // ------------------------------------------------------------------
    EventBus&       events()       { return m_events; }
    const EventBus& events() const { return m_events; }

    // ------------------------------------------------------------------
    // 高级：直接访问底层 registry
    // ------------------------------------------------------------------
    Registry&       registry()       { return m_registry; }
    const Registry& registry() const { return m_registry; }

    // ------------------------------------------------------------------
    // 便利：按 Tag 名查找实体（线性扫描，少量实体用）
    // 找不到返回 kNullEntity
    // ------------------------------------------------------------------
    Entity findByName(const std::string& name) const
    {
        auto view = m_registry.view<const Tag>();
        for (auto e : view)
            if (view.get<const Tag>(e).name == name)
                return e;
        return kNullEntity;
    }

    // ------------------------------------------------------------------
    // 实体计数（场景切换/调试用）。
    // EnTT 3.13：const 上下文中 storage<Entity>() 返回指针，故用 -> 访问。
    // ------------------------------------------------------------------
    size_t entityCount() const
    {
        return m_registry.storage<Entity>()->size();
    }

    // ------------------------------------------------------------------
    // 实体列表缓存（v1.0.2）
    // createEntity/destroyEntity 时自动维护，O(1) 遍历全部存活实体，
    // 供需要"每帧遍历所有实体"的批处理/统计逻辑使用，避免重复构造 view。
    // ------------------------------------------------------------------
    const std::vector<Entity>& aliveEntities() const { return m_entityCache; }

    // ------------------------------------------------------------------
    // 组件变更事件（v1.0.2）
    // 为组件类型 T 开启自动事件发布：挂载→ComponentAdded<T>，移除→ComponentRemoved<T>
    // 通过 events().subscribe 订阅。对不关心的组件类型无需开启，零开销。
    // ------------------------------------------------------------------
    template <typename T>
    void autoPublishComponentEvents()
    {
        m_registry.on_construct<T>().connect([this](entt::registry&, entt::entity e) {
            m_events.publish(ComponentAdded<T>{ static_cast<Entity>(e), &m_registry.get<T>(e) });
        });
        m_registry.on_destroy<T>().connect([this](entt::registry&, entt::entity e) {
            m_events.publish(ComponentRemoved<T>{ static_cast<Entity>(e) });
        });
    }

    // ------------------------------------------------------------------
    // 清空所有实体（保留系统/事件订阅）
    // 用于场景切换：SceneManager 在 onExit 后调用此方法
    // 注意：先 collect 后 destroy，避免迭代中销毁导致 UB
    // EnTT 3.13：storage::each() 无参重载返回 iterable（元素为 tuple），
    //            此处用 begin()/end() 直接拿到 entity，更直观。
    // ------------------------------------------------------------------
    void clear()
    {
        std::vector<Entity> toDestroy;
        auto& pool = m_registry.storage<Entity>();
        toDestroy.reserve(pool.size());
        for (auto e : pool)
            toDestroy.push_back(e);
        m_entityCache.clear();       // 先清缓存：destroyEntity 逐个移除退化为 O(1) 快速路径
        for (Entity e : toDestroy)
            destroyEntity(e);        // 发布 EntityDestroyed 事件
    }

    // ------------------------------------------------------------------
    // 遍历所有实体（按 entity pool 顺序）
    // EnTT 3.13：const 上下文 storage<Entity>() 返回指针，需判空后解引用。
    // ------------------------------------------------------------------
    template <typename Fn>
    void eachEntity(Fn&& fn) const
    {
        if (auto* pool = m_registry.storage<Entity>())
            for (auto e : *pool)
                fn(e);
    }

private:
    Registry       m_registry;
    SystemManager  m_systemMgr;
    EventBus       m_events;
    std::vector<Entity> m_entityCache;  // v1.0.2 存活实体缓存
};

// ============================================================================
// EntityBuilder —— 链式实体创建器
//
// 由 Coordinator::entity() 返回，调用 .with<Component>(args...) 累积组件，
// .build() / 隐式转换 Entity 完成创建。临时对象使用，不可长期持有。
//
// 模板方法 with<T> 必须在 Coordinator 完整定义之后实现，以便访问其
// addComponent<T> 接口；故本类定义在 Coordinator 之后。
// ============================================================================
class EntityBuilder
{
public:
    EntityBuilder(Coordinator& c, Entity e) : m_coord(c), m_entity(e) {}

    template <typename T, typename... Args>
    EntityBuilder& with(Args&&... args)
    {
        m_coord.template addComponent<T>(m_entity, std::forward<Args>(args)...);
        return *this;
    }

    // 显式取出实体句柄
    Entity build() const { return m_entity; }
    // 隐式转换：可写 auto e = coord.entity("x").with<...>();
    operator Entity() const { return m_entity; }

private:
    Coordinator& m_coord;
    Entity       m_entity;
};

// Coordinator::entity 的延迟实现（需要 EntityBuilder 完整类型）
inline EntityBuilder Coordinator::entity(const std::string& name)
{
    return EntityBuilder(*this, createEntity(name));
}

} // namespace ecs
