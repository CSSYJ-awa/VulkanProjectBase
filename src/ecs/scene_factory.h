/**
 * scene_factory.h —— 场景实体工厂（v1.0.2）
 *
 * 一键创建"常用场景实体"，自动装配组件（Transform/Camera/Light/Collider/
 * Spawner/TriggerZone/Text 等），返回实体句柄。所有方法均可通过参数高度
 * 自定义，是 EntityBuilder 之上的一层"语义化"快捷封装。
 *
 * 用法：
 *   ecs::SceneFactory scene(*m_ecs, m_assets.get());
 *   auto cam = scene.createCamera("main_cam", {0,6,12}, {0,1,0});
 *   auto sun = scene.createDirectionalLight("sun", {-0.5,-1,-0.3}, {1,1,0.9}, 1.2f);
 *   auto box = scene.createCube("box", {0,1,0}, 1.0f, {0.9f,0.4f,0.2f});
 *   scene.createTriggerSphere("goal", {0,0,0}, 2.0f);
 *
 * 说明：
 *   - createMesh / createShape 需要 AssetManager（VulkanApp 已初始化）；
 *     传入 nullptr 时返回 kNullEntity。
 *   - createText 需要调用方传入已 upload 的 TextRenderer（字体资源由调用方管理）。
 *   - 纯头文件实现，零 cpp；不需要加入 build.json。
 */
#pragma once

#include "coordinator.h"
#include "asset_manager.h"
#include "components.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace ecs {

class SceneFactory
{
public:
    // coord：ECS 协调器；assets：资源工厂（可空，createMesh/createShape 需要）
    SceneFactory(Coordinator& coord, const AssetManager* assets = nullptr)
        : m_coord(coord), m_assets(assets) {}

    // ====================================================================
    // 基础
    // ====================================================================

    // 空实体（可选 Tag）
    Entity createEmpty(const std::string& name = "")
    {
        return name.empty() ? m_coord.createEntity() : m_coord.createEntity(name);
    }

    // ====================================================================
    // 相机
    // ====================================================================

    // 创建相机：pos 位置、lookAt 朝向（自动换算 yaw/pitch）
    Entity createCamera(const std::string& name,
                        const glm::vec3& pos     = glm::vec3(0.0f, 5.0f, 12.0f),
                        const glm::vec3& lookAt  = glm::vec3(0.0f),
                        bool isPrimary  = true,
                        bool flyEnabled = false)
    {
        Entity e = createEmpty(name);
        m_coord.addComponent<Transform>(e, Transform{ pos, glm::vec3(0.0f), glm::vec3(1.0f) });
        Camera cam;
        cam.position  = pos;
        cam.isPrimary = isPrimary;
        cam.flyEnabled = flyEnabled;
        glm::vec3 d = lookAt - pos;
        if (glm::length(d) > 1e-5f)
        {
            d = glm::normalize(d);
            cam.yaw   =  glm::degrees(std::atan2(d.z, d.x));
            cam.pitch =  glm::degrees(std::asin(glm::clamp(d.y, -1.0f, 1.0f)));
        }
        m_coord.addComponent<Camera>(e, cam);
        return e;
    }

    // 跟随相机：挂在带 Follow 的相机实体上（跟随 target 的 Transform）
    Entity createFollowCamera(const std::string& name, Entity target,
                              const glm::vec3& offset = glm::vec3(0.0f, 3.0f, 8.0f),
                              float lerp = 4.0f, bool lookAt = true)
    {
        Entity e = createEmpty(name);
        m_coord.addComponent<Transform>(e, Transform{ glm::vec3(0.0f) });
        Camera cam;
        cam.isPrimary  = true;
        cam.flyEnabled = false;
        m_coord.addComponent<Camera>(e, cam);
        m_coord.addComponent<Follow>(e, Follow{ target, offset, lerp, lookAt });
        return e;
    }

    // ====================================================================
    // 灯光
    // ====================================================================

    // 方向光（主光）：dir 为光线传播方向，color 为光色，intensity 强度
    Entity createDirectionalLight(const std::string& name,
                                  const glm::vec3& dir      = glm::vec3(-0.5f, -1.0f, -0.3f),
                                  const glm::vec3& color    = glm::vec3(1.0f),
                                  float intensity = 1.0f,
                                  bool  isPrimary = true)
    {
        Entity e = createEmpty(name);
        Light l;
        l.kind      = Light::kDirectional;
        l.direction = glm::normalize(dir);
        l.color     = color;
        l.intensity = intensity;
        l.isPrimary = isPrimary;
        m_coord.addComponent<Light>(e, l);
        return e;
    }

