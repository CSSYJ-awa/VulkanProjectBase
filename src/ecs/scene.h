/**
 * scene.h —— ECS 场景抽象与多场景管理
 *
 * 用途：
 *   把"一组实体 + 装配逻辑 + 进入/退出回调"封装成 Scene 对象，由
 *   SceneManager 统一调度。实现多场景切换、暂停/恢复、热重载。
 *
 * 设计：
 *   - Scene 是抽象基类；用户继承并实现 onEnter/onExit/onUpdate
 *   - SceneManager 持有 Scene 注册表（按名字查找）和当前激活场景
 *   - 切换场景时：旧场景 onExit → 清空实体 → 新场景 onEnter。
 *     事件总线保持不变（跨场景订阅）
 *   - Scene::m_owner 由 SceneManager 在 add 时注入；场景内可访问 coord
 *
 * 使用流程：
 *   1. 派生 Scene 子类：实现 name() 与 onEnter()（用 EntityBuilder +
 *      AssetManager 等创建实体）；可选覆写 onExit/onUpdate
 *   2. 在 VulkanApp::registerScenes() 中注册：m_scenes->add<MyScene>()
 *   3. 切换：m_scenes->switchTo(coord, "my_scene", userCtx)
 *
 * 设计权衡：
 *   - SceneManager 不持有 AssetManager/Prefab：场景通过 owner() 访问
 *     Coordinator，通过 VulkanApp 注入的回调访问 AssetManager
 *   - 切换时清空所有实体：简单可靠；如需"持久实体"（如 UI）可改为
 *     "保留实体白名单"模式（扩展点）
 *   - 场景间共享：EventBus、System 注册表、AssetManager（资源缓存）
 */
#pragma once

#include "ecs_types.h"

#include <any>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <type_traits>
#include <utility>

namespace ecs {

class Coordinator;

// ============================================================================
// Scene —— 抽象场景基类
// ============================================================================
class Scene
{
public:
    virtual ~Scene() = default;

    // 场景名（用于 SceneManager::switchTo 查找）
    virtual const char* name() const = 0;

    // 进入场景时调用：在此创建实体
    // coord 已由 SceneManager 注入；assets/prefabs 可通过 m_userCtx 访问
    virtual void onEnter() {}

    // 退出场景时调用：默认实现无需写，SceneManager 已会清空所有实体
    // 覆写以执行自定义清理（如释放预制件、退订事件）
    virtual void onExit() {}

    // 每帧调用：可选实现场景级逻辑（不与 ECS 系统冲突，用于触发场景事件）
    virtual void onUpdate(float /*dt*/) {}

    // 由 SceneManager 注入
    Coordinator& owner() { return *m_owner; }
    const Coordinator& owner() const { return *m_owner; }

    // 用户自定义上下文（如 AssetManager*、PrefabRegistry* 等）
    // VulkanApp 在 switchTo 前调用 setUserContext 注入；场景通过 ctx() 访问
    // 用法：auto* ctx = scene->ctxAs<MySceneContext>();
    //   ctxAs 内部用 any_cast 的指针形式，类型不匹配时返回 nullptr（不抛异常）
    template <typename T>
    T* ctxAs()
    {
        return std::any_cast<T>(std::addressof(m_userCtx));
    }
    template <typename T>
    const T* ctxAs() const
    {
        return std::any_cast<T>(std::addressof(m_userCtx));
    }
    const std::any& ctx() const { return m_userCtx; }

protected:
    Scene() = default;
    friend class SceneManager;

    Coordinator* m_owner = nullptr;
    std::any     m_userCtx;
};

// ============================================================================
// SceneManager —— 场景注册表与切换调度
// ============================================================================
class SceneManager
{
public:
    // 注册场景实例（unique_ptr）
    // 同名场景会被替换
    void add(std::unique_ptr<Scene> scene)
    {
        if (!scene) return;
        m_scenes[scene->name()] = std::move(scene);
    }

    // 便捷模板：注册派生类
    template <typename T, typename... Args>
    T* add(Args&&... args)
    {
        static_assert(std::is_base_of_v<Scene, T>, "T must derive from Scene");
        auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = ptr.get();
        add(std::move(ptr));
        return raw;
    }

    // 切换到指定场景：若当前有场景，先 onExit + 清空 Coordinator
    // 返回是否切换成功
    bool switchTo(Coordinator& coord, const std::string& name,
                  const std::any& userCtx = {})
    {
        auto it = m_scenes.find(name);
        if (it == m_scenes.end() || !it->second) return false;

        // 退出旧场景：先 onExit（可访问旧实体），再清空所有实体
        if (m_current)
        {
            m_current->onExit();
            coord.clear();  // 清空所有实体，发布 EntityDestroyed 事件
        }

        // 进入新场景
        m_current = it->second.get();
        m_current->m_owner    = &coord;
        m_current->m_userCtx = userCtx;
        m_current->onEnter();
        return true;
    }

    // 每帧调用：转发给当前场景
    void update(float dt)
    {
        if (m_current) m_current->onUpdate(dt);
    }

    Scene*       current()       { return m_current; }
    const Scene* current() const { return m_current; }

    bool has(const std::string& name) const
    {
        return m_scenes.find(name) != m_scenes.end();
    }

    size_t size() const { return m_scenes.size(); }

    // 列出所有场景名（调试用）
    std::vector<std::string> listNames() const
    {
        std::vector<std::string> v;
        v.reserve(m_scenes.size());
        for (const auto& [n, _] : m_scenes) v.push_back(n);
        return v;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Scene>> m_scenes;
    Scene* m_current = nullptr;
};

} // namespace ecs
