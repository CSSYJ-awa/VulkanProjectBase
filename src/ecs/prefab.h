/**
 * prefab.h —— ECS 预制件（可复用实体模板）
 *
 * 用途：把"创建一个复杂实体"的逻辑封装成预制件，按名字实例化。
 *
 *   // 注册：把构造函数登记为预制件（lambda 内可访问 Coordinator/AssetManager）
 *   prefabs.add("player", [](Coordinator& c, Entity e, const PrefabCtx& ctx) {
 *       auto mesh = ctx.assets->createSphere(0.5f, 0.2f, 0.9f, 0.3f);
 *       c.addComponent<Transform>(e, ...);
 *       c.addComponent<Mesh3DComponent>(e, std::move(mesh));
 *       c.addComponent<Input>(e, ...);
 *   });
 *
 *   // 实例化：返回已构造的实体
 *   Entity player = prefabs.instantiate(coord, "player", ctx);
 *
 * PrefabCtx 是可选的上下文（assets/payload），由调用方提供。
 * 不需要上下文时用 Prefab::instantiate(coord, name) 即可。
 *
 * 设计：
 *   - PrefabRegistry 不持有 Coordinator：每次 instantiate 时传入
 *   - 一个 Prefab = 一段工厂 lambda（void(Coordinator&, Entity, const PrefabCtx&)）
 *   - 适合子弹/敌人生成等"重复构造相同结构"场景
 */
#pragma once

#include "ecs_types.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <any>

namespace ecs {

class Coordinator;

// 传给预制件工厂的上下文（可选使用）
struct PrefabCtx
{
    // 用户可塞任意东西：AssetManager*、生成器实体、初始位置等
    std::any userData;
    // 便捷访问：常见用法是塞 AssetManager* 进 userData
    // 不强制类型；用户调用方与预制件工厂约定一致即可
};

// 单个预制件工厂函数
using PrefabFactory = std::function<void(Coordinator&, Entity, const PrefabCtx&)>;

class PrefabRegistry
{
public:
    // 注册预制件
    void add(const std::string& name, PrefabFactory factory)
    {
        m_factories[name] = std::move(factory);
    }

    // 实例化预制件：在 coord 中创建实体并应用工厂
    // 返回创建的实体；找不到预制件返回 kNullEntity
    Entity instantiate(Coordinator& coord, const std::string& name,
                       const PrefabCtx& ctx = {}) const
    {
        auto it = m_factories.find(name);
        if (it == m_factories.end() || !it->second) return kNullEntity;
        Entity e = coord.createEntity();
        it->second(coord, e, ctx);
        return e;
    }

    // 实例化并挂 Tag（推荐：方便后续 findByName）
    Entity instantiateNamed(Coordinator& coord, const std::string& name,
                            const std::string& tagName,
                            const PrefabCtx& ctx = {}) const
    {
        auto it = m_factories.find(name);
        if (it == m_factories.end() || !it->second) return kNullEntity;
        Entity e = coord.createEntity(tagName);
        it->second(coord, e, ctx);
        return e;
    }

    bool has(const std::string& name) const
    {
        return m_factories.find(name) != m_factories.end();
    }

    size_t size() const { return m_factories.size(); }

private:
    std::unordered_map<std::string, PrefabFactory> m_factories;
};

} // namespace ecs