    // 点光源：pos 位置、color 光色、range 衰减半径、intensity 强度
    Entity createPointLight(const std::string& name,
                            const glm::vec3& pos,
                            const glm::vec3& color   = glm::vec3(1.0f),
                            float range     = 10.0f,
                            float intensity = 1.0f)
    {
        Entity e = createEmpty(name);
        m_coord.addComponent<Transform>(e, Transform{ pos });
        Light l;
        l.kind      = Light::kPoint;
        l.color     = color;
        l.range     = range;
        l.intensity = intensity;
        l.isPrimary = false;
        m_coord.addComponent<Light>(e, l);
        return e;
    }

    // ====================================================================
    // 3D 网格（需要 AssetManager）
    // ====================================================================

    // 立方体：pos 位置、size 边长、color 颜色、rot 欧拉角（弧度）
    Entity createCube(const std::string& name,
                      const glm::vec3& pos,
                      float size = 1.0f,
                      const glm::vec3& color = glm::vec3(0.8f, 0.6f, 0.4f),
                      const glm::vec3& rot = glm::vec3(0.0f))
    {
        if (!m_assets || !m_assets->ready()) return kNullEntity;
        Entity e = createEmpty(name);
        m_coord.addComponent<Transform>(e, Transform{ pos, rot, glm::vec3(size) });
        m_coord.addComponent<Mesh3DComponent>(e,
            Mesh3DComponent(m_assets->createCube(1.0f, color.r, color.g, color.b)));
        return e;
    }

    // 球体（正二十面体近似）：pos、scale（半径）、color
    Entity createSphere(const std::string& name,
                        const glm::vec3& pos,
                        float scale = 0.5f,
                        const glm::vec3& color = glm::vec3(0.4f, 0.7f, 0.9f))
    {
        if (!m_assets || !m_assets->ready()) return kNullEntity;
        Entity e = createEmpty(name);
        m_coord.addComponent<Transform>(e, Transform{ pos, glm::vec3(0.0f), glm::vec3(scale) });
        m_coord.addComponent<Mesh3DComponent>(e,
            Mesh3DComponent(m_assets->createSphere(1.0f, color.r, color.g, color.b)));
        return e;
    }

    // 多面体：type 几何类型、pos、scale、color
    Entity createPolyhedron(const std::string& name, Polyhedron::Type type,
                            const glm::vec3& pos,
                            float scale = 1.0f,
                            const glm::vec3& color = glm::vec3(0.85f, 0.85f, 0.9f))
    {
        if (!m_assets || !m_assets->ready()) return kNullEntity;
        Entity e = createEmpty(name);
        m_coord.addComponent<Transform>(e, Transform{ pos, glm::vec3(0.0f), glm::vec3(scale) });
        m_coord.addComponent<Mesh3DComponent>(e,
            Mesh3DComponent(m_assets->createPolyhedron(type, 1.0f, color.r, color.g, color.b)));
        return e;
    }

    // 从已注册的网格模板创建（模板名见 AssetManager::registerMeshTemplate）
    Entity createMeshFromTemplate(const std::string& name, const std::string& templateName,
                                  const glm::vec3& pos,
                                  const glm::vec3& scale = glm::vec3(1.0f))
    {
        if (!m_assets || !m_assets->ready() || !m_assets->hasTemplate(templateName))
            return kNullEntity;
        Entity e = createEmpty(name);
        m_coord.addComponent<Transform>(e, Transform{ pos, glm::vec3(0.0f), scale });
        m_coord.addComponent<Mesh3DComponent>(e,
            Mesh3DComponent(m_assets->instantiate(templateName)));
        return e;
    }

    // ====================================================================
    // 2D 形状（NDC 屏幕空间，需要 AssetManager）
    // ====================================================================

    // 圆形（2D）：cx/cy 为 NDC 坐标、radius 半径、segments 分段数、color
    Entity createCircle(const std::string& name,
                        float cx, float cy, float radius,
                        int segments = 24,
                        const glm::vec3& color = glm::vec3(0.9f, 0.6f, 0.2f))
    {
        if (!m_assets || !m_assets->ready()) return kNullEntity;
        Entity e = createEmpty(name);
        m_coord.addComponent<Shape2DComponent>(e,
            Shape2DComponent(m_assets->createCircle(cx, cy, radius, segments,
                                                    color.r, color.g, color.b)));
        return e;
    }

    // ====================================================================
    // 文字
    // ====================================================================

    // 文字实体：renderer 须已上传（字体资源由调用方管理）
    Entity createText(const std::string& name, std::unique_ptr<TextRenderer> renderer)
    {
        if (!renderer) return kNullEntity;
        Entity e = createEmpty(name);
        m_coord.addComponent<TextComponent>(e, TextComponent(std::move(renderer)));
        return e;
    }

    // ====================================================================
    // 生成器（Spawner）
    // ====================================================================

    // 2D 粒子生成器：origin 中心、radius 散射半径、interval 间隔、maxAlive 上限、
    // color 基色、lifetime 粒子寿命
    Entity createParticleSpawner(const std::string& name,
                                 const glm::vec3& origin,
                                 float interval   = 0.1f,
                                 int   maxAlive   = 200,
                                 const glm::vec3& color = glm::vec3(0.9f, 0.6f, 0.2f),
                                 float radius     = 0.5f,
                                 float lifetime   = 2.0f)
    {
        Entity e = createEmpty(name);
        Spawner s;
        s.kind          = Spawner::kParticle;
        s.interval      = interval;
        s.maxAlive      = maxAlive;
        s.spawnOrigin   = origin;
        s.spawnRadius   = radius;
        s.spawnLifetime = lifetime;
        s.baseColor     = color;
        m_coord.addComponent<Spawner>(e, s);
        return e;
    }

    // 3D 立方体生成器（粒子为小立方体）
    Entity createCubeSpawner(const std::string& name,
                             const glm::vec3& origin,
                             float interval   = 0.15f,
                             int   maxAlive   = 120,
                             const glm::vec3& color = glm::vec3(0.9f, 0.6f, 0.2f),
                             float radius     = 1.0f,
                             float lifetime   = 3.0f)
    {
        Entity e = createEmpty(name);
        Spawner s;
        s.kind          = Spawner::kCube;
        s.interval      = interval;
        s.maxAlive      = maxAlive;
        s.spawnOrigin   = origin;
        s.spawnRadius   = radius;
        s.spawnLifetime = lifetime;
        s.baseColor     = color;
        m_coord.addComponent<Spawner>(e, s);
        return e;
    }

    // ====================================================================
    // 触发器区域（TriggerZone，由 TriggerSystem 检测进出）
    // ====================================================================

    // AABB 区域：center 中心、halfExtents 半盒
    Entity createTriggerZone(const std::string& name,
                             const glm::vec3& center,
                             const glm::vec3& halfExtents = glm::vec3(1.0f))
    {
        Entity e = createEmpty(name);
        m_coord.addComponent<Transform>(e, Transform{ center });
        TriggerZone z;
        z.kind        = TriggerZone::kAABB;
        z.halfExtents = halfExtents;
        m_coord.addComponent<TriggerZone>(e, z);
        return e;
    }

    // 球形区域：center 中心、radius 半径
    Entity createTriggerSphere(const std::string& name,
                               const glm::vec3& center,
                               float radius = 1.0f)
    {
        Entity e = createEmpty(name);
        m_coord.addComponent<Transform>(e, Transform{ center });
        TriggerZone z;
        z.kind   = TriggerZone::kSphere;
        z.radius = radius;
        m_coord.addComponent<TriggerZone>(e, z);
        return e;
    }

    // ====================================================================
    // 碰撞体（Collider，由 ColliderSystem 检测）
    // ====================================================================

    // AABB 碰撞盒：pos 中心、halfExtents 半盒、isStatic 静态（不参与位移修正）
    Entity createCollider(const std::string& name,
                          const glm::vec3& pos,
                          const glm::vec3& halfExtents = glm::vec3(0.5f),
                          bool isStatic = false,
                          bool isTrigger = false)
    {
        Entity e = createEmpty(name);
        m_coord.addComponent<Transform>(e, Transform{ pos });
        Collider c;
        c.halfExtents = halfExtents;
        c.isStatic    = isStatic;
        c.isTrigger   = isTrigger;
        m_coord.addComponent<Collider>(e, c);
        return e;
    }

    // 便捷：碰撞体挂在已有实体上（只加 Collider 组件）
    void attachCollider(Entity e, const glm::vec3& halfExtents = glm::vec3(0.5f),
                        bool isStatic = false)
    {
        if (!m_coord.valid(e)) return;
        Collider c;
        c.halfExtents = halfExtents;
        c.isStatic    = isStatic;
        m_coord.replaceComponent<Collider>(e, c);
    }

private:
    Coordinator&        m_coord;
    const AssetManager* m_assets;
};

} // namespace ecs
